#include "moonraker_thumb.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "addr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "json_scan.h"

static const char *TAG = "moonraker";

/* The slicer's pictures of a G-code file are listed in its metadata (`thumbnails`: width, height and a
 * path relative to the file's own folder) and served with the file under /server/files/gcodes/. Tiger
 * Studio Manager reads them the same way. The file is asked of Moonraker (`print_stats.filename`, with
 * its folder) rather than rebuilt from the name on the view, which has lost the folder and the
 * extension. Plain HTTP on the local network: no TLS, so almost nothing in internal RAM. */
bool moonraker_thumb_fetch(const account_printer_t *p, const char *file, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    if (!p->ip[0] || p->cloud || !file[0])
    {
        return false;
    }
    char hp[ADDR_HOSTPORT_MAX];
    addr_with_port(hp, sizeof hp, p->ip, 7125);
    const char *key = p->key[0] ? p->key : NULL;
    char url[640], path[560], fname[200] = "";
    uint8_t *js = NULL;
    size_t jl = 0;

    /* 1. The file that is printing, with its folder. */
    snprintf(url, sizeof url, "http://%s/printer/objects/query?print_stats=filename", hp);
    if (thumb_http_get_key(url, key, 8 * 1024, &js, &jl) != 200)
    {
        return false;
    }
    const char *ps = strstr((const char *)js, "\"print_stats\"");
    const char *fk = ps ? strstr(ps, "\"filename\"") : NULL;
    if (fk)
    {
        const char *v = fk + strlen("\"filename\"");
        while (*v == ' ' || *v == ':')
        {
            v++;
        }
        if (*v == '"')
        {
            json_scan_str(v, fname, sizeof fname);
        }
    }
    heap_caps_free(js);
    js = NULL;
    const char *base = strrchr(fname, '/');
    base = base ? base + 1 : fname;
    /* `file` is the name without ".gcode", cut at the view's width: it must be what Moonraker names. */
    if (!fname[0] || strncmp(base, file, strlen(file)) != 0)
    {
        ESP_LOGI(TAG, "the printing file is not the one the view names");
        return false;
    }

    /* 2. Its metadata: the pictures it carries. */
    size_t n = (size_t)snprintf(path, sizeof path, "/server/files/metadata?filename=");
    thumb_url_append(path, n, sizeof path, fname, false);
    snprintf(url, sizeof url, "http://%s%s", hp, path);
    if (thumb_http_get_key(url, key, 64 * 1024, &js, &jl) != 200)
    {
        return false;
    }
    /* The smallest picture that is at least as big as ours, else the largest there is. */
    char best[200] = "";
    int best_side = 0;
    bool best_big = false;
    const char *ta = strstr((const char *)js, "\"thumbnails\"");
    const char *q = ta ? strchr(ta, '[') : NULL;
    for (q = q ? q + 1 : NULL; q;)
    {
        q = strpbrk(q, "{]");
        if (!q || *q == ']')
        {
            break;
        }
        const char *e = json_scan_obj_end(q);
        if (!e)
        {
            break;
        }
        char rel[200];
        double w = 0, h = 0;
        if (json_scan_field_str(q, e, "relative_path", rel, sizeof rel))
        {
            json_scan_field_num(q, e, "width", &w);
            json_scan_field_num(q, e, "height", &h);
            const int side = (int)(w > h ? w : h);
            const bool big = side >= THUMB_SIZE;
            if (!best[0] || (big && (!best_big || side < best_side)) || (!big && !best_big && side > best_side))
            {
                strlcpy(best, rel, sizeof best);
                best_side = side;
                best_big = big;
            }
        }
        q = e;
    }
    heap_caps_free(js);
    if (!best[0])
    {
        ESP_LOGI(TAG, "no picture in the file's metadata");
        return false;
    }

    /* 3. The picture, its path being relative to the file's own folder. */
    char full[420], dir[200] = "";
    const char *sl = strrchr(fname, '/');
    if (sl && (size_t)(sl - fname) + 2 < sizeof dir)
    {
        memcpy(dir, fname, (size_t)(sl - fname) + 1);
        dir[sl - fname + 1] = '\0';
    }
    snprintf(full, sizeof full, "%s%s", dir, best);
    n = (size_t)snprintf(path, sizeof path, "/server/files/gcodes/");
    thumb_url_append(path, n, sizeof path, full, true);
    snprintf(url, sizeof url, "http://%s%s", hp, path);
    return thumb_http_get_key(url, key, 256 * 1024, data, len) == 200;
}
