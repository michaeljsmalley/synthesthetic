/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware's engine table as JSON on stdout: each engine's name, page titles, its eight EDIT parameters (label,
 * range, default, value names) and its factory presets (name, the eight values, the envelope, mono). web/test_web.mjs
 * holds the editor's simulated FM-1 (editor.html makeMockDevice: ENG) to it, so the editor's offline tables and its
 * audio preview say what the firmware says (Synthesthetic's curated sample sets: tools/gen_samples.py).
 *   tests/run_tests.sh builds it into build/host/engine_dump. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static void jstr(const char *s)
{
    putchar('"');
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\')
            putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

int main(void)
{
    uint32_t e, i, k;
    printf("[");
    for (e = 0; e < NENGINES; e++) {
        const engine_t *g = ENGINES[e];
        printf("%s\n{\"name\":", e ? "," : "");
        jstr(g->name);
        printf(",\"titles\":[");
        jstr(g->page_title[0]);
        printf(",");
        jstr(g->page_title[1]);
        printf("],\"edit\":[");
        for (i = 0; i < 8u; i++) {
            const param_desc_t *d = &g->edit[i];
            printf("%s{\"label\":", i ? "," : "");
            jstr(d->label);
            printf(",\"min\":%d,\"max\":%d,\"def\":%d,\"names\":[", d->min, d->max, d->def);
            if (d->fmt == F_ENUM && d->names)
                for (k = 0; (int32_t)k <= d->max - d->min; k++) {
                    printf("%s", k ? "," : "");
                    jstr(d->names[k]);
                }
            printf("]}");
        }
        printf("],\"presets\":[");
        for (k = 0; k < g->npresets; k++) {
            const preset_t *p = &g->presets[k];
            printf("%s{\"name\":", k ? "," : "");
            jstr(p->name);
            printf(",\"e\":[%d,%d,%d,%d,%d,%d,%d,%d],\"env\":[%d,%d,%d,%d],\"mono\":%d}", p->e[0], p->e[1], p->e[2], p->e[3],
                   p->e[4], p->e[5], p->e[6], p->e[7], p->env[0], p->env[1], p->env[2], p->env[3], p->mono);
        }
        printf("]}");
    }
    printf("\n]\n");
    return 0;
}
