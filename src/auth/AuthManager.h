#pragma once

#include <Arduino.h>
#include <map>
#include "config/Config.h"

/**
 * Web paneli kimlik doğrulama (auth) yöneticisi.
 *
 * - PBKDF2-HMAC-SHA256 (tuz + iterasyon) ile şifre doğrulama.
 * - RAM içinde oturum (session) token'ları: token -> {bittiZamanı, csrf}.
 * - Giriş denemesi rate limit (IP başına).
 * - Şifre tanımlı değilse kilit KAPALI: sistem açık (ilk kurulumda
 *   kullanıcı kilidi boş bırakmadan önce şifre belirler).
 */
class AuthManager {
 public:
  void begin(AppConfig& cfg);

  /** Süresi dolan oturumları temizler; loop() içinde her döngüde çağrılmalı. */
  void loop();

  bool isEnabled() const;

  /** Kullanıcı adı/şifre doğruysa true döner (kilit kapalıysa her zaman true). */
  bool authenticate(const String& username, const String& password);

  /** Yeni oturum oluşturur; token değerini döndürür. */
  String createSession();

  /** Geçerli (süresi dolmamış) oturum mu? */
  bool validateSession(const String& token) const;

  /** Oturuma ait CSRF token'ı; geçersizse boş String. */
  String getCsrf(const String& token) const;

  /** Oturumu sonlandırır. */
  void invalidate(const String& token);

  /** CSRF token doğrulaması (POST/PUT/DELETE için). */
  bool validateCsrf(const String& token, const String& csrf) const;

  // ---- Şifre hash yardımcıları ----

  /** Düz metin şifreyi PBKDF2-HMAC-SHA256 formatında döndürür. */
  static String hashPassword(const String& password);

  /** Verilen şifre, kayıtlı hash ile eşleşiyor mu? */
  static bool verifyPassword(const String& password, const String& storedHash);

  // ---- Rate limit ----

  /** IP için giriş denemesi izinli mi? (pencere içinde max deneme) */
  bool isLoginAllowed(const String& ip) const;

  /** IP için başarısız giriş denemesi kaydeder. */
  void recordLoginFail(const String& ip);

  void resetLoginFails(const String& ip);

 private:
  struct Session {
    uint32_t expiresAt;  // Epoch saniye
    String csrf;         // İsteğe bağlı CSRF token'ı
  };

  AppConfig* cfg_ = nullptr;
  std::map<String, Session> sessions_;            // token -> oturum
  std::map<String, uint32_t> loginFails_;         // IP -> başarısız deneme sayısı
  std::map<String, uint32_t> loginFailWindow_;    // IP -> ilk deneme zamanı (epoch)
  uint32_t tokenSeq_ = 0;

  static constexpr uint32_t kMaxLoginAttempts = 5;   // pencere başına
  static constexpr uint32_t kLoginWindowSec = 60;    // 60 saniyelik pencere
};
