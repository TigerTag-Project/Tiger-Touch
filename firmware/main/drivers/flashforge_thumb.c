#include "flashforge_thumb.h"

#include <stdio.h>
#include <string.h>

#include "addr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "http_lite.h"
#include "json_scan.h"

static const char *TAG = "flashforge";

/* The answer of POST /detail, the one the driver already asks every two seconds, carries the address of
 * the slicer's picture: `printFileThumbUrl`, a full address of the printer's own file server (a JPEG, in
 * Tiger Studio Manager's notes). Only its path is used, on the address the account gives, so the request
 * cannot be sent anywhere else. It needs no authentication. Plain HTTP on the local network. */
bool flashforge_thumb_fetch(const account_printer_t *p, const char *file, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    if (!p->ip[0] || !p->sn[0] || p->cloud || !file[0])
    {
        return false;
    }
    char hp[ADDR_HOSTPORT_MAX], sn[48], url[300], body[160], name[96] = "", thumb[200] = "";
    addr_with_port(hp, sizeof hp, p->ip, 8898);
    snprintf(sn, sizeof sn, "%s%s", strncmp(p->sn, "SN", 2) == 0 ? "" : "SN", p->sn);
    snprintf(url, sizeof url, "http://%s/detail", hp);
    snprintf(body, sizeof body, "{\"serialNumber\":\"%s\",\"checkCode\":\"%s\"}", sn, p->cc);
    const size_t cap = 8 * 1024;
    char *reply = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!reply)
    {
        return false;
    }
    const http_lite_req_t req = {.url = url, .method = HTTP_METHOD_POST, .body = body, .timeout_ms = 4000};
    const bool ok = http_lite(&req, reply, cap) == 200;
    if (ok)
    {
        const char *end = reply + strlen(reply);
        if (!json_scan_field_str(reply, end, "printFileName", name, sizeof name))
        {
            json_scan_field_str(reply, end, "printJobName", name, sizeof name);
        }
        json_scan_field_str(reply, end, "printFileThumbUrl", thumb, sizeof thumb);
    }
    heap_caps_free(reply);
    memset(body, 0, sizeof body); /* it held the check code */
    /* `file` is the name without its extension, cut at the view's width. */
    if (!ok || !name[0] || strncmp(name, file, strlen(file)) != 0)
    {
        ESP_LOGI(TAG, "no detail, or the printing file is not the one the view names");
        return false;
    }
    const char *path = strstr(thumb, "://");
    path = path ? strchr(path + 3, '/') : (thumb[0] == '/' ? thumb : NULL);
    if (!path)
    {
        ESP_LOGI(TAG, "no picture in the detail");
        return false;
    }
    snprintf(url, sizeof url, "http://%s%s", hp, path);
    return thumb_http_get_key(url, NULL, 256 * 1024, data, len) == 200;
}
