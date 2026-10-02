#!/usr/bin/env python3
"""Mock of the base's HTTP API for working on firmware/web/index.html without hardware.

    python tools/mock_server.py [--port 8080]

Serves the real index.html and fakes /api/* with generated history. Mirrors the
contract of firmware/src/web_ui.cpp - keep both in sync. Standard library only.
"""

import argparse
import copy
import json
import math
import random
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
INDEX = ROOT / "firmware" / "web" / "index.html"

MODES = ("off", "moisture", "schedule")

config = {
    "modules": [
        {"enabled": True, "name": "Томаты", "mode": "moisture", "time": "07:00", "everyDays": 1,
         "threshold": 40, "duration": 40, "dryRaw": 17600, "wetRaw": 7200, "calibrated": True, "lineSense": True},
        {"enabled": True, "name": "Огурцы", "mode": "moisture", "time": "07:10", "everyDays": 1,
         "threshold": 50, "duration": 30, "dryRaw": 17400, "wetRaw": 7000, "calibrated": True, "lineSense": False},
        {"enabled": True, "name": "Клубника", "mode": "schedule", "time": "20:00", "everyDays": 2,
         "threshold": 35, "duration": 20, "dryRaw": 17600, "wetRaw": 7200, "calibrated": True, "lineSense": False},
        {"enabled": False, "name": "Модуль 4", "mode": "moisture", "time": "07:00", "everyDays": 1,
         "threshold": 35, "duration": 30, "dryRaw": 17600, "wetRaw": 7200, "calibrated": True, "lineSense": False},
    ],
    "wifi": {"ssid": "Dacha", "passwordSet": True, "hostname": "watering"},
    "time": {"tz": "MSK-3", "ntp": "pool.ntp.org"},
    "battery": {"check": True, "minMv": 3500, "abortMv": 3300, "calibration": 1.0},
    "water": {"check": True, "presentHigh": True},
    "pump": {"maxSec": 300, "valveLeadMs": 300, "valveTailMs": 500, "catchUpMin": 180},
    "sleep": {"wifiMin": 10, "ntpDays": 7},
    "security": {"adminPasswordSet": False},
}

state = {"job": None, "queue": [], "boot": time.time(), "last_block": "none",
         "awake_until": time.time() + 600, "alarms": 0}
history = []
events = []


def now() -> int:
    return int(time.time())


def pct_of(raw, m):
    span = m["dryRaw"] - m["wetRaw"]
    return max(0, min(100, int((m["dryRaw"] - raw) * 100 / span))) if span else 0


def generate():
    t_end = now()
    t = t_end - 120 * 86400
    moist = [55.0, 60.0, 45.0, 50.0]
    mv = 4150
    while t < t_end:
        mv = max(3450, min(4190, mv + random.randint(-40, 35)))
        for i, m in enumerate(config["modules"][:3]):
            moist[i] -= random.uniform(4, 9)
            pct = int(max(5, min(95, moist[i])))
            raw = int(m["dryRaw"] - pct * (m["dryRaw"] - m["wetRaw"]) / 100)
            skipped_wet = m["mode"] == "moisture" and pct >= m["threshold"]
            history.append([t + i * 600, i, pct, raw, mv, 0, 4 if skipped_wet else 0])
            if pct < m["threshold"] or m["mode"] == "schedule":
                history.append([t + i * 600 + 60, i, pct, 0, mv - 150, m["duration"], 1])
                moist[i] += m["duration"] * 0.9
        t += 86400
    events.extend([[t_end - 3600 * 5, 1, 0, -1, 1, 0], [t_end - 3600 * 5 + 20, 6, 0, -1, -61, 0],
                   [t_end - 3600 * 5 + 21, 3, 0, -1, 0, 0], [t_end - 3600 * 2, 12, 0, 0, 13100, 43],
                   [t_end - 3600 * 2 + 1, 20, 0, 0, 2, 43], [t_end - 3600, 24, 1, 1, 3, 0],
                   [t_end - 600, 26, 2, 2, 0, 0], [t_end - 590, 27, 0, -1, 0, 0],
                   [t_end - 300, 31, 0, -1, 7, 0], [t_end - 299, 39, 0, -1, 1, 0]])


live = {}


def live_reading(i):
    m = config["modules"][i]
    base = {0: 43, 1: 37, 2: 61}.get(i)
    if base is None:
        return None
    pct = base + int(3 * math.sin(time.time() / 60 + i))
    return int(m["dryRaw"] - pct * (m["dryRaw"] - m["wetRaw"]) / 100)


def tick():
    job = state["job"]
    if job and now() >= job["end"]:
        if job["kind"] != 2:
            history.append([now(), job["module"], 0, 0, 3950, job["seconds"], 1 | 16])
            events.append([now(), 22, 0, job["module"], job["seconds"], 0])
            live[job["module"]] = {"ts": now(), "sec": job["seconds"]}
        else:
            events.append([now(), 29, 0, job["module"], 0, 0])
        state["job"] = None
    if not state["job"] and state["queue"]:
        j = state["queue"].pop(0)
        j["end"] = now() + j["seconds"]
        state["job"] = j
        events.append([now(), 21, 0, j["module"], j["seconds"], j["kind"]])


def next_check(m):
    h, mi = map(int, m["time"].split(":"))
    lt = time.localtime()
    target = time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, h, mi, 0, 0, 0, -1))
    if target <= time.time():
        target += 86400 * m["everyDays"]
    return int(target)


def status():
    tick()
    job = state["job"]
    modules = []
    for i, m in enumerate(config["modules"]):
        raw = live_reading(i)
        lw = live.get(i, {})
        modules.append({
            "index": i, "address": 0x48 + i, "name": m["name"], "enabled": m["enabled"], "mode": m["mode"],
            "present": raw is not None, "readingOk": raw is not None, "raw": raw or 0,
            "pct": pct_of(raw, m) if raw else 0, "threshold": m["threshold"], "calibrated": m["calibrated"],
            "lastMeasureTs": now() - 7200 if raw else 0, "lastWaterTs": lw.get("ts", now() - 86400 + i * 600),
            "lastWaterSec": lw.get("sec", m["duration"]),
            "nextCheckTs": next_check(m) if m["enabled"] and m["mode"] != "off" else 0,
            "valveOpen": bool(job and job["module"] == i),
        })
    return {
        "fw": "0.1.0-mock", "uptime": int(time.time() - state["boot"]),
        "time": {"valid": True, "epoch": now(), "tz": config["time"]["tz"],
                 "local": time.strftime("%Y-%m-%d %H:%M:%S")},
        "battery": {"mv": 3870 if not job else 3720, "present": True, "pct": 62, "check": True,
                    "minMv": config["battery"]["minMv"], "underLoad": bool(job and job["kind"] != 2)},
        "water": {"present": True, "check": config["water"]["check"]},
        "controller": {"state": "pumping" if job else "idle", "module": job["module"] if job else -1,
                       "kind": job["kind"] if job else 0, "remainingSec": max(0, job["end"] - now()) if job else 0,
                       "queued": len(state["queue"]), "pumpOn": bool(job and job["kind"] != 2),
                       "lastBlock": state["last_block"]},
        "net": {"sta": True, "ssid": config["wifi"]["ssid"], "ip": "192.168.1.57", "rssi": -61, "ap": False,
                "apSsid": "Watering-1A2B", "hostname": config["wifi"]["hostname"]},
        "power": {"mode": "session", "sleepInSec": max(0, int(state["awake_until"] - time.time())),
                  "nextWakeSec": min((n["nextCheckTs"] for n in modules if n["nextCheckTs"]), default=now() - 1) - now()},
        "alarms": {"noWater": bool(state["alarms"] & 1), "lowBattery": bool(state["alarms"] & 2)},
        "modules": modules,
    }


def apply_config(body):
    # Firmware validates atomically: a rejected update changes nothing.
    snapshot = copy.deepcopy(config)
    try:
        _apply_config(body)
    except ValueError:
        config.clear()
        config.update(snapshot)
        raise


def _apply_config(body):
    for i, m in enumerate(body.get("modules") or []):
        if m is None:
            continue
        if "mode" in m and m["mode"] not in MODES:
            raise ValueError("Неизвестный режим модуля")
        config["modules"][i].update({k: v for k, v in m.items() if k != "calibrated"})
        cm = config["modules"][i]
        cm["calibrated"] = abs(cm["dryRaw"] - cm["wetRaw"]) >= 500
    for group in ("wifi", "time", "battery", "water", "pump", "sleep", "security"):
        if group not in body:
            continue
        for k, v in body[group].items():
            if k in ("password", "apPassword", "adminPassword"):
                if group == "wifi" and k == "password":
                    config["wifi"]["passwordSet"] = bool(v)
                if group == "security":
                    config["security"]["adminPasswordSet"] = bool(v)
                continue
            config[group][k] = v
    if config["battery"]["abortMv"] >= config["battery"]["minMv"]:
        raise ValueError("Порог аварийной остановки должен быть ниже порога старта")
    events.append([now(), 5, 0, -1, 0, 0])


class Handler(BaseHTTPRequestHandler):
    def _json(self, code, obj):
        data = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        if self.headers.get("Content-Type", "").startswith("application/json"):
            return json.loads(raw or b"{}")
        return {}

    def log_message(self, *_):
        pass

    def do_GET(self):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        if url.path == "/":
            data = INDEX.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif url.path == "/api/status":
            state["awake_until"] = time.time() + config["sleep"]["wifiMin"] * 60
            self._json(200, status())
        elif url.path == "/api/config":
            self._json(200, config)
        elif url.path == "/api/history":
            days = int(q.get("days", 30))
            mod = int(q.get("module", -1))
            since = now() - days * 86400 if days > 0 else 0
            recs = [r for r in history if r[0] >= since and (mod < 0 or r[1] == mod)]
            self._json(200, {"now": now(), "timeValid": True, "records": recs})
        elif url.path == "/api/log":
            limit = int(q.get("limit", 200))
            self._json(200, {"now": now(), "uptime": int(time.time() - state["boot"]), "records": events[-limit:]})
        else:
            self._json(404, {"ok": False, "error": "Не найдено"})

    def do_POST(self):
        url = urlparse(self.path)
        if url.path == "/api/update":
            self.rfile.read(int(self.headers.get("Content-Length") or 0))
            self._json(200, {"ok": True})
            return
        try:
            body = self._body()
        except json.JSONDecodeError:
            self._json(400, {"ok": False, "error": "Некорректный JSON"})
            return
        if url.path == "/api/config":
            try:
                apply_config(body)
            except ValueError as e:
                self._json(400, {"ok": False, "error": str(e)})
                return
            self._json(200, {"ok": True, "wifiReconnect": "wifi" in body and "ssid" in body["wifi"]})
        elif url.path == "/api/time":
            self._json(200, {"ok": True})
        elif url.path == "/api/action":
            cmd = body.get("cmd")
            if cmd in ("water", "valve_test"):
                m = body.get("module", -1)
                busy = [j["module"] for j in state["queue"]] + ([state["job"]["module"]] if state["job"] else [])
                if m in busy:
                    self._json(409, {"ok": False, "error": "Модуль уже в очереди"})
                    return
                kind = 2 if cmd == "valve_test" else 1
                secs = 5 if kind == 2 else int(body.get("seconds") or config["modules"][m]["duration"])
                state["queue"].append({"module": m, "seconds": secs, "kind": kind})
                tick()
            elif cmd == "stop":
                state["queue"].clear()
                if state["job"]:
                    events.append([now(), 23, 1, state["job"]["module"], 1, 3])
                state["job"] = None
            elif cmd == "measure":
                for i in range(3):
                    raw = live_reading(i)
                    m = config["modules"][i]
                    history.append([now(), i, pct_of(raw, m), raw, 3870, 0, 16])
                    events.append([now(), 12, 0, i, raw, pct_of(raw, m)])
            elif cmd == "calibrate":
                m = body["module"]
                raw = live_reading(m)
                if raw is None:
                    self._json(500, {"ok": False, "error": "Не удалось прочитать датчик"})
                    return
                config["modules"][m]["wetRaw" if body.get("point") == "wet" else "dryRaw"] = raw
            elif cmd == "clear_log":
                events.clear()
            elif cmd == "clear_history":
                history.clear()
            elif cmd == "sleep":
                events.append([now(), 39, 0, -1, 0, 0])
                events.append([now(), 30, 0, -1, 3600, 0])
            elif cmd != "reboot":
                self._json(400, {"ok": False, "error": "Неизвестная команда"})
                return
            self._json(200, {"ok": True})
        else:
            self._json(404, {"ok": False, "error": "Не найдено"})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--alarms", action="store_true", help="show both latched alarms (no water, low battery)")
    args = ap.parse_args()
    generate()
    if args.alarms:
        state["alarms"] = 3
        events.append([now() - 30, 37, 1, -1, 3, 3380])
    print(f"Mock base: http://localhost:{args.port}/")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
