#include "movie.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const names[8] = { "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN" };

void movie_append(Movie *m, uint8_t buttons)
{
    if (m->count == m->capacity) {
        m->capacity = m->capacity ? m->capacity * 2 : 1024;
        m->inputs = realloc(m->inputs, m->capacity);
    }
    m->inputs[m->count++] = buttons;
}

void movie_free(Movie *m)
{
    free(m->inputs);
    memset(m, 0, sizeof *m);
}

static int parse_buttons(const char *s, uint8_t *out)
{
    *out = 0;
    if (strcmp(s, "-") == 0)
        return 0;
    char buf[128];
    snprintf(buf, sizeof buf, "%s", s);
    for (char *tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
        int found = 0;
        for (int i = 0; i < 8; i++)
            if (strcasecmp(tok, names[i]) == 0) { *out |= 1 << i; found = 1; }
        if (!found)
            return -1;
    }
    return 0;
}

int movie_load(Movie *m, const char *path)
{
    memset(m, 0, sizeof *m);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char btn[128] = "";
        char rep[32] = "";
        if (sscanf(line, "%127s %31s", btn, rep) < 1)
            continue;
        uint8_t b;
        long count = 1;
        if (parse_buttons(btn, &b) != 0) {
            fprintf(stderr, "%s:%d: bad buttons '%s'\n", path, lineno, btn);
            fclose(f);
            return -1;
        }
        if (rep[0]) {
            if (rep[0] != '*' || (count = strtol(rep + 1, NULL, 10)) <= 0) {
                fprintf(stderr, "%s:%d: bad repeat '%s'\n", path, lineno, rep);
                fclose(f);
                return -1;
            }
        }
        while (count--)
            movie_append(m, b);
    }
    fclose(f);
    return 0;
}

int movie_save(const Movie *m, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# Super Mario Land input movie: one line per joypad poll (game frame),\n"
               "# BUTTONS [*COUNT]; buttons: A B SELECT START RIGHT LEFT UP DOWN, '-' = none\n");
    for (size_t i = 0; i < m->count;) {
        size_t j = i;
        while (j < m->count && m->inputs[j] == m->inputs[i]) j++;
        char buf[128] = "";
        uint8_t b = m->inputs[i];
        if (!b) strcpy(buf, "-");
        for (int k = 0; k < 8; k++)
            if (b & (1 << k)) {
                if (buf[0]) strcat(buf, "+");
                strcat(buf, names[k]);
            }
        if (j - i > 1) fprintf(f, "%s *%zu\n", buf, j - i);
        else fprintf(f, "%s\n", buf);
        i = j;
    }
    return fclose(f);
}
