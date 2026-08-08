#include "auth/AuthManager.h"

#include <SHA2Builder.h>
#include <HashBuilder.h>
#include <PBKDF2_HMACBuilder.h>
#include <esp_system.h>
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
  String salt = randomHex(8);  // 16 hex karakterlik tuz
  SHA256Builder sha;
  PBKDF2_HMACBuilder pbkdf2(&sha, password, salt, kPbkdf2Iterations);
  pbkdf2.calculate();
  String hash = pbkdf2.toString();
  return "pbkdf2$" + String(kPbkdf2Iterations) + "$" + salt + "$" + hash;
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

  SHA256Builder sha;
  PBKDF2_HMACBuilder pbkdf2(&sha, password, salt, iters);
  pbkdf2.calculate();
  String actualHash = pbkdf2.toString();
  return actualHash == expectedHash;
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
