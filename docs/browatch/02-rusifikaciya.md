# Русификация BroWatch (RU/EN)

> Статус: план (см. [00-PLAN, раздел 2](00-PLAN.md)). Решение: двуязычный режим с переключателем `LANGUAGE / ЯЗЫК`.

## Факты

- Кириллицы в прошивке нет: `font 1` (GLCD 5x7, почти весь UI), `font 2` (Font16, пузыри),
  `BangersFont` 1bpp (только `A-Z0-9 !-'`, заголовки). Генератор `tools/ttf2gfx.py` — `range(32,127)`.
- Строки — инлайн-литералы в 33× `src/ui_*.cpp` + `src/squachy.cpp` (реплики),
  `src/detection_info.cpp` (`EXPLAIN_TEXT`), `src/signatures.cpp`, `src/settings.cpp`. i18n нет.
- Вёрстка зависит от `textWidth`/`wrapText` (`lines[][48]`, байты ≠ глифы в UTF-8).

## Порядок

1. `tools/extract_strings.py` → `include/strings_en.h`.
2. `Settings::lang` (NVS) + пункт меню + команда `LANG RU|EN`.
3. Шрифты: сначала `font 1` (кириллица 5x7 + UTF-8→CP1251), затем `font 2`, Bangers — транслит заголовков.
4. `include/strings_ru.h` — перевод.
5. Проверка: `sim` скриншоты до/после + `pio run` + `make -C test`.
