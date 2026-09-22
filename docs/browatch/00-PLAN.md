# BroWatch — пошаговый план имплементации

> Форк SquachWatch-CYD → BroWatch. Решения пользователя (2026-09-21):
> - Прошивка: двуязычный режим RU/EN с переключателем `ЯЗЫК / LANGUAGE` в настройках.
> - Веб: v0.2.0 — полный паритет с прошивкой (пакет `web/` внутри этого репо, порт **40400**).
> - Android: **Native Kotlin + BLE**.
> - Вики: `docs/browatch/` внутри репо BroWatch.
> - Репы: `web/` (веб, здесь), `/run/media/al/ARCHIVE/code/browatch-app` (Android).

## 0. Что показало исследование (кратко)

- Детектор: ESP32-2432S028R (CYD), пассивный WiFi + BLE скан, 17 типов детекций, `include/signatures.h`, `src/detection.cpp`, `src/signatures.cpp`. Confidence — свойство сигнатуры (32 High / 4 Medium / 40 Low из 76 строк). Доки: `README.md`, `docs/DESIGN.md`, `FAQ.md`, `BUILD.md`, `DETECTIONS.md`, `PINOUT.md`, `SQUACHWARE-AESTHETIC.md`, `IDEAS.md`.
- SquachMesh: два транспорта BLE manufacturer data под CID `0xFFFF`: advert `SQM1` (8/20 байт: magic+version+appearance word+flags+имя 12 ASCII) каждые 1500 мс + scan-response `ST`/`MeshMsg` (VERSION=2, HDR 6 байт, FRAME_MAX=27, виды CANNED/TEXT/EMOTE/NUDGE/WIFI/UPDATED/INVITE_PUB/INVITE_KEY/HELLO/READ). Текст: до 48 символов, алфавит 43 символа ` A-Z0-9.,?!'-`, упаковка 6 бит, до 3 частей. Крипто: AES-128-CCM tag 8B, ключ PBKDF2-HMAC-SHA256 (фраза 5 слов, salt `SquachWatch/msg/v1`, 20000 итераций ~2.7 с на ESP32), инвайт X25519 + код 0..9999. Файлы: `include/squachmesh.h`, `include/meshmsg.h`, `src/squachmesh.cpp`, `src/meshmsg.cpp`, `src/meshtalk.cpp`, `src/mesh.cpp`, `src/detection.cpp:726-753,835-847`.
- Важно для моста: **пассивный скан не видит scan-response** (имена, сообщения) — нужен активный скан. Счётчик монотонный 24 бит, без wrap; nonce = MAC+counter; replay-таблица 4 отправителя во flash.
- Шрифты: весь UI — TFT_eSPI. `font 1` (GLCD 5x7, почти всё), `font 2` (Font16, пузыри/реплики), custom `BangersFont` 1bpp (только `A-Z0-9 !-'`, 40 глифов, заголовки ALERT/LOG/часы). **Кириллицы нет нигде** (`grep [А-Я]` — пусто, `tools/ttf2gfx.py` — `range(32,127)`). Строки — инлайн-литералы в 33× `src/ui_*.cpp` + `src/squachy.cpp` + `src/detection_info.cpp`; i18n нет.
- Serial-консоль: 2000000 бод (CYD), команды `TIME <epoch>`, `ZONE <name>`, `CLOCK`, `SCAN ACTIVE/PASSIVE/AUTO`, `[ota]/[clock]/[mem]` и т.д. (`src/clock.cpp:400-612`). Выделенного JSON-протокола для моста **нет** — его предстоит добавить (этап 4).
- Эмулятор: `sim/Makefile` (нативные `squachsim`/`squachsim-live` + `make wasm` → `web-flasher/emulator/`). Веб-прошивальщик: `web-flasher/index.html` (ESP Web Tools).
- В апстриме `SquachWatch-CYD/wiki/` уже есть RU-вики (`01-obzor-i-gayd.md`, `02-shkolniki-it-ai.md`) — берём как основу для `docs/browatch/`, переименовывая SquachWatch → BroWatch.
- Оригинал: `github.com/skizzophrenic/SquachWatch-CYD`, наш форк: `github.com/teletonn/BroWatch` (origin), статус апстрима Shipping, лицензия README — GPL-3.0 (в DESIGN/FAQ встречается MIT — зафиксировать GPL-3.0 для форка).

## 1. Вики BroWatch — [x] готово

1. [x] `docs/browatch/README.md` — индекс вики.
2. [x] `docs/browatch/01-obzor.md` — обзор + железо + детекции + BroMesh.
3. [x] `docs/browatch/02-rusifikaciya.md` — RU/EN режим, шрифт, что переведено, таблица шаблонов.
4. [x] `docs/browatch/03-web-most.md` — архитектура шлюза USB/Serial + API.
5. [x] `docs/browatch/04-android.md` — архитектура Kotlin BLE.
6. [x] `docs/browatch/05-rebrand.md` — BroWatch/BroMesh: что переименовано, что оставлено.

## 2. Русификация прошивки — [x] готово, прошито, проверено на железе

Фактический путь (отличается от чернового плана выше — план сохранён как история):

1. [x] Инвентаризация: `tools/extract_strings.py` → `tools/strings_inventory.txt` (3704 литерала).
2. [x] `Settings::lang` (0=EN, 1=RU, **по умолчанию RU**) в NVS + `LANGUAGE / ЯЗЫК` на SYSTEM-странице + команда `LANG RU|EN` по Serial (`src/settings.cpp`, `src/clock.cpp`, `src/main.cpp`, `test/lang_test.cpp`).
3. [x] Шрифт: вместо GLCD-таблицы — отдельный GFX-шрифт **RuCyr8** (Liberation Sans Bold 9px + суперсемплинг, U+0401–U+0451, `include/ru_font.h`, ~300 байт) + собственный попиксельный рендерер `drawRuGlyph` в `src/theme.cpp` (FreeFont на железе врал — проверено `RUTEST` с платы; свой рендерер даёт одинаковые пиксели в sim и на железе). Базовая линия `RU_YSHIFT=+6`, замер ширины по чернилам (`ruInk`). Генератор: `tools/ttf2gfx.py --codes … [--supersample N] [--thresh N]`.
4. [x] Перевод через `Theme::tr()` + `printRU/textWidthRU/wrapTextRU`: кнопки, всё меню настроек и подэкраны, тосты, 48 шаблонов + эмоции + FILL + хинты, входящие, ~300 реплик Сквачи, туториал, инвайт, фраза, обновление, дневник, бинго, деск, журнал, алерт, охота, скан, сети, PIN/телефон, бут-скрин, дата. Сознательно EN: коды типов, слова фразы/клавиатуры (протокол), динамика (имена/MAC/SSID), имена костюмов/палитр/зон, диагностика, Bangers-заголовки, OTA-домен, консольные теги. Детали — в `02-rusifikaciya.md`.
5. [x] Проверки: `make -C test` 23/23; `make -C sim` + скриншоты RU (`compose`, `clear --inbox`, `roster`, `settings`, `boot`, `log`); `pio run` — `cyd`, `cyd-ili9341`, `awok` SUCCESS (Flash 88.0%, RAM 27.7%); плата перепрошита (`cyd-ili9341`), отвечает `[lang] RU`.
6. Критерий готовности закрыт: русский по умолчанию после прошивки, главный путь читаем на RU.

## 2b. Ребренд — [x] готово (детали — в `05-rebrand.md`)

Видимые имена BroWatch/BroMesh (UI, BLE-имя `BroWatch-XXXX`, SD-лог `browatch-*.log`, README).
Протокол (SQM1, соль фразы, NVS-ключи, идентификаторы) не тронут — совместимость сохранена.

## 3. Протокол моста прошивка ↔ шлюз — [x] реализован (USB-проверка на железе — за пользователем)

1. [x] Спека `docs/browatch/03-web-most.md`: строковый JSON-lines протокол поверх USB-Serial (2M бод), префикс `[BW]`:
   - `→` плата: `[BW {"t":"peer","mac":"…","name":"…","rssi":-61,"appearance":1234}]`, `[BW {"t":"msg","from":"…","text":"…","ts":…}]`, `[BW {"t":"detection","type":"FLOCK",…}]`, `[BW {"t":"emote",…}]`.
   - `←` шлюз: `[BW {"t":"send","text":"…"}]`, `[BW {"t":"phrase","words":"…"}]` (позже), `[BW {"t":"hello"}]`.
2. [x] Реализация в прошивке: `src/bw_bridge.cpp` + `include/bw_bridge.h` (`-DBW_BRIDGE=1` в `platformio.ini` для всех отгрузочных окружений, кроме замороженного `cyd35`); радиотракт не тронут; `BW PING` не добавлялся — вместо него `[BW {"t":"ping"}]` → announce + snapshot. Сборки `cyd-ili9341` и `rlphantom` — SUCCESS (Flash ~89.5%, RAM ~27.8%). Серийный читатель один (`Clock::pollSerial` отдаёт `[`-строки в `BwBridge::onLine`); при заблокированном PIN мост молчит.
3. [x] Тест без железа: кадры платы прогнаны через `gateway.parse_line` + `/api/ingest`; `gateway.py --demo` работает.

## 4. browatch-web (порт 40400) — [x] v0.2.0: полный паритет с прошивкой (проверено на железе)

1. [x] Пакет `web/` внутри этого репо (источник правды; бывший отдельный checkout `/run/media/al/ARCHIVE/code/browatch-web` оставлен как есть): `server.py` (stdlib, порт 40400), `static/` (чат + отряд + радар + PWA), `gateway.py` (Serial↔SSE + очередь→Serial, reconnect, ping), `API.md`, `VERSION`, `smoke.py`, `tools/` (иконки, `web-install.sh`), `systemd/` (user-юниты + udev).
2. [x] REST: `GET /api/peers`, `GET /api/messages?since=`, `POST /api/messages {from,text}`, `GET /api/emotions?since=` + `POST /api/emotions`, `GET /api/squad` (presence + last_msg), `GET /api/detections?since=`, `POST /api/bridge/send` (очередь + локальное эхо `via: board`), `GET /api/bridge/outbox` (drain), `GET /api/health` (версия, плата).
3. [x] Presence как на плате: «в эфире» ≤15 с (плата держит 12 с), «недавно были» ≤5 мин, дальше забываем; пиры привозит только шлюз, `POST /api/peers` — heartbeat веба (`web:*`), фантомы не создаются (сообщения по нику цепляются к пиру, а не плодят записи).
4. [x] Realtime: SSE primary (`peer`/`leave`/`message`/`emotion`/`detection`/`board`/`hello`) + incremental poll-fallback.
5. [x] Паритет 1:1 (сверен скриптом с исходниками): шаблоны 50 + 6 табов, эмоции 35 + 6 табов + RU-ярлыки, цвета/имена типов, LILGUY 80 мс, фоны. Реакции 👍/👎 = canned 48/49 в эфир.
6. [x] PWA (`manifest` + `sw.js` + иконки из кадра LILGUY), уведомления браузера при скрытой вкладке + тосты, десктоп 3 колонки (Отряд|Чат|Радар), мобильная вёрстка.
7. [x] Проверено на железе 2026-09-22: плата 2 (`rlphantom`, USB-1388) + плата 1 (SHADOW по эфиру) в скваде, `bridge/send` canned 12 записан шлюзом в Serial, `smoke.py` зелёный.
8. [x] Деплой: `web/tools/web-install.sh` — user-сервисы + udev-автостарт шлюза при подключении CH340 (см. `web/systemd/README.md`).
9. [x] v0.2.1: у веба нет своей персоны — всё говорится от персонажа платы (`nick` в announce моста: payphone NAME, иначе индексный ник); без платы отправка невозможна (503); `POST /api/peers` закрыт (410).
10. [x] v0.2.2 Логово: таб-часы DESK (SEG 1:1, сквад в скинах outfit/shade из моста, XP-письмо, детекция, местный таймер); `desk`-настройки переносятся с платы; мост отдаёт скин+настройки, шлюз — сквозной, demo — с демо-платой.

## 5. browatch-app (Native Kotlin BLE)

1. [x] Каркас `/run/media/al/ARCHIVE/code/browatch-app`: `settings.gradle`, `app/build.gradle`, `AndroidManifest` (BLUETOOTH_SCAN/CONNECT, ACCESS_FINE_LOCATION), `MainActivity.kt`, `MeshBleScanner.kt` (заглушка), свой git-репо.
2. [ ] Сканер: `BluetoothLeScanner` + `ScanFilter` по manufacturerData CID `0xFFFF` + magic `SQ`/`ST`; парсинг advert `SQM1` (appearance word, имя) — порт `src/squachmesh.cpp:67-126`.
3. [ ] Экран списка пиров + чат (Jetpack Compose) + хранение фраза-ключа (EncryptedSharedPreferences), позже — полный MeshMsg (AES-CCM через BouncyCastle, PBKDF2 20000).
4. [ ] Режим USB-моста (параллельно BLE, детали — в `04-android.md`): OTG-serial к плате или D1-донглу, тот же протокол `[BW …]` — полный UI без BLE и без разбора эфира на телефоне.
5. [ ] Проверки: `./gradlew assembleDebug` (нужен Android SDK), юнит-тест парсера advert на golden-векторах из `src/squachmesh.cpp`.
6. Критерий: телефон видит BroWatch/CYD с включённым TRANSMIT как пира с именем и уровнем сигнала.

## 5a. USB-донгл на ESP32 — [x] реализован (головное окружение `esp32-dongle`)

Решение пользователя: ESP8266 D1 Mini не берём (у него физически нет BLE —
ни BLE-детекций, ни SquachMesh; только WiFi-половина). Донгл — классический
ESP32 (DevKit/WROOM-32): BLE 4.2 хватает и mesh, и BLE-детекциям, а CP210x
уверенно держит 921600.

Что сделано:

1. Новое окружение `[env:esp32-dongle]` в `platformio.ini` (в `default_envs`):
   `-DDONGLE=1`, `-DSERIAL_BAUD=921600`, `-DBW_BRIDGE=1`, `monitor_speed 921600`.
   Без TFT-конфига: драйвер по умолчанию компилируется, его пины инертны.
2. Тот же код, те же детекции и mesh, что на платах: `src/main.cpp` под
   `-DDONGLE=1` пропускает дисплей/тач/калибровку/фреймбуфер (−75 КБ RAM) и
   UI-экраны; `loop()` — короткий цикл `engine + Mesh/MeshTalk + Bingo +
   BwBridge + OtaCore` раз в 10 мс. Сборка — SUCCESS (RAM 20.6%, Flash 66.4%).
3. Тот же протокол `[BW …]` → ноль изменений в `gateway.py`/`server.py`/вебе;
   в вебе донгл виден как `USB-XXXX` (`client: bw esp32-dongle/…`).
4. Ограничения headless-режима: нет часов DESK/бинго/дневника/настроек на самом
   донгле (весь UI — в вебе/приложении); PIN-экран недоступен — мост всегда
   открыт, кто с USB-кабелем, тот и хозяин; нуджи апдейта не обрабатываются
   (обновление — по USB/OTA вручную).
5. Проверка на железе — за пользователем: `pio run -e esp32-dongle -t upload`,
   затем `gateway.py --port /dev/ttyUSB0 --baud 921600`.
6. Позже: C3/S3-mini варианты, когда один из них окажется на стенде
   (S2 исключён — у него тоже нет Bluetooth).

## 5b. Android: полный UI через USB-мост (в план по запросу пользователя)

Дополнение к §5, не замена: рядом с BLE-прямым путём — режим OTG-serial
(библиотека `usb-serial-for-android`, драйверы CH340/CP210x уже внутри):

1. Приложение открывает USB-порт платы (мост `-DBW_BRIDGE=1`) или донглов из §8,
   читает `[BW …]` (тот же парсер, что `gateway.parse_line`), рисует чат/сквад/детекции.
2. Отправка — теми же `[BW {"t":"send",…}]` в порт; mesh-эфир, фраза и ключи живут
   на плате/донгле, телефон эфир не разбирает и ключи может не хранить.
3. Плюс: полный UI на любом телефоне с OTG без BLE-скана и криптозависимостей;
   минус: нужен кабель/донгл. Детали — в `04-android.md`.

## 6. Порядок выполнения (рекомендуемый)

1. Вики каркас (этот файл + 01-obzor) → 2. browatch-web каркас + MVP чат → 3. browatch-app каркас → 4. строки-инвентаризация + LANG toggle → 5. шрифты RU → 6. перевод → 7. BW-протокол + gateway → 8. BLE-парсинг в app/web-bluetooth.
2. Каждый этап заканчивается проверкой из своего раздела. Коммиты мелкие, по-русски или по-английски — как остальной лог (`git log` — английский).

## 7. Риски

- Кириллица в GLCD/Font16/Bangers — самый ёмкий этап; начать с `font 1`, Bangers отложить (транслит заголовков).
- `wrapText`/`textWidth` вёрстка поедет на широких буквах (Ж, Ш, Щ) — заложить ручную правку ключевых экранов.
- Flash/RAM: +108 КБ flash / +6 КБ RAM свободно (IDEAS.md) — RU-строки держать во flash (`PROGMEM`), не в RAM.
- NimBLE + Serial-шлюз одновременно: шлюз только читает/пишет UART, радио не трогает; бод 2M — кабель и драйвер CH340/CP210x критичны.
- Порт 40400: проверить `lsof -i :40400` перед стартом; в коде — константа `PORT=40400`, переопределяемая env `BROWATCH_PORT`.
