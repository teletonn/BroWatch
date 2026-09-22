# browatch-web API (v0.2.0)

База: `http://127.0.0.1:40400` (именно `127.0.0.1`: `localhost` может
резолвиться в `::1` и падать с connection refused). Всё — JSON, UTF-8.

## Presence: как на плате

Сквад платы — это кого слышно по эфиру последние 12 с (`PEER_STALE_MS`,
`BroWatch/src/mesh.cpp`). Сервер зеркалит:

- пир **«в эфире»** (`in_range: true`), пока его кадры свежее **15 с**
  (снапшот сквада с платы идёт каждые 5 с + запас; announce самой платы —
  каждые 30 с, для неё порог **35 с**);
- **«недавно был»** — свежее 5 минут (`RECENT_S`), потом забываем совсем;
- сортировка — как на плате: сначала кто здесь, потом остальные по свежести.

Пиры привозит **только шлюз** через `/api/ingest` (как плата слышит эфир).
`POST /api/peers` — только heartbeat самого веба (`id: "web:ИМЯ"`,
`client: "web"`), чужие id отклоняются (400). Клиент шлёт heartbeat каждые
10 с, пока вкладка открыта. Неизвестные кадры моста в чат **не** падают
(`kind: "drop"`) — мост шлёт только 4 вида.

## REST

- `GET /api/health` → `{"ok":true,"version":"0.2.0","port":40400,"peers":N,"in_range":K,"messages":M,"board_online":true,"board":{…}|null}`
- `GET /api/peers` → недавние пиры: `[{"id":"…","name":"…","rssi":-61,"client":"…","lang":"R","last_seen":…,"age_s":3,"in_range":true}]`
- `POST /api/peers` — heartbeat веба: `{"id":"web:имя","name":"имя","client":"web"}`
- `GET /api/squad` → то же + `last_msg` (последнее сообщение каждого)
- `GET /api/messages?since=ID` → `[{"id":1,"from":"…","text":"…","ts":…,"via":"web|board|mesh"}]`.
  `via`: `web` — локально в вебе, `board` — ушло в эфир через плату,
  `mesh` — пришло из эфира через плату
- `POST /api/messages` — `{"from":"web","text":"привет"}` → веб-локально (`via: web`)
- `GET /api/emotions?since=TS` / `POST /api/emotions` — `{"from":"…","emote":"WAVE|👍…","ts":…,"via":…}`.
  С платы приходят **имена** (`WAVE`, `HIGH FIVE`, … — `EmoteScript::name`,
  35 штук `MeshMsg::Emote`); клиент маппит их в эмодзи + RU-ярлыки
- `GET /api/detections?since=TS` → `[{"type":"FLOCK","mac":"…","rssi":-70,"vendor":"…","ts":…}]`
- `POST /api/ingest` — универсальный вход шлюза:
  - `{"t":"peer","mac":"…","name":"…","rssi":-61,"client":"bw …","lang":"R"}`
  - `{"t":"msg","from":"…","text":"…"}`, `{"t":"emote","from":"…","emote":"WAVE"}`,
    `{"t":"detection","type":"FLOCK","mac":"…","rssi":-70,"vendor":"…"}`
- `POST /api/bridge/send` — веб → плата. `text` обязателен (подпись для
  локального чата); плюс одно из: ничего (просто текст), `{"canned":0..49}`,
  `{"emote":0..34}` (индексы эфира, см. `meshmsg.h`). Сервер кладёт
  `{t:"send",…}` в очередь **и** сразу показывает сообщение в чате
  (`via: "board"`), потому что плата свои отправки в inbox не кладёт.
  → `{"ok":true,"queued":{…},"message":{…}}`
- `GET /api/bridge/outbox` — очередь для шлюза, отдаётся целиком и очищается (drain)

## Мост платы (USB-Serial, платы 2000000 бод, донгл 921600)

Прошивка с `-DBW_BRIDGE=1` (`BroWatch/src/bw_bridge.cpp`, флаг уже в
`platformio.ini` для всех отгрузочных окружений) пишет строки `[BW {json}]`
и читает такие же:

- плата → веб (через `gateway.py` → `POST /api/ingest`):
  - `{"t":"peer","mac":"AA:BB:..","name":"USB-A3B0","client":"bw <env>/<версия>","lang":"R"}` — сама плата при старте, каждые 30 с и на hello/ping; соседи по squad — каждые 5 с;
  - `{"t":"msg","from":"…","text":"…"}` — новые входящие mesh-сообщения (текст или расшифрованный canned);
  - `{"t":"emote","from":"AA:BB:..","emote":"WAVE"}` — эмоции (подглядка: на экране платы они тоже проигрываются);
  - `{"t":"detection","type":"FLOCK","mac":"…","rssi":-70,"vendor":"…"}` — свежая детекция из журнала.
- веб → плата (`POST /api/bridge/send` → шлюз кладёт `[BW {...}]` в Serial):
  - `{"t":"send","text":"…"}` / `{"t":"send","canned":N}` / `{"t":"send","emote":N}`.
- `{"t":"hello"}` / `{"t":"ping"}` от шлюза (старт, каждые 25 с): плата отвечает announce + snapshot (пиры, backlog inbox, текущая детекция).
- Когда плата заблокирована (PIN), мост молчит: разблокируйте на экране.

## Realtime

- `GET /api/stream` — Server-Sent Events. События: `peer` (новый/изменился/вернулся-ушёл — с `in_range`), `leave` (`{id}` — забыт), `message`, `emotion`, `detection`, `board` (`{online, board}`), `hello` (`{ok, version}`).
  Формат: `event: message\ndata: {...}\n\n`. Клиент: SSE primary, при обрыве — incremental poll (`since=`) каждые 2 с.

## Паритет с прошивкой (что сверено 1:1)

- Шаблоны `CANNED_RU[0..49]` + 6 табов = `src/meshwords.cpp` (48/49 — реакции, в пикер не входят, кнопки 👍/👎).
- Эмоции: 35 `MeshMsg::Emote`, имена = `EmoteScript::NAME[][0]`, RU-ярлыки = `NAME_RU`, табы = `TAB_NAME_RU` + `TAB` (`src/emote_script.cpp`).
- Цвета типов = `Theme::colorFor`, имена = `TypeNames::ru`; маскот LILGUY 80 мс; фоны — подмножество `Settings::Background`.
