# Веб-мост BroWatch (USB/Serial → :40400)

> Статус: MVP `browatch-web` работает (чат + пиры + эмоции + SSE).
> Репо: `/run/media/al/ARCHIVE/code/browatch-web` (`server.py`, `gateway.py`, `API.md`).

## Архитектура MVP

```
[плата CYD] --USB Serial 2M бод--> [gateway.py] --POST /api/ingest--> [server.py :40400] --SSE/REST--> [браузер/телефон]
```

- `server.py` — stdlib, без зависимостей. REST: `/api/peers`, `/api/messages`,
  `/api/emotions`, `/api/ingest`; realtime: SSE `/api/stream` + poll 2 с.
- `gateway.py` — парсит строки `[BW {json}]`, без платы — `--demo`.
- Формат кадров платы (план): `peer` / `msg` / `detection` / `emote` (см. `API.md`).

## Что дальше

1. `src/bw_bridge.cpp` в прошивке — отправка `[BW …]` по USB (пиры, сообщения, детекции).
2. Приём `[BW {"t":"send",…}]` с веба на плату.
3. WebSocket вместо SSE, история, фото пиров (аватар appearance).
4. Позже — WebBluetooth-вариант прямого сканирования (пока выбран шлюз).
