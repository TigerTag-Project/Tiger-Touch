#include "json_scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The JSON string whose opening quote is at `p`, into `out`, with its escapes
 * undone; the pointer after the closing quote, or NULL. */
const char *json_scan_str(const char *p, char *out, size_t cap)
{
    size_t n = 0;
    for (p++; *p && *p != '"'; p++)
    {
        char c = *p;
        if (c == '\\')
        {
            c = *++p;
            if (c == 'u')
            {
                unsigned u = 0;
                for (int i = 0; i < 4; i++)
                {
                    const char h = *++p;
                    if (!h)
                    {
                        return NULL;
                    }
                    u = (u << 4) | (unsigned)(h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                }
                char b[3];
                int k;
                if (u < 0x80)
                {
                    b[0] = (char)u;
                    k = 1;
                }
                else if (u < 0x800)
                {
                    b[0] = (char)(0xC0 | (u >> 6));
                    b[1] = (char)(0x80 | (u & 0x3F));
                    k = 2;
                }
                else
                {
                    b[0] = (char)(0xE0 | (u >> 12));
                    b[1] = (char)(0x80 | ((u >> 6) & 0x3F));
                    b[2] = (char)(0x80 | (u & 0x3F));
                    k = 3;
                }
                for (int i = 0; i < k && n + 1 < cap; i++)
                {
                    out[n++] = b[i];
                }
                continue;
            }
            if (c == 'n')
            {
                c = '\n';
            }
            else if (c == 't')
            {
                c = '\t';
            }
            else if (!c)
            {
                break;
            }
        }
        if (n + 1 < cap)
        {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return *p == '"' ? p + 1 : NULL;
}

/* Just after the '}' that closes the object opening at `p`, or NULL. */
const char *json_scan_obj_end(const char *p)
{
    int depth = 0;
    bool in = false;
    for (; *p; p++)
    {
        if (in)
        {
            if (*p == '\\' && p[1])
            {
                p++;
            }
            else if (*p == '"')
            {
                in = false;
            }
        }
        else if (*p == '"')
        {
            in = true;
        }
        else if (*p == '{')
        {
            depth++;
        }
        else if (*p == '}' && --depth == 0)
        {
            return p + 1;
        }
    }
    return NULL;
}

/* The string value of `key` inside [s, e). */
bool json_scan_field_str(const char *s, const char *e, const char *key, char *out, size_t cap)
{
    char pat[16];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const size_t pl = strlen(pat);
    for (const char *q = s; (q = strstr(q, pat)) && q < e; q += pl)
    {
        const char *v = q + pl;
        while (*v == ' ' || *v == ':')
        {
            v++;
        }
        if (*v == '"')
        {
            return json_scan_str(v, out, cap) != NULL;
        }
    }
    return false;
}

bool json_scan_field_num(const char *s, const char *e, const char *key, double *out)
{
    char pat[16];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const size_t pl = strlen(pat);
    for (const char *q = s; (q = strstr(q, pat)) && q < e; q += pl)
    {
        const char *v = q + pl;
        while (*v == ' ' || *v == ':')
        {
            v++;
        }
        if ((*v >= '0' && *v <= '9') || *v == '-')
        {
            *out = strtod(v, NULL);
            return true;
        }
    }
    return false;
}
