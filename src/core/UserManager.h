#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

/** Kart tipi. Admin kartlar her zaman geçer; zaman/tatil kuralları uygulanmaz. */
enum class CardType : uint8_t { Normal = 0, Admin = 1 };

/** Bir kullanıcıyı (kartı) temsil eder. */
struct User {
  String uid;            // Benzersiz kart UID'si (anahtar)
  String cardName;       // Kart üzerindeki ad
  String fullName;       // Ad Soyad
  String department;     // Departman
  String phone;          // Telefon
  String note;           // Not
  CardType type = CardType::Normal;
  bool active = true;    // Aktif/Pasif
  uint32_t createdAt = 0;     // Kart oluşturma zamanı (epoch saniye)
  uint32_t lastAccessAt = 0;  // Son başarılı geçiş zamanı (epoch saniye)
  uint32_t accessCount = 0;   // Toplam başarılı geçiş sayısı
};

/**
 * Kullanıcı yöneticisi.
 *
 * - Sınırsız kullanıcı; LittleFS üzerinde /users.json (JSON dizisi) saklanır.
 * - Yapısal değişiklikler (ekle/sil/güncelle) anında yazılır.
 * - Geçiş sayacı güncellemeleri debounce'lu yazılır (flash aşınmasını azaltır).
 */
class UserManager {
 public:
  void begin();
  void loop();

  /** UID ile kullanıcı arar (büyük/küçük harf duyarsız). */
  User* find(const String& uid);

  /** Yeni kullanıcı ekler. UID boşsa veya zaten varsa false döner. */
  bool add(const User& user);

  /** Mevcut kullanıcıyı günceller. Bulunamazsa false döner. */
  bool update(const User& user);

  /** UID'li kullanıcıyı siler. Bulunamazsa false döner. */
  bool remove(const String& uid);

  const std::vector<User>& all() const { return users_; }
  size_t count() const { return users_.size(); }
  size_t countAdmins() const;
  size_t countActive() const;

  /** Başarılı geçişte sayaç ve son geçiş zamanını günceller (ertelenmiş yazım). */
  void recordAccess(User& user, bool allowed);

  /** Disk'teki dosyayı yeniden okur. */
  bool load();

  /** Tüm kullanıcıları diske yazar. */
  bool save();

  // JSON serileştirme yardımcıları (web API tarafından kullanılır).
  static void toJson(const User& user, JsonObject obj);
  static User fromJson(JsonObject obj);

 private:
  std::vector<User> users_;
  bool dirty_ = false;
  unsigned long lastSaveMs_ = 0;
};
