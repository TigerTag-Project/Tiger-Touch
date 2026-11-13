#include "prusa_thumb.h"

#include <stdio.h>
#include <string.h>

#include "addr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "json_scan.h"

static const char *TAG = "prusa";

/* PrusaLink names the print in progress, with the address of its pictures, in `GET /api/v1/job`:
 * `file.display_name` and `file.refs` (`thumbnail`, else `icon`: a path on the printer). As Tiger Studio
 * Manager reads it. The requests are authenticated the way the driver's are: Digest with the user and the
 * password of the account, or only the `X-Api-Key` when the account holds a legacy key. Plain HTTP on
 * the local network. */
bool prusa_thumb_fetch(const account_printer_t *p, const char *file, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    if (!p->ip[0] || p->cloud || !file[0])
    {
        return false;
    }
    char hp[ADDR_HOSTPORT_MAX];
    addr_with_port(hp, sizeof hp, p->ip, 80);
    const char *key = p->key[0] ? p->key : NULL;
    const char *user = key ? NULL : (p->user[0] ? p->user : "maker");
    char url[300], name[96] = "", ref[200] = "";
    uint8_t *js = NULL;
    size_t jl = 0;

    snprintf(url, sizeof url, "http://%s/api/v1/job", hp);
    if (thumb_http_get_auth(url, user, p->cc, key, 16 * 1024, &js, &jl) != 200)
    {
        return false;
    }
    const char *fk = strstr((const char *)js, "\"file\"");
    const char *fo = fk ? strchr(fk, '{') : NULL;
    const char *fe = fo ? json_scan_obj_end(fo) : NULL;
    if (fe)
    {
        if (!json_scan_field_str(fo, fe, "display_name", name, sizeof name))
        {
            json_scan_field_str(fo, fe, "name", name, sizeof name);
        }
        const char *rk = strstr(fo, "\"refs\"");
        const char *ro = rk && rk < fe ? strchr(rk, '{') : NULL;
        const char *re = ro ? json_scan_obj_end(ro) : NULL;
        if (re && !json_scan_field_str(ro, re, "thumbnail", ref, sizeof ref))
        {
            json_scan_field_str(ro, re, "icon", ref, sizeof ref);
        }
    }
    heap_caps_free(js);
    /* `file` is the name without its extension, cut at the view's width. */
    if (!name[0] || strncmp(name, file, strlen(file)) != 0)
    {
        ESP_LOGI(TAG, "the printing job is not the one the view names");
        return false;
    }
    if (!ref[0])
    {
        ESP_LOGI(TAG, "no picture in the job's file");
        return false;
    }
    snprintf(url, sizeof url, "http://%s%s%s", hp, ref[0] == '/' ? "" : "/", ref);
    return thumb_http_get_auth(url, user, p->cc, key, 256 * 1024, data, len) == 200;
}
