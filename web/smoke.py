#!/usr/bin/env python3
"""browatch-web smoke: HTTP-уровень против живого server.py. Только stdlib.
Запуск: python3 smoke.py  (поднимает сервер на :40499 сам)."""
import json
import queue
import subprocess
import sys
import threading
import time
import urllib.request
import urllib.error

PORT = 40499
BASE = "http://127.0.0.1:%d" % PORT


def call(method, path, obj=None):
    data = json.dumps(obj).encode() if obj is not None else None
    req = urllib.request.Request(BASE + path, data=data,
                                 headers={"Content-Type": "application/json"},
                                 method=method)
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status, json.loads(r.read().decode() or "null")


def get(p):
    return call("GET", p)[1]


def post(p, o):
    return call("POST", p, o)[1]


def main():
    srv = subprocess.Popen([sys.executable, "server.py"],
                           env={"PATH": "/usr/bin:/bin", "BROWATCH_PORT": str(PORT)},
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            try:
                h = get("/api/health")
                break
            except Exception:
                time.sleep(0.2)
        else:
            raise SystemExit("server did not start")
        assert h["ok"] and h["version"], h
        print("health", h["version"], "OK")

        # веб-персоны нет: heartbeat закрыт, чужие пиры отклонялись бы и раньше
        try:
            call("POST", "/api/peers", {"id": "web:test", "name": "test", "client": "web"})
            raise SystemExit("web persona accepted!")
        except urllib.error.HTTPError as e:
            assert e.code == 410, e.code
        print("no-web-persona gate OK")

        # без платы в эфире отправка невозможна (свежий сервер, пиров нет)
        try:
            call("POST", "/api/bridge/send", {"canned": 12, "text": "x"})
            raise SystemExit("boardless send accepted!")
        except urllib.error.HTTPError as e:
            assert e.code == 503, e.code
        print("boardless 503 OK")
        for p, o in (("/api/bridge/read", {}), ("/api/bridge/react", {"kind": "like"})):
            try:
                call("POST", p, o)
                raise SystemExit("boardless %s accepted!" % p)
            except urllib.error.HTTPError as e:
                assert e.code == 503, (p, e.code)
        print("boardless read/react 503 OK")

        # мост: announce платы + сквад (со скинами и настройками DESK для Логова)
        desk = {"squad": 1, "crowd": 4, "visit": 0,
                "clk": 1, "clkfont": 0, "clkbg": 0, "bg": 1}
        post("/api/ingest", {"t": "peer", "mac": "AA:BB:CC:DD:EE:FF",
                             "name": "USB-FFFF", "nick": "Tester",
                             "client": "bw rlphantom/v", "lang": "R", "usb": 1,
                             "outfit": 7, "shade": 2, "desk": desk})
        post("/api/ingest", {"t": "peer", "mac": "11:22:33:44:55:66",
                             "name": "SHADOW", "client": "bw",
                             "outfit": 4, "shade": 1})
        # сосед по эфиру с полным client, но без usb-флага и свежее платы —
        # платой всё равно считается та, что на USB
        post("/api/ingest", {"t": "peer", "mac": "77:88:99:AA:BB:CC",
                             "name": "NEIGHBOUR", "client": "bw cyd-ili9341/v",
                             "outfit": 1, "shade": 0})
        # мусор вместо скинов отбрасывается, валидное остаётся
        post("/api/ingest", {"t": "peer", "mac": "11:22:33:44:55:66",
                             "outfit": 99, "shade": -1, "desk": [1, 2]})
        sq = get("/api/squad")
        by_id = {p["id"]: p for p in sq}
        assert by_id["AA:BB:CC:DD:EE:FF"]["in_range"] is True, sq
        assert by_id["11:22:33:44:55:66"]["in_range"] is True, sq
        b = by_id["AA:BB:CC:DD:EE:FF"]
        assert (b["outfit"], b["shade"]) == (7, 2) and b["desk"] == desk, b
        m = by_id["11:22:33:44:55:66"]
        assert (m["outfit"], m["shade"]) == (4, 1) and m.get("desk") is None, m
        h = get("/api/health")
        assert h["board_online"] is True and h["board"]["id"] == "AA:BB:CC:DD:EE:FF", h
        assert h["board"]["desk"] == desk, h
        assert h["board"]["usb"] is True, h
        print("presence + board + den data OK")

        # прочитано и реакции уходят в очередь мосту
        r = post("/api/bridge/read", {})
        assert r == {"ok": True}, r
        assert get("/api/bridge/outbox?board=AA:BB:CC:DD:EE:FF") == [{"t": "read", "for": "AA:BB:CC:DD:EE:FF"}], "read outbox"
        r = post("/api/bridge/react", {"kind": "like"})
        assert r == {"ok": True, "kind": "like"}, r
        r = post("/api/bridge/react", {"kind": "dislike"})
        assert r == {"ok": True, "kind": "dislike"}, r
        assert get("/api/bridge/outbox?board=AA:BB:CC:DD:EE:FF") == [{"t": "react", "kind": "like", "for": "AA:BB:CC:DD:EE:FF"},
                                             {"t": "react", "kind": "dislike", "for": "AA:BB:CC:DD:EE:FF"}], "react outbox"
        assert get("/api/bridge/outbox?board=AA:BB:CC:DD:EE:FF") == [], "outbox must drain"
        try:
            call("POST", "/api/bridge/react", {"kind": "haha"})
            raise SystemExit("bad react accepted!")
        except urllib.error.HTTPError as e:
            assert e.code == 400, e.code
        # квитанция прочитано: в чат не падает, событие ок
        before = get("/api/messages?since=0")
        rr = post("/api/ingest", {"t": "readby", "who": "SHADOW"})
        assert rr["ok"] is True and rr["kind"] == "readby", rr
        assert len(get("/api/messages?since=0")) == len(before), "readby in chat!"
        print("read/react/readby OK")

        # сообщения — только от лица платы
        r = post("/api/bridge/send", {"canned": 12, "text": "Тут камера Флок."})
        assert r["ok"] and r["queued"] == {"t": "send", "canned": 12, "for": "AA:BB:CC:DD:EE:FF"}, r
        assert r["persona"] == "Tester" and r["message"]["via"] == "board", r
        assert r["message"]["from"] == "Tester", r
        out = get("/api/bridge/outbox?board=AA:BB:CC:DD:EE:FF")
        assert out == [{"t": "send", "canned": 12, "for": "AA:BB:CC:DD:EE:FF"}], out
        assert get("/api/bridge/outbox?board=AA:BB:CC:DD:EE:FF") == [], "outbox must drain"
        # кастомный текст: строчные верхнятся, русский — транслитом в эфир
        r = post("/api/bridge/send", {"text": "привет Ща!"})
        assert r["queued"] == {"t": "send", "text": "PRIVET SHCHA!", "for": "AA:BB:CC:DD:EE:FF"}, r
        assert get("/api/bridge/outbox?board=AA:BB:CC:DD:EE:FF") == [{"t": "send", "text": "PRIVET SHCHA!", "for": "AA:BB:CC:DD:EE:FF"}]
        assert get("/api/bridge/outbox?board=AA:BB:CC:DD:EE:FF") == [], "outbox must drain"
        # нечего нести (эмодзи-only) — честные 400, а не молча в никуда
        try:
            call("POST", "/api/bridge/send", {"text": "😀😀"})
            raise SystemExit("unsendable accepted!")
        except urllib.error.HTTPError as e:
            assert e.code == 400, e.code
        print("send sanitize OK")
        try:
            call("POST", "/api/bridge/send", {"from": "x", "canned": 99, "text": "z"})
            raise SystemExit("bad canned accepted!")
        except urllib.error.HTTPError as e:
            assert e.code == 400, e.code
        # входящий кадр платы неизвестного вида в чат не падает
        before = get("/api/messages?since=0")
        post("/api/ingest", {"t": "weird", "x": 1})
        after = get("/api/messages?since=0")
        assert len(before) == len(after), (before, after)
        post("/api/ingest", {"t": "msg", "from": "SHADOW", "text": "привет"})
        post("/api/ingest", {"t": "emote", "from": "11:22:33:44:55:66", "emote": "WAVE"})
        post("/api/ingest", {"t": "detection", "type": "FLOCK", "mac": "AA:1", "rssi": -70})
        msgs = get("/api/messages?since=0")
        assert any(m["via"] == "mesh" and m["text"] == "привет" for m in msgs), msgs
        assert get("/api/emotions?since=0")[-1]["emote"] == "WAVE"
        assert get("/api/detections?since=0")[-1]["type"] == "FLOCK"
        print("bridge + ingest OK")

        # выбор платы-моста: вторая USB-плата, ручной выбор, чужой drain
        post("/api/ingest", {"t": "peer", "mac": "BB:BB:BB:BB:BB:BB",
                             "name": "USB-BBBB", "nick": "Second",
                             "client": "bw cyd-ili9341/v", "lang": "R", "usb": 1,
                             "outfit": 2, "shade": 1, "desk": desk})
        h = get("/api/health")
        assert len(h["usb_boards"]) == 2 and h["selected_board"] is None, h
        # по умолчанию — свежайшая (вторая)
        assert h["board"]["id"] == "BB:BB:BB:BB:BB:BB", h
        r = post("/api/bridge/select", {"id": "AA:BB:CC:DD:EE:FF"})
        assert r["selected"] == "AA:BB:CC:DD:EE:FF" and r["board"]["id"] == "AA:BB:CC:DD:EE:FF", r
        assert get("/api/health")["selected_board"] == "AA:BB:CC:DD:EE:FF"
        try:
            call("POST", "/api/bridge/select", {"id": "00:00:00:00:00:00"})
            raise SystemExit("bad select accepted!")
        except urllib.error.HTTPError as e:
            assert e.code == 404, e.code
        # очередь едет выбранной; чужой шлюз её не забирает
        post("/api/bridge/send", {"canned": 5, "text": "x"})
        assert get("/api/bridge/outbox?board=BB:BB:BB:BB:BB:BB") == [], "foreign drain!"
        mine = get("/api/bridge/outbox?board=AA:BB:CC:DD:EE:FF")
        assert mine == [{"t": "send", "canned": 5, "for": "AA:BB:CC:DD:EE:FF"}], mine
        assert get("/api/bridge/outbox?board=AA:BB:CC:DD:EE:FF") == [], "must drain"
        # назад на авто
        r = post("/api/bridge/select", {"id": ""})
        assert r["selected"] is None, r
        print("board select + routed outbox OK")
        seen = {}
        def sse():
            req = urllib.request.Request(BASE + "/api/stream")
            with urllib.request.urlopen(req, timeout=10) as r:
                buf = ""
                while True:
                    chunk = r.read(1).decode()
                    if not chunk:
                        return
                    buf += chunk
                    while "\n\n" in buf:
                        frame, buf = buf.split("\n\n", 1)
                        kind = None
                        data = ""
                        for line in frame.split("\n"):
                            if line.startswith("event:"):
                                kind = line[6:].strip()
                            elif line.startswith("data:"):
                                data += line[5:].strip()
                        if kind:
                            seen[kind] = data
                            if "message" in seen and "hello" in seen:
                                return
        th = threading.Thread(target=sse, daemon=True)
        th.start()
        time.sleep(0.5)
        post("/api/messages", {"from": "web:test", "text": "sse-ping"})
        th.join(timeout=8)
        assert "hello" in seen and "message" in seen, seen
        assert json.loads(seen["message"])["text"] == "sse-ping"
        print("SSE OK")
        print("SMOKE GREEN")
    finally:
        srv.terminate()


if __name__ == "__main__":
    main()
