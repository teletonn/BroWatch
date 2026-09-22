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

        # мост: announce платы + сквад
        post("/api/ingest", {"t": "peer", "mac": "AA:BB:CC:DD:EE:FF",
                             "name": "USB-FFFF", "nick": "Tester",
                             "client": "bw rlphantom/v", "lang": "R"})
        post("/api/ingest", {"t": "peer", "mac": "11:22:33:44:55:66",
                             "name": "SHADOW", "client": "bw"})
        sq = get("/api/squad")
        by_id = {p["id"]: p for p in sq}
        assert by_id["AA:BB:CC:DD:EE:FF"]["in_range"] is True, sq
        assert by_id["11:22:33:44:55:66"]["in_range"] is True, sq
        h = get("/api/health")
        assert h["board_online"] is True and h["board"]["id"] == "AA:BB:CC:DD:EE:FF", h
        print("presence + board OK")

        # сообщения — только от лица платы
        r = post("/api/bridge/send", {"canned": 12, "text": "Тут камера Флок."})
        assert r["ok"] and r["queued"] == {"t": "send", "canned": 12}, r
        assert r["persona"] == "Tester" and r["message"]["via"] == "board", r
        assert r["message"]["from"] == "Tester", r
        out = get("/api/bridge/outbox")
        assert out == [{"t": "send", "canned": 12}], out
        assert get("/api/bridge/outbox") == [], "outbox must drain"
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

        # SSE: hello + живое событие
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
