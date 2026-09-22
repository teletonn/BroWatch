# Веб-мост BroWatch (USB/Serial → :40400)

> Статус: v0.2.0 — полный паритет с прошивкой, проверено на железе 2026-09-22.
> Пакет живёт в этом репо: `web/` (`server.py` + `gateway.py` + `static/` + `API.md`
> + `systemd/` + `tools/`). Бывший отдельный checkout
> `/run/media/al/ARCHIVE/code/browatch-web` оставлен как есть, источник правды — `web/`.

## Архитектура

```
[плата CYD] --USB Serial 2M бод--> [gateway.py] --POST /api/ingest--> [server.py :40400] --SSE/REST--> [браузер/телефон]
[плата CYD] <--USB Serial 2M бод-- [gateway.py] <--GET /api/bridge/outbox-- [server.py :40400] <--POST /api/bridge/send-- [кнопка «→плата»]
```

- Прошивка: `src/bw_bridge.cpp` + `include/bw_bridge.h` (только с `-DBW_BRIDGE=1`;
  флаг уже стоит в `platformio.ini` для `cyd`, `cyd-ili9341`, `cyd-fast`,
  `cyd-ili9341-fast`, `rlphantom`, `rlphantom-r`, `awok`; `cyd35` заморожен — без моста).
  Радиотракт не тронут: мост только читает состояние и шлёт в эфир как экран.
- `server.py` — stdlib, без зависимостей. REST: `/api/peers` (heartbeat веба),
  `/api/messages?since=`, `/api/emotions?since=`, `/api/detections?since=`,
  `/api/squad` (presence + last_msg), `/api/health`, `/api/ingest`,
  `/api/bridge/send` (очередь + локальное эхо `via: board`) + `/api/bridge/outbox`;
  realtime: SSE `/api/stream` (`peer`/`leave`/`message`/`emotion`/`detection`/`board`/`hello`) + incremental poll-fallback.
- `gateway.py` — парсит строки `[BW {json}]`, пишет очередь в Serial, reconnect
  с backoff при обрыве USB, ping платы каждые 25 с, дефолт `127.0.0.1`
  (не `localhost` — тот резолвится в `::1`); без платы — `--demo`.
- Формат кадров платы: `peer` / `msg` / `detection` / `emote` туда;
  `send` (text/canned/emote) / `hello` / `ping` обратно (см. `web/API.md` + шапку `bw_bridge.h`).

## Presence: как на плате

Плата держит пира 12 с без кадров (`PEER_STALE_MS`, `src/mesh.cpp`).
Сервер зеркалит: «в эфире» ≤15 с (снапшот сквада идёт каждые 5 с + запас;
announce самой платы — каждые 30 с, для неё порог 35 с), «недавно были» ≤5 мин,
дальше пир забывается. Пиры привозит только шлюз; `POST /api/peers` —
heartbeat веба (`web:*`, каждые 10 с); сообщения по нику цепляются
к известному пиру, а не создают фантомов. Сортировка — как на плате:
сначала кто здесь.

## Что зеркалится в веб (полный UI мостового режима)

- Сама плата (`USB-XXXX`, env/версия/язык) + сквад рядом (снапшот каждые 5 с).
- Входящие mesh-сообщения (canned расшифровываются на плате), эмоции 1:1
  (35 штук, 6 табов, RU-ярлыки, анимация маскота), свежие детекции из журнала.
- Обратно: текст/шаблон (50 строк, 6 табов)/эмоция/реакции (canned 48/49)
  из веба уходят в mesh-эфир с платы (нужны фраза и TRANSMIT) и сразу
  показываются в чате с пометкой «в эфир».
- Не зеркалится (локально на плате): часы DESK, бинго-карта, дневник, настройки,
  ростер/счётчики встреч (нужен новый кадр моста — следующим этапом).

## Проверка (пройдена 2026-09-22, плата 2 по USB + плата 1 по эфиру)

- [x] `web/smoke.py`: health, heartbeat-gate (чужие пиры отклоняются), presence,
  bridge/send + drain, drop неизвестных кадров, SSE hello + событие.
- [x] Живьём: сквад `USB-1388` + `SHADOW` оба `in_range`, `last_msg` SHADOW
  («Понравилось.») прицеплен по нику без фантома; `bridge/send` canned 12
  записан шлюзом в Serial (`<- {'t': 'send', 'canned': 12, …}` в логе).
- [x] Статика/PWA: `/`, `/sw.js`, `/manifest.webmanifest`, иконки — 200
  с верными MIME; таблицы CANNED/EMOTE сверены скриптом с `meshwords.cpp`
  и `emote_script.cpp` 1:1.
- [ ] Два браузера + тап вживую на плате — за пользователем
  (http://127.0.0.1:40400, кнопка «Я тут», таб Отряд).

## Дубликаты каждые 25 с (найдено и вылечено 2026-09-22)

Симптом: сообщения в чате двоились/троились, детекции и тосты повторялись.
В журнале шлюза — `msg`-кадр раз в ~25 с. Цепочка: `gateway.py` слал
`[BW {"t":"ping"}]` каждые 25 с → прошивка считала `ping == hello` и сбрасывала
курсоры (`s_lastMsgAt = 0` в `BwBridge::onLine`) → весь бэклог inbox уходил
в веб заново → сервер создавал новые строки. Лечение в двух местах:

1. Прошивка: `ping` — keepalive (announce + snapshot, без перемотки),
   `hello` — полный ресинк (см. `include/bw_bridge.h`). Плата 2 перепрошита.
2. Сервер: safety-net — одинаковые mesh-кадры чаще раза в 45 с глотаются
   (`kind: "dupe"`, бэклог после reconnect шлюза не двоится).

## Деплой на хосте

`bash web/tools/web-install.sh`: `~/.local/share/browatch-web` + user-юниты
(`browatch-web`, `browatch-gateway@ttyUSB0`) + linger + udev-автостарт шлюза
при подключении CH340. Детали — `web/systemd/README.md`.

## Что дальше

1. WebSocket вместо SSE, история между рестартами (сейчас store в RAM).
2. Кадр `roster` в мосте (прошивка) — счётчики встреч в веб-отряд.
3. Позже — WebBluetooth-вариант прямого сканирования (пока выбран шлюз).
