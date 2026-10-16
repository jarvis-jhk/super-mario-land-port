/* Release update logic without SDL or network access, see update_core.h. */
#include "update_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION_PARTS 4

/* Parses "v1.2.3" (up to four numbers); the end must be a space, newline,
   quote or NUL. Returns the number of parts, or 0. */
static int parse_version(const char *s, long out[VERSION_PARTS], const char **end)
{
    int n = 0;
    if (*s == 'v')
        s++;
    memset(out, 0, VERSION_PARTS * sizeof out[0]);
    for (;;) {
        if (!isdigit((unsigned char)*s) || n == VERSION_PARTS)
            return 0;
        char *e;
        out[n++] = strtol(s, &e, 10);
        s = e;
        if (*s != '.')
            break;
        s++;
    }
    if (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && *s != '"')
        return 0;
    if (end)
        *end = s;
    return n;
}

int version_compare(const char *a, const char *b)
{
    long va[VERSION_PARTS], vb[VERSION_PARTS];
    if (!a || !b || !parse_version(a, va, NULL) || !parse_version(b, vb, NULL))
        return -2;
    for (int i = 0; i < VERSION_PARTS; i++)
        if (va[i] != vb[i])
            return va[i] < vb[i] ? -1 : 1;
    return 0;
}

int update_latest_tag(const char *json, char *out, size_t out_len)
{
    int found = 0;
    char best[32] = "";
    for (const char *p = json; (p = strstr(p, "\"tag_name\"")); ) {
        p += 10;
        while (*p == ' ' || *p == ':')
            p++;
        if (*p != '"')
            continue;
        p++;
        const char *start = *p == 'v' ? p + 1 : p, *end;
        long v[VERSION_PARTS];
        if (!parse_version(start, v, &end) || *end != '"' || end - start >= (long)sizeof best)
            continue;
        char tag[32];
        snprintf(tag, sizeof tag, "%.*s", (int)(end - start), start);
        if (!found || version_compare(tag, best) > 0) {
            snprintf(best, sizeof best, "%s", tag);
            found = 1;
        }
    }
    if (!found)
        return -1;
    snprintf(out, out_len, "%s", best);
    return 0;
}

int update_manifest_find(const char *manifest, const char *version, const char *asset,
                         uint64_t *size, char sha256[65])
{
    int version_ok = 0, file_ok = 0;
    const char *line = manifest;
    while (line && *line) {
        const char *next = strchr(line, '\n');
        size_t len = next ? (size_t)(next - line) : strlen(line);
        char buf[512], name[256], hex[80], extra[2];
        unsigned long long n;
        if (len < sizeof buf) {
            memcpy(buf, line, len);
            buf[len] = 0;
            if (len && buf[len - 1] == '\r')
                buf[len - 1] = 0;
            if (!strncmp(buf, "version ", 8))
                version_ok = version_compare(buf + 8, version) == 0;
            else if (sscanf(buf, "file %255s %llu %79s %1s", name, &n, hex, extra) == 3 &&
                     !strcmp(name, asset) && strlen(hex) == 64 && n > 0) {
                int good = 1;
                for (int i = 0; i < 64; i++)
                    if (!isxdigit((unsigned char)hex[i]))
                        good = 0;
                if (good) {
                    for (int i = 0; i < 65; i++)
                        sha256[i] = (char)tolower((unsigned char)hex[i]);
                    *size = n;
                    file_ok = 1;
                }
            }
        }
        line = next ? next + 1 : NULL;
    }
    return version_ok && file_ok ? 0 : -1;
}

char *changelog_since(const char *changelog, const char *seen, const char *current)
{
    size_t cap = strlen(changelog) + 1, used = 0;
    char *out = malloc(cap);
    if (!out)
        return NULL;
    int keep = 0;
    for (const char *line = changelog; line && *line; ) {
        const char *next = strchr(line, '\n');
        size_t len = next ? (size_t)(next - line + 1) : strlen(line);
        if (!strncmp(line, "## ", 3)) {
            char ver[32];
            snprintf(ver, sizeof ver, "%.*s", (int)strcspn(line + 3, " \t\r\n"), line + 3);
            if (seen && seen[0] && version_compare(seen, current) != -2)
                keep = version_compare(ver, seen) > 0 && version_compare(ver, current) <= 0;
            else
                keep = version_compare(ver, current) == 0;
        } else if (!strncmp(line, "# ", 2)) {
            keep = 0;
        }
        if (keep) {
            memcpy(out + used, line, len);
            used += len;
        }
        line = next ? next + 1 : NULL;
    }
    out[used] = 0;
    if (!used) {
        free(out);
        return NULL;
    }
    return out;
}

char *changelog_reflow(const char *markdown)
{
    char *out = malloc(strlen(markdown) + 2);
    if (!out)
        return NULL;
    size_t used = 0;
    int in_paragraph = 0;
    for (const char *line = markdown; *line; ) {
        size_t len = strcspn(line, "\n");
        const char *next = line[len] ? line + len + 1 : line + len;
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' '))
            len--;
        const char *text = line;
        while (text < line + len && (*text == ' ' || *text == '\t'))
            text++;
        size_t n = (size_t)(line + len - text);
        if (!n) {
            if (in_paragraph)
                out[used++] = '\n';
            out[used++] = '\n';
            in_paragraph = 0;
        } else {
            int starts = text == line && (!strncmp(text, "- ", 2) || text[0] == '#');
            if (in_paragraph && !starts)
                out[used++] = ' ';
            else if (in_paragraph)
                out[used++] = '\n';
            if (!strncmp(text, "## ", 3)) {
                text += 3;
                n -= 3;
            }
            memcpy(out + used, text, n);
            used += n;
            in_paragraph = 1;
        }
        line = next;
    }
    if (in_paragraph)
        out[used++] = '\n';
    out[used] = 0;
    return out;
}
