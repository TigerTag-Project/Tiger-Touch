#include "thumb.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "miniz.h"
#include "rom/tjpgd.h"
#include "src/libs/lodepng/lodepng.h"

static const char *TAG = "thumb";

#define RETRY_US (30LL * 1000 * 1000) /* a print's task can reach the maker's list a moment after it starts */
#define MAX_TRIES 8
#define MIN_INTERNAL (50 * 1024) /* free internal RAM a request needs: the survival floor (32 KB), the request's
                                  * measured 11 KB and a margin. Under it the printers come first and it waits. */
#define MAX_SIDE 1024 /* a picture bigger than this is not a thumbnail: it is not decoded */
#define BG_R 0x2a
#define BG_G 0x2d
#define BG_B 0x35 /* the tile's colour behind a picture that is not square or has holes */

typedef struct
{
    uint16_t *px;
    lv_image_dsc_t dsc;
} frame_t;

/* Written by the feed task (thumb_want), the service's task and the screen
 * (thumb_get); every field under `lock`. */
static struct
{
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    bool want;
    uint32_t gen; /* changes with every new wish: a result for an older one is thrown away */
    account_printer_t pr;
    char file[48];
    thumb_fetch_fn fetch;
    thumb_state_t state;
    int tries;
    int64_t next_try_us;
    frame_t frame[2]; /* the picture on show and the one that replaces it */
    int active;       /* -1 when none */
    uint32_t version;
    bool drop; /* the frames are stale: the screen's next look frees them (under the LVGL lock) */
} S EXT_RAM_BSS_ATTR = {.active = -1};

static void free_frames(void)
{
    for (int i = 0; i < 2; i++)
    {
        heap_caps_free(S.frame[i].px);
        memset(&S.frame[i], 0, sizeof S.frame[i]);
    }
    S.active = -1;
}

/* ---- One GET into PSRAM ------------------------------------------------- */

typedef struct
{
    uint8_t *buf;
    size_t len, cap, max;
    bool over;
} acc_t;

static esp_err_t on_data(esp_http_client_event_t *e)
{
    acc_t *a = e->user_data;
    if (e->event_id == HTTP_EVENT_HEADERS_SENT)
    {
        /* A Digest exchange answers 401 first: only the last answer counts. */
        a->len = 0;
        a->over = false;
        if (a->buf)
        {
            a->buf[0] = 0;
        }
        return ESP_OK;
    }
    if (e->event_id != HTTP_EVENT_ON_DATA || e->data_len <= 0 || a->over)
    {
        return ESP_OK;
    }
    const size_t need = a->len + (size_t)e->data_len + 1;
    if (need > a->cap)
    {
        size_t nc = a->cap ? a->cap * 2 : 16 * 1024;
        while (nc < need)
        {
            nc *= 2;
        }
        if (nc > a->max)
        {
            nc = a->max;
        }
        uint8_t *nb = need <= nc ? heap_caps_realloc(a->buf, nc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
        if (!nb)
        {
            a->over = true;
            return ESP_OK;
        }
        a->buf = nb;
        a->cap = nc;
    }
    memcpy(a->buf + a->len, e->data, (size_t)e->data_len);
    a->len += (size_t)e->data_len;
    a->buf[a->len] = 0;
    return ESP_OK;
}

static int http_get(const char *url, const char *bearer, const char *api_key, const char *user, const char *pass,
                    bool bbl_identity, size_t cap, uint8_t **out, size_t *len)
{
    *out = NULL;
    *len = 0;
    acc_t a = {.max = cap};
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_data,
        .user_data = &a,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    if (user && user[0])
    {
        cfg.username = user;
        cfg.password = pass ? pass : "";
        cfg.auth_type = HTTP_AUTH_TYPE_DIGEST;
    }
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c)
    {
        return -1;
    }
    char ua[48]; /* "tiger-touch/" and a version of up to 31 characters */
    snprintf(ua, sizeof ua, "tiger-touch/%s", esp_app_get_description()->version);
    esp_http_client_set_header(c, "User-Agent", ua);
    if (api_key && api_key[0])
    {
        esp_http_client_set_header(c, "X-Api-Key", api_key);
    }
    if (bbl_identity)
    {
        esp_http_client_set_header(c, "X-BBL-Client-Name", "tiger-touch");
        esp_http_client_set_header(c, "X-BBL-Client-Type", "embedded");
        esp_http_client_set_header(c, "X-BBL-Client-Version", "01.00.00.00");
        esp_http_client_set_header(c, "X-BBL-Language", "en-US");
        esp_http_client_set_header(c, "X-BBL-OS-Type", "linux");
        esp_http_client_set_header(c, "X-BBL-Agent-Version", "01.00.00.00");
        esp_http_client_set_header(c, "X-BBL-Executable-info", "{}");
        esp_http_client_set_header(c, "X-BBL-Agent-OS-Type", "linux");
    }
    char *auth = NULL;
    if (bearer && bearer[0])
    {
        const size_t n = strlen(bearer) + 8;
        auth = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (auth)
        {
            snprintf(auth, n, "Bearer %s", bearer);
            esp_http_client_set_header(c, "Authorization", auth);
        }
    }
    const esp_err_t e = esp_http_client_perform(c);
    int status = e == ESP_OK ? esp_http_client_get_status_code(c) : -1;
    esp_http_client_cleanup(c);
    if (auth)
    {
        memset(auth, 0, strlen(auth)); /* a live token */
        heap_caps_free(auth);
    }
    if (a.over)
    {
        status = -1;
    }
    if (status != 200 || !a.buf)
    {
        heap_caps_free(a.buf);
        return status;
    }
    *out = a.buf;
    *len = a.len;
    return status;
}

int thumb_http_get(const char *url, const char *bearer, bool bbl_identity, size_t cap, uint8_t **out, size_t *len)
{
    return http_get(url, bearer, NULL, NULL, NULL, bbl_identity, cap, out, len);
}

int thumb_http_get_key(const char *url, const char *api_key, size_t cap, uint8_t **out, size_t *len)
{
    return http_get(url, NULL, api_key, NULL, NULL, false, cap, out, len);
}

int thumb_http_get_auth(const char *url, const char *user, const char *pass, const char *api_key, size_t cap, uint8_t **out,
                        size_t *len)
{
    return http_get(url, NULL, api_key, user, pass, false, cap, out, len);
}

size_t thumb_url_append(char *out, size_t n, size_t cap, const char *s, bool keep_slash)
{
    for (const unsigned char *c = (const unsigned char *)s; *c && n + 4 < cap; c++)
    {
        if (isalnum(*c) || strchr("-_.~", *c) || (keep_slash && *c == '/'))
        {
            out[n++] = (char)*c;
        }
        else
        {
            n += (size_t)snprintf(out + n, 4, "%%%02X", *c);
        }
    }
    out[n] = '\0';
    return n;
}

/* ---- Decode and shrink -------------------------------------------------- */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* The fallback for the PNGs the line-by-line reader does not take (16 bits, interlaced):
 * the whole picture is decoded, which wants two blocks of a megabyte for a 512 x 512 one and
 * fails when the PSRAM is too fragmented to give them.
 *
 * A THUMB_SIZE square of RGB565, in PSRAM; NULL when it is not one we
 * decode. The whole picture is decoded first (about eight bytes of PSRAM per
 * source pixel at the peak, all of it freed before this returns), then averaged
 * down, so nothing about the source's size reaches the screen's memory. */
static uint16_t *shrink_whole(const uint8_t *data, size_t len)
{
    static const uint8_t PNG_MAGIC[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (len < 33 || memcmp(data, PNG_MAGIC, 8) != 0)
    {
        ESP_LOGW(TAG, "not a PNG (%u bytes)", (unsigned)len);
        return NULL;
    }
    const uint32_t pw = be32(data + 16), ph = be32(data + 20);
    if (pw == 0 || ph == 0 || pw > MAX_SIDE || ph > MAX_SIDE)
    {
        ESP_LOGW(TAG, "picture %ux%u not decoded", (unsigned)pw, (unsigned)ph);
        return NULL;
    }
    const int64_t t0 = esp_timer_get_time();
    unsigned char *rgba = NULL;
    unsigned w = 0, h = 0;
    const unsigned err = lodepng_decode32(&rgba, &w, &h, data, len);
    const int64_t t1 = esp_timer_get_time();
    if (err || !rgba)
    {
        ESP_LOGW(TAG, "PNG decode error %u", err);
        lv_free(rgba);
        return NULL;
    }
    uint16_t *px = heap_caps_malloc(THUMB_SIZE * THUMB_SIZE * sizeof *px, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!px)
    {
        lv_free(rgba);
        return NULL;
    }
    const uint16_t bg = rgb565(BG_R, BG_G, BG_B);
    for (int i = 0; i < THUMB_SIZE * THUMB_SIZE; i++)
    {
        px[i] = bg;
    }
    const float sc = (float)(w > h ? w : h) / THUMB_SIZE; /* source pixels per output pixel */
    int dw = (int)((float)w / sc + 0.5f), dh = (int)((float)h / sc + 0.5f);
    dw = dw < 1 ? 1 : dw > THUMB_SIZE ? THUMB_SIZE : dw;
    dh = dh < 1 ? 1 : dh > THUMB_SIZE ? THUMB_SIZE : dh;
    const int ox = (THUMB_SIZE - dw) / 2, oy = (THUMB_SIZE - dh) / 2;
    for (int y = 0; y < dh; y++)
    {
        int sy0 = (int)((float)y * sc), sy1 = (int)((float)(y + 1) * sc);
        sy1 = sy1 <= sy0 ? sy0 + 1 : sy1;
        sy1 = sy1 > (int)h ? (int)h : sy1;
        for (int x = 0; x < dw; x++)
        {
            int sx0 = (int)((float)x * sc), sx1 = (int)((float)(x + 1) * sc);
            sx1 = sx1 <= sx0 ? sx0 + 1 : sx1;
            sx1 = sx1 > (int)w ? (int)w : sx1;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = sy0; sy < sy1; sy++)
            {
                const unsigned char *row = rgba + ((size_t)sy * w + (size_t)sx0) * 4;
                for (int sx = sx0; sx < sx1; sx++, row += 4)
                {
                    const uint32_t a = row[3]; /* over the tile's colour, so holes are not black */
                    r += (row[0] * a + BG_R * (255 - a)) / 255;
                    g += (row[1] * a + BG_G * (255 - a)) / 255;
                    b += (row[2] * a + BG_B * (255 - a)) / 255;
                    n++;
                }
            }
            if (n)
            {
                px[(oy + y) * THUMB_SIZE + ox + x] = rgb565((int)(r / n), (int)(g / n), (int)(b / n));
            }
        }
    }
    const int64_t t2 = esp_timer_get_time();
    lv_free(rgba);
    ESP_LOGI(TAG, "%ux%u PNG of %u bytes: decoded in %d ms, shrunk in %d ms", (unsigned)w, (unsigned)h, (unsigned)len,
             (int)((t1 - t0) / 1000), (int)((t2 - t1) / 1000));
    return px;
}


/* ---- Line by line --------------------------------------------------------
 *
 * The PNG's deflate stream is read with the ROM's tinfl in 32 KB pieces: each
 * line of the picture is unfiltered, then added to the output pixel it falls in
 * (a box average, with the picture's transparency laid over the tile's colour).
 * Nothing the size of the picture is ever held: about 80 KB of PSRAM in small
 * blocks, where decoding it whole asks for two blocks of a megabyte. */

typedef struct
{
    uint32_t sr[THUMB_SIZE], sg[THUMB_SIZE], sb[THUMB_SIZE];
    uint16_t n[THUMB_SIZE];
} acc_t_row;

static inline int paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

static void flush_row(acc_t_row *A, uint16_t *px, int row, int ox, int dw)
{
    for (int x = 0; x < dw; x++)
    {
        if (A->n[x])
        {
            px[row * THUMB_SIZE + ox + x] = rgb565((int)(A->sr[x] / A->n[x]), (int)(A->sg[x] / A->n[x]), (int)(A->sb[x] / A->n[x]));
        }
    }
    memset(A, 0, sizeof *A);
}

/* `*fallback` is set when the picture is a PNG this reader does not take. */
static uint16_t *shrink_lines(const uint8_t *d, size_t len, bool *fallback)
{
    static const uint8_t PNG_MAGIC[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    *fallback = false;
    if (len < 33 || memcmp(d, PNG_MAGIC, 8) != 0)
    {
        return NULL;
    }
    uint32_t w = 0, h = 0;
    int bits = 0, ctype = -1, interlace = 0, nplte = 0;
    uint8_t plte[256 * 3];
    uint8_t trns[256];
    memset(trns, 255, sizeof trns);
    size_t zlen = 0;
    for (size_t pos = 8; pos + 12 <= len;)
    {
        const uint32_t cl = be32(d + pos);
        const uint8_t *ty = d + pos + 4, *cd = d + pos + 8;
        if (cl > len - pos - 12)
        {
            break;
        }
        if (!memcmp(ty, "IHDR", 4) && cl >= 13)
        {
            w = be32(cd);
            h = be32(cd + 4);
            bits = cd[8];
            ctype = cd[9];
            interlace = cd[12];
        }
        else if (!memcmp(ty, "PLTE", 4))
        {
            nplte = (int)(cl / 3 > 256 ? 256 : cl / 3);
            memcpy(plte, cd, (size_t)nplte * 3);
        }
        else if (!memcmp(ty, "tRNS", 4) && ctype == 3)
        {
            memcpy(trns, cd, cl > 256 ? 256 : cl);
        }
        else if (!memcmp(ty, "IDAT", 4))
        {
            zlen += cl;
        }
        else if (!memcmp(ty, "IEND", 4))
        {
            break;
        }
        pos += 12 + (size_t)cl;
    }
    int bpp = 0; /* bytes per pixel, 8-bit samples */
    switch (ctype)
    {
    case 0: bpp = 1; break;
    case 2: bpp = 3; break;
    case 3: bpp = nplte > 0 ? 1 : 0; break;
    case 4: bpp = 2; break;
    case 6: bpp = 4; break;
    }
    if (w == 0 || h == 0 || w > MAX_SIDE || h > MAX_SIDE || zlen == 0)
    {
        return NULL;
    }
    if (bits != 8 || interlace != 0 || bpp == 0)
    {
        *fallback = true;
        return NULL;
    }
    const size_t rowbytes = (size_t)w * (size_t)bpp;
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    uint8_t *zbuf = heap_caps_malloc(zlen, caps);
    uint8_t *dict = heap_caps_malloc(TINFL_LZ_DICT_SIZE, caps);
    tinfl_decompressor *tinfl = heap_caps_malloc(sizeof *tinfl, caps);
    uint8_t *rowa = heap_caps_calloc(1, rowbytes + 1, caps);
    uint8_t *rowb = heap_caps_calloc(1, rowbytes + 1, caps);
    uint8_t *dxmap = heap_caps_malloc(w + h, caps);
    uint16_t *px = heap_caps_malloc(THUMB_SIZE * THUMB_SIZE * sizeof *px, caps);
    acc_t_row *A = heap_caps_calloc(1, sizeof *A, caps);
    uint16_t *ok = NULL;
    if (!zbuf || !dict || !tinfl || !rowa || !rowb || !dxmap || !px || !A)
    {
        ESP_LOGW(TAG, "no memory for the line reader (%u KB free, largest block %u KB)",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                 (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
        goto out;
    }
    /* The IDAT chunks, end to end: one zlib stream. */
    size_t zp = 0;
    for (size_t pos = 8; pos + 12 <= len && zp < zlen;)
    {
        const uint32_t cl = be32(d + pos);
        if (cl > len - pos - 12)
        {
            break;
        }
        if (!memcmp(d + pos + 4, "IDAT", 4))
        {
            memcpy(zbuf + zp, d + pos + 8, cl);
            zp += cl;
        }
        pos += 12 + (size_t)cl;
    }
    uint8_t *dymap = dxmap + w;
    const uint16_t bg = rgb565(BG_R, BG_G, BG_B);
    for (int i = 0; i < THUMB_SIZE * THUMB_SIZE; i++)
    {
        px[i] = bg;
    }
    const float sc = (float)(w > h ? w : h) / THUMB_SIZE;
    int dw = (int)((float)w / sc + 0.5f), dh = (int)((float)h / sc + 0.5f);
    dw = dw < 1 ? 1 : dw > THUMB_SIZE ? THUMB_SIZE : dw;
    dh = dh < 1 ? 1 : dh > THUMB_SIZE ? THUMB_SIZE : dh;
    const int ox = (THUMB_SIZE - dw) / 2, oy = (THUMB_SIZE - dh) / 2;
    for (uint32_t x = 0; x < w; x++)
    {
        const int v = (int)((float)x / sc);
        dxmap[x] = (uint8_t)(v >= dw ? dw - 1 : v);
    }
    for (uint32_t y = 0; y < h; y++)
    {
        const int v = (int)((float)y / sc);
        dymap[y] = (uint8_t)(v >= dh ? dh - 1 : v);
    }

    const int64_t t0 = esp_timer_get_time();
    tinfl_init(tinfl);
    uint8_t *cur = rowa, *prev = rowb; /* a line is its filter byte, then its samples */
    size_t fill = 0, in_pos = 0, dict_ofs = 0;
    uint32_t y = 0;
    int acc_row = 0;
    bool failed = false;
    while (!failed && y < h)
    {
        size_t in_bytes = zlen - in_pos, out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;
        const tinfl_status st = tinfl_decompress(tinfl, zbuf + in_pos, &in_bytes, dict, dict + dict_ofs, &out_bytes,
                                                 TINFL_FLAG_PARSE_ZLIB_HEADER);
        in_pos += in_bytes;
        const uint8_t *o = dict + dict_ofs;
        for (size_t i = 0; i < out_bytes && y < h; i++)
        {
            cur[fill++] = o[i];
            if (fill < rowbytes + 1)
            {
                continue;
            }
            fill = 0;
            /* Undo the line's filter, with the previous line (zeros before the first). */
            const int ft = cur[0];
            uint8_t *r = cur + 1, *pr = prev + 1;
            if (ft > 4)
            {
                failed = true;
                break;
            }
            for (size_t k = 0; k < rowbytes; k++)
            {
                const int a = k >= (size_t)bpp ? r[k - bpp] : 0, b = pr[k], c = k >= (size_t)bpp ? pr[k - bpp] : 0;
                switch (ft)
                {
                case 1: r[k] = (uint8_t)(r[k] + a); break;
                case 2: r[k] = (uint8_t)(r[k] + b); break;
                case 3: r[k] = (uint8_t)(r[k] + ((a + b) >> 1)); break;
                case 4: r[k] = (uint8_t)(r[k] + paeth(a, b, c)); break;
                default: break;
                }
            }
            if (dymap[y] != acc_row)
            {
                flush_row(A, px, oy + acc_row, ox, dw);
                acc_row = dymap[y];
            }
            for (uint32_t x = 0; x < w; x++)
            {
                int cr, cg, cb, ca = 255;
                const uint8_t *q = r + (size_t)x * (size_t)bpp;
                switch (ctype)
                {
                case 0: cr = cg = cb = q[0]; break;
                case 2: cr = q[0]; cg = q[1]; cb = q[2]; break;
                case 3: cr = plte[q[0] * 3]; cg = plte[q[0] * 3 + 1]; cb = plte[q[0] * 3 + 2]; ca = trns[q[0]]; break;
                case 4: cr = cg = cb = q[0]; ca = q[1]; break;
                default: cr = q[0]; cg = q[1]; cb = q[2]; ca = q[3]; break;
                }
                const int dx = dxmap[x];
                A->sr[dx] += (uint32_t)((cr * ca + BG_R * (255 - ca)) / 255);
                A->sg[dx] += (uint32_t)((cg * ca + BG_G * (255 - ca)) / 255);
                A->sb[dx] += (uint32_t)((cb * ca + BG_B * (255 - ca)) / 255);
                A->n[dx]++;
            }
            uint8_t *t = cur; /* this line is the next one's "previous" */
            cur = prev;
            prev = t;
            y++;
        }
        dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
        if (st <= TINFL_STATUS_DONE)
        {
            failed = failed || st < TINFL_STATUS_DONE;
            break;
        }
    }
    if (failed || y < h)
    {
        ESP_LOGW(TAG, "PNG stream ended after %u of %u lines", (unsigned)y, (unsigned)h);
        goto out;
    }
    flush_row(A, px, oy + acc_row, ox, dw);
    ESP_LOGI(TAG, "%ux%u PNG of %u bytes read line by line in %d ms (PSRAM free %u KB, largest block %u KB)", (unsigned)w,
             (unsigned)h, (unsigned)len, (int)((esp_timer_get_time() - t0) / 1000),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
    ok = px;
    px = NULL;
out:
    heap_caps_free(zbuf);
    heap_caps_free(dict);
    heap_caps_free(tinfl);
    heap_caps_free(rowa);
    heap_caps_free(rowb);
    heap_caps_free(dxmap);
    heap_caps_free(A);
    heap_caps_free(px);
    return ok;
}

/* ---- A JPEG ------------------------------------------------------------------
 *
 * The ROM's tjpgd (baseline JPEG) decodes it already scaled down by 2, 4 or 8, as far as the picture
 * stays at least as big as ours: what is then held is at most about 90 KB of RGB, whatever the size
 * of the file. It is averaged down to the tile from there, as the PNG is. A progressive JPEG is not
 * supported by tjpgd: no picture. */

#define JPG_POOL (16 * 1024) /* tjpgd's working memory: the tables, the MCU, the line buffers */

typedef struct
{
    const uint8_t *p;
    size_t pos, len;
    uint8_t *rgb; /* the decoded picture, 3 bytes a pixel */
    int sw, sh;   /* its size, after the scaling */
} jpg_t;

static UINT jpg_in(JDEC *jd, BYTE *buf, UINT n)
{
    jpg_t *j = jd->device;
    const size_t left = j->len - j->pos;
    if (n > left)
    {
        n = (UINT)left;
    }
    if (buf)
    {
        memcpy(buf, j->p + j->pos, n);
    }
    j->pos += n;
    return n;
}

static UINT jpg_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jpg_t *j = jd->device;
    const uint8_t *src = bitmap;
    const int w = r->right - r->left + 1;
    const int cw = r->left + w > j->sw ? j->sw - r->left : w; /* the MCUs reach past the picture's edge */
    for (int y = r->top; y <= r->bottom; y++, src += w * 3)
    {
        if (y < j->sh && cw > 0)
        {
            memcpy(j->rgb + ((size_t)y * (size_t)j->sw + r->left) * 3, src, (size_t)cw * 3);
        }
    }
    return 1;
}

/* A box average of `rgb` (w x h) into a THUMB_SIZE square, the picture kept in proportion. */
static uint16_t *scale_rgb(const uint8_t *rgb, int w, int h)
{
    uint16_t *px = heap_caps_malloc(THUMB_SIZE * THUMB_SIZE * sizeof *px, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!px)
    {
        return NULL;
    }
    const uint16_t bg = rgb565(BG_R, BG_G, BG_B);
    for (int i = 0; i < THUMB_SIZE * THUMB_SIZE; i++)
    {
        px[i] = bg;
    }
    const float sc = (float)(w > h ? w : h) / THUMB_SIZE;
    int dw = (int)((float)w / sc + 0.5f), dh = (int)((float)h / sc + 0.5f);
    dw = dw < 1 ? 1 : dw > THUMB_SIZE ? THUMB_SIZE : dw;
    dh = dh < 1 ? 1 : dh > THUMB_SIZE ? THUMB_SIZE : dh;
    const int ox = (THUMB_SIZE - dw) / 2, oy = (THUMB_SIZE - dh) / 2;
    for (int y = 0; y < dh; y++)
    {
        int sy0 = (int)((float)y * sc), sy1 = (int)((float)(y + 1) * sc);
        sy1 = sy1 <= sy0 ? sy0 + 1 : sy1;
        sy1 = sy1 > h ? h : sy1;
        for (int x = 0; x < dw; x++)
        {
            int sx0 = (int)((float)x * sc), sx1 = (int)((float)(x + 1) * sc);
            sx1 = sx1 <= sx0 ? sx0 + 1 : sx1;
            sx1 = sx1 > w ? w : sx1;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = sy0; sy < sy1; sy++)
            {
                const uint8_t *q = rgb + ((size_t)sy * (size_t)w + (size_t)sx0) * 3;
                for (int sx = sx0; sx < sx1; sx++, q += 3)
                {
                    r += q[0];
                    g += q[1];
                    b += q[2];
                    n++;
                }
            }
            if (n)
            {
                px[(oy + y) * THUMB_SIZE + ox + x] = rgb565((int)(r / n), (int)(g / n), (int)(b / n));
            }
        }
    }
    return px;
}

static uint16_t *shrink_jpeg(const uint8_t *data, size_t len)
{
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    void *pool = heap_caps_malloc(JPG_POOL, caps);
    JDEC *jd = heap_caps_calloc(1, sizeof *jd, caps);
    jpg_t j = {.p = data, .len = len};
    uint16_t *px = NULL;
    if (!pool || !jd)
    {
        goto out;
    }
    const int64_t t0 = esp_timer_get_time();
    const JRESULT pr = jd_prepare(jd, jpg_in, pool, JPG_POOL, &j);
    if (pr != JDR_OK)
    {
        ESP_LOGW(TAG, "JPEG not read (prepare %d: progressive or damaged?)", (int)pr);
        goto out;
    }
    const int w = (int)jd->width, h = (int)jd->height;
    if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE)
    {
        ESP_LOGW(TAG, "JPEG %dx%d not decoded", w, h);
        goto out;
    }
    int s = 0; /* the most the decoder may shrink it and leave it at least as big as the tile */
    while (s < 3 && ((w > h ? w : h) >> (s + 1)) >= THUMB_SIZE)
    {
        s++;
    }
    /* tjpgd cuts a partial MCU on the shifted size, so the picture is w >> s wide: rounding up would leave a
     * column the decoder never writes (seen as a dark edge on a 550 x 368 picture at 1/4). */
    j.sw = (w >> s) > 0 ? (w >> s) : 1;
    j.sh = (h >> s) > 0 ? (h >> s) : 1;
    j.rgb = heap_caps_calloc((size_t)j.sw * (size_t)j.sh, 3, caps);
    if (!j.rgb)
    {
        goto out;
    }
    const JRESULT dr = jd_decomp(jd, jpg_out, (BYTE)s);
    if (dr != JDR_OK)
    {
        ESP_LOGW(TAG, "JPEG decode error %d", (int)dr);
        goto out;
    }
    px = scale_rgb(j.rgb, j.sw, j.sh);
    ESP_LOGI(TAG, "%dx%d JPEG of %u bytes read in %d ms (scaled 1/%d first)", w, h, (unsigned)len,
             (int)((esp_timer_get_time() - t0) / 1000), 1 << s);
out:
    heap_caps_free(j.rgb);
    heap_caps_free(jd);
    heap_caps_free(pool);
    return px;
}

static uint16_t *shrink(const uint8_t *data, size_t len)
{
    if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8)
    {
        return shrink_jpeg(data, len);
    }
    bool fallback = false;
    uint16_t *px = shrink_lines(data, len, &fallback);
    if (!px && fallback)
    {
        px = shrink_whole(data, len);
    }
    else if (!px)
    {
        ESP_LOGW(TAG, "not a picture this reader takes (%u bytes)", (unsigned)len);
    }
    return px;
}

/* ---- The task ----------------------------------------------------------- */

static void install(uint16_t *px)
{
    const int idx = S.active == 0 ? 1 : 0; /* the picture on show stays untouched */
    heap_caps_free(S.frame[idx].px);
    frame_t *f = &S.frame[idx];
    memset(f, 0, sizeof *f);
    f->px = px;
    f->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    f->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    f->dsc.header.w = THUMB_SIZE;
    f->dsc.header.h = THUMB_SIZE;
    f->dsc.header.stride = THUMB_SIZE * 2;
    f->dsc.data_size = THUMB_SIZE * THUMB_SIZE * 2;
    f->dsc.data = (const uint8_t *)px;
    S.active = idx;
    S.version++;
    S.state = THUMB_READY;
    S.drop = false; /* this is the new picture: a look must not free it */
}

static void worker(void *arg)
{
    (void)arg;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));

        account_printer_t *pr = heap_caps_malloc(sizeof *pr, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!pr)
        {
            continue;
        }
        char file[sizeof S.file];
        thumb_fetch_fn fetch = NULL;
        uint32_t gen = 0;
        const bool room = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= MIN_INTERNAL;
        xSemaphoreTake(S.lock, portMAX_DELAY);
        if (room && S.want && S.state != THUMB_READY && S.tries < MAX_TRIES && esp_timer_get_time() >= S.next_try_us)
        {
            *pr = S.pr;
            strlcpy(file, S.file, sizeof file);
            fetch = S.fetch;
            gen = S.gen;
        }
        xSemaphoreGive(S.lock);

        if (fetch)
        {
            uint8_t *data = NULL;
            size_t len = 0;
            uint16_t *px = NULL;
            const int64_t t0 = esp_timer_get_time();
            if (fetch(pr, file, &data, &len) && data)
            {
                ESP_LOGI(TAG, "fetched %u bytes in %d ms", (unsigned)len, (int)((esp_timer_get_time() - t0) / 1000));
                px = shrink(data, len);
            }
            heap_caps_free(data);

            xSemaphoreTake(S.lock, portMAX_DELAY);
            if (gen != S.gen)
            {
                heap_caps_free(px); /* the wish changed while this ran */
            }
            else if (px)
            {
                install(px);
            }
            else
            {
                S.tries++;
                S.state = THUMB_NONE; /* the placeholder goes; a later try may still bring the picture */
                S.next_try_us = esp_timer_get_time() + RETRY_US;
            }
            xSemaphoreGive(S.lock);
        }
        heap_caps_free(pr);
    }
}

void thumb_want(const account_printer_t *p, thumb_fetch_fn fetch, const char *file)
{
    if (!S.lock)
    {
        S.lock = xSemaphoreCreateMutex();
        if (!S.lock)
        {
            return;
        }
    }
    const bool want = p && fetch && file && file[0];
    xSemaphoreTake(S.lock, portMAX_DELAY);
    if (!want)
    {
        if (S.want)
        {
            S.want = false;
            S.gen++;
            S.state = THUMB_NONE;
            S.drop = true;
        }
    }
    else if (!S.want || strcmp(S.pr.id, p->id) != 0 || strcmp(S.file, file) != 0)
    {
        S.pr = *p;
        strlcpy(S.file, file, sizeof S.file);
        S.fetch = fetch;
        S.want = true;
        S.gen++;
        S.state = THUMB_LOADING;
        S.tries = 0;
        S.next_try_us = 0;
        S.drop = true; /* the picture of the previous print goes */
    }
    const bool start = want && !S.task;
    xSemaphoreGive(S.lock);

    if (start)
    {
        if (xTaskCreatePinnedToCoreWithCaps(worker, "thumb", 12 * 1024, NULL, 2, &S.task, 0, MALLOC_CAP_SPIRAM) != pdPASS)
        {
            S.task = NULL;
            ESP_LOGW(TAG, "task not started");
        }
    }
    if (want && S.task)
    {
        xTaskNotifyGive(S.task);
    }
}

thumb_state_t thumb_get(const lv_image_dsc_t **img, uint32_t *version)
{
    if (!S.lock)
    {
        return THUMB_NONE;
    }
    xSemaphoreTake(S.lock, portMAX_DELAY);
    if (S.drop)
    {
        free_frames(); /* under the LVGL lock: the caller hides the widget in the same breath */
        S.drop = false;
    }
    thumb_state_t st = S.state;
    if (st == THUMB_READY)
    {
        if (S.active >= 0)
        {
            *img = &S.frame[S.active].dsc;
            *version = S.version;
        }
        else
        {
            st = THUMB_NONE;
        }
    }
    xSemaphoreGive(S.lock);
    return st;
}

void thumb_forget(void)
{
    if (!S.lock)
    {
        return;
    }
    xSemaphoreTake(S.lock, portMAX_DELAY);
    S.want = false;
    S.gen++;
    S.state = THUMB_NONE;
    free_frames();
    S.drop = false;
    xSemaphoreGive(S.lock);
}
