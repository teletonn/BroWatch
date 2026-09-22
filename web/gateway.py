#!/usr/bin/env python3
"""browatch-web gateway: USB-Serial платы -> POST /api/ingest.

Понимает строки вида:  [BW {"t":"peer","mac":"…",…}]
Без платы: --demo генерирует пиров и сообщения.
"""
import argparse
import json
import random
import time
import urllib.request

API = "http://127.0.0.1:40400/api/ingest"   # 127.0.0.1, не localhost: тот резолвится в ::1 и падает

# Сколько подряд ошибок чтения терпим до переоткрытия порта.
# После выдергивания кабеля readline() падает на мёртвом хэндле вечно —
# раньше pump() глотал это (sleep+continue), и шлюз никогда не оживал
# даже если плата вернулась. Теперь рвём наружу: serial_loop переоткроет.
READ_ERR_LIMIT = 5


def post(obj, api=None):
    api = api or API
    req = urllib.request.Request(api, data=json.dumps(obj).encode("utf-8"),
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.read().decode("utf-8")
    except Exception as e:
        print("ingest failed:", e)
        return None


def parse_line(line):
    line = line.strip()
    if "[BW" not in line:
        return None
    try:
        js = line[line.index("[BW") + 3:].strip().rstrip("]")
        return json.loads(js)
    except Exception:
        return None


def outbox_url(api=None, board=""):
    api = api or API
    base = api.rsplit("/api/ingest", 1)[0] if "/api/ingest" in api else api
    url = base + "/api/bridge/outbox"
    return url + ("?board=%s" % board if board else "")


def fetch_outbox(api=None, board=""):
    try:
        with urllib.request.urlopen(outbox_url(api, board), timeout=5) as r:
            return json.loads(r.read().decode("utf-8") or "[]")
    except Exception:
        return []


def resolve_port(port):
    """Configured port wins, always. An earlier version grabbed "the
    first live USB-serial" when the configured node was gone
    (ttyUSB0 -> ttyUSB1 renames) -- but a pty test proved it hijacks
    somebody else's device instead: after a port death it opened the
    real board at the test baud and started helloing it. Renames are
    what the udev rule's per-port instances are for
    (browatch-gateway@ttyUSB1); a missing node just fails open and the
    backoff below retries it until the board is back."""
    return port


def serial_loop(port, baud):
    try:
        import serial
    except ImportError:
        print("pyserial нет (pip install pyserial). Выход.")
        return
    backoff = 2
    while True:
        want = resolve_port(port)
        try:
            ser = serial.Serial(want, baud, timeout=1)
        except Exception as e:
            print("serial open %s failed: %s; retry in %d s" % (want, e, backoff), flush=True)
            time.sleep(backoff)
            backoff = min(backoff * 2, 60)
            continue
        backoff = 2
        try:
            pump(ser)
        except Exception as e:
            print("serial error:", e, flush=True)
        try:
            ser.close()
        except Exception:
            pass
        print("serial closed; reconnect in %d s" % backoff, flush=True)
        time.sleep(backoff)
        backoff = min(backoff * 2, 60)


def pump(ser):
    print("gateway: читаю %s @ %d" % (ser.port, ser.baudrate), flush=True)
    try:
        ser.reset_input_buffer()  # после переподключения — рваные строки
    except Exception:
        pass
    # эхо-хеллоу плате (мост bw_bridge ответит announce + snapshot):
    # это же ресинк после каждого переоткрытия — сервер сразу видит плату.
    ser.write(b"[BW {\"t\":\"hello\",\"from\":\"web\"}]\n")
    last_poll = 0
    last_ping = time.time()
    read_errs = 0
    my_board = ""  # MAC своей платы (её own-announce с "usb":1): очередь
    # моста адресована платам, чужое не забираем, когда плат несколько.
    while True:
        try:
            raw = ser.readline().decode("utf-8", "replace")
        except Exception as e:
            read_errs += 1
            print("serial read error (%d/%d): %s" % (read_errs, READ_ERR_LIMIT, e), flush=True)
            if read_errs >= READ_ERR_LIMIT:
                raise RuntimeError("serial dead, reopen")
            time.sleep(1)
            continue
        read_errs = 0
        if raw.strip():
            obj = parse_line(raw)
            if obj is not None:
                print("->", obj, flush=True)
                if obj.get("usb") and obj.get("mac"):
                    my_board = str(obj["mac"])[:32]
                post(obj)
        # Веб -> плата: забираем очередь и кладём [BW {...}] в Serial.
        if time.time() - last_poll > 2:
            last_poll = time.time()
            for item in fetch_outbox(board=my_board):
                # Компактно: serial-лайн платы ограничен, каждый байт на счету.
                line = "[BW %s]\n" % json.dumps(item, ensure_ascii=False,
                                                separators=(",", ":"))
                print("<-", item, flush=True)
                try:
                    ser.write(line.encode("utf-8"))
                except Exception as e:
                    print("serial write error:", e, flush=True)
                    raise
        # Периодический ping: плата отвечает announce + snapshot,
        # веб-присутствие не рвётся, даже если анонс потерялся.
        if time.time() - last_ping > 25:
            last_ping = time.time()
            try:
                ser.write(b"[BW {\"t\":\"ping\",\"from\":\"web\"}]\n")
            except Exception as e:
                print("serial ping error:", e, flush=True)
                raise


def demo_loop():
    names = ["Bro-1", "Bro-2", "Phone-Pixel"]
    looks = {"Bro-1": (4, 1), "Bro-2": (13, 2), "Phone-Pixel": (2, 3)}
    print("gateway: DEMO-режим (без железа)", flush=True)
    # Демо-плата: announce как у моста, с ником, скином и настройками DESK —
    # Логово в демо выглядит как с живой платой.
    post({"t": "peer", "mac": "DE:AD:BE:EF:00:01", "name": "USB-0001",
          "nick": "ДЕМО", "client": "bw demo/0", "lang": "R", "usb": 1,
          "outfit": 7, "shade": 0,
          "desk": {"squad": 1, "crowd": 4, "visit": 0,
                   "clk": 1, "clkfont": 0, "clkbg": 0, "bg": 1}})
    for n in names:
        o, s = looks[n]
        post({"t": "peer", "mac": "demo:%s" % n, "name": n,
              "rssi": random.randint(-75, -45), "client": "demo",
              "outfit": o, "shade": s})
    i = 0
    phrases = ["привет с демо-шлюза", "как слышно?", "тест автообновления"]
    while True:
        time.sleep(12)
        post({"t": "peer", "mac": "demo:%s" % random.choice(names),
              "rssi": random.randint(-80, -40), "client": "demo"})
        if i % 2 == 0:
            post({"t": "msg", "from": random.choice(names), "text": random.choice(phrases)})
        i += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=2000000)
    ap.add_argument("--demo", action="store_true")
    ap.add_argument("--api", default="http://127.0.0.1:40400/api/ingest")
    a = ap.parse_args()
    globals()["API"] = a.api
    if a.demo:
        demo_loop()
    else:
        serial_loop(a.port, a.baud)


if __name__ == "__main__":
    main()
