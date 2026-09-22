# BroWatch Android (Native Kotlin + BLE)

> Статус: каркас. Репо: `/run/media/al/ARCHIVE/code/browatch-app`.

## Что есть

- `MeshBleScanner.kt` — активный скан (`SCAN_MODE_LOW_LATENCY`); пассивный не видит
  scan-response с сообщениями (`src/detection.cpp:835-847`).
- `SquachMeshParser.kt` — порт декодера `src/squachmesh.cpp:67-126`
  (magic `SQM1`, version=1, flags=0, согласие custom↔len, ASCII-имя, clamp индексов).
- `SquachMeshParserTest.kt` — 4 юнит-теста (векторы сверены с правилами прошивки).
- `AndroidManifest.xml` — `BLUETOOTH_SCAN/CONNECT`, LE-фильтр.

## Что дальше

1. Экран пиров + чат (Compose), хранение фразы-ключа.
2. Scan-response `ST`/MeshMsg: CANNED/TEXT/EMOTE (см. `include/meshmsg.h`).
3. Крипто: PBKDF2 20000 + AES-128-CCM (BouncyCastle), инвайт X25519.
4. `./gradlew assembleDebug` (нужен Android SDK) + тест на железе рядом с платой (TRANSMIT вкл).

## Режим USB-моста (в план по запросу пользователя)

Параллельно BLE-прямому пути: телефон через OTG-кабель открывает USB-serial платы
(`-DBW_BRIDGE=1`, 2000000 бод) или ESP32-донгла (`00-PLAN.md` §5a, 921600 бод)
и говорит тем же протоколом `[BW …]`, что `gateway.py`
(парсер — порт `gateway.parse_line`, ~10 строк).

- Чтение: `peer`/`msg`/`detection`/`emote` → экраны пиров/чата/детекций (те же ViewModel,
  что у BLE-пути; источник данных подменяется).
- Запись: `send` (text/canned/emote) и `hello`/`ping` — теми же строками в порт.
- Библиотека порта: `usb-serial-for-android` (CH340/CP210x из коробки, разрешения
  через `UsbManager.requestPermission`).
- Плюс: полный UI без BLE-скана, криптоподписей и хранения ключей на телефоне —
  эфир, фраза и ключи живут на плате/донгле. Минус: нужен кабель или донгл.
- Проверки: юнит-тест парсера `[BW]` на кадрах из `API.md` веб-репо; живой тест —
  плата по OTG, чат туда-обратно со второй платой.
