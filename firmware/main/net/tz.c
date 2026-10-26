#include "tz.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "settings.h"
#include "tz_table.h"

static const char *TAG = "tz";

/* What the account gave last time, so that the zone is right at the next power-on
 * before the account has been read again. */
#define KEY_ACCOUNT "tz_a"
/* The line the person picked, by the name of its zone, so that the list can change without
 * moving what was kept. */
#define KEY_MANUAL "tz_m"

#define CODE_MAX 6 /* "FR" or "CA-QC", and the NUL */

static char s_account[CODE_MAX];
static char s_router[CODE_MAX];
static tz_source_t s_source = TZ_NONE;
static bool s_logged;
static int s_manual = -1; /* index in TZ_PICKS, -1 for none */

static const char *rule_for(const char *code)
{
    size_t lo = 0;
    size_t hi = sizeof TZ_BY_CODE / sizeof TZ_BY_CODE[0];
    while (lo < hi)
    {
        const size_t mid = (lo + hi) / 2;
        const int c = strcmp(code, TZ_BY_CODE[mid].code);
        if (c == 0)
        {
            return TZ_RULES[TZ_BY_CODE[mid].rule];
        }
        if (c < 0)
        {
            hi = mid;
        }
        else
        {
            lo = mid + 1;
        }
    }
    return NULL;
}

/* "fr" -> "FR", "ca-qc" -> "CA-QC"; false for anything that is not one of the
 * two shapes (the account can also hold "CUSTOM"). */
static bool normalise(const char *in, char out[CODE_MAX])
{
    out[0] = '\0';
    if (!in)
    {
        return false;
    }
    const size_t n = strlen(in);
    if (n != 2 && n != 5)
    {
        return false;
    }
    for (size_t i = 0; i < n; i++)
    {
        const bool dash = (n == 5 && i == 2);
        if (dash ? in[i] != '-' : !isalpha((unsigned char)in[i]))
        {
            return false;
        }
        out[i] = dash ? '-' : (char)toupper((unsigned char)in[i]);
    }
    out[n] = '\0';
    return true;
}

/* The account holds two countries. The one picked for the ranking is the person's
 * own; the one of the prices can be a Canadian province, which names a finer zone
 * than "CA" does, and is used when it agrees with the country. */
static bool account_code(const char *squad, const char *vat, char out[CODE_MAX])
{
    char s[CODE_MAX];
    char v[CODE_MAX];
    const bool has_s = normalise(squad, s);
    const bool has_v = normalise(vat, v);
    const bool province = has_v && strlen(v) == 5;

    if (province && (!has_s || strcmp(s, "CA") == 0) && rule_for(v))
    {
        strlcpy(out, v, CODE_MAX);
        return true;
    }
    if (has_s && strlen(s) == 2 && rule_for(s))
    {
        strlcpy(out, s, CODE_MAX);
        return true;
    }
    if (has_v)
    {
        char country[3] = {v[0], v[1], '\0'};
        if (rule_for(country))
        {
            strlcpy(out, country, CODE_MAX);
            return true;
        }
    }
    return false;
}

static void apply(const char *rule)
{
    setenv("TZ", rule, 1);
    tzset();
}

#define PICK_COUNT ((int)(sizeof TZ_PICKS / sizeof TZ_PICKS[0]))

/* The line picked by hand, else the account's country, else the access point's, else UTC. */
static void settle(void)
{
    const char *rule;
    tz_source_t src;
    if (s_manual >= 0 && s_manual < PICK_COUNT)
    {
        rule = TZ_RULES[TZ_PICKS[s_manual].rule];
        src = TZ_MANUAL;
    }
    else if (s_account[0] && (rule = rule_for(s_account)) != NULL)
    {
        src = TZ_ACCOUNT;
    }
    else if (s_router[0] && (rule = rule_for(s_router)) != NULL)
    {
        src = TZ_ROUTER;
    }
    else
    {
        rule = "UTC0";
        src = TZ_NONE;
    }
    apply(rule);
    if (!s_logged || src != s_source)
    {
        /* Which source, never which country: a country is where somebody lives. */
        ESP_LOGI(TAG, "time zone from %s", src == TZ_MANUAL    ? "the setting"
                                           : src == TZ_ACCOUNT ? "the account"
                                           : src == TZ_ROUTER  ? "the access point"
                                                               : "nothing known (UTC)");
        s_logged = true;
    }
    s_source = src;
}

static int pick_index(const char *zone)
{
    for (int i = 0; i < PICK_COUNT; i++)
    {
        if (strcmp(zone, TZ_PICKS[i].zone) == 0)
        {
            return i;
        }
    }
    return -1;
}

void tz_init(void)
{
    char saved[CODE_MAX];
    char zone[40];
    s_account[0] = s_router[0] = '\0';
    s_manual = settings_get(KEY_MANUAL, zone, sizeof zone) ? pick_index(zone) : -1;
    if (settings_get(KEY_ACCOUNT, saved, sizeof saved) && normalise(saved, s_account) && !rule_for(s_account))
    {
        s_account[0] = '\0';
    }
    settle();
}

tz_source_t tz_from_router(const char *country)
{
    char c[CODE_MAX];
    s_router[0] = '\0';
    if (normalise(country, c) && strlen(c) == 2)
    {
        strlcpy(s_router, c, sizeof s_router);
    }
    settle();
    return s_source;
}

tz_source_t tz_from_account(const char *squad, const char *vat)
{
    char code[CODE_MAX];
    if (account_code(squad, vat, code))
    {
        if (strcmp(code, s_account) != 0)
        {
            settings_set(KEY_ACCOUNT, code);
        }
        strlcpy(s_account, code, sizeof s_account);
    }
    else
    {
        if (s_account[0])
        {
            settings_erase(KEY_ACCOUNT);
        }
        s_account[0] = '\0';
    }
    settle();
    return s_source;
}

tz_source_t tz_source(void)
{
    return s_source;
}

/* ---- The list to pick from -------------------------------------------------- */

int tz_pick_count(void)
{
    return PICK_COUNT;
}

void tz_pick_city(int pick, char *out, size_t n)
{
    if (n == 0)
    {
        return;
    }
    out[0] = '\0';
    if (pick < 0 || pick >= PICK_COUNT)
    {
        return;
    }
    const char *places = TZ_PICKS[pick].places;
    const char *comma = strchr(places, ',');
    size_t len = comma ? (size_t)(comma - places) : strlen(places);
    if (len >= n)
    {
        len = n - 1;
    }
    memcpy(out, places, len);
    out[len] = '\0';
}

const char *tz_pick_places(int pick)
{
    return pick >= 0 && pick < PICK_COUNT ? TZ_PICKS[pick].places : "";
}

bool tz_pick_summer(int pick)
{
    return pick >= 0 && pick < PICK_COUNT && strstr(TZ_RULES[TZ_PICKS[pick].rule], "DST") != NULL;
}

/* ---- What a line gives at an instant ---------------------------------------
 *
 * The rules are the generator's own, in one shape: "STD<off>" or
 * "STD<off>DST[<off>],Mm.w.d[/time],Mm.w.d[/time]", an offset being written west of UTC
 * as in POSIX. This follows make_tz_table.py's offset_at(), which checked every rule
 * against the database. The C library's own conversion is not used: it answers for the
 * zone in force only, and the list shows sixty-three. */

static bool read_hms(const char **p, int *secs)
{
    const char *s = *p;
    int sign = 1;
    if (*s == '-')
    {
        sign = -1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }
    if (!isdigit((unsigned char)*s))
    {
        return false;
    }
    int v[3] = {0, 0, 0};
    int n = 0;
    for (;;)
    {
        while (isdigit((unsigned char)*s))
        {
            v[n] = v[n] * 10 + (*s++ - '0');
        }
        n++;
        if (*s == ':' && n < 3)
        {
            s++;
        }
        else
        {
            break;
        }
    }
    *secs = sign * (v[0] * 3600 + v[1] * 60 + v[2]);
    *p = s;
    return true;
}

/* Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

/* "Mm.w.d[/time]": the local second of that day, w = 5 meaning the last such weekday. */
static bool read_date(const char **p, int year, int64_t *local)
{
    const char *s = *p;
    if (*s != 'M')
    {
        return false;
    }
    s++;
    int m = 0, w = 0, d = 0;
    while (isdigit((unsigned char)*s))
    {
        m = m * 10 + (*s++ - '0');
    }
    if (*s++ != '.')
    {
        return false;
    }
    while (isdigit((unsigned char)*s))
    {
        w = w * 10 + (*s++ - '0');
    }
    if (*s++ != '.')
    {
        return false;
    }
    while (isdigit((unsigned char)*s))
    {
        d = d * 10 + (*s++ - '0');
    }
    int secs = 7200;
    if (*s == '/')
    {
        s++;
        if (!read_hms(&s, &secs))
        {
            return false;
        }
    }
    if (m < 1 || m > 12 || w < 1 || w > 5 || d < 0 || d > 6)
    {
        return false;
    }
    const int64_t first = days_from_civil(year, m, 1);
    const int wd_first = (int)(((first % 7) + 11) % 7); /* 1970-01-01 was a Thursday (4); Sunday is 0 */
    const int in_month = (int)(days_from_civil(m == 12 ? year + 1 : year, m == 12 ? 1 : m + 1, 1) - first);
    int day = 1 + (d - wd_first + 7) % 7 + (w - 1) * 7;
    while (day > in_month)
    {
        day -= 7;
    }
    *local = days_from_civil(year, m, day) * 86400 + secs;
    *p = s;
    return true;
}

/* The rule's two offsets (equal when it has no summer time) and the one in force at `utc`,
 * all in minutes east of UTC. */
static void rule_offsets(const char *rule, time_t utc, int *std_min, int *dst_min, int *now_min)
{
    *std_min = *dst_min = *now_min = 0;
    const char *p = rule;
    int soff;
    if (strncmp(p, "STD", 3) != 0)
    {
        return;
    }
    p += 3;
    if (!read_hms(&p, &soff))
    {
        return;
    }
    const int std_east = -soff;
    *std_min = *dst_min = *now_min = std_east / 60;
    if (strncmp(p, "DST", 3) != 0)
    {
        return;
    }
    p += 3;
    int dst_east = std_east + 3600;
    int doff;
    const char *q = p;
    if (read_hms(&q, &doff))
    {
        dst_east = -doff;
        p = q;
    }
    *dst_min = dst_east / 60;
    if (*p++ != ',')
    {
        return;
    }
    struct tm tm;
    gmtime_r(&utc, &tm);
    const int year = tm.tm_year + 1900;
    int64_t start, end;
    if (!read_date(&p, year, &start) || *p++ != ',' || !read_date(&p, year, &end))
    {
        return;
    }
    const int64_t s_utc = start - std_east; /* the start is read on the standard clock */
    const int64_t e_utc = end - dst_east;   /* the end is read on the daylight clock */
    const int64_t t = (int64_t)utc;
    const bool dst = s_utc < e_utc ? (t >= s_utc && t < e_utc) : !(t >= e_utc && t < s_utc);
    *now_min = (dst ? dst_east : std_east) / 60;
}

int tz_pick_offset_min(int pick, time_t utc)
{
    int a, b, now;
    if (pick < 0 || pick >= PICK_COUNT)
    {
        return 0;
    }
    rule_offsets(TZ_RULES[TZ_PICKS[pick].rule], utc, &a, &b, &now);
    return now;
}

/* Summer is the higher of the two offsets. For Ireland the standard time of the rule is the
 * summer one (UTC+1, winter being a negative daylight saving), so this reads offsets, never names. */
bool tz_pick_in_summer(int pick, time_t utc)
{
    int a, b, now;
    if (pick < 0 || pick >= PICK_COUNT)
    {
        return false;
    }
    rule_offsets(TZ_RULES[TZ_PICKS[pick].rule], utc, &a, &b, &now);
    return a != b && now == (a > b ? a : b);
}

int tz_manual(void)
{
    return s_manual;
}

bool tz_set_manual(int pick)
{
    if (pick < 0)
    {
        if (s_manual >= 0)
        {
            settings_erase(KEY_MANUAL);
        }
        s_manual = -1;
        settle();
        return true;
    }
    if (pick >= PICK_COUNT)
    {
        return false;
    }
    settings_set(KEY_MANUAL, TZ_PICKS[pick].zone);
    s_manual = pick;
    settle();
    return true;
}

void tz_format_offset(int minutes, char *out, size_t n)
{
    const int a = minutes < 0 ? -minutes : minutes;
    if (a % 60)
    {
        snprintf(out, n, "UTC%c%d:%02d", minutes < 0 ? '-' : '+', a / 60, a % 60);
    }
    else
    {
        snprintf(out, n, "UTC%c%d", minutes < 0 ? '-' : '+', a / 60);
    }
}
