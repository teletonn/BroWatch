# BroWatch в России: что детектируется, чем и почему (2020–2026)

Исследование под прошивку: какие устройства каждой категории реально встречаются
в РФ и какие у них радио-сигнатуры, видимые ESP32 (BLE service UUID / имя,
WiFi SSID / BSSID OUI). Собрано в сентябре 2026: три поисковых прохода +
сверка с первичными источниками (мануалы вендоров, IEEE OUI, исходники
Meshtastic/MeshCore/RNode, OpenDroneID, нормативка РФ).

Легенда: **CONFIRMED** — значение найдено с источником; **UNVERIFIED** — вывод
по аналогии/вторичный источник; **NOT FOUND** — в открытых источниках нет
(выдумывать запрещено); **NEGATIVE** — проверено, что излучения нет.

## 0. Коротко: что реально ловит ESP32 в РФ

| Видно в эфире | Сигнатура в прошивке | Тип |
|---|---|---|
| `EZVIZ_XXXXXX` (setup AP) + OUI EZVIZ/Hikvision | SSID `EZVIZ_`, OUI ×15 + Hikvision ×13 | CAMERA |
| `HAP_xxxxxx` (setup AP Hikvision) | SSID `HAP_` | CAMERA |
| `DAP-XXXXXXXXX` (setup AP Dahua/Imou) | SSID `DAP-`/`DAP_` | CAMERA |
| `Tapo_Cam_XXXX` (setup AP) | SSID `Tapo_Cam_` | CAMERA |
| Подключённые камеры по BSSID | OUI Dahua ×27, Imou ×3, Imilab ×5, D-Link, Axis, Hanwha | CAMERA |
| Meshtastic-узлы (всегда, BLE) | SVC `6ba1b218-…` (HIGH), имя `Meshtastic_xxxx` | MESH |
| MeshCore companion (BLE) | NUS + имя `MeshCore-*`, `Whisper-`, `WisCore-`, `LowMesh_MC_` | MESH |
| RNode с включённым BT | NUS + имя `RNode XXXX` | MESH |
| `MeshCore-OTA` (минуты OTA) | SSID `MeshCore-OTA` | MESH |
| DJI/Autel/самоделки с Remote ID | `0xFFFA` + декод OpenDroneID (уже было) | DRONE |
| AirTag/SmartTag/Chipolo | штатные сигнатуры (параллельный импорт = то же железо) | трекеры |
| Деаутентификаторы, Pineapple, Flipper | штатные сигнатуры (железо то же) | HACKER/DEAUTH |

**Невидимо для ESP32 (NEGATIVE, проверено):** всё государственное —
камеры «Безопасного города» (проводной PoE), домофоны Beward/DKS, нагрудные
«Дозор», комплексы АвтоУраган/Вокорд/Стрелка/Автодория/Кордон (провод/LTE/
радар 24 ГГц, BLE/WiFi нет), Dahua VTO2202F, аналоговые домофоны. Их нет в
таблицах сигнатур сознательно: детектор не должен врать.

## 1. Государственные камеры: «Безопасный город», ЕЦХД

- NtechLab — только софт (FindFace Security): контракт через «Электронную
  Москву» → ДИТ с 01.01.2020, ~105 000 подъездных камер, всего до 175 000;
  мультивендор NtechLab + VisionLabs + Tevian. У полиции — мобильное
  приложение с алертами по LTE. Своих радиомаяков нет. CONFIRMED
  (vedomosti.ru, habr.com/ru/news/486008, wired.com, biometricupdate.com).
- Железо Москвы 2025–2026 по тендерам: Hikvision `DS-2CD2123G2-IU`,
  купольные `i-FLOW F-IC-2442C2MS`, подрядчики «Ситроникс», префектуры.
  Всё PoE-провод. CONFIRMED (rostender.info, synapsenet.ru).
- BEWARD: >70 000 домофонных IP-систем + >15 000 купольных в Москве;
  оператор НКС; модели `BD75-1-VP`, `M-962-RZH02A`. Провод. CONFIRMED
  (beward.ru/projects).
- Ростелеком: Москва >46 тыс. подъездных + >10 тыс. дворовых → ЕЦХД;
  СПб >9507 домофонных камер (Beward, Huawei, Byterg); Орёл/Липецк RVi +
  BEWARD; Ростов-2024 +647 камер, 254 с биометрией. Провод, защищённые
  каналы. CONFIRMED (company.rt.ru, beward.ru).
- RVi — российский вендор с разделом «Безопасный город», но модель
  контракта Москвы не найдена; производство — OEM Hikvision и Dahua
  (CONFIRMED: ipvm.com Hikvision-OEM directory, learncctv.com Dahua-OEM
  list — в обоих списках «RVi»). Следствие для детектора: RVi ловится
  пулами Hikvision/Dahua, своих OUI у RVi в IEEE нет (NOT FOUND).
- Qtech в московском Safe City — NOT FOUND, в код не вносилось.

**Вывод:** городские камеры — `wired_only`. По OUI их видно только в
проводном ARP-скане, не ESP32. В прошивке — только страницы HIKVISION/
DAHUA (OUI-пулы покрывают и OEM-перемаркировки).

## 2. Полиция: «Дозор», АвтоУраган, Вокорд, радары

- Нагрудные регистраторы «Дозор-77/78» (БайтЭрг, >200 000 силовикам):
  интерфейсы — только Mini-USB + терминал + ПО Windows; WiFi/Bluetooth
  отсутствуют; GPS в Дозор-78 — приёмник (ничего не излучает). CONFIRMED
  NEGATIVE (byterg.ru, police-cam.ru). В код не вносится вообще.
- АвтоУраган-ВСМ2/ВСМ2-М (Технологии Распознавания): видеодатчики RNC/RN,
  блоки SP-V2/КУВ-А, ГЛОНАСС-приёмник, ИК-прожектор; связь — провод/LAN.
  VOCORD Traffic/MicroCyclops: то же через интеграторов. Радар — 24 ГГц
  (не WiFi/BLE). Публичных AP-SSID/BLE-сервисов нет. CONFIRMED NEGATIVE
  (recognize.ru, буклет ВСМ2-М v4.1, vocord.ru).
- Стрелка-СТ, Автодория, Кордон, Кречет, MultaRadar: бэкхол —
  оптика/Ethernet + сотовая; в даташитах нет setup-AP/SSID/BLE.
  NEGATIVE как класс (оговорка: сервисный LTE-роутер рядом возможен, но
  это не сигнатура комплекса).
- BODY-CAM G-0 и прочие эпизодические бодикамы в закупках МВД: публикаций
  с WiFi нет — UNVERIFIED, не вносится.

## 3. Потребительские камеры (главная цель в РФ)

### 3.1 EZVIZ (суббренд Hikvision) — приоритет №1
Модели в РФ: C6N, C3W/C3TN, H8c, EB3, BC1/BC2, BM1, DB1, звонки DB1/DB2.
Setup-AP **CONFIRMED**: `EZVIZ_XXXXXX` / `EZVIZ_<SN>` (SN — 9-значный
серийник; вариант `ezviz_xxxxxx`), пароль `EZVIZ_<VerificationCode>`
(6 заглавных с шильдика); вход — Reset 4–5 с, LED быстро синий.
Источники: support.ezviz.com (Unable-to-join-network FAQ), QSG DB1
UD11788B («Wi-Fi Name: EZVIZ_XXXXX»), home-assistant community.
OUI **CONFIRMED** (свои, Hangzhou EZVIZ Software, 15 блоков MA-L):
`0C:A6:4C, 20:BB:BC, 34:C6:DD, 38:F2:5D, 54:D6:0D, 58:8F:CF, 64:24:4D,
64:F2:FB, 78:A6:A0, 78:C1:AE, 94:EC:13, AC:1C:26, EC:97:E0, F4:70:18,
FC:24:22` (maclookup.app, netify.ai). В прошивке: SSID `EZVIZ_` + все 15
OUI → страница EZVIZ.

### 3.2 Hikvision / HiWatch
WiFi-модели частного сектора: `DS-2CD2443G0-IW, DS-2CD2443G2-IW,
DS-2CD2421G0-IW, DS-2CD2432F-W` (802.11 b/g/n, клиент к домашнему роутеру).
Setup-AP **CONFIRMED**: `HAP_xxxxxx` (xxxxxx = verification code с
шильдика), пароль — последние 8 серийника. Источник: официальный PDF
Hikvision «How to Set Wi-Fi Connection of Wi-Fi Camera via Mobile Phone».
WiFi-NVR раздаёт свою сеть, камеры — клиенты. OUI **CONFIRMED** (84 блока;
в прошивке 13 якорных: `44:19:B6, C0:56:E3, 28:57:BE, 4C:BD:8F, C4:2F:90,
54:C4:15, 18:68:CB, 64:DB:8B, BC:AD:28, D4:88:90, 94:E1:AC, A4:14:37,
B4:A3:82`; полный список — в IEEE). HiWatch своего OUI/SSID не имеет —
это переупакованный Hikvision (NOT FOUND, покрывается его пулом).
В прошивке: SSID `HAP_` + OUI → страница HIKVISION.

### 3.3 Dahua / Imou
Модели: Imou Ranger 2, Bullet 2, Cell Go, Cruiser, Rex; Dahua DH-IPC-*.
Setup-AP **CONFIRMED**: хотспот `DAP-XXXXXXXXX` / `DAP-<serial>`
(телефон цепляется к нему для передачи WiFi-кредов; есть AP Mode для
прямого просмотра). Источники: store.imou.com FAQ («hotspot signal named
that begins with DAP»), мануал Ranger 2, DMSS App Manual V1.1/V1.3
(«AP configuration: turn on device hotspot»). WiFi-NVR раздаёт
`NVR2.4G` (в сигнатуры не внесён — один wiki-источник, см. §8).
OUI Dahua **CONFIRMED** (27 блоков): `08:ED:ED, 14:A7:8B, 24:52:6A,
38:AF:29, 3C:E3:6B, 3C:EF:8C, 4C:11:BF, 5C:F5:1A, 64:FD:29, 6C:1C:71,
74:C9:29, 8C:E9:B4, 90:02:A9, 98:F9:CC, 9C:14:63, A0:BD:1D, B4:4C:3B,
BC:32:5F, C0:39:5A, C4:AA:C4, D4:43:0E, E0:2E:FE, E0:50:8B, E4:24:6C,
F4:B1:C2, FC:5F:49, FC:B6:9D`. OUI Imou (Hangzhou Huacheng)
**CONFIRMED**: `90:6A:94, A8:31:62, 30:24:50` (часть железа несёт OUI
Dahua — оба пула сматчены). В прошивке: SSID `DAP-`/`DAP_` + 30 OUI →
страницы DAHUA, IMOU. ActiveCam (DSSL) — Dahua OEM (CONFIRMED, ipvm):
покрывается пулом Dahua, своей строки нет.

### 3.4 TP-Link Tapo / VIGI
Модели: Tapo C100/C110/C200/C210/C220/C310/C320WS/C400/C500/C510W,
C125/C225. Setup-AP **CONFIRMED**: `Tapo_Cam_XXXX` (XXXX — последние 4
знака MAC); сброс — Reset >5 с; только 2.4 ГГц у части; новые модели
дополнительно спариваются по BLE (префикс не опубликован). Источники:
tp-link.com FAQ 2747/2710, tapo.com FAQ 51/67. OUI: своего «Tapo-OUI»
нет (263+ блоков TP-Link, в основном роутеры) — **сознательно не
матчится** (правило Sonos: пул на роутерах даст камеры из каждого
подъезда). VIGI — в основном проводной PoE, SoftAP-префикс NOT FOUND.
В прошивке: только SSID `Tapo_Cam_` → страница TAPO CAM.

### 3.5 Xiaomi / Mi Home (Imilab)
Модели: MJSXJ05CM, MJSXJ10CM, C200/C300/C400/C500, CW400, AW300.
Setup-AP **отсутствует** (CONFIRMED NEGATIVE): привязка — QR-код с экрана
телефона к объективу (mi.com FAQ KA-12766/KA-06578). Детект — только по
OUI производителя камер: Shanghai Imilab **CONFIRMED**
`60:7E:A4, 78:DF:72, 94:F8:27, B8:88:80, B4:10:1C` (macverify, hwaddress,
maclookup). Корпоративные блоки Xiaomi **сознательно не матчатся**: 338
префиксов сидят на телефонах (правило Sonos). В прошивке: 5 OUI →
страница IMILAB CAM.

### 3.6 Остальные
- D-Link DCS: настройка WPS/mydlink, своего AP нет (CONFIRMED, мануалы
  DCS-5010L/5030L). `B0:C5:54` — CONFIRMED на живой DCS-5020L (Shodan
  серт.). В прошивке один OUI, MED → страница D-LINK. Широкие блоки
  D-Link — роутеры, не матчатся.
- Axis: OUI **CONFIRMED** (4 блока IEEE: `00:40:8C, AC:CC:8E, B8:A4:4F,
  E8:27:25`; серийник = MAC). Профи-провод, AP нет. В прошивке 4 OUI →
  страница AXIS (была).
- Hanwha Vision (ex-Samsung Techwin): OUI **CONFIRMED** (`00:09:18`
  Samsung Techwin, `E4:30:22` Hanwha Vietnam); `30:C9:AB, 00:16:75` —
  UNVERIFIED, не внесены. Профи-провод. В прошивке 2 OUI → страница HANWHA.
- Beward: своего OUI в IEEE нет (NOT FOUND), камеры проводные; детект по
  OUI невозможен. Не вносится.
- Novicam: своего OUI нет; в списке Hikvision-OEM (CONFIRMED, ipvm) —
  покрывается пулом Hikvision. Не вносится отдельно.
- Falcon Eye: своего OUI нет; бюджетные линейки — Xiongmai-платформа
  (приложения V380/XMEye, хотспот `MV+ID`, пароль часто `1234567890` —
  CONFIRMED для платформы: v380.org, manuals.plus). В прошивку **не
  внесено**: префикс `MV` из двух букв — не сигнатура (коллизии с любыми
  сетями). Кандидат на будущее при подтверждающем замере.
- RVi: см. §1 (Hikvision/Dahua OEM, покрывается пулами).
- Tuya/Smart Life (часть WiFi-камер Tantos/Falcon Eye, розетки, лампы):
  транзиентный хотспот `SmartLife-XXXX` / `Tuya-XXXX` (XXXX — последние
  цифры MAC) **CONFIRMED** (developer.tuya.com: «AP defaults to
  SmartLife-xxxx»). Не цель детектора (шум настройки), в сигнатуры не
  внесён; страница TUYA покрывает OUI-матчи.

### 3.7 BLE у камер
Итог: ни один вендор камер 2020–2026 не публикует BLE Local Name или
GATT Service UUID для камер (Imou Cell Go упоминает «bluetooth pairing»
без UUID; у Tapo BLE-спаривание без префикса). NOT FOUND — UUID не
вносятся. BLE-матчинг камер — только по MAC-OUI из ADV-пакета.

## 4. Домофоны и звонки

- EZVIZ DB1/DB2: AP Pairing, setup-AP `EZVIZ_XXXXXX` (CONFIRMED, тот же
  QSG) → покрывается SSID `EZVIZ_`, страница EZVIZ упоминает звонки.
- Hikvision DS-KB6003/6403-WIP: AP-режим по Reset 5 с (CONFIRMED,
  мануал UD06661B), но фиксированный паттерн SSID в мануалах **NOT
  FOUND** — только событие без префикса, не вносится.
- Hikvision DS-KD/KV (подъездные/коттеджные), Dahua VTO2202F, Beward DKS,
  Commax, аналоговые Tantos/Slinex/Falcon Eye: провод или WiFi-клиент,
  своих AP нет (CONFIRMED по даташитам) — NEGATIVE.
- Xiaomi Doorbell 3/3S: WiFi-клиент 2.4 ГГц + звонок 433 МГц SRD
  (CONFIRMED, manualslib); BLE нет («Bluetooth gateway not supported»).
- Slinex/Tantos-WiFi: мониторы — клиенты облаков (Slinex Smart Call,
  vhOme), панели — аналог/PoE. Setup-AP NOT FOUND.
- BAS-IP (Украина, до 2022 популярен в РФ, дистрибьютор bas-ip.shop
  активен — CONFIRMED): панели — проводной PoE, но **есть BLE**
  (считыватели UKEY Mobile Access, открытие смартфоном). Точный Service
  UUID **NOT FOUND** — в детектор не внесён, помечен как известный
  фоновый BLE-класс access-control в подъездах.

## 5. Меш-сети (новый тип MESH, значение 5)

### 5.1 Meshtastic — детектируется отлично
- BLE Service UUID **CONFIRMED** (firmware `BluetoothCommon.h`,
  `NimbleBluetooth.cpp`, `NRF52Bluetooth.cpp`, доки client-api,
  `python/ble_interface.py`):
  `6ba1b218-15a8-461f-9fa8-5dcae273eafd`
  (+ характеристики ToRadio `f75c76d2-…`, FromRadio `2c55e69e-…`,
  FromNum `ed9da18c-…` — для детектора не нужны). UUID уникален,
  коллизий нет → в прошивке HIGH.
- BLE-имя **CONFIRMED** (`main.cpp getDeviceName`): дефолт
  `Meshtastic_ab13` (последние 2 байта MAC), после настройки
  `<ShortName>_ab13`. Имя — в **scan response**, не в primary adv:
  сканер обязан active-scan (у нас уже такой — имена Flipper ловятся
  тем же путём). Переименованные узлы всё равно ловятся по UUID.
- WiFi AP **CONFIRMED отсутствует**: firmware 2.x (2022–2026) — только
  STA-клиент (`WiFiAPClient.cpp`), hostname `Meshtastic-XXXX`, mDNS
  `meshtastic.local`; SoftAP не поддерживается (доки radio/network).
  Старый SoftAP 2020–2021 без дефолтного SSID — legacy, не ловится.
- LoRa-задел (для будущего модуля): EU_433 — 433.875 МГц, EU_868 —
  869.525 МГц, RU — 868.7–869.2 МГц, модем LongFast
  (SF11/BW250/CR 4/5). Источник: meshtastic.org (lora, radio-settings).

### 5.2 MeshCore — средне (только companion)
- BLE Service **CONFIRMED**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
  (RX `…02`, TX `…03`) — это стандартный **Nordic UART Service**.
  Тот же UUID у RNode, ESP-IDF ble_uart example, Adafruit Bluefruit —
  **по одному UUID вердикт не выносится** (в прошивке NUS не матчится
  вовсе; голый NUS остаётся UNKNOWN — лучше молчать, чем врать).
- BLE-имя **CONFIRMED** (`MyMesh.h BLE_NAME_PREFIX "MeshCore-"`;
  сторонние клиенты: `MeshCore-, Whisper-, WisCore-, HT-,
  LowMesh_MC_`, плюс вендорные Seeed/Lilygo). В прошивке — связка
  «имя из списка» → MESH/MED, вендор MeshCore.
- Репитеры/room-server BLE для телефона **не поднимают** (CONFIRMED,
  EastMesh wiki) — для BLE-сканера невидимы. WiFi — STA-клиент; SSID
  задаёт оператор (дефолта нет). Единственный известный дефолтный AP:
  `MeshCore-OTA` при OTA без настроенного WiFi (CONFIRMED, EastMesh) —
  в прошивке SSID-правило, окно — минуты.

### 5.3 Reticulum / RNode — тихо по умолчанию
- Сам Reticulum не beacon'ит (CONFIRMED, reticulum.network manual) —
  штатно детекту нет, и это ожидаемо.
- RNode firmware с включённым BT **CONFIRMED** (`Bluetooth.h
  bt_setup_hw`): имя `RNode XXXX` (пробел + 4 HEX от BT-MAC-хеша),
  транспорт — тот же NUS `6E400001-…` (код RNode_Firmware,
  Retichat iOS-клиент). BT по умолчанию **выключен** (флаг в EEPROM,
  включается через rnodeconf/кнопку). В прошивке: префикс `RNode ` +
  (NUS виден сканеру, но вердикт — по имени) → MESH/MED. Молчание —
  норма, не доказательство отсутствия.
- WiFi-конвенций сообщества **нет** (CONFIRMED): SSID/PSK задаёт
  оператор через rnodeconf, дефолта нет; Reticulum поверх WiFi — обычные
  UDP/TCP без beacon-SSID.
- Форки (`reticulum-rnode`, `reticulum-espnow`) рекламируются так же:
  покрываются тем же правилом.
- `ble-reticulum` (BlueZ GATT для Linux): UUID/имя из конфига, единого
  нет — не вносится.

### 5.4 Железо и OUI
Espressif — ~339 MA-L (частые `24:0A:C4, 24:6F:28, 30:AE:A4…`), все
интерфейсные MAC выводятся из одного base (BT = base+2). Но: кастомный
MAC и рандомизация ломают метод, а главное — проект сознательно не
матчит голый Espressif (см. комментарий у HACKER: иначе детектор
флагает полкомнаты и сам себя). nRF52-платы (T-Echo, RAK4631, T1000-E)
— random static из FICR, OUI-фильтр бесполезен вовсе. **Приоритет:
UUID → имя → (OUI никогда).**
TC2-BBS/TinyBBS — софт поверх/внутри Meshtastic, отдельных сигнатур
нет (наследуют UUID). «Benson» за 2024–2026 в primary sources не
найден — не вносится.

## 6. Дроны (тип DRONE уже был — OpenDroneID)

- DJI (доминирует в РФ через параллельный импорт: Mini/Neo/Mavic):
  вещают стандартный ASTM F3411 Remote ID — BLE legacy advertisements
  (Mfg `0xFFFA`, app `0x0D`) и/или WiFi beacon/NAN на 6 канале 2.4 ГГц
  (DJIs — скорее 2.4, Skydio — 5 ГГц). DJI шлёт серийник как ID.
  CONFIRMED (daniellethurow.com/blog 07.2026 разбор F3411 побайтово,
  r/dji тест Mini 4/5 Pro 03.2026, DJI Remote ID FAQ). Собственный
  DJI DroneID (OcuSync/enhanced WiFi) декодируется только SDR
  (DragonSDR/AntSDR + droneid-go) — ESP32 его не видит; в детектор не
  вносится, в вики зафиксировано как ограничение.
- Проекты-декодеры (для понимания покрытия): OpenDroneID core-c
  (кодирование/декодирование F3411 + ASD-STAN prEN 4709-002), ESP32
  receiver examples, lukeswitz/WiFi-RemoteID (ESP32-C3/S3 промisc +
  BT4/5), alphafox02/droneid-go (WarDragon). Наш движок — тот же класс:
  `0xFFFA` + mergeRemoteId, BLE 4.2 (long-range BT5-форма — невидим,
  задокументировано на странице типа).
- FPV/аналог: аналоговое видео 5.8 ГГц + ELRS/Crossfire — не BLE/WiFi
  по конструкции, ESP32 их не видит (инженерный факт, не требует
  источника: другие диапазоны и модуляции).
- Российские ZALA/Supercam/Orlan/Geoscan: публичных BLE/WiFi/Remote ID
  данных нет (военные) — NOT FOUND, не вносится.
- Нормативка РФ (влияет на то, ЧТО летает с Remote ID): учёт БВС
  0.15–30 кг с 2019 (ПП-658, Госуслуги/Росавиация); ПП-1701 от
  30.11.2024 — требования к оборудованию удалённой идентификации
  (опознавательный индекс, категория, высота, координаты + СКЗИ);
  НЛГ УИ-БАС (приказ Росавиации 829-П от 01.11.2025) — нормы лётной
  годности этого оборудования. Т.е. легальные борта в РФ всё чаще
  светятся — детектор их видит штатным DRONE-путём. Новая страница
  REMOTE ID упоминает DJI и ПП-1701.

## 7. Трекеры, очки, фоновый шум

- AirTag/2 (параллельный импорт, Device365/xtexno каталоги; UWB Precision
  Finding в РФ ограничен Apple — CONFIRMED): сигнатура та же
  (`CID 0x004C & type 0x12`) — уже в прошивке, ничего менять не надо.
  Параллельный импорт 2025 — $23,1 млрд (ria.ru 02.2026), механизм
  продлён до конца 2026.
- Samsung SmartTag/2: протокол Offline Finding, helper-фильтр `0xFD69`
  (CONFIRMED: USENIX 2024, слайды Yu). Наш `0xFD5A` — собственный SIG
  UUID discovery (уже в прошивке). Наличие в РФ — UNVERIFIED
  (прямых поставок нет, параллельный импорт вероятен); сигнатура та же.
- Google Find Hub Network: актуальные трекеры — НЕ 0xFEAA-Eddystone
  (это легаси 2015–2016), а FHN Accessory Spec (Fast Pair + эфемерные
  ID, DULT `0xFCB2`, Network ID `0x02`). Наш `0xFEAA`-матч — MED с
  оговоркой в коде; массовых продаж Chipolo/Moto Tag в РФ — NOT FOUND
  (если ввезено — сигнатура та же).
- Tile: SIG Company ID `0x02E5` (CONFIRMED); в РФ практически
  отсутствует (NOT FOUND) — в целевые для РФ не ставится.
- Дешёвые BLE-метки (Nutale/iTag/iSearching — массово на Ozon/WB,
  CONFIRMED на уровне категории): connectable ADV, имена `iTAG/Nut/TAG`,
  фиксированного UUID нет (часть — перепрограммируемые nRF-iBeacon).
  Без жёсткой сигнатуры — не вносится (кандидат: словарь имён + RSSI).
- Ray-Ban Meta Gen1/2: наличие в РФ CONFIRMED (параллельный импорт,
  rifastore/onlyphones 2024–2026); BLE-пара `CID 0x01AB + SVC 0xFD5F`
  CONFIRMED (реверс banrays) — уже в прошивке (`0xFD5F` + company IDs).
  Важно: детект только в pairing/power-on/case-out; при ношении —
  рандомизированный MAC (ограничение задокументировано в DETECTIONS.md).
- Xiaomi AI Glasses (06.2025): только Китай, в РФ NOT FOUND.
- Yandex Station (Алиса): WiFi-клиент + BT-колонка; временная AP
  `Yandex Station XXXX` — UNVERIFIED (нет официальной цитаты). Шум, не
  цель — не вносится.
- BAS-IP UKEY BLE в подъездах: известный фоновый класс без точной
  сигнатуры (см. §4).

## 8. Сознательно НЕ внесено (и почему)

Правило проекта (см. историю с Sonos в signatures.cpp): лучше молчать,
чем врать. Не внесены: корпоративные OUI Xiaomi и пул TP-Link (сидят на
телефонах/роутерах), `MV+ID` Xiongmai (двухбуквенный префикс — не
сигнатура; ждёт подтверждающего замера), `NVR2.4G` Dahua (один источник),
голый NUS UUID (неразличим: MeshCore/RNode/DIY), голый Espressif OUI
(флагает полкомнаты), проводные гос-системы (невидимы), Qtech-контракт,
Xiaomi AI Glasses, Tile-для-РФ, «Benson», кастомный SSID Meshtastic
(дефолта нет — ловить нечего).

## 9. Что может измениться (watchlist)

- LoRa-модуль BroWatch: частоты 433.875 / 869.525 МГц LongFast первые
  кандидаты; MeshCore/Reticulum частоты операторские (фиксированных нет).
- НЛГ УИ-БАС + ПП-1701: по мере оснащения легальных бортов DRONE-хиты
  будут расти — нормально, не баг.
- Falcon Eye `MV+ID`: подтвердить замером nRF Connect — тогда внести.
- BAS-IP UKEY UUID: снять adv-дамп у панели с UKEY — тогда внести класс.
- Xiaomi AI Glasses при появлении в РФ: снять BLE adv, проверить UUID.
- Обязательная маркировка/учёт дронов дальше 2026: следить за приказами
  Росавиации (829-П — первый звонок).
