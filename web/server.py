#!/usr/bin/env python3
"""browatch-web: чат + presence + мост платы. Порт 40400. Только stdlib.

Паритет присутствия с платой: сквад платы — это кого слышно по эфиру
последние PEER_STALE_MS=12 с (BroWatch src/mesh.cpp). Здесь то же самое:
пир «в эфире», пока его кадры/heartbeat свежее IN_RANGE_S; «недавно был» —
пока свежее RECENT_S; старше — забываем совсем, как плата.
"""
import json
import os
import queue
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

VERSION = "0.2.3"
PORT = int(os.environ.get("BROWATCH_PORT", "40400"))
ROOT = os.path.dirname(os.path.abspath(__file__))
STATIC = os.path.join(ROOT, "static")

# --- presence, в секундах (паритет: плата держит пира 12 с без кадров) ---
IN_RANGE_S = 15     # «в эфире»: свежее 15 с (снапшот платы каждые 5 с + запас)
RECENT_S = 300      # «недавно был»: помним 5 минут, потом забываем
BOARD_STALE_S = 35  # плата онлайн: её announce идёт каждые 30 с + запас

store_lock = threading.Lock()
peers = {}        # id -> {id,name,rssi,client,lang,last_seen,first_seen,_pub_range}
messages = []     # [{id,from,text,ts,via}] via: web|board|mesh
emotions = []     # [{from,emote,ts,via}]
detections = []   # [{type,mac,rssi,vendor,ts}]
next_msg_id = [1]
subs = []         # list[queue.Queue]
subs_lock = threading.Lock()
outbox = []       # web -> board: [{"t":"send",...,"for":<board id>}], drained by gateway
board_cache = {"online": None, "board": None}
# Какую USB-плату использовать как мост, когда их несколько. None = авто
# (свежайшая). Выбор живёт в RAM: хост один, вкладки делят его.
selected_board = {"id": None}

# Safety net против повторных кадров моста: hello/reconnect шлюза
# пересылает бэклог inbox целиком, и без этого окна каждая такая пересылка
# плодила бы дубликаты в чате. Цена: одинаковый текст от того же отправителя
# чаще раза в MESH_DUPE_S глотается (только направление mesh, веб-чат не тронут).
MESH_DUPE_S = 45
recent_mesh = []  # [(ts, kind, key)] kind: msg|emote|detection


def mesh_dupe(kind, key):
    t = now()
    with store_lock:
        recent_mesh[:] = [(ts, k, ke) for (ts, k, ke) in recent_mesh if t - ts <= MESH_DUPE_S]
        for (_, k, ke) in recent_mesh:
            if k == kind and ke == key:
                return True
        recent_mesh.append((t, kind, key))
        recent_mesh[:] = recent_mesh[-200:]
        return False


# Санитизация текста веб -> эфир. Эфир — верхний Latin + цифры + ` .,?!'-`
# (MeshMsg::TEXT_CHARSET, 6 бит), поэтому строчные верхнятся, кириллица идёт
# транслитом, остальное (эмодзи и т.п.) дропается. Таблица — 1:1 с RU_TR
# в src/qwerty.cpp (порядок RU_GLYPH: ЙЦУКЕН/ФЫВАП/ЯЧСМИТЬ, Ё едет на Е):
# мост (bw_bridge.cpp) делает то же самое authoritatively через тот же
# transliterateRu; здесь — чтобы очередь уже лежала чистой, а пустое
# (смайлик-only) отклонялось 400, а не улетало молча в никуда.
RU_TR = ["J", "TS", "U", "K", "E", "N", "G", "SH", "SHCH", "Z", "H",
         "F", "Y", "V", "A", "P", "R", "O", "L", "D", "ZH", "E",
         "YA", "CH", "S", "M", "I", "T", "'", "B", "YU", "'"]
RU_IDX = {ch: i for i, ch in enumerate("ЙЦУКЕНГШЩЗХФЫВАПРОЛДЖЭЯЧСМИТЬБЮЪ")}
AIR_KEEP = set("0123456789 .,?!'-")


def to_air(text):
    out = []
    for ch in (text or ""):
        if "a" <= ch <= "z":
            out.append(chr(ord(ch) - 32))
        elif "A" <= ch <= "Z" or ch in AIR_KEEP:
            out.append(ch)
            continue
        else:
            u = ch.upper()
            if u == "Ё":
                out.append("E")
            elif u in RU_IDX:
                out.append(RU_TR[RU_IDX[u]])
    return "".join(out)[:48]


def now():
    return time.time()


def in_range(p, t=None):
    # Сама плата анонсируется раз в 30 с (мост OWN_EVERY_MS) — ей порог шире,
    # остальным хватает 15 с (снапшот сквада с платы идёт каждые 5 с).
    limit = BOARD_STALE_S if is_board(p) else IN_RANGE_S
    return (t or now()) - p.get("last_seen", 0) <= limit


def is_board(p):
    return (p.get("client") or "").startswith("bw ")


def pub_peer(p, t=None):
    """Публичная форма пира для API/SSE. outfit/shade — индексы общих таблиц
    (аутфит/очки с платы, для скинов в Логове); desk — настройки DESK MODE
    с платы (только у её announce), Логово зеркалит их, а не выдумывает."""
    t = t or now()
    return {"id": p["id"], "name": p.get("name") or p["id"],
            "nick": p.get("nick"),
            "outfit": p.get("outfit"), "shade": p.get("shade"),
            "desk": p.get("desk"), "usb": bool(p.get("usb")),
            "rssi": p.get("rssi"), "client": p.get("client"),
            "lang": p.get("lang"), "last_seen": p.get("last_seen", 0),
            "age_s": max(0, int(t - p.get("last_seen", t))),
            "in_range": in_range(p, t)}


def publish(kind, payload):
    with subs_lock:
        dead = []
        for q in subs:
            try:
                q.put_nowait((kind, payload))
            except Exception:
                dead.append(q)
        for q in dead:
            try:
                subs.remove(q)
            except ValueError:
                pass


def usb_boards_locked(t=None):
    """Все USB-платы (мост, 'usb':1 в own-announce), свежие первыми."""
    t = t or now()
    lst = [pub_peer(p, t) for p in peers.values()
           if p.get("usb") and t - p.get("last_seen", 0) <= BOARD_STALE_S]
    lst.sort(key=lambda p: -p["last_seen"])
    return lst


def board_snapshot_locked(t=None):
    """Плата-мост: выбранная вручную, если свежая, иначе свежайшая USB.
    USB-признак ('usb':1 в own-announce моста) всегда побеждает свежесть:
    сосед по эфиру с тем же клиентом — не та плата, через которую шлём."""
    t = t or now()
    cands = [p for p in peers.values()
             if is_board(p) and t - p.get("last_seen", 0) <= BOARD_STALE_S]
    if not cands:
        return None
    sel = selected_board["id"]
    if sel:
        for p in cands:
            if p["id"] == sel:
                return pub_peer(p, t)
    best = None
    for p in cands:
        if best is None:
            best = p
        elif p.get("usb") and not best.get("usb"):
            best = p
        elif bool(p.get("usb")) == bool(best.get("usb")) and p["last_seen"] > best["last_seen"]:
            best = p
    return pub_peer(best, t) if best else None


def _int01(v, lo, hi):
    try:
        n = int(v)
    except (TypeError, ValueError):
        return None
    if isinstance(v, bool) or not lo <= n <= hi:
        return None
    return n


def add_peer(pid, name=None, rssi=None, client=None, lang=None, nick=None,
             outfit=None, shade=None, desk=None, usb=None):
    t = now()
    with store_lock:
        p = peers.get(pid)
        if p is None:
            p = {"id": pid, "first_seen": t}
            peers[pid] = p
            created = True
        else:
            created = False
        p["last_seen"] = t
        if name is not None:
            p["name"] = name
        if nick is not None:
            p["nick"] = nick
        if rssi is not None:
            p["rssi"] = rssi
        if client is not None:
            p["client"] = client
        if lang is not None:
            p["lang"] = lang
        o = _int01(outfit, 0, 15)
        if o is not None:
            p["outfit"] = o
        s = _int01(shade, 0, 3)
        if s is not None:
            p["shade"] = s
        if isinstance(desk, dict) and desk:
            p["desk"] = {str(k)[:12]: desk[k] for k in list(desk)[:12]}
        if usb:
            p["usb"] = True
        rng = in_range(p, t)
        changed = created or p.get("_pub_range") != rng
        p["_pub_range"] = rng
        payload = pub_peer(p, t)
    if changed:
        publish("peer", payload)
    return payload


def touch_sender(frm):
    """Сообщение/эмоция доказывает, что отправителя слышно, но пир создаётся
    только кадром peer (как плата: сквад — это услышанные, не написавшие).
    Поэтому здесь лишь трогаем известный пир: по id, иначе по уникальному
    имени (имена в кадрах msg — ники, не MAC). Фантомов не создаём."""
    if not frm:
        return
    t = now()
    with store_lock:
        p = peers.get(frm)
        if p is None:
            same = [q for q in peers.values()
                    if (q.get("name") or "").lower() == str(frm).lower()]
            p = same[0] if len(same) == 1 else None
        if p is None:
            return
        p["last_seen"] = t
        rng = in_range(p, t)
        changed = p.get("_pub_range") != rng
        p["_pub_range"] = rng
        payload = pub_peer(p, t)
    if changed:
        publish("peer", payload)


def add_message(frm, text, via="web"):
    t = now()
    with store_lock:
        m = {"id": next_msg_id[0], "from": frm, "text": text, "ts": t, "via": via}
        next_msg_id[0] += 1
        messages.append(m)
        messages[:] = messages[-500:]
        payload = dict(m)
    touch_sender(frm)
    publish("message", payload)
    return payload


def add_emotion(frm, emote, via="web"):
    e = {"from": frm, "emote": emote, "ts": now(), "via": via}
    with store_lock:
        emotions.append(e)
        emotions[:] = emotions[-500:]
    touch_sender(frm)
    publish("emotion", dict(e))
    return e


def ingest(obj):
    """Универсальный вход шлюза. Возвращает (kind, payload)."""
    t = obj.get("t")
    if t == "send":
        # Кадр направления веб -> плата (очередь /api/bridge/send): в ingest
        # ему делать нечего, иначе его JSON-дамп засорит веб-чат как "gw".
        return "send", {"ok": False, "note": "web->board direction, use /api/bridge/send"}
    if t == "peer":
        pid = obj.get("mac") or obj.get("id") or "unknown"
        if not isinstance(pid, str) or len(pid) > 32:
            return "drop", {"ok": False, "note": "bad peer id"}
        return "peer", add_peer(pid, obj.get("name"), obj.get("rssi"),
                                 obj.get("client"), obj.get("lang"), obj.get("nick"),
                                 obj.get("outfit"), obj.get("shade"), obj.get("desk"),
                                 obj.get("usb"))
    if t == "readby":
        # Кто-то открыл наше последнее сообщение (мост отзеркалил квитанцию
        # KIND_READ для веб-тоста; плата тостит её сама). В чат не кладём.
        who = str(obj.get("who", "?"))[:32]
        d = {"who": who, "ts": now()}
        publish("readby", d)
        return "readby", d
    if t in ("msg", "message"):
        frm, text = obj.get("from", "unknown"), obj.get("text", "")
        if mesh_dupe("msg", (frm, text)):
            return "dupe", {"ok": False, "note": "mesh resend suppressed"}
        return "message", add_message(frm, text, via="mesh")
    if t == "emote":
        frm, em = obj.get("from", "unknown"), obj.get("emote", "?")
        if mesh_dupe("emote", (frm, em)):
            return "dupe", {"ok": False, "note": "mesh resend suppressed"}
        return "emotion", add_emotion(frm, em, via="mesh")
    if t == "detection":
        key = (obj.get("mac"), obj.get("type"))
        if mesh_dupe("detection", key):
            return "dupe", {"ok": False, "note": "mesh resend suppressed"}
        d = {"type": obj.get("type"), "mac": obj.get("mac"), "rssi": obj.get("rssi"),
             "vendor": obj.get("vendor"), "ts": now()}
        with store_lock:
            detections.append(d)
            detections[:] = detections[-500:]
        publish("detection", d)
        return "detection", d
    # Неизвестные кадры в чат не кладём: мост шлёт только 4 вида,
    # остальное — мусор, а не сообщения (раньше падало в чат как "gw").
    return "drop", {"ok": False, "note": "unknown frame kind: %s" % (t,)}


def sweep_once():
    """Переходы в эфире/из эфира + забывание старых + flip платы. Вызывать под sweeping."""
    t = now()
    with store_lock:
        for p in peers.values():
            rng = in_range(p, t)
            if p.get("_pub_range") != rng:
                p["_pub_range"] = rng
                publish("peer", pub_peer(p, t))
        gone = [pid for pid, p in peers.items()
                if t - p.get("last_seen", 0) > RECENT_S]
        for pid in gone:
            del peers[pid]
            publish("leave", {"id": pid})
        board = board_snapshot_locked(t)
        online = board is not None
    if board_cache["online"] != online:
        board_cache["online"] = online
        board_cache["board"] = board
        publish("board", {"online": online, "board": board})
    elif online:
        board_cache["board"] = board


def sweeper():
    while True:
        time.sleep(5)
        try:
            sweep_once()
        except Exception:
            pass


MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8", ".json": "application/json",
        ".webmanifest": "application/manifest+json",
        ".svg": "image/svg+xml", ".png": "image/png"}


class H(BaseHTTPRequestHandler):
    server_version = "BroWatchWeb/" + VERSION

    def log_message(self, *a):
        pass

    def _send(self, code, body, ctype="application/json", extra=None):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def _json(self, code, obj):
        self._send(code, json.dumps(obj, ensure_ascii=False))

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        u = urlparse(self.path)
        path = u.path
        q = parse_qs(u.query)
        if path == "/":
            return self._serve_file("index.html")
        if path == "/sw.js":
            return self._serve_file("sw.js", extra={"Service-Worker-Allowed": "/"})
        if path == "/manifest.webmanifest":
            return self._serve_file("manifest.webmanifest")
        if path.startswith("/static/"):
            return self._serve_file(path[len("/static/"):])
        if path == "/api/health":
            with store_lock:
                t = now()
                recent = [p for p in peers.values()
                          if t - p.get("last_seen", 0) <= RECENT_S]
                board = board_snapshot_locked(t)
                return self._json(200, {"ok": True, "version": VERSION, "port": PORT,
                                        "peers": len(recent),
                                        "in_range": sum(1 for p in recent if in_range(p, t)),
                                        "messages": len(messages),
                                        "board_online": board is not None,
                                        "board": board,
                                        "usb_boards": usb_boards_locked(t),
                                        "selected_board": selected_board["id"]})
        if path == "/api/peers":
            with store_lock:
                t = now()
                lst = sorted((pub_peer(p, t) for p in peers.values()
                              if t - p.get("last_seen", 0) <= RECENT_S),
                             key=lambda p: (not p["in_range"], -p["last_seen"]))
            return self._json(200, lst)
        if path == "/api/squad":
            with store_lock:
                t = now()
                # Кадры msg несут ник, а не MAC: цепляем и по имени.
                name_to_id = {}
                for p in peers.values():
                    nm = (p.get("name") or "").lower()
                    if nm and nm not in name_to_id:
                        name_to_id[nm] = p["id"]
                last_by_peer = {}
                for m in reversed(messages):
                    if m["from"] not in last_by_peer:
                        last_by_peer[m["from"]] = m
                    pid = name_to_id.get(str(m["from"]).lower())
                    if pid:
                        last_by_peer.setdefault(pid, m)
                lst = []
                lower = {str(k).lower(): v for k, v in last_by_peer.items()}
                for p in peers.values():
                    if t - p.get("last_seen", 0) > RECENT_S:
                        continue
                    d = pub_peer(p, t)
                    d["last_msg"] = (last_by_peer.get(p["id"])
                                     or lower.get(str(p.get("nick") or "").lower())
                                     or lower.get(str(p.get("name") or "").lower()))
                    # Как на плате: сначала кто здесь, потом остальные по свежести.
                    lst.append(d)
                lst.sort(key=lambda p: (not p["in_range"], -p["last_seen"]))
            return self._json(200, lst)
        if path == "/api/messages":
            since = int(q.get("since", ["0"])[0] or 0)
            with store_lock:
                lst = [m for m in messages if m["id"] > since]
            return self._json(200, lst)
        if path == "/api/emotions":
            since = float(q.get("since", ["0"])[0] or 0)
            with store_lock:
                lst = [e for e in emotions if e["ts"] > since][-100:]
            return self._json(200, lst)
        if path == "/api/detections":
            since = float(q.get("since", ["0"])[0] or 0)
            with store_lock:
                lst = [d for d in detections if d["ts"] > since][-100:]
            return self._json(200, lst)
        if path == "/api/bridge/outbox":
            # Шлюз забирает только очередь СВОЕЙ платы (?board=<mac> из её
            # announce): при двух платах на USB чужие кадры остаются лежать.
            # Без board — всё безадресное (совместимость).
            want = (q.get("board", [""])[0] or "")[:32]
            with store_lock:
                mine = [it for it in outbox
                        if not it.get("for") or it.get("for") == want]
                mine_ids = set(map(id, mine))
                outbox[:] = [it for it in outbox if id(it) not in mine_ids]
            return self._json(200, mine)
        if path == "/api/stream":
            return self._sse()
        return self._json(404, {"error": "not found"})

    def _serve_file(self, name, extra=None):
        safe = os.path.normpath(name).lstrip("/")
        fp = os.path.join(STATIC, safe)
        if not fp.startswith(STATIC) or not os.path.isfile(fp):
            return self._json(404, {"error": "not found"})
        ext = os.path.splitext(fp)[1].lower()
        ctype = MIME.get(ext, "application/octet-stream")
        with open(fp, "rb") as f:
            data = f.read()
        self._send(200, data, ctype, extra)

    def _sse(self):
        q = queue.Queue()
        with subs_lock:
            subs.append(q)
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        try:
            hello = json.dumps({"ok": True, "version": VERSION}, ensure_ascii=False)
            self.wfile.write(("event: hello\ndata: %s\n\n" % hello).encode("utf-8"))
            self.wfile.flush()
            while True:
                try:
                    kind, payload = q.get(timeout=25)
                    line = "event: %s\ndata: %s\n\n" % (kind, json.dumps(payload, ensure_ascii=False))
                    self.wfile.write(line.encode("utf-8"))
                    self.wfile.flush()
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            with subs_lock:
                try:
                    subs.remove(q)
                except ValueError:
                    pass

    def do_POST(self):
        u = urlparse(self.path)
        path = u.path
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = 0
        raw = self.rfile.read(n) if n > 0 else b"{}"
        try:
            obj = json.loads(raw.decode("utf-8") or "{}")
        except Exception:
            return self._json(400, {"error": "bad json"})
        if path == "/api/messages":
            frm, text = obj.get("from", "web"), obj.get("text", "")
            if not text or not str(text).strip():
                return self._json(400, {"error": "empty text"})
            return self._json(200, add_message(str(frm)[:24], str(text)[:500], via="web"))
        if path == "/api/peers":
            # Веб-персоны нет: веб говорит только устами платы (см. /api/bridge/send).
            # Heartbeat-эндпоинт закрыт, чтобы веб не плодил членов сквада.
            return self._json(410, {"error": "gone: web has no persona, speak as the board"})
        if path == "/api/emotions":
            return self._json(200, add_emotion(str(obj.get("from", "web"))[:24],
                                               str(obj.get("emote", "?"))[:24], via="web"))
        if path == "/api/ingest":
            kind, payload = ingest(obj)
            ok = kind in ("peer", "message", "emotion", "detection", "readby")
            return self._json(200, {"ok": ok, "kind": kind, "data": payload})
        if path == "/api/bridge/send":
            # Веб -> плата (через gateway.py в Serial): текст, canned-индекс
            # или emote-индекс MeshMsg::Emote. text — подпись для локального
            # чата (шаблон/эмоция уже расшифрованы клиентом); в очередь плате
            # уходит только {t:send,...}. Сообщение сразу видно в вебе
            # с пометкой «в эфир»: плата свои отправки в inbox не кладёт.
            #
            # Персона одна — персона платы (nick из её announce, иначе имя
            # устройства). Поле from от клиента игнорируется: веб не заводит
            # своих персонажей. Без платы в эфире — 503, молчание честнее
            # выдуманного собеседника.
            with store_lock:
                b = board_snapshot_locked()
                persona = ((b.get("nick") or b.get("name")) if b else None)
            if not persona:
                return self._json(503, {"error": "board offline"})
            text = str(obj.get("text", "")).strip()
            if not text:
                return self._json(400, {"error": "text required (display label)"})
            fwd = {"t": "send"}
            if "canned" in obj:
                try:
                    fwd["canned"] = int(obj["canned"])
                except (TypeError, ValueError):
                    return self._json(400, {"error": "bad canned index"})
                if not 0 <= fwd["canned"] <= 49:
                    return self._json(400, {"error": "canned index 0..49"})
            elif "emote" in obj:
                try:
                    fwd["emote"] = int(obj["emote"])
                except (TypeError, ValueError):
                    return self._json(400, {"error": "bad emote index"})
                if not 0 <= fwd["emote"] <= 34:
                    return self._json(400, {"error": "emote index 0..34"})
            else:
                air = to_air(text)
                if not air:
                    return self._json(400, {"error": "nothing the air can carry"})
                fwd["text"] = air
            with store_lock:
                fwd["for"] = b["id"]  # плате-мосту, не кому попало по USB
                outbox.append(fwd)
                outbox[:] = outbox[-50:]
            m = add_message(persona, text[:500], via="board")
            return self._json(200, {"ok": True, "queued": fwd, "message": m,
                                    "persona": persona})
        if path == "/api/bridge/read":
            # Веб тапнул письмо в Логове: плате [BW {"t":"read"}] — та же
            # квитанция KIND_READ, что при тапе по письму на устройстве.
            with store_lock:
                b = board_snapshot_locked()
            if not b:
                return self._json(503, {"error": "board offline"})
            with store_lock:
                outbox.append({"t": "read", "for": b["id"]})
                outbox[:] = outbox[-50:]
            return self._json(200, {"ok": True})
        if path == "/api/bridge/react":
            # Лайк/дизлайк письму в Логове: плате [BW {"t":"react",...}] —
            # обычный canned 48/49 в эфир + markRead, как кнопки на письме
            # устройства (адресации в эфире нет, увидят все с той же фразой).
            kind = str(obj.get("kind", "")).lower()
            if kind not in ("like", "dislike"):
                return self._json(400, {"error": "kind must be like|dislike"})
            with store_lock:
                b = board_snapshot_locked()
            if not b:
                return self._json(503, {"error": "board offline"})
            with store_lock:
                outbox.append({"t": "react", "kind": kind, "for": b["id"]})
                outbox[:] = outbox[-50:]
            return self._json(200, {"ok": True, "kind": kind})
        if path == "/api/bridge/select":
            # Выбор платы-моста при нескольких USB. Пустой id = авто.
            want = str(obj.get("id", "") or "")[:32]
            with store_lock:
                if want:
                    ok_ids = [p["id"] for p in peers.values()
                              if p.get("usb") and now() - p.get("last_seen", 0) <= BOARD_STALE_S]
                    if want not in ok_ids:
                        return self._json(404, {"error": "no such usb board online"})
                selected_board["id"] = want or None
                b = board_snapshot_locked()
            publish("board", {"online": b is not None, "board": b})
            return self._json(200, {"ok": True, "selected": selected_board["id"],
                                    "board": b})
        return self._json(404, {"error": "not found"})


def main():
    threading.Thread(target=sweeper, daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), H)
    print("browatch-web %s on http://localhost:%d (BROWATCH_PORT to override)"
          % (VERSION, PORT), flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
