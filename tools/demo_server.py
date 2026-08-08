#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 Kapı Kontrol - Demo Mock Sunucusu
=======================================
Gerçek donanım olmadan web arayüzünü uçtan uca denemek için ESP32 REST API'sini
taklit eder. data/ klasöründeki index.html'i servis eder ve /api/* uç noktalarını
hafıza içi demo verilerle yanıtlar.

Çalıştırma:
    python tools/demo_server.py [port]   (varsayılan 8123)

Demo giriş bilgileri:  kullanıcı: admin   şifre: admin
"""
import json
import os
import time
import threading
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8123
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
ROOT = os.path.abspath(ROOT)

START = time.time()

# ---------------------------------------------------------------------------
# Demo veri
# ---------------------------------------------------------------------------
NOW = int(time.time())

USERS = [
    {"uid": "04A2B3C1D5", "cardName": "Yönetici Kartı", "fullName": "Ahmet Yılmaz", "department": "Yönetim",
     "phone": "+90 532 111 22 33", "note": "Sistem yöneticisi", "type": "admin", "active": True,
     "createdAt": NOW - 90 * 86400, "lastAccessAt": NOW - 360, "accessCount": 152},
    {"uid": "13D5E6F7A8", "cardName": "Personel-01", "fullName": "Ayşe Demir", "department": "İnsan Kaynakları",
     "phone": "+90 532 222 33 44", "note": "", "type": "normal", "active": True,
     "createdAt": NOW - 60 * 86400, "lastAccessAt": NOW - 7200, "accessCount": 87},
    {"uid": "29B1C2D3E4", "cardName": "Personel-02", "fullName": "Mehmet Kaya", "department": "Muhasebe",
     "phone": "+90 533 333 44 55", "note": "Saat 09-18 çalışır", "type": "normal", "active": True,
     "createdAt": NOW - 45 * 86400, "lastAccessAt": NOW - 26000, "accessCount": 64},
    {"uid": "5F6A7B8C9D", "cardName": "Personel-03", "fullName": "Zeynep Şahin", "department": "Teknik Servis",
     "phone": "+90 534 444 55 66", "note": "", "type": "normal", "active": False,
     "createdAt": NOW - 30 * 86400, "lastAccessAt": NOW - 200000, "accessCount": 23},
    {"uid": "A1B2C3D4E5", "cardName": "Personel-04", "fullName": "Emre Aydın", "department": "Güvenlik",
     "phone": "+90 535 555 66 77", "note": "Gece vardiyası", "type": "normal", "active": True,
     "createdAt": NOW - 15 * 86400, "lastAccessAt": NOW - 5400, "accessCount": 41},
]

REASONS = ["valid", "out_of_hours", "card_not_found", "card_inactive", "holiday", "door_open"]
ACCESS_LOG = []
_seq = 0
for i in range(60):
    _seq += 1
    u = USERS[i % len(USERS)]
    allowed = (i % 4 != 2)
    ACCESS_LOG.append({
        "id": _seq, "ts": NOW - i * 900, "uid": u["uid"], "name": u["fullName"], "door": "Ana Kapı",
        "result": "allowed" if allowed else "denied",
        "reason": "valid" if allowed else REASONS[(i % 4) + 1],
        "ip": f"192.168.1.{10 + (i % 20)}", "rssi": -45 - (i % 20),
    })
ACCESS_LOG.reverse()

EVENTS = [
    {"ts": NOW - 30, "type": "login", "message": "Web girişi: admin (IP 192.168.1.50)"},
    {"ts": NOW - 300, "type": "ntp_sync", "message": "NTP senkronu tamamlandı (pool.ntp.org)"},
    {"ts": NOW - 360, "type": "rfid_changed", "message": "Okuyucu tipi değiştirildi: MFRC522"},
    {"ts": NOW - 900, "type": "config_save", "message": "Yapılandırma kaydedildi"},
    {"ts": NOW - 3600, "type": "wifi_connected", "message": "WiFi bağlandı: AKEVTEK_OFIS"},
    {"ts": NOW - 7200, "type": "mqtt_connected", "message": "MQTT bağlandı: 192.168.1.100"},
    {"ts": NOW - 86400, "type": "boot", "message": "Sistem başlatıldı"},
    {"ts": NOW - 86400, "type": "firmware_update", "message": "Firmware güncellendi: v1.0.0"},
]

HOLIDAYS = [
    {"date": "2026-01-01", "name": "Yılbaşı"},
    {"date": "2026-04-23", "name": "Ulusal Egemenlik ve Çocuk Bayramı"},
    {"date": "2026-05-19", "name": "Atatürk'ü Anma, Gençlik ve Spor Bayramı"},
    {"date": "2026-08-30", "name": "Zafer Bayramı"},
    {"date": "2026-10-29", "name": "Cumhuriyet Bayramı"},
]

DEFAULT_SCHEDULE = {
    "enabled": True,
    "days": [
        {"enabled": True, "ranges": [{"start": 8 * 60, "end": 12 * 60}, {"start": 13 * 60, "end": 18 * 60}]},
        {"enabled": True, "ranges": [{"start": 8 * 60, "end": 12 * 60}, {"start": 13 * 60, "end": 18 * 60}]},
        {"enabled": True, "ranges": [{"start": 8 * 60, "end": 12 * 60}, {"start": 13 * 60, "end": 18 * 60}]},
        {"enabled": True, "ranges": [{"start": 8 * 60, "end": 12 * 60}, {"start": 13 * 60, "end": 18 * 60}]},
        {"enabled": True, "ranges": [{"start": 8 * 60, "end": 12 * 60}, {"start": 13 * 60, "end": 18 * 60}]},
        {"enabled": False, "ranges": []},
        {"enabled": False, "ranges": []},
    ],
}

SCHEDULES = {u["uid"]: dict(DEFAULT_SCHEDULE) for u in USERS}

CONFIG = {
    "wifi": {"ssid": "AKEVTEK_OFIS", "password": "********", "hostname": "esp32-kapi"},
    "ntp": {"server": "pool.ntp.org", "utcOffsetMin": 180, "autoDst": True},
    "auth": {"username": "admin", "passwordHash": "********", "enabled": True, "sessionTimeoutMin": 60},
    "lcd": {"enabled": True, "sdaPin": -1, "sclPin": -1, "address": 0, "cols": 16, "rows": 2},
}

GPIO = {
    "config": {"relayPin": 4, "relayActiveLow": False, "relayActiveMs": 1000,
               "buzzerPin": 16, "statusLedPin": 2, "doorSensorPin": 17,
               "doorSensorActiveLow": True, "exitButtonPin": 18},
    "state": "closed", "doorOpenPhysical": False, "exitPressed": False,
    "melody": None,  # son çalınan melodi (demo görselleştirme)
}

LCD = {"present": True, "detectedSda": 21, "detectedScl": 22, "detectedAddress": 0x27,
       "config": {"enabled": True, "sdaPin": 21, "sclPin": 22, "address": 0x27, "cols": 16, "rows": 2}}

RFID = {"ready": True, "enroll": False, "lastUid": None,
        "config": {"type": "mfrc522", "hz1050RxPin": 25, "hz1050TxPin": 26, "hz1050Baud": 9600,
                   "mfrc522SckPin": 18, "mfrc522MisoPin": 19, "mfrc522MosiPin": 23,
                   "mfrc522SdaPin": 5, "mfrc522RstPin": 21}}

def rfid_active_reader():
    t = RFID["config"]["type"]
    if t == "hz1050":
        return "HZ1050 (UART)"
    if t == "mfrc522":
        return "MFRC522 (SPI)"
    return "MFRC522 (SPI)"  # demo: auto detected as MFRC522

MQTT = {"enabled": True, "connected": True,
        "config": {"enabled": True, "server": "192.168.1.100", "port": 1883, "username": "ha",
                   "password": "********", "clientId": "esp32-kapi", "topicPrefix": "esp32kapi",
                   "discoveryPrefix": "homeassistant"}}

TOKENS = {}
CSRF = "demo-csrf-token"


# ---------------------------------------------------------------------------
# Yardımcılar
# ---------------------------------------------------------------------------
def today_entries():
    from datetime import datetime
    today = datetime.now().strftime("%Y-%m-%d")
    return sum(1 for e in ACCESS_LOG if datetime.fromtimestamp(e["ts"]).strftime("%Y-%m-%d") == today)


def fmt_ts(ts):
    from datetime import datetime
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")


def dashboard():
    allowed = sum(1 for e in ACCESS_LOG if e["result"] == "allowed")
    denied = len(ACCESS_LOG) - allowed
    return {
        "users": {"total": len(USERS), "admins": sum(1 for u in USERS if u["type"] == "admin")},
        "access": {"today": today_entries(), "todayAllowed": max(1, today_entries() - 2), "todayDenied": 2},
        "last": [dict(e, ts_str=fmt_ts(e["ts"])) for e in ACCESS_LOG[:3]],
        "system": {"app": "ESP32 Kapı Kontrol", "company": "akevtek", "version": "1.0.0",
                   "heap": 198432, "cpuFreq": 240, "flashSize": 4194304, "sketchSize": 1322485,
                   "uptime_ms": int((time.time() - START) * 1000), "ip": "192.168.1.50",
                   "rssi": -52, "ntp_synced": True, "time": time.strftime("%Y-%m-%d %H:%M:%S")},
        "hardware": {"doorOpen": GPIO["state"] == "open", "doorOpenPhysical": GPIO["doorOpenPhysical"],
                     "exitPressed": GPIO["exitPressed"]},
        "rfid": {"type": RFID["config"]["type"], "reader": rfid_active_reader(), "ready": RFID["ready"]},
        "mqtt": {"enabled": MQTT["enabled"], "connected": MQTT["connected"]},
        "events": len(EVENTS), "holidays": len(HOLIDAYS),
    }


def config_response():
    c = json.loads(json.dumps(CONFIG))
    c["mqtt"] = {k: v for k, v in MQTT["config"].items()}
    c["gpio"] = GPIO["config"]
    c["lcd"] = LCD["config"]
    c["rfid"] = RFID["config"]
    return c


# ---------------------------------------------------------------------------
# HTTP Handler
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[demo] %s %s\n" % (self.address_string(), fmt % args))

    # -- yardımcılar ------------------------------------------------------
    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False)
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self):
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0:
            return {}
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except Exception:
            return {}

    def _auth_ok(self):
        token = self.headers.get("X-Auth-Token")
        return token in TOKENS

    def _require_auth(self):
        if not self._auth_ok():
            self._send(401, {"error": "Unauthorized"})
            return False
        return True

    # -- rota dağıtımı ----------------------------------------------------
    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/" or path == "/index.html":
            self._serve_static("/index.html")
            return
        if path.startswith("/api/"):
            self._api_get(path)
            return
        self._serve_static(path)

    def do_POST(self):
        path = self.path.split("?")[0]
        if path == "/api/auth/login":
            self._api_login()
            return
        if path.startswith("/api/"):
            self._api_post(path)

    def do_PUT(self):
        path = self.path.split("?")[0]
        self._api_put(path)

    def do_DELETE(self):
        path = self.path.split("?")[0]
        self._api_delete(path)

    def _serve_static(self, path):
        if ".." in path:
            self._send(400, "Bad Request", "text/plain")
            return
        rel = path.lstrip("/")
        fp = os.path.join(ROOT, rel) if rel else os.path.join(ROOT, "index.html")
        if os.path.isfile(fp):
            with open(fp, "rb") as f:
                data = f.read()
            ctype = "text/html" if fp.endswith(".html") else "application/octet-stream"
            self._send(200, data, ctype)
        else:
            self._send(404, "Not Found", "text/plain")

    # -- auth -------------------------------------------------------------
    def _api_login(self):
        body = self._json()
        u, p = body.get("username", ""), body.get("password", "")
        if u == "admin" and p == "admin":
            token = "demo-token-%d" % int(time.time() * 1000)
            TOKENS[token] = time.time()
            EVENTS.insert(0, {"ts": int(time.time()), "type": "login", "message": "Web girişi: admin (IP demo)"})
            self._send(200, {"token": token, "csrf": CSRF, "username": u})
        else:
            self._send(401, {"error": "Invalid credentials"})

    # -- GET API ----------------------------------------------------------
    def _api_get(self, path):
        if path == "/api/auth/status":
            token = self.headers.get("X-Auth-Token")
            self._send(200, {"enabled": True, "authenticated": token in TOKENS, "username": "admin"})
        elif path == "/api/dashboard":
            if not self._require_auth(): return
            self._send(200, dashboard())
        elif path == "/api/config":
            self._send(200, config_response())
        elif path == "/api/users":
            if not self._require_auth(): return
            self._send(200, {"users": USERS})
        elif path == "/api/logs/access":
            if not self._require_auth(): return
            self._api_logs_access()
        elif path == "/api/logs/access.csv":
            if not self._require_auth(): return
            self._api_logs_access_csv()
        elif path == "/api/logs/event":
            if not self._require_auth(): return
            self._api_logs_event()
        elif path == "/api/logs/event.csv":
            if not self._require_auth(): return
            self._api_logs_event_csv()
        elif path == "/api/mqtt":
            if not self._require_auth(): return
            self._send(200, MQTT)
        elif path == "/api/gpio":
            if not self._require_auth(): return
            self._send(200, GPIO)
        elif path == "/api/lcd":
            if not self._require_auth(): return
            self._send(200, LCD)
        elif path == "/api/rfid":
            if not self._require_auth(): return
            d = json.loads(json.dumps(RFID))
            d["active"] = rfid_active_reader()
            d["detecting"] = False
            self._send(200, d)
        elif path == "/api/wifi/scan":
            if not self._require_auth(): return
            self._send(200, {"scanning": False, "networks": [
                {"ssid": "AKEVTEK_OFIS", "open": False, "rssi": -52},
                {"ssid": "AKEVTEK_MISAFIR", "open": True, "rssi": -61},
                {"ssid": "TURKCELL_5G", "open": True, "rssi": -70},
                {"ssid": "TP-LINK_7F2A", "open": False, "rssi": -78},
            ]})
        elif path == "/api/rfid/enroll":
            self._send(200, {"enroll": False, "uid": None})
        elif path == "/api/holidays":
            if not self._require_auth(): return
            self._send(200, {"holidays": HOLIDAYS})
        elif path == "/api/backup":
            if not self._require_auth(): return
            self._send(200, {
                "app": "ESP32 Kapı Kontrol", "company": "akevtek", "version": "1.0.0",
                "exported": time.strftime("%Y-%m-%d %H:%M:%S"),
                "users": USERS, "holidays": HOLIDAYS,
                "config": config_response(),
            })
        elif path == "/api/ota/status":
            self._send(200, {"busy": False})
        elif path.startswith("/api/schedule/"):
            if not self._require_auth(): return
            uid = path.split("/")[-1]
            self._send(200, {"schedule": SCHEDULES.get(uid, DEFAULT_SCHEDULE)})
        else:
            self._send(404, {"error": "Not Found"})

    def _api_logs_access(self):
        from urllib.parse import urlparse, parse_qs
        q = parse_qs(urlparse(self.path).query)
        result = q.get("result", [""])[0]
        query = q.get("q", [""])[0].lower()
        try:
            limit = int(q.get("limit", ["50"])[0])
        except Exception:
            limit = 50
        entries = ACCESS_LOG
        if result:
            entries = [e for e in entries if e["result"] == result]
        if query:
            entries = [e for e in entries if query in (e["uid"] + e["name"]).lower()]
        entries = [dict(e, ts_str=fmt_ts(e["ts"])) for e in entries[:limit]]
        self._send(200, {"entries": entries})

    def _api_logs_access_csv(self):
        import io
        buf = io.StringIO()
        buf.write("Tarih,UID,Ad Soyad,Kapı,Sonuç,Sebep,IP,RSSI\n")
        for e in ACCESS_LOG[:100]:
            buf.write("%s,%s,%s,%s,%s,%s,%s,%s\n" % (
                fmt_ts(e["ts"]), e["uid"], e["name"], e["door"],
                e["result"], e["reason"], e["ip"], e["rssi"]))
        self._send(200, buf.getvalue(), "text/csv")

    def _api_logs_event(self):
        from urllib.parse import urlparse, parse_qs
        q = parse_qs(urlparse(self.path).query)
        etype = q.get("type", [""])[0]
        query = q.get("q", [""])[0].lower()
        try:
            limit = int(q.get("limit", ["50"])[0])
        except Exception:
            limit = 50
        entries = EVENTS
        if etype:
            entries = [e for e in entries if e["type"] == etype]
        if query:
            entries = [e for e in entries if query in e["message"].lower()]
        entries = [dict(e, ts_str=fmt_ts(e["ts"])) for e in entries[:limit]]
        self._send(200, {"entries": entries})

    def _api_logs_event_csv(self):
        import io
        buf = io.StringIO()
        buf.write("Tarih,Tip,Mesaj\n")
        for e in EVENTS[:100]:
            buf.write("%s,%s,%s\n" % (fmt_ts(e["ts"]), e["type"], e["message"]))
        self._send(200, buf.getvalue(), "text/csv")

    # -- POST API ---------------------------------------------------------
    def _api_post(self, path):
        if path == "/api/auth/logout":
            token = self.headers.get("X-Auth-Token")
            if token in TOKENS:
                del TOKENS[token]
            self._send(200, {"ok": True})
        elif path == "/api/config":
            if not self._require_auth(): return
            body = self._json()
            if "wifi" in body:
                CONFIG["wifi"].update({k: v for k, v in body["wifi"].items() if v})
            if "ntp" in body:
                CONFIG["ntp"].update(body["ntp"])
            if "auth" in body:
                CONFIG["auth"].update({k: v for k, v in body["auth"].items() if v})
            EVENTS.insert(0, {"ts": int(time.time()), "type": "config_save", "message": "Yapılandırma kaydedildi"})
            self._send(200, {"ok": True})
        elif path == "/api/users":
            if not self._require_auth(): return
            body = self._json()
            if any(u["uid"] == body.get("uid") for u in USERS):
                self._send(409, {"error": "UID zaten var"})
                return
            body.setdefault("type", "normal")
            body.setdefault("active", True)
            body.setdefault("accessCount", 0)
            body["createdAt"] = int(time.time())
            body["lastAccessAt"] = None
            USERS.append(body)
            SCHEDULES[body["uid"]] = json.loads(json.dumps(DEFAULT_SCHEDULE))
            EVENTS.insert(0, {"ts": int(time.time()), "type": "user_added", "message": "Kullanıcı eklendi: " + body.get("fullName", body["uid"])})
            self._send(200, {"ok": True})
        elif path == "/api/logs/clear":
            if not self._require_auth(): return
            ACCESS_LOG.clear()
            self._send(200, {"ok": True})
        elif path == "/api/logs/event/clear":
            if not self._require_auth(): return
            EVENTS.clear()
            self._send(200, {"ok": True})
        elif path == "/api/mqtt":
            if not self._require_auth(): return
            body = self._json()
            MQTT["config"].update({k: v for k, v in body.items() if v is not None})
            MQTT["enabled"] = bool(body.get("enabled", MQTT["enabled"]))
            self._send(200, {"ok": True})
        elif path == "/api/gpio":
            if not self._require_auth(): return
            body = self._json()
            GPIO["config"].update({k: v for k, v in body.items() if v is not None})
            self._send(200, {"ok": True})
        elif path == "/api/gpio/action":
            if not self._require_auth(): return
            body = self._json()
            act = body.get("action", "")
            if act == "open":
                GPIO["state"] = "open"
                GPIO["melody"] = "open"  # uzun tek bip
                ACCESS_LOG.insert(0, {"id": _seq + 1, "ts": int(time.time()), "uid": "MANUAL",
                                       "name": "Manuel Açma", "door": "Ana Kapı", "result": "allowed",
                                       "reason": "valid", "ip": "192.168.1.50", "rssi": -52})
                threading.Timer(1.5, lambda: GPIO.update(state="closed")).start()
            elif act == "beep":
                GPIO["melody"] = "beep"
            elif act == "melody":
                GPIO["melody"] = body.get("melody", "open")
            elif act == "led":
                GPIO["melody"] = None
            self._send(200, {"ok": True})
        elif path == "/api/lcd":
            if not self._require_auth(): return
            body = self._json()
            LCD["config"].update({k: v for k, v in body.items() if v is not None})
            self._send(200, {"ok": True})
        elif path == "/api/rfid":
            if not self._require_auth(): return
            body = self._json()
            if body.get("type") in ("auto", "hz1050", "mfrc522"):
                RFID["config"]["type"] = body["type"]
            for k in ("hz1050RxPin", "hz1050TxPin", "hz1050Baud",
                      "mfrc522SckPin", "mfrc522MisoPin", "mfrc522MosiPin",
                      "mfrc522SdaPin", "mfrc522RstPin"):
                if k in body and body[k] is not None:
                    RFID["config"][k] = body[k]
            RFID["ready"] = True
            EVENTS.insert(0, {"ts": int(time.time()), "type": "rfid_changed",
                              "message": "Okuyucu değiştirildi: " + rfid_active_reader()})
            self._send(200, {"ok": True})
        elif path == "/api/lcd/action":
            if not self._require_auth(): return
            act = self._json().get("action", "")
            if act == "scan":
                self._send(200, {"ok": True, "found": True, "sdaPin": 21, "sclPin": 22})
            else:
                self._send(200, {"ok": True})
        elif path == "/api/rfid/enroll":
            body = self._json()
            self._send(200, {"ok": True, "enroll": bool(body.get("enroll"))})
        elif path == "/api/holidays":
            if not self._require_auth(): return
            body = self._json()
            HOLIDAYS.append({"date": body.get("date", ""), "name": body.get("name", "")})
            self._send(200, {"ok": True})
        elif path == "/api/restart":
            self._send(200, {"ok": True})
        elif path == "/api/backup":
            if not self._require_auth(): return
            try:
                body = self._json()
                if "users" in body:
                    USERS[:] = body["users"]
                if "holidays" in body:
                    HOLIDAYS[:] = body["holidays"]
                EVENTS.insert(0, {"ts": int(time.time()), "type": "config_restore", "message": "Yedek geri yüklendi"})
                self._send(200, {"ok": True})
            except Exception:
                self._send(400, {"error": "Invalid backup file"})
        elif path == "/api/ota":
            self._send(200, {"ok": True})
        else:
            self._send(404, {"error": "Not Found"})

    # -- PUT / DELETE -----------------------------------------------------
    def _api_put(self, path):
        if not self._require_auth():
            return
        if path.startswith("/api/users/"):
            uid = path.split("/")[-1]
            body = self._json()
            for u in USERS:
                if u["uid"] == uid:
                    u.update({k: v for k, v in body.items() if v is not None})
                    self._send(200, {"ok": True})
                    return
            self._send(404, {"error": "Not Found"})
        elif path.startswith("/api/schedule/"):
            uid = path.split("/")[-1]
            body = self._json()
            SCHEDULES[uid] = {"enabled": bool(body.get("enabled", True)), "days": body.get("days", DEFAULT_SCHEDULE["days"])}
            self._send(200, {"ok": True})
        else:
            self._send(404, {"error": "Not Found"})

    def _api_delete(self, path):
        if not self._require_auth():
            return
        if path.startswith("/api/users/"):
            uid = path.split("/")[-1]
            USERS[:] = [u for u in USERS if u["uid"] != uid]
            SCHEDULES.pop(uid, None)
            EVENTS.insert(0, {"ts": int(time.time()), "type": "user_deleted", "message": "Kullanıcı silindi: " + uid})
            self._send(200, {"ok": True})
        elif path.startswith("/api/holidays/"):
            date = path.split("/")[-1]
            HOLIDAYS[:] = [h for h in HOLIDAYS if h["date"] != date]
            self._send(200, {"ok": True})
        elif path.startswith("/api/schedule/"):
            uid = path.split("/")[-1]
            SCHEDULES.pop(uid, None)
            self._send(200, {"ok": True})
        else:
            self._send(404, {"error": "Not Found"})


def main():
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print("=" * 60)
    print("  ESP32 Kapı Kontrol - DEMO SUNUCUSU")
    print("  URL      : http://127.0.0.1:%d/index.html" % PORT)
    print("  Kullanıcı: admin   Şifre: admin")
    print("=" * 60, flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
