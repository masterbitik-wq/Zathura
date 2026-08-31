# Финальная работа: утечка памяти в Zathura при пролистывании PDF

Тема средней сложности из предложенных. Остальные (патч dwm/spectrwm на N панелей, плагин mpd со стримингом) заметно тяжелее: там X11/оконный менеджер или аудиостек + HTTP API. Здесь тот же круг тем, что уже был в семестре: память, GTK, жизненный цикл буферов.

## Что внутри

| путь | зачем |
|---|---|
| `ANALYSIS.md` | разбор всех домашних за семестр и почему выбрана эта тема |
| `REPORT.md` | как искали утечку, в чём баг, как чинится |
| `LETTER.txt` | письмо преподавателю |
| `patches/0001-fix-page-cache-surface-leak-on-scroll.patch` | патч к `zathura/page-widget.c` |
| `patches/fixed-fragments.c` | те же правки текстом, удобно читать |
| `demo/` | модель кэша страниц, баг воспроизводится без GTK/PDF |

## Демо (можно сдать и проверить без Linux)

```text
cd demo
make
./page_cache_scroll          # рост памяти, код выхода 2
./page_cache_scroll --fix    # live держится около размера кэша
```

На Windows, если есть gcc:

```text
gcc -std=c11 -Wall -Wextra -O0 -g -o page_cache_scroll.exe page_cache_scroll.c
page_cache_scroll.exe
page_cache_scroll.exe --fix
```

Ожидаемый результат на 40 страницах (поверхность 256 КБ, кэш=2):

```text
режим BUG:  live растёт до ~10240 KB, 40 поверхностей не освобождены, frees=0
режим FIX:  live ≈ 512 KB, остаётся 2 страницы вьюпорта, frees=38
```

## Патч к Zathura

Нужны исходники (тег 0.5.x / ветка develop):

```text
git clone https://github.com/pwmt/zathura.git
cd zathura
patch -p1 < ../patches/0001-fix-page-cache-surface-leak-on-scroll.patch
```

Если `patch` ругнётся на контекст — правки те же, что в `fixed-fragments.c`, функции `cb_cache_invalidated`, `draw_thumbnail_image`, `zathura_page_widget_update_view_time`.

Проверка на Linux (после сборки zathura с `-Db_sanitize=address` или через valgrind):

```text
valgrind --leak-check=full --log-file=vg.log zathura long.pdf
# пролистать документ от начала до конца, затем q
```

До патча RSS растёт почти линейно с числом просмотренных страниц.
После — держится около `page-cache-size` полноразмерных поверхностей.
