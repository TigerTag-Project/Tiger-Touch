#include "bambu_lan_thumb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#include "addr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "json_scan.h"
#include "mbedtls/md5.h"

static const char *TAG = "bambu-lan";

/* Bambu's own LAN file protocol, as ha-bambulab speaks it (media_sources.py, Tcp6000MediaSource): TLS
 * on port 6000, every message a frame of a 16-byte header (payload length, magic, sequence: three
 * little-endian words, then zeros) and its payload. Login (16 bytes: "bblp" and the access code, 8
 * each, padded with zeros), a fixed JSON setup, then a JSON download request whose answers are a JSON
 * object, a blank line and a piece of the file, `result` 1 meaning more is coming and 0 the end. */
#define PORT 6000
#define MAGIC_LOGIN 0x0101013Fu
#define MAGIC_CTRL 0x0102013Fu
#define MTYPE_SETUP 12291
#define MTYPE_JSON 12289
#define MAX_FRAME (256 * 1024)
#define MAX_FILE (256 * 1024)
#define TOTAL_US (25LL * 1000 * 1000)

typedef struct
{
    esp_tls_t *tls;
    uint32_t seq;
    int64_t deadline_us;
} sess_t;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool write_all(sess_t *s, const uint8_t *d, size_t n)
{
    while (n)
    {
        if (esp_timer_get_time() > s->deadline_us)
        {
            return false;
        }
        const int w = esp_tls_conn_write(s->tls, d, n);
        if (w == ESP_TLS_ERR_SSL_WANT_READ || w == ESP_TLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }
        if (w <= 0)
        {
            return false;
        }
        d += w;
        n -= (size_t)w;
    }
    return true;
}

static bool read_all(sess_t *s, uint8_t *d, size_t n)
{
    while (n)
    {
        if (esp_timer_get_time() > s->deadline_us)
        {
            return false;
        }
        const int r = esp_tls_conn_read(s->tls, d, n);
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE)
        {
            continue; /* the socket's timeout came: try again until the deadline */
        }
        if (r <= 0)
        {
            return false;
        }
        d += r;
        n -= (size_t)r;
    }
    return true;
}

static bool send_frame(sess_t *s, uint32_t magic, const void *payload, size_t n)
{
    uint8_t h[16] = {0};
    put32(h, (uint32_t)n);
    put32(h + 4, magic);
    put32(h + 8, s->seq++);
    return write_all(s, h, sizeof h) && (n == 0 || write_all(s, payload, n));
}

/* One frame's payload, in PSRAM and NUL-terminated (the caller frees it). */
static bool read_frame(sess_t *s, uint8_t **payload, size_t *plen)
{
    uint8_t h[16];
    *payload = NULL;
    if (!read_all(s, h, sizeof h))
    {
        return false;
    }
    const uint32_t n = get32(h);
    if (n > MAX_FRAME)
    {
        return false;
    }
    uint8_t *p = heap_caps_malloc((size_t)n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)
    {
        return false;
    }
    if (n && !read_all(s, p, n))
    {
        heap_caps_free(p);
        return false;
    }
    p[n] = '\0';
    *payload = p;
    *plen = n;
    return true;
}

/* `<folder of the gcode file>/plate_<N>.png`, N read from the file's own name (plate_N.gcode), 1 else. */
static bool plate_path(const char *gcode, char *out, size_t cap)
{
    const char *slash = strrchr(gcode, '/');
    if (!slash)
    {
        return false;
    }
    int plate = 1;
    const char *pl = strstr(slash + 1, "plate_");
    if (pl && atoi(pl + 6) > 0)
    {
        plate = atoi(pl + 6);
    }
    const int folder = (int)(slash - gcode) + 1;
    return snprintf(out, cap, "%.*splate_%d.png", folder, gcode, plate) < (int)cap;
}

bool bambu_lan_thumb_fetch(const char *ip, const char *access_code, const char *gcode_file, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    char path[160];
    if (!ip[0] || !access_code[0] || !gcode_file[0] || !plate_path(gcode_file, path, sizeof path))
    {
        return false;
    }
    char host[ADDR_HOSTPORT_MAX];
    addr_host_only(host, sizeof host, ip);

    esp_tls_t *tls = esp_tls_init();
    if (!tls)
    {
        return false;
    }
    esp_tls_cfg_t cfg = {.skip_common_name = true, .timeout_ms = 5000}; /* the printer's own certificate */
    if (esp_tls_conn_new_sync(host, (int)strlen(host), PORT, &cfg, tls) != 1)
    {
        ESP_LOGI(TAG, "no connection to the printer's port 6000");
        esp_tls_conn_destroy(tls);
        return false;
    }
    int fd = -1;
    if (esp_tls_get_conn_sockfd(tls, &fd) == ESP_OK)
    {
        struct timeval tv = {.tv_sec = 3};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    sess_t s = {.tls = tls, .seq = 1, .deadline_us = esp_timer_get_time() + TOTAL_US};
    uint8_t *frame = NULL, *file = NULL;
    size_t flen = 0, fcap = 0, plen = 0;
    bool ok = false;
    mbedtls_md5_context md5;
    mbedtls_md5_init(&md5);
    mbedtls_md5_starts(&md5);

    /* 1. Login. */
    uint8_t login[16] = {0};
    memcpy(login, "bblp", 4);
    memcpy(login + 8, access_code, strnlen(access_code, 8));
    if (!send_frame(&s, MAGIC_LOGIN, login, sizeof login) || !read_frame(&s, &frame, &plen))
    {
        ESP_LOGI(TAG, "login not answered");
        goto out;
    }
    memset(login, 0, sizeof login);
    heap_caps_free(frame);
    frame = NULL;

    /* 2. Setup. */
    char text[360];
    int n = snprintf(text, sizeof text,
                     "{\"sequence\":0,\"mtype\":%d,\"req\":{\"t_av\":1,\"mtype\":%d,\"peer_t\":3,\"pid\":\"%08x\","
                     "\"ver\":\"02.03.00.00\"}}",
                     MTYPE_SETUP, MTYPE_JSON, (unsigned)esp_random());
    if (!send_frame(&s, MAGIC_CTRL, text, (size_t)n) || !read_frame(&s, &frame, &plen))
    {
        ESP_LOGI(TAG, "setup not answered");
        goto out;
    }
    double r = -1;
    const char *je = frame[0] == '{' ? json_scan_obj_end((const char *)frame) : NULL;
    if (!je || !json_scan_field_num((const char *)frame, je, "result", &r) || r != 0)
    {
        ESP_LOGI(TAG, "setup refused (result %d)", (int)r);
        goto out;
    }
    heap_caps_free(frame);
    frame = NULL;

    /* 3. The picture. */
    n = snprintf(text, sizeof text, "{\"mtype\":%d,\"cmdtype\":4,\"sequence\":1,\"req\":{\"path\":\"%s\",\"offset\":0}}",
                 MTYPE_JSON, path);
    if (n >= (int)sizeof text || !send_frame(&s, MAGIC_CTRL, text, (size_t)n))
    {
        goto out;
    }
    double total = 0;
    char want_md5[40] = "";
    for (;;)
    {
        if (!read_frame(&s, &frame, &plen))
        {
            ESP_LOGI(TAG, "the download stopped");
            goto out;
        }
        je = frame[0] == '{' ? json_scan_obj_end((const char *)frame) : NULL;
        double seq = -1;
        if (!je || !json_scan_field_num((const char *)frame, je, "sequence", &seq) || seq != 1)
        {
            heap_caps_free(frame); /* another answer: not ours */
            frame = NULL;
            continue;
        }
        const uint8_t *chunk = (const uint8_t *)je;
        const size_t rest = (size_t)((const uint8_t *)frame + plen - chunk); /* what follows the JSON */
        if (rest >= 2 && chunk[0] == '\n' && chunk[1] == '\n')
        {
            chunk += 2;
        }
        else if (rest >= 4 && !memcmp(chunk, "\r\n\r\n", 4))
        {
            chunk += 4;
        }
        const size_t clen = (size_t)((const uint8_t *)frame + plen - chunk);
        double mem = 0;
        const bool is_param = json_scan_field_num((const char *)frame, je, "mem_dl_param_size", &mem) && mem != 0;
        if (clen && !is_param)
        {
            if (flen + clen > MAX_FILE)
            {
                goto out;
            }
            if (flen + clen > fcap)
            {
                const size_t nc = fcap ? fcap * 2 : 32 * 1024;
                uint8_t *nb = heap_caps_realloc(file, nc > flen + clen ? nc : flen + clen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!nb)
                {
                    goto out;
                }
                file = nb;
                fcap = nc > flen + clen ? nc : flen + clen;
            }
            memcpy(file + flen, chunk, clen);
            mbedtls_md5_update(&md5, chunk, clen);
            flen += clen;
        }
        double res = -1;
        json_scan_field_num((const char *)frame, je, "result", &res);
        if (res == 1)
        {
            heap_caps_free(frame);
            frame = NULL;
            continue; /* more is coming */
        }
        if (res == 0)
        {
            json_scan_field_num((const char *)frame, je, "total", &total);
            json_scan_field_str((const char *)frame, je, "file_md5", want_md5, sizeof want_md5);
            break;
        }
        ESP_LOGI(TAG, "the printer refused the picture (result %d)", (int)res);
        goto out;
    }
    uint8_t sum[16];
    mbedtls_md5_finish(&md5, sum);
    char have_md5[33];
    for (int i = 0; i < 16; i++)
    {
        snprintf(have_md5 + i * 2, 3, "%02x", sum[i]);
    }
    if ((total > 0 && (size_t)total != flen) || (want_md5[0] && strcasecmp(want_md5, have_md5) != 0) || flen < 8 ||
        memcmp(file, "\x89PNG", 4) != 0)
    {
        ESP_LOGI(TAG, "the picture is not whole or not a PNG (%u bytes)", (unsigned)flen);
        goto out;
    }
    *data = file;
    *len = flen;
    file = NULL;
    ok = true;
out:
    heap_caps_free(frame);
    heap_caps_free(file);
    mbedtls_md5_free(&md5);
    esp_tls_conn_destroy(tls);
    return ok;
}
