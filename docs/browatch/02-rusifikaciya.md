# Русификация BroWatch (RU/EN)

> Статус: stage 1 и шаблоны — **готово и собирается**; полное меню — позже.
> Решение: двуязычный режим с переключателем `LANGUAGE / ЯЗЫК`.

## Что уже работает

- `Settings::lang` (NVS `lang`), строка `LANGUAGE` на SYSTEM-странице, команда `LANG RU|EN` по Serial.
- Шрифт **RuCyr8** (`include/ru_font.h`, Liberation Sans 8px, U+0401–U+0451, ~1 КБ flash) через `LOAD_GFXFF` во всех бордах; EN-путь попиксельно не изменился.
- `Theme::printRU / textWidthRU / wrapTextRU` (`src/theme.cpp`) — смешанная ASCII/кириллица печать; `RuText` — UTF-8-декодер и счёт глифов.
- **48 шаблонных сообщений + 6 вкладок + 8 FILL-заготовок + хинты** экрана сообщений — на русском при `lang==RU`, на английском при `lang==EN`. Эфир несёт индекс, так что RU-плата и EN-плата понимают друг друга.
- Входящие шаблоны по-русски: красный бабл, сквад-лист, деск-коробка, экран сообщений.
- Проверки: `test/lang_test`, `test/ru_text_test`, `test/canned_ru_test` (паритет индексов, длины, рендеримость); `sim --lang 1` (`compose`, `clear --inbox N`, `roster`); `pio run` для `cyd`, `cyd-ili9341`, `awok`.
- Веб (`browatch-web`): интерфейс на русском + пикер тех же 48 шаблонов (паритет проверен скриптом).

## Известные ограничения

- Баблы font-2 (деск, красный бабл) рисуют RU мелким font-1-миксфейсом — читаемо, но мельче; 16px-кириллица — следующий этап.
- Клавиатура и эфирный алфавит — латиница: набранный русский по эфиру не ходит (только через веб-мост, он UTF-8).
- Полное меню (3704 литерала, `tools/strings_inventory.txt`) — не переведено; только экран сообщений и `LANGUAGE`.

## Таблица шаблонов (EN → RU)

| # | EN | RU |
|---|---|---|
| 0 | On my way. | Уже еду. |
| 1 | Where are you? | Ты где? |
| 2 | All clear here. | У меня чисто. |
| 3 | Something's nearby. | Тут что-то есть. |
| 4 | Heading out. | Выхожу. |
| 5 | Be right back. | Ща вернусь. |
| 6 | Yes. | Ага. |
| 7 | No. | Не-а. |
| 8 | Maybe. | Может. |
| 9 | Meet at the car. | Жду у машины. |
| 10 | Watch your back. | Смотри в оба. |
| 11 | Camera on my left. | Камера слева. |
| 12 | Found a Flock cam. | Тут камера Флок. |
| 13 | Stay put. | Замри. |
| 14 | Come to me. | Давай ко мне. |
| 15 | Leaving now. | Сваливаю. |
| 16 | Five minutes. | Пять минут. |
| 17 | Running late. | Опаздываю. |
| 18 | Ha. | Ха. |
| 19 | Nice. | Найс. |
| 20 | Thanks. | Спасибо. |
| 21 | Snacks? | Перекус? |
| 22 | Is it following you? | Хвост за тобой? |
| 23 | Going dark. | Ухожу в тень. |
| 24 | Cop car ahead. | Копы впереди. |
| 25 | Drone overhead. | Дрон сверху. |
| 26 | ALPR on the pole. | Камера на номера. |
| 27 | Two of them now. | Их уже двое. |
| 28 | It's gone now. | Уже ушли. |
| 29 | I'm safe. | Я в порядке. |
| 30 | Don't come here. | Сюда не иди. |
| 31 | Turn around. | Разворачивайся. |
| 32 | Being followed. | За мной хвост. |
| 33 | You okay? | Ты в порядке? |
| 34 | Still there? | Ты на месте? |
| 35 | Can you talk? | Говорить можешь? |
| 36 | Which way? | Куда дальше? |
| 37 | How many? | Сколько их? |
| 38 | Need a ride? | Подвезти? |
| 39 | Call when you can. | Набери как сможешь. |
| 40 | Got it. | Принял. |
| 41 | On it. | Уже делаю. |
| 42 | Not yet. | Пока нет. |
| 43 | Copy that. | Вас понял. |
| 44 | Squatch out. | Сквач, отбой. |
| 45 | Big if true. | Мощно, если так. |
| 46 | Beep boop. | Бип-буп. |
| 47 | Stay squachy. | Будь сквачем. |

Вкладки: GOING→ПУТЬ, SEEN→ВИЖУ, SAFE→СТАТУС, ASK→ВОПРОС, REPLY→ОТВЕТ, SQUACH→СКВАЧ.
FILL: MEET AT→ВСТРЕЧА В, I'M AT→Я НА, BACK IN→ВЕРНУСЬ ЧЕРЕЗ, CALL ME AT→ЗВОНИ В, HEADING TO→ЕДУ В, LOOK FOR THE→ИЩИ, BRING THE→ПРИХВАТИ, SAW A→ВИДЕЛ.

## Порядок дальше

1. `tools/extract_strings.py` → `include/strings_en.h` (инвентарь готов: `tools/strings_inventory.txt`).
2. ~~`Settings::lang` (NVS) + пункт меню + команда `LANG RU|EN`.~~ ✅
3. ~~Шрифты: `font 1` (RuCyr8 + UTF-8), `font 2` позже, Bangers — транслит.~~ ✅ частично (font 2 — позже)
4. `include/strings_ru.h` — перевод меню (остаток).
5. Проверка: `sim` скриншоты до/после + `pio run` + `make -C test`.
