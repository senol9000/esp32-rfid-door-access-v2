#include "auth/AuthManager.h"

#include <esp_system.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <time.h>

// ---- Özel yardımcılar ----

namespace {

/** Rastgele hex string üretir (token, salt için). */
String randomHex(size_t bytes) {
  String out;
  out.reserve(bytes * 2);
  uint8_t buf[16];
  for (size_t off = 0; off < bytes; off += sizeof(buf)) {
    size_t chunk = std::min(sizeof(buf), bytes - off);
    esp_fill_random(buf, chunk);
    for (size_t i = 0; i < chunk; ++i) {
      char c = "0123456789abcdef"[buf[i] & 0x0F];
      out += c;
      c = "0123456789abcdef"[(buf[i] >> 4) & 0x0F];
      out += c;
    }
  }
  return out;
}

/** Hex string'i bayt dizisine çevirir; işlenen bayt sayısını döndürür. */
size_t hexToBytes(const String& hex, uint8_t* out, size_t maxLen) {
  auto nib = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0;
  };
  size_t len = 0;
  for (size_t i = 0; i + 1 < hex.length() && len < maxLen; i += 2) {
    out[len++] = (uint8_t)((nib(hex[i]) << 4) | nib(hex[i + 1]));
  }
  return len;
}

/** Bayt dizisini hex string'e çevirir. */
String bytesToHex(const uint8_t* in, size_t len) {
  static const char* kHex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += kHex[(in[i] >> 4) & 0x0F];
    out += kHex[in[i] & 0x0F];
  }
  return out;
}

/**
 * PBKDF2-HMAC-SHA256 (mbedtls).
 * Core 3.x'te SHA2Builder/PBKDF2_HMACBuilder kaldırıldığı için mbedtls kullanılır.
 */
String pbkdf2Sha256(const String& password, const uint8_t* salt, size_t saltLen,
                    uint32_t iterations, uint8_t* out, size_t outLen) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (md == nullptr || mbedtls_md_setup(&ctx, md, 1) != 0) {
    mbedtls_md_free(&ctx);
    return String();
  }
  int rc = mbedtls_pkcs5_pbkdf2_hmac(&ctx, (const unsigned char*)password.c_str(),
                                     password.length(), salt, saltLen, iterations,
                                     outLen, out);
  mbedtls_md_free(&ctx);
  return rc == 0 ? bytesToHex(out, outLen) : String();
}

/** Epoch saniye (NTP senkron değilse millis tabanlı geri düşer). */
uint32_t epochNow() {
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) == 0 && tv.tv_sec > 1600000000) {
    return (uint32_t)tv.tv_sec;
  }
  // NTP yokken: uptime tabanlı yaklaşık zaman (oturum süresi için yeterli).
  return 1600000000u + (uint32_t)(millis() / 1000u);
}

constexpr uint32_t kPbkdf2Iterations = 5000;  // ESP32 için dengeli maliyet

}  // namespace

void AuthManager::begin(AppConfig& cfg) {
  cfg_ = &cfg;
  sessions_.clear();
  loginFails_.clear();
  loginFailWindow_.clear();
}

void AuthManager::loop() {
  if (sessions_.empty()) return;
  const uint32_t now = epochNow();
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (it->second.expiresAt < now) {
      it = sessions_.erase(it);
    } else {
      ++it;
    }
  }
  // Eski rate limit pencerelerini temizle (bellek şişmesin).
  for (auto it = loginFailWindow_.begin(); it != loginFailWindow_.end();) {
    if (it->second + kLoginWindowSec < now) {
      loginFails_.erase(it->first);
      it = loginFailWindow_.erase(it);
    } else {
      ++it;
    }
  }
}

bool AuthManager::isEnabled() const {
  // Kilit açık sayılır: auth yapılandırması açık VE şifre tanımlı.
  return cfg_ && cfg_->auth.enabled && !cfg_->auth.passwordHash.isEmpty();
}

bool AuthManager::authenticate(const String& username, const String& password) {
  if (!isEnabled()) return true;  // kilit kapalı: herkes içeri
  if (username != cfg_->auth.username) return false;
  return verifyPassword(password, cfg_->auth.passwordHash);
}

String AuthManager::createSession() {
  if (!cfg_) return "";
  String token = randomHex(16);  // 32 karakterlik rastgele token
  Session s;
  s.expiresAt = epochNow() + (uint32_t)(cfg_->auth.sessionTimeoutMin * 60);
  s.csrf = randomHex(16);
  sessions_[token] = s;
  return token;
}

bool AuthManager::validateSession(const String& token) const {
  if (!isEnabled()) return true;  // kilit kapalıysa her istek geçerli
  if (token.isEmpty()) return false;
  auto it = sessions_.find(token);
  if (it == sessions_.end()) return false;
  return it->second.expiresAt >= epochNow();
}

String AuthManager::getCsrf(const String& token) const {
  auto it = sessions_.find(token);
  return it == sessions_.end() ? String() : it->second.csrf;
}

void AuthManager::invalidate(const String& token) {
  sessions_.erase(token);
}

bool AuthManager::validateCsrf(const String& token, const String& csrf) const {
  if (!isEnabled()) return true;
  String expected = getCsrf(token);
  return !expected.isEmpty() && csrf == expected;
}

String AuthManager::hashPassword(const String& password) {
  String salt = randomHex(8);  // 16 hex karakterlik tuz (8 bayt)
  uint8_t saltBytes[16];
  size_t saltLen = hexToBytes(salt, saltBytes, sizeof(saltBytes));
  uint8_t hash[32];
  String hashHex =
      pbkdf2Sha256(password, saltBytes, saltLen, kPbkdf2Iterations, hash, sizeof(hash));
  return "pbkdf2$" + String(kPbkdf2Iterations) + "$" + salt + "$" + hashHex;
}

bool AuthManager::verifyPassword(const String& password, const String& storedHash) {
  if (storedHash.isEmpty()) return false;
  // Format: pbkdf2$<iterations>$<saltHex>$<hashHex>
  int p1 = storedHash.indexOf('$');
  int p2 = storedHash.indexOf('$', p1 + 1);
  int p3 = storedHash.indexOf('$', p2 + 1);
  if (p1 <= 0 || p2 <= 0 || p3 <= 0) return false;
  String iterStr = storedHash.substring(p1 + 1, p2);
  String salt = storedHash.substring(p2 + 1, p3);
  String expectedHash = storedHash.substring(p3 + 1);
  uint32_t iters = (uint32_t)iterStr.toInt();
  if (iters == 0) return false;

  uint8_t saltBytes[16];
  size_t saltLen = hexToBytes(salt, saltBytes, sizeof(saltBytes));
  uint8_t actual[32];
  String actualHex = pbkdf2Sha256(password, saltBytes, saltLen, iters, actual, sizeof(actual));
  // Zaman-sabit (constant-time) karşılaştırma: uzunluklar eşitse hex karşılaştır.
  if (actualHex.length() != expectedHash.length()) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < actualHex.length(); ++i) {
    diff |= (unsigned)(unsigned char)actualHex[i] ^ (unsigned)(unsigned char)expectedHash[i];
  }
  return diff == 0;
}

bool AuthManager::isLoginAllowed(const String& ip) const {
  if (!isEnabled()) return true;
  auto it = loginFails_.find(ip);
  if (it == loginFails_.end()) return true;
  return it->second < kMaxLoginAttempts;
}

void AuthManager::recordLoginFail(const String& ip) {
  if (!isEnabled()) return;
  const uint32_t now = epochNow();
  auto w = loginFailWindow_.find(ip);
  if (w == loginFailWindow_.end()) {
    loginFailWindow_[ip] = now;
    loginFails_[ip] = 1;
    return;
  }
  if (w->second + kLoginWindowSec < now) {
    // Pencere süresi doldu: sayacı sıfırla
    loginFails_[ip] = 1;
    w->second = now;
  } else {
    loginFails_[ip]++;
  }
}

void AuthManager::resetLoginFails(const String& ip) {
  loginFails_.erase(ip);
  loginFailWindow_.erase(ip);
}
