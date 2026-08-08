# ESP32 Kapı Kontrol — RFID Access Control System

ESP32 üzerinde çalışan, tamamen web arayüzünden yönetilebilen, kurumsal seviyede bir **RFID Kapı Geçiş Sistemi**.

- **Firma:** akevtek
- **Yazılım:** ESP32 Kapı Kontrol
- **Platform:** PlatformIO + Arduino Framework
- **Dil:** C++17
- **Web Server:** ESPAsyncWebServer
- **JSON:** ArduinoJson 7
- **Dosya Sistemi:** LittleFS
- **Realtime:** FreeRTOS task yapısı (delay() kullanılmaz, tamamen async)

> Home Assistant ile MQTT Auto Discovery üzerinden otomatik entegre olur.

---

## Özellikler

- **Çift RFID okuyucu desteği** — HZ1050 (UART) ve MFRC522 (SPI), web üzerinden derleme gerektirmeden değiştirilebilir (plugin mimarisi).
- **Sınırsız kullanıcı** — LittleFS üzerinde JSON olarak saklanır. Her kullanıcı için UID, isim, departman, telefon, not, kart tipi (normal/admin), aktif/pasif, oluşturma tarihi, son geçiş ve toplam geçiş sayısı.
- **Haftalık geçiş takvimi** — Her kullanıcı için 7 gün, her gün birden fazla zaman aralığı (örn. 08:00–12:00, 13:00–18:00, 18:30–22:00).
- **Tatil yönetimi** — Resmi tatillerde normal kartlar engellenir, admin kartlar etkilenmez.
- **Admin kartlar** — Zaman/haftalık/tatil kurallarına tabi değildir, kapı kilitliyken bile açar.
- **NTP + RTC benzeri zaman** — Timezone seçimi, otomatik yaz/kış saati, internet koptuğunda saat çalışmaya devam eder.
- **Access Log** — Tarih, saat, UID, ad soyad, sonuç, sebep, IP, RSSI. Filtreleme ve CSV indirme.
- **Event Log** — WiFi, MQTT, NTP, boot, restart, OTA, config, login/logout ve hata olayları.
- **MQTT + Home Assistant Auto Discovery** — Kapı durumu, son kart, son kullanıcı, RSSI, heap, IP, online/LWT; aç/restart/buzzer/LED komutları.
- **Güvenlik** — Session tabanlı giriş, PBKDF2-HMAC-SHA256 şifre hash, CSRF koruması, rate limit, auto logout, HTTPS reverse proxy uyumlu.
- **Backup / Restore** — Tek tuşla JSON export/import, tüm yapılandırma + kullanıcılar + loglar.
- **OTA** — `.bin` yükleme, ilerleme çubuğu, hata durumunda önceki firmware'e rollback.
- **Buzzer melodileri** — Başarılı kart (bip-bip), reddedilen kart (bip-bip-bip), kapı açma (uzun bip) için farklı melodiler.
- **I2C 16x2 LCD** — PCF8574 adaptörlü HD44780; pinler ve I2C adresi otomatik algılanır.
- **Demo sunucu** — `tools/demo_server.py` ile web arayüzü ESP32 olmadan tarayıcıda denenebilir.

---

## Donanım Bağlantıları

Varsayılan pinler web arayüzünden (**Donanım** sayfası) değiştirilebilir.

### HZ1050 RFID Reader (UART)

| HZ1050 | ESP32 |
|--------|-------|
| TX | GPIO25 (RX) |
| RX | GPIO26 (TX) |
| VCC | 3.3V / 5V |
| GND | GND |

Baud seçimi: **9600** veya **19200** (web üzerinden).

### MFRC522 (SPI)

| MFRC522 | ESP32 |
|---------|-------|
| SDA (SS) | GPIO5 |
| SCK | GPIO18 |
| MOSI | GPIO23 |
| MISO | GPIO19 |
| RST | GPIO21 |
| 3.3V | 3.3V |
| GND | GND |

### Çıkışlar / Girişler

| Fonksiyon | GPIO | Not |
|-----------|------|-----|
| Röle (kapı kilidi) | GPIO4 | Aktif süresi: 0.5/1/2/5/10 sn, NO/NC seçilebilir |
| Buzzer | GPIO16 | Melodiler: başarılı / reddedilen / açma |
| Status LED | GPIO2 | On-board LED |
| Kapı sensörü (manyetik kontak) | GPIO17 | Aktifken LOW (ayarlanabilir) |
| Çıkış butonu | GPIO32 | Aktifken HIGH, basınca kapı açılır |

### I2C LCD (16x2, PCF8574)

Pinler **otomatik algılanır** (varsayılan SDA=21, SCL=22; adres 0x27/0x3F taranır). Web üzerinden "Otomatik Algıla" butonu da vardır.

---

## Kurulum ve Derleme

### Gereksinimler

- [Visual Studio Code](https://code.visualstudio.com/) + [PlatformIO IDE](https://platformio.org/install/ide?install=vscode)
- Python 3.8+ (demo sunucu için)

### Derleme

```bash
pio run
```

### Flash Etme

```bash
pio run -t upload
pio run -t uploadfs   # web arayüzü (data/) LittleFS'e yüklenir
```

### Seri Monitör

```bash
pio device monitor
```

---

## İlk Açılış (WiFi Kurulumu)

1. Cihaz açıldığında WiFi bulamazsa **AP moduna** geçer: SSID `ESP32-Setup`.
2. Telefon/bilgisayar bu ağa bağlanın, `http://192.168.4.1` adresini açın.
3. SSID/şifre girin; bilgiler LittleFS'teki `/config.json` dosyasına kaydedilir.
4. Sonraki açılışlarda cihaz otomatik olarak **STA modunda** bağlanır.
5. Cihazın IP adresini ağdan öğrenin (hostname: `esp32-kapi`) ve web arayüzüne girin.
6. İlk girişte **Donanım** sayfasından RFID okuyucu tipini seçin (otomatik algılama denenir, olmazsa sihirbaz).
7. **Güvenlik** sayfasından yönetici şifresi belirleyin (sistem o zaman kilitlenir).

Varsayılan demo girişi (kilit kapalıyken): `admin` / `admin`.

---

## Web Paneli

Responsive, mobil öncelikli, koyu temalı panel. Bootstrap 5 + Chart.js + Fetch API + DataTables + Toast/Modal.

| Sayfa | İçerik |
|-------|--------|
| **Dashboard** | Kart/admin sayısı, bugünkü geçiş, son kart, son hata, RSSI, IP, heap, CPU, flash, uptime, MQTT/NTP durumu, saat |
| **Kullanıcılar** | Kullanıcı CRUD, kart kayıt |
| **Kartlar** | RFID kart listesi, kart ekleme |
| **Geçiş Saatleri** | Kullanıcı bazında haftalık zaman planları |
| **Tatiller** | Resmi tatil yönetimi |
| **MQTT** | Broker ayarları, Home Assistant Discovery |
| **WiFi** | SSID/şifre, AP modu |
| **NTP** | Sunucu, timezone, yaz/kış saati |
| **Donanım** | RFID okuyucu tipi/pinleri, röle, buzzer, LED, kapı sensörü, çıkış butonu, LCD |
| **Logs** | Access log (filtre + CSV) |
| **Events** | Sistem olayları |
| **System** | Sistem bilgileri, yeniden başlatma |
| **Backup** | JSON export / import |
| **Restore** | Yedekten geri yükleme |
| **Firmware Update** | OTA `.bin` yükleme + ilerleme çubuğu |

---

## REST API

Tüm API'ler JSON tabanlıdır. Kimlik doğrulaması kapalıysa `X-Auth-Token` gerekmez; açıksa önce `/api/auth/login` ile token alınır ve isteklere `X-Auth-Token` header'ı eklenir. CSRF koruması açıksa `X-CSRF-Token` de gönderilmelidir.

### Auth

```http
POST /api/auth/login
Content-Type: application/json

{"username": "admin", "password": "sifre"}
```

Yanıt: `{"token": "...", "csrf": "...", "username": "admin"}`

```http
GET  /api/auth/status
POST /api/auth/logout
```

### Dashboard / Sistem

| Metod | Uç Nokta | Açıklama |
|-------|----------|----------|
| GET | `/api/status` | Sistem durumu |
| GET | `/api/dashboard` | Dashboard özeti |
| GET | `/api/config` | Yapılandırma (wifi, ntp, gpio, rfid, mqtt, auth, lcd) |
| POST | `/api/config` | Yapılandırmayı güncelle |
| GET | `/api/wifi/scan` | WiFi taraması |
| POST | `/api/restart` | Cihazı yeniden başlat |

### Kullanıcılar

| Metod | Uç Nokta | Açıklama |
|-------|----------|----------|
| GET | `/api/users` | Tüm kullanıcılar |
| POST | `/api/users` | Kullanıcı ekle (`uid`, `fullName`, `type`...) |
| PUT | `/api/users/*` | Kullanıcı güncelle (path: UID) |
| DELETE | `/api/users/*` | Kullanıcı sil |

```http
POST /api/users
Content-Type: application/json

{
  "uid": "A1B2C3D4",
  "cardName": "Personel Karti 1",
  "fullName": "Ali Veli",
  "department": "Muhasebe",
  "phone": "0555 000 00 00",
  "note": "",
  "type": "normal",
  "active": true
}
```

### Geçiş Saatleri (Schedule)

| Metod | Uç Nokta | Açıklama |
|-------|----------|----------|
| GET | `/api/schedule` | Tüm planlar |
| GET | `/api/schedule/*` | Kullanıcı planı (path: UID) |
| PUT | `/api/schedule/*` | Kullanıcı planını güncelle |
| DELETE | `/api/schedule/*` | Kullanıcı planını sıfırla |

```http
PUT /api/schedule/A1B2C3D4
Content-Type: application/json

{
  "days": [
    {"day": 1, "enabled": true, "slots": [{"start": "08:00", "end": "12:00"}, {"start": "13:00", "end": "18:00"}]},
    {"day": 2, "enabled": true, "slots": [{"start": "08:00", "end": "18:00"}]}
  ]
}
```

### Tatiller

| Metod | Uç Nokta | Açıklama |
|-------|----------|----------|
| GET | `/api/holidays` | Tüm tatiller |
| POST | `/api/holidays` | Tatil ekle |
| DELETE | `/api/holidays/*` | Tatil sil |

### Loglar

| Metod | Uç Nokta | Açıklama |
|-------|----------|----------|
| GET | `/api/logs/access` | Access log (`?q=`, `?result=`, `?limit=`, `?page=` destekler) |
| GET | `/api/logs/access.csv` | CSV indir |
| POST | `/api/logs/clear` | Access log temizle |
| GET | `/api/logs/event` | Event log |
| GET | `/api/logs/event.csv` | CSV indir |
| POST | `/api/logs/event/clear` | Event log temizle |

### Donanım / RFID / GPIO / LCD

| Metod | Uç Nokta | Açıklama |
|-------|----------|----------|
| GET | `/api/rfid` | Okuyucu durumu ve yapılandırma |
| POST | `/api/rfid` | Okuyucu tipi/pinler/baud güncelle |
| GET/POST | `/api/rfid/enroll` | Kart kayıt modu |
| GET | `/api/gpio` | GPIO durumu |
| POST | `/api/gpio` | GPIO yapılandırması |
| POST | `/api/gpio/action` | Aksiyon: `open`, `beep`, `led`, `melody` |
| GET | `/api/lcd` | LCD durumu |
| POST | `/api/lcd` | LCD yapılandırması |
| POST | `/api/lcd/action` | LCD aksiyonu (örn. `detect`) |

```http
POST /api/rfid
Content-Type: application/json

{
  "type": "hz1050",
  "hz1050RxPin": 25,
  "hz1050TxPin": 26,
  "hz1050Baud": 19200
}
```

```http
POST /api/gpio/action
Content-Type: application/json

{"action": "melody", "melody": "success"}   # success | denied | open
```

### MQTT

| Metod | Uç Nokta | Açıklama |
|-------|----------|----------|
| GET | `/api/mqtt` | MQTT durumu |
| POST | `/api/mqtt` | Broker ayarları + discovery |

### Backup / OTA

| Metod | Uç Nokta | Açıklama |
|-------|----------|----------|
| GET | `/api/backup` | JSON yedek indir |
| POST | `/api/backup` | JSON yedek yükle (restore) |
| GET | `/api/ota/status` | OTA durumu |
| POST | `/api/ota` | Firmware `.bin` yükle (multipart) |

---

## MQTT Topic Yapısı

Varsayılan `topicPrefix = esp32door`. Tüm topic'ler: `<prefix>/<alt>`.

### Yayınlanan (Publish)

| Topic | Örnek Payload | Açıklama |
|-------|---------------|----------|
| `<prefix>/door` | `open` / `closed` | Kapı durumu (retained) |
| `<prefix>/access` | `{"result":"allowed","uid":"A1B2","name":"Ali Veli","reason":"valid","ts":1690000000,"rssi":-52}` | Son geçiş |
| `<prefix>/last_result` | `allowed` | Son sonuç (retained) |
| `<prefix>/telemetry` | `{"rssi":-52,"heap":198432,"ip":"192.168.1.50","uptime_ms":123456,"ntp_synced":true}` | Periyodik telemetri |
| `<prefix>/status` | `online` / `offline` | LWT (son vasiyet), retained |

### Dinlenen (Subscribe)

| Topic | Payload | Açıklama |
|-------|---------|----------|
| `<prefix>/cmd/door` | `open` / `close` | Kapıyı aç/kapat |
| `<prefix>/cmd/restart` | herhangi | Cihazı yeniden başlat |
| `<prefix>/cmd/beep` | herhangi | Buzzer çal |
| `<prefix>/cmd/led` | herhangi | LED yanıp sönsün |
| `<prefix>/cmd/reload` | herhangi | Yapılandırmayı yeniden yükle |

---

## Home Assistant Entegrasyonu

Cihaz, MQTT broker'a bağlandığında **Auto Discovery** mesajlarını otomatik yayınlar
(varsayılan discovery prefix `homeassistant`). Home Assistant'a ekstra yapılandırma gerekmez.

Oluşturulan varlıklar (device: **ESP32 Kapı Kontrol**):

| Tür | Object ID | Durum |
|-----|-----------|-------|
| `binary_sensor` | `esp32door_door` | Kapı açık/kapalı |
| `binary_sensor` | `esp32door_status` | Çevrimiçi/çevrimdışı |
| `sensor` | `esp32door_last_result` | Son sonuç (allowed/denied) |
| `sensor` | `esp32door_last_uid` | Son kart UID |
| `sensor` | `esp32door_last_user` | Son kullanıcı |
| `sensor` | `esp32door_rssi` | WiFi RSSI (dBm) |
| `sensor` | `esp32door_heap` | Boş heap (B) |
| `sensor` | `esp32door_ip` | IP adresi |
| `button` | `esp32door_open_door` | Kapıyı aç butonu |

Dashboard'da kullanım örneği:

```yaml
# Lovelace kartı
type: entities
entities:
  - entity: binary_sensor.esp32door_door
  - entity: binary_sensor.esp32door_status
  - entity: sensor.esp32door_last_result
  - entity: sensor.esp32door_last_user
  - entity: button.esp32door_open_door
```

Otomasyon örneği (kapı açıkken bildirim):

```yaml
alias: Kapı Açık Bildirimi
trigger:
  - platform: state
    entity_id: binary_sensor.esp32door_door
    to: "on"
    for: "00:01:00"
action:
  - service: notify.mobile_app_telefon
    data:
      message: "Kapı açık kaldı!"
```

---

## Güvenlik

- **Oturum (session) tabanlı giriş** — başarılı girişte imzalı token döner, `X-Auth-Token` header ile gönderilir.
- **PBKDF2-HMAC-SHA256** — şifreler tuzlanarak hash'lenir, düz metin asla saklanmaz.
- **CSRF koruması** — form/API isteklerinde `X-CSRF-Token` doğrulanır.
- **Rate limit** — giriş denemeleri sınırlanır.
- **Auto logout** — oturum süresi dolunca panel kilitlenir.
- **Reverse proxy uyumlu** — HTTPS sonlandıran nginx/Caddy arkasında çalışır.
- **Kart bazlı yetki** — admin kartlar zaman/tatil kurallarına tabi değildir.

---

## Proje Yapısı

```
├── data/                  # Web arayüzü (LittleFS'e yüklenir)
│   └── index.html
├── include/
│   └── Types.h            # Ortak tipler (EventType vb.)
├── partitions.csv         # OTA uyumlu partition tablosu (app0+app1, LittleFS ~960KB)
├── platformio.ini
├── src/
│   ├── main.cpp           # Giriş noktası, FreeRTOS task'lar
│   ├── core/              # App, AccessEngine, UserManager
│   ├── rfid/              # IRfidReader, HZ1050Reader, MFRC522Reader, ReaderFactory, RfidManager
│   ├── web/               # WebServerManager (REST API + panel)
│   ├── api/               # API yardımcıları
│   ├── mqtt/              # MqttManager (PubSubClient + HA discovery)
│   ├── storage/           # Storage, Backup
│   ├── config/            # ConfigManager (/config.json)
│   ├── log/               # AccessLog, EventLog
│   ├── auth/              # AuthManager (PBKDF2, session, CSRF, rate limit)
│   ├── scheduler/         # TimeSchedule, HolidayManager
│   ├── ntp/               # NtpManager (NTP + DST, RTC benzeri)
│   ├── gpio/              # GpioManager (röle, buzzer melodileri, LED, sensörler)
│   ├── lcd/               # LcdManager (I2C 16x2, otomatik algılama)
│   ├── net/               # WifiManager (STA/AP)
│   └── utils/             # JsonFile vb.
├── test/                  # Unity birim testleri
└── tools/
    └── demo_server.py     # Tarayıcı demo sunucusu (ESP32 olmadan)
```

---

## Demo Sunucu (ESP32'siz Web Denemesi)

Web arayüzünü donanım olmadan tarayıcıda denemek için:

```bash
python tools/demo_server.py 8123
```

- Adres: `http://127.0.0.1:8123/index.html`
- Giriş: `admin` / `admin`

Demo sunucu gerçek ESP32 REST API'sini taklit eder: dashboard, kullanıcılar, loglar, RFID okuyucu,
melodi aksiyonları ve tüm ayarlar çalışır.

---

## Lisans

Bu proje özel (proprietary) kullanım için geliştirilmiştir. İzinsiz dağıtılamaz.
