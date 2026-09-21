# BroWatch — обзор и гайд по использованию

> Адаптация исходной RU-вики апстрима (`SquachWatch-CYD/wiki/01-obzor-i-gayd.md`, v1.16.1) под форк BroWatch.
> Здесь — то, что уже есть и работает. Про планы (RU-прошивка, веб, Android) см. [00-PLAN](00-PLAN.md).

## 1. Что это такое

**BroWatch** — прошивка для платы **ESP32-2432S028R** («Cheap Yellow Display», CYD):
дешёвый ESP32-модуль со встроенным TFT 320×240, резистивным тачем и слотом microSD.
Плата целиком и есть устройство; питание — по USB. Ничего допаивать не нужно.

Два режима в одном устройстве:

1. **Детектор.** Пассивно слушает Wi-Fi и Bluetooth 2.4 ГГц и сверяет пакеты
   с таблицами сигнатур: камеры-АЛПР (Flock Safety), боди-камеры (Axon), умные очки
   (Ray-Ban Meta, Snap), скиммеры, AirTag / SmartTag / Tile / Google-трекеры, Ring,
   дроны Remote ID (с декодом ASTM F3411: позиция, высота, серийник, позиция оператора),
   пенттест-железо (Flipper Zero, Pwnagotchi, Pineapple) и т.д. Всего **17 типов**.
   Совпадение — полноэкранный ALERT с карточкой устройства (вендор, имя, MAC, уровень сигнала).
2. **Коммуникатор (BroMesh).** Платы в радиусе BLE-видимости образуют «сквад»:
   presence-реклама внешности, **шифрованные сообщения** (AES-128-CCM, ключ из фразы 5 слов через PBKDF2 20000),
   совместные emote-сценки, инвайты без набора фразы (X25519 + 4 цифры сверки),
   пеленгация «охота на лиса» (HUNT), групповое обновление прошивок (UPDATE SQUAD).
   Работает **без интернета, без pairing, без аккаунтов**.

Обе «вещающие» половины по умолчанию выключены: `DETECT` (только приём) и `TRANSMIT`
(видимость) включаются явно после экрана-предупреждения. Код: `src/squachmesh.cpp`,
`src/meshtalk.cpp`, `src/meshmsg.cpp`, `src/mesh.cpp`, заголовки `include/squachmesh.h`, `include/meshmsg.h`.

## 2. Железо

| Плата | Статус | Заметки |
|---|---|---|
| 2.8" CYD ESP32-2432S028R, ST7789 (`env:cyd`) | основная | самый массовый вариант |
| 2.8" CYD, ILI9341 (`env:cyd-ili9341`) | поддерживается | та же плата, другая партия дисплея; неверный драйвер = белый экран |
| AWOK 2.4" на ESP32-Marauder V6.1 (`env:awok`) | поддерживается | другая распиновка, тач на общей шине |
| RL Phantom 2.4" резистивный (`env:rlphantom-r`) | поддерживается | на публичном прошивальщике |
| RL Phantom 2.4" ёмкостный (`env:rlphantom`) | только CI-сборка | на железе не проверен |
| 3.5" CYD (`env:cyd35`) | **заморожен, НЕ покупать** | boot loop на железе, исключён из сборки по умолчанию |

Распиновка 2.8" — `docs/PINOUT.md`. USB-мост обычно CH340 (CYD) или CP210x (AWOK);
нужен **дата-кабель**. Serial-консоль CYD — **2000000 бод** (`platformio.ini`).

## 3. Что детектируется (17 типов)

| Тип | Цель | Сигнатура (кратко) |
|---|---|---|
| `FLOCK` | Flock Safety ALPR | 29 Wi-Fi OUI + BLE-имя + company `0x09C8` |
| `AXON` | Axon боди-камеры, TASER | 3 OUI + SSID `AB2-/AB3-/AB4-/AXON-` |
| `META` | Ray-Ban Meta, Snap очки | BLE `0xFD5F` + company Meta/Luxottica/Snap |
| `SKIMMER` | BT-скиммеры | имена HC-03/05/06, RN42, BT04-A + SPP `0x1101` + 3 OUI |
| `RAVEN` | детектор выстрелов Raven | service UUID `0x3100–0x3500` |
| `AIRTAG` | Apple AirTag / FindMy | company `0x004C` + проверка payload |
| `DRONE` | дроны Remote ID | `0xFFFA` + декод ASTM F3411 |
| `ALPR` | Motorola / Genetec | 6 Wi-Fi OUI |
| `CAMERA` | IP-камеры | 17 OUI (Wyze, Amazon, Tuya, Verkada, Axis…) |
| `SAMSUNG_TAG` | Galaxy SmartTag | `0xFD5A` |
| `GOOGLE_TAG` | Chipolo / Pebblebee / Moto Tag | `0xFEAA` |
| `TILE` | Tile | `0xFEED/0xFEEC` |
| `RING` | Ring | 15 OUI |
| `DEAUTH` | деаутентификация | rate-burst, не сигнатура |
| `EVILTWIN` | rogue AP | один SSID с двух BSSID и разным шифрованием |
| `IBEACON` | proximity-маяки | `4C 00 02 15`, **выкл. по умолчанию** |
| `HACKER` | Flipper / Pwnagotchi / Pineapple | UUID `0x3081–83`, company `0x0E29`, OUI `0C:FA:22`, SSID `Pineapple_/pwned` |

Confidence — свойство **сигнатуры**, не типа: из 76 строк **32 High, 4 Medium, 40 Low**.
`ALERT FILTER` — порог показа. Подробности и происхождение каждой сигнатуры — `docs/DETECTIONS.md`.

## 4. Экраны и управление

- `SCAN` — главный экран (фон, Squachy, счётчики). `LOG` — последние 200 детекций. `DESK` — часы + фокус-таймер. На LOG третья кнопка — `CLR`.
- ALERT — полноэкранная карточка 60 с, тап — закрыть. `SNOOZE` — тихо до ребута, `IGNORE` — навсегда (`src/ui_alert.cpp`, `src/ignore_list.cpp`).
- Часы: NTP при boot-проверке обновлений, `TIME <epoch>` / `ZONE <name>` по USB-Serial (2000000 бод), зона из веб-флешера, раздача времени по скваду (`src/clock.cpp`).
- SD-лог: `squachwatch-<день>.log`, CSV `ts,type,rssi,mac,channel,vendor,ssid` (`src/sd_log.cpp`).
- Статус-светодиод RGB: дыхание темы, вспышки цвета детекции, двойной блинк непрочитанных (`src/status_light.cpp`).

## 5. BroMesh коротко (протокол — SquachMesh, совместим с оригиналом)

- Presence: BLE advert `SQM1` каждые 1500 мс, 8/20 байт (appearance word: ник/костюм/очки + имя 12 ASCII). Распознаётся в scan-callback раньше сигнатурных таблиц — свои своих не детектят.
- Сообщения: `Settings → BROMESH → MESSAGES → PHRASE` (5 слов; ROLL/ENTER; ~3 с PBKDF2). 24 готовые строки или до 48 символов с клавиатуры. Красный пузырь, подтверждение перед отправкой, read receipts.
- Сквад: до 16 мемберов, INVITE/AWAY/FORGET; `ADD TO SQUAD` — инвайт с 4 цифрами сверки без набора фразы.
- HUNT — пеленгация по шкале сигнала; UPDATE SQUAD — раздача прошивки по скваду.
- Важно для разработчиков моста: пассивный скан **не видит** scan-response (имена, сообщения) — нужен активный (`src/detection.cpp:835-847`).

## 6. Прошивка и сборка

- Быстрый путь без сборки: веб-флешер https://squachwatch.com/ (для BroWatch будет свой; пока собирается из `web-flasher/`).
- Сборка: PlatformIO (`platformio.ini`, `espressif32@6.5.0`), `pio run -t upload`; окружения `cyd`, `cyd-ili9341`, `awok` по умолчанию. Дока — `docs/BUILD.md`.
- Эмулятор: `sim/` (`make -j8`, `make shots`, `make wasm`); тесты: `make -C test`.
- Версия прошивки штампуется из `git describe` (`extra_script.py`); релизы — тегом `v*.*.*`.

## 7. Что дальше (форк BroWatch)

- Двуязычие RU/EN + кириллические шрифты.
- `browatch-web` (порт 40400): шлюз USB/Serial + чат с автообновлением.
- `browatch-app`: нативный Kotlin + BLE-сканер BroMesh.
- Детали — [план](00-PLAN.md).
