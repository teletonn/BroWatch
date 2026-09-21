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
