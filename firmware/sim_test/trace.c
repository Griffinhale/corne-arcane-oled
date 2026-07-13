#include "trace.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *path, int lineno, const char *msg) {
    fprintf(stderr, "%s:%d: %s\n", path, lineno, msg);
    return -1;
}

int trace_load(const char *path, trace_t *t) {
    memset(t, 0, sizeof *t);
    t->seed = 1;

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "trace: cannot open %s\n", path);
        return -1;
    }

    char line[128];
    int lineno = 0, saw_end = 0, saw_version = 0;
    uint32_t last_tick = 0;

    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char word[32];
        uint32_t num;
        if (sscanf(line, " %31s", word) != 1) continue; // blank

        if (saw_end) { fclose(f); return fail(path, lineno, "content after 'end'"); }

        if (strcmp(word, "version") == 0) {
            if (sscanf(line, " version %u", &num) != 1 || num != 1) {
                fclose(f); return fail(path, lineno, "unsupported version");
            }
            saw_version = 1;
        } else if (strcmp(word, "seed") == 0) {
            if (sscanf(line, " seed %u", &t->seed) != 1) {
                fclose(f); return fail(path, lineno, "bad seed");
            }
        } else if (strcmp(word, "start_tick") == 0) {
            if (sscanf(line, " start_tick %u", &t->start_tick) != 1) {
                fclose(f); return fail(path, lineno, "bad start_tick");
            }
        } else if (word[0] == '@') {
            char verb[16], sidec;
            unsigned row, col;
            int n = sscanf(line, " @%u %15s %c %u %u", &num, verb, &sidec, &row, &col);
            if (n < 2) { fclose(f); return fail(path, lineno, "bad event line"); }
            if (num < last_tick) { fclose(f); return fail(path, lineno, "ticks must be nondecreasing"); }
            last_tick = num;

            if (strcmp(verb, "end") == 0) {
                t->end_tick = t->start_tick + num;
                saw_end = 1;
                continue;
            }
            uint8_t kind;
            if      (strcmp(verb, "press")   == 0) kind = TRACE_EV_PRESS;
            else if (strcmp(verb, "release") == 0) kind = TRACE_EV_RELEASE;
            else { fclose(f); return fail(path, lineno, "unknown event verb"); }

            if (n != 5) { fclose(f); return fail(path, lineno, "press/release need: L|R row col"); }
            if (sidec != 'L' && sidec != 'R') { fclose(f); return fail(path, lineno, "side must be L or R"); }
            if (row > 3 || col > 5) { fclose(f); return fail(path, lineno, "row 0..3, col 0..5"); }
            if (t->n_ev >= TRACE_MAX_EVENTS) { fclose(f); return fail(path, lineno, "too many events"); }

            t->ev[t->n_ev++] = (trace_ev_t){
                .tick = t->start_tick + num,
                .kind = kind,
                .side = (sidec == 'R'),
                .row  = (uint8_t)row,
                .col  = (uint8_t)col,
            };
        } else {
            fclose(f); return fail(path, lineno, "unknown directive");
        }
    }
    fclose(f);

    if (!saw_version) return fail(path, lineno, "missing 'version 1'");
    if (!saw_end)     return fail(path, lineno, "missing '@N end'");
    return 0;
}
