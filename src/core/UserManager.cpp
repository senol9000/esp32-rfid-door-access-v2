#include "core/UserManager.h"
#include <time.h>
#include "utils/JsonFile.h"

static const char* kUsersPath = "/users.json";
static const unsigned long kSaveIntervalMs = 2000;  // ertelenmiş yazım aralığı

static void userToJsonImpl(const User& u, JsonObject obj) {
  obj["uid"] = u.uid;
  obj["cardName"] = u.cardName;
  obj["fullName"] = u.fullName;
  obj["department"] = u.department;
  obj["phone"] = u.phone;
  obj["note"] = u.note;
  obj["type"] = (u.type == CardType::Admin) ? "admin" : "normal";
  obj["active"] = u.active;
  obj["createdAt"] = u.createdAt;
  obj["lastAccessAt"] = u.lastAccessAt;
  obj["accessCount"] = u.accessCount;
}

void UserManager::toJson(const User& u, JsonObject obj) { userToJsonImpl(u, obj); }

User UserManager::fromJson(JsonObject obj) {
  User u;
  u.uid = obj["uid"] | "";
  u.cardName = obj["cardName"] | "";
  u.fullName = obj["fullName"] | "";
  u.department = obj["department"] | "";
  u.phone = obj["phone"] | "";
  u.note = obj["note"] | "";
  const char* type = obj["type"] | "normal";
  u.type = (strcmp(type, "admin") == 0) ? CardType::Admin : CardType::Normal;
  u.active = obj["active"] | true;
  u.createdAt = obj["createdAt"] | 0u;
  u.lastAccessAt = obj["lastAccessAt"] | 0u;
  u.accessCount = obj["accessCount"] | 0u;
  return u;
}

void UserManager::begin() {
  lastSaveMs_ = millis();
  if (load()) {
    Serial.printf("[USR] %u kullanıcı yüklendi.\n", users_.size());
  } else {
    Serial.println("[USR] Kullanıcı dosyası yok; boş liste ile başlandı.");
  }
}

void UserManager::loop() {
  if (!dirty_) {
    return;
  }
  const unsigned long now = millis();
  if (now - lastSaveMs_ < kSaveIntervalMs) {
    return;
  }
  lastSaveMs_ = now;
  if (save()) {
    dirty_ = false;
  } else {
    Serial.println("[USR] HATA: kullanıcı kaydı yazılamadı.");
  }
}

bool UserManager::load() {
  JsonDocument doc;
  if (!JsonFile::load(kUsersPath, doc)) {
    return false;
  }
  users_.clear();
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    users_.push_back(fromJson(obj));
  }
  return true;
}

bool UserManager::save() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const User& u : users_) {
    JsonObject obj = arr.add<JsonObject>();
    userToJsonImpl(u, obj);
  }
  return JsonFile::saveAtomic(kUsersPath, doc);
}

User* UserManager::find(const String& uid) {
  for (User& u : users_) {
    if (u.uid.equalsIgnoreCase(uid)) {
      return &u;
    }
  }
  return nullptr;
}

bool UserManager::add(const User& user) {
  if (user.uid.isEmpty() || find(user.uid) != nullptr) {
    return false;
  }
  users_.push_back(user);
  dirty_ = false;
  return save();
}

bool UserManager::update(const User& user) {
  for (User& u : users_) {
    if (u.uid.equalsIgnoreCase(user.uid)) {
      u = user;
      dirty_ = false;
      return save();
    }
  }
  return false;
}

bool UserManager::remove(const String& uid) {
  for (auto it = users_.begin(); it != users_.end(); ++it) {
    if (it->uid.equalsIgnoreCase(uid)) {
      users_.erase(it);
      dirty_ = false;
      return save();
    }
  }
  return false;
}

size_t UserManager::countAdmins() const {
  size_t n = 0;
  for (const User& u : users_) {
    if (u.type == CardType::Admin) {
      n++;
    }
  }
  return n;
}

size_t UserManager::countActive() const {
  size_t n = 0;
  for (const User& u : users_) {
    if (u.active) {
      n++;
    }
  }
  return n;
}

void UserManager::recordAccess(User& user, bool allowed) {
  if (allowed) {
    user.accessCount++;
    user.lastAccessAt = (uint32_t)time(nullptr);
  }
  dirty_ = true;
}
