/*
 * Модель кэша страниц Zathura.
 *
 * Воспроизводит утечку, которая проявляется при пролистывании PDF:
 * при вытеснении страницы из LRU-кэша поверхность не освобождается,
 * если флаг visible ещё true (он обновляется ПОСЛЕ рендера).
 * После этого cached сбрасывается в 0, и повторного шанса освободить
 * поверхность уже нет — она живёт до закрытия документа.
 *
 * Сборка:
 *   make
 *
 * Запуск:
 *   ./page_cache_scroll          # баг: рост live-памяти
 *   ./page_cache_scroll --fix    # исправление
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_PAGES        40
#define CACHE_SIZE     2   /* как «текущий разворот»: вытеснение бьёт по ещё visible */
#define VIEWPORT       2
#define SURFACE_BYTES  (256u * 1024u)  /* ~256 КБ, как уменьшенный cairo surface */

typedef struct {
    unsigned char *surface;
    int cached;
    int visible;
} Page;

static Page pages[N_PAGES];
static int cache[CACHE_SIZE];
static int n_cached = 0;
static size_t live_bytes = 0;
static size_t peak_bytes = 0;
static unsigned allocs = 0;
static unsigned frees = 0;
static int use_fix = 0;

static void account(void) {
    if (live_bytes > peak_bytes)
        peak_bytes = live_bytes;
}

static void page_alloc_surface(Page *p) {
    if (p->surface)
        return;
    p->surface = malloc(SURFACE_BYTES);
    if (!p->surface) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }
    memset(p->surface, 0xA5, SURFACE_BYTES);
    live_bytes += SURFACE_BYTES;
    allocs++;
    account();
}

static void page_free_surface(Page *p) {
    if (!p->surface)
        return;
    free(p->surface);
    p->surface = NULL;
    live_bytes -= SURFACE_BYTES;
    frees++;
}

/* Баг из cb_cache_invalidated(): поверхность сбрасывается только если
 * страница уже !visible. Флаг cached обнуляется в любом случае. */
static void cache_invalidate_one(int idx) {
    Page *p = &pages[idx];
    if (p->surface && p->cached) {
        if (use_fix) {
            /* Всегда выбрасываем полноразмерную поверхность при вытеснении. */
            page_free_surface(p);
        } else if (!p->visible) {
            page_free_surface(p);
        }
        /* else: поверхность остаётся, а cached станет 0 — утечка */
    }
    p->cached = 0;

    for (int i = 0; i < n_cached; i++) {
        if (cache[i] == idx) {
            cache[i] = cache[n_cached - 1];
            n_cached--;
            break;
        }
    }
}

static int cache_contains(int idx) {
    for (int i = 0; i < n_cached; i++) {
        if (cache[i] == idx)
            return 1;
    }
    return 0;
}

static void cache_add(int idx) {
    if (cache_contains(idx))
        return;
    if (n_cached == CACHE_SIZE) {
        /* LRU = самый старый элемент (индекс 0) */
        cache_invalidate_one(cache[0]);
    }
    cache[n_cached++] = idx;
    pages[idx].cached = 1;
}

/* Порядок как в Zathura: сначала рендер + кэш, потом пересчёт visibility. */
static void scroll_to(int first_visible) {
    int last_visible = first_visible + VIEWPORT - 1;
    if (last_visible >= N_PAGES)
        last_visible = N_PAGES - 1;

    for (int i = first_visible; i <= last_visible; i++) {
        pages[i].visible = 1;
        page_alloc_surface(&pages[i]);
        cache_add(i);
    }

    for (int i = 0; i < N_PAGES; i++) {
        int now_visible = (i >= first_visible && i <= last_visible);
        pages[i].visible = now_visible;
        /* В исходном коде здесь нет освобождения: visibility-обработчик
         * не сбрасывает surface, если cached уже false. */
        if (use_fix && !now_visible && !pages[i].cached)
            page_free_surface(&pages[i]);
    }
}

static void dump_live(const char *tag) {
    printf("%-16s live=%6zu KB  peak=%6zu KB  allocs=%u  frees=%u  cached=%d\n",
           tag, live_bytes / 1024, peak_bytes / 1024, allocs, frees, n_cached);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fix") == 0)
            use_fix = 1;
    }

    printf("pages=%d cache=%d viewport=%d surface=%u KB  mode=%s\n",
           N_PAGES, CACHE_SIZE, VIEWPORT, SURFACE_BYTES / 1024,
           use_fix ? "FIX" : "BUG");

    memset(pages, 0, sizeof(pages));
    for (int i = 0; i < CACHE_SIZE; i++)
        cache[i] = -1;

    for (int top = 0; top <= N_PAGES - VIEWPORT; top++) {
        scroll_to(top);
        if (top == 0 || top == 10 || top == 20 || top == N_PAGES - VIEWPORT)
            dump_live(top == 0 ? "after page 1" :
                      top == 10 ? "after page 11" :
                      top == 20 ? "after page 21" : "after last");
    }

    size_t leftover = 0;
    int leftover_pages = 0;
    for (int i = 0; i < N_PAGES; i++) {
        if (pages[i].surface) {
            leftover += SURFACE_BYTES;
            leftover_pages++;
        }
    }

    printf("\nСтраниц с непогашенной поверхностью: %d (%zu KB)\n",
           leftover_pages, leftover / 1024);
    if (!use_fix && leftover_pages > CACHE_SIZE + VIEWPORT) {
        printf("УТЕЧКА: поверхностей больше, чем кэш + вьюпорт.\n");
    } else if (use_fix && leftover_pages <= CACHE_SIZE + VIEWPORT) {
        printf("Исправление работает: живы только страницы кэша/вьюпорта.\n");
    }

    for (int i = 0; i < N_PAGES; i++)
        page_free_surface(&pages[i]);

    return (!use_fix && leftover_pages > CACHE_SIZE + VIEWPORT) ? 2 : 0;
}
