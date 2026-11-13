#include "octoprint_thumb.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "json_scan.h"

static const char *TAG = "octoprint";

/* OctoPrint keeps the slicer's picture only when a thumbnail plugin made it: the file's record
 * (`GET /api/files/local/<path>`) then has a `thumbnail` address. Without the plugin the field is
 * null and there is no picture. As Tiger Studio Manager does: the file is the one `/api/job` names, its
 * record gives the address, and the address is fetched with the same API key. Plain HTTP on the local
 * network. */
bool octoprint_thumb_fetch(const account_printer_t *p, const char *file, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    if (!p->ip[0] || p->cloud || !file[0] || !p->key[0])
    {
        return false;
    }
    char base[56], url[640], path[200] = "", origin[16] = "", rel[300];
    snprintf(base, sizeof base, "http://%s", p->ip); /* "host" or "host:port", as the driver reads it */
    uint8_t *js = NULL;
    size_t jl = 0;

    /* 1. The file that is printing, with its folder. */
    snprintf(url, sizeof url, "%s/api/job", base);
    if (thumb_http_get_key(url, p->key, 16 * 1024, &js, &jl) != 200)
    {
        return false;
    }
    const char *fk = strstr((const char *)js, "\"file\"");
    const char *fo = fk ? strchr(fk, '{') : NULL;
    const char *fe = fo ? json_scan_obj_end(fo) : NULL;
    if (fe)
    {
        if (!json_scan_field_str(fo, fe, "path", path, sizeof path))
        {
            json_scan_field_str(fo, fe, "name", path, sizeof path);
        }
        json_scan_field_str(fo, fe, "origin", origin, sizeof origin);
    }
    heap_caps_free(js);
    js = NULL;
    const char *base_name = strrchr(path, '/');
    base_name = base_name ? base_name + 1 : path;
    /* A file on the printer's SD card has no record here; `file` is the name without its extension. */
    if (!path[0] || (origin[0] && strcmp(origin, "local") != 0) || strncmp(base_name, file, strlen(file)) != 0)
    {
        ESP_LOGI(TAG, "the printing file is not a local one the view names");
        return false;
    }

    /* 2. Its record: the address of the picture. */
    size_t n = (size_t)snprintf(url, sizeof url, "%s/api/files/local/", base);
    thumb_url_append(url, n, sizeof url, path, true);
    if (thumb_http_get_key(url, p->key, 16 * 1024, &js, &jl) != 200)
    {
        return false;
    }
    const bool has = json_scan_field_str((const char *)js, (const char *)js + jl, "thumbnail", rel, sizeof rel);
    heap_caps_free(js);
    if (!has || !rel[0])
    {
        ESP_LOGI(TAG, "no picture in the file's record (no thumbnail plugin)");
        return false;
    }

    /* 3. The picture, its address as the record gives it (it may carry a "?" and a stamp). */
    const char *r = rel;
    while (*r == '/')
    {
        r++;
    }
    snprintf(url, sizeof url, "%s/%s", base, r);
    return thumb_http_get_key(url, p->key, 256 * 1024, data, len) == 200;
}
