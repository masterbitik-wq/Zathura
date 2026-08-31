# Отчёт: утечка памяти в Zathura при пролистывании PDF

## 1. Постановка

Zathura (https://github.com/pwmt/zathura) рендерит страницы PDF в `cairo_surface_t` и кладёт их в LRU-кэш (`page-cache-size`, по умолчанию 15). При быстром пролистывании длинного документа RSS растёт и не возвращается, хотя кэш формально ограничен.

Это не «Poppler жрёт память на поиске» (issues #132, #197) и не утечка ToC (#767). Здесь именно скролл: страницы входят в кэш и не отдают полноразмерную поверхность, когда слот кэша уже отдан другой странице.

## 2. Как искали

1. Исходники `develop`: `zathura/render.c` (индексы кэша) и `zathura/page-widget.c` (сами поверхности).
2. Известные тикеты: #449 (рост памяти на длинных PDF), #864 (скролл и кэш GtkViewport), комментарий в коде: *«cached visibility flag can lag behind layout»*.
3. Модель кэша в `demo/page_cache_scroll.c` — тот же порядок операций, без GTK.

Полный valgrind по GTK-приложению на этой машине не гонялся (Windows, нет сборочного окружения Zathura). Логика бага проверяется демо; патч смотрит в те же функции, что открываются по stack/debug логам рендерера.

## 3. Устройство кэша

В `render.c` кэш — массив индексов страниц. Вытеснение:

```c
/* page_cache_lru_invalidate() */
g_signal_emit(request, request_signals[REQUEST_CACHE_INVALIDATED], 0);
priv->page_cache.cache[lru_index] = -1;
```

Саму cairo-поверхность рендерер не держит. Её уничтожает виджет в `cb_cache_invalidated`.

Исходный обработчик:

```c
if (have_surface && priv->cached && zathura_page_get_visibility(page) == false)
    zathura_page_widget_update_surface(widget, NULL, false);
priv->cached = false;
```

Две ошибки на одном пути.

**А.** `zathura_page_get_visibility()` отстаёт от раскладки. Авторы это знают: рядом написан `page_widget_on_screen()`, который смотрит геометрию. При непрерывном скролле вытесненная страница ещё `visible == true`.

**Б.** `priv->cached = false` выполняется всегда. Если поверхность из-за (А) не освободили, второго сигнала `cache-invalidated` не будет. В `zathura_page_widget_abort_render_request()` есть запасной `free`, но на обычный скролл этот путь не вызывается (там даже TODO: *Maybe this should be moved somewhere else*).

Итог: полноразмерная поверхность живёт до закрытия документа. Каждая новая страница — ещё один битмап. Кэш индексов при этом ограничен, RSS — нет.

Дополнительно: в `draw_thumbnail_image()` при ошибке `cairo_surface_create_similar` nil-surface не уничтожается. Cairo всегда возвращает указатель, его нужно `cairo_surface_destroy`. Мелочь, но на error-path это тоже утечка.

## 4. Исправление

1. При вытеснении **всегда** выбрасывать полноразмерную поверхность.
2. Если виджет ещё в кадре — оставить thumbnail (`keep_thumbnail = true`), чтобы не мигало «Loading…».
3. Попадание в кадр проверять через `page_widget_on_screen()`, не через устаревший флаг.
4. В `zathura_page_widget_update_view_time()` (это дёргается при скролле) добить поверхности у страниц, которые уже не в кэше и не на экране.
5. Уничтожать nil-surface на error-path превью.

Патч: `patches/0001-fix-page-cache-surface-leak-on-scroll.patch`.
Текстом: `patches/fixed-fragments.c`.

## 5. Проверка моделью

`demo/page_cache_scroll.c` повторяет порядок Zathura: сначала render + `cache_add` (возможное вытеснение), потом пересчёт `visible`.

40 страниц, кэш 2, вьюпорт 2, поверхность 256 КБ.

Без `--fix`:

```text
after page 1     live=  512 KB
after page 11    live= 3072 KB
after page 21    live= 5632 KB
after last       live=10240 KB
поверхностей не погашено: 40, frees=0
```

С `--fix`:

```text
after page 1     live=  512 KB
after page 11    live=  512 KB
after last       live=  512 KB
остаётся 2 страницы вьюпорта, frees=38
```

В баге live растёт линейно с номером страницы. После фикса — полка на размере кэша.

На Linux после патча имеет смысл прогнать:

```text
valgrind --leak-check=full zathura sample.pdf
```

и пролистать файл. Ожидается, что «definitely lost», связанные с `cairo_image_surface_create` / `cb_update_surface`, пропадут; оставшиеся утечки GTK/GLib — шум, не этот баг.

## 6. Что сознательно не трогали

- Поиск по всему PDF (Poppler, issues #132/#197) — другая задача, не скролл.
- Динамическое создание виджетов страниц (#721) — большой рефакторинг UI, не «закрыть утечку».
- `page-cache-size` в конфиге — это костыль, он не чинит логику `cached`/`visible`.
