#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Reading a few fields out of a JSON document as text, without building a cJSON tree: a tree is
 * hundreds of small blocks, and those land in internal RAM. For the picture fetches, whose
 * documents are a few KB to a hundred and come from the makers' servers. */

/* The string whose opening quote is at `p`, into `out`, its escapes undone; the pointer after the
 * closing quote, or NULL. */
const char *json_scan_str(const char *p, char *out, size_t cap);

/* Just after the '}' that closes the object opening at `p`, or NULL. */
const char *json_scan_obj_end(const char *p);

/* The string value of `key` inside [s, e). */
bool json_scan_field_str(const char *s, const char *e, const char *key, char *out, size_t cap);

/* The number value of `key` inside [s, e). */
bool json_scan_field_num(const char *s, const char *e, const char *key, double *out);
