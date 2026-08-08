#pragma once

#include <ArduinoJson.h>
#include "config/Config.h"
#include "core/UserManager.h"
#include "log/AccessLog.h"
#include "log/EventLog.h"
#include "scheduler/HolidayManager.h"
#include "scheduler/TimeSchedule.h"

/**
 * Yedekleme / geri yükleme modülü.
 *
 * - exportAll: config, kullanıcılar, zaman planları, tatiller ve logları
 *   tek bir JSON belgesinde toplar (web panelinden .json indirme).
 * - restoreAll: aynı biçimdeki JSON'u doğrular ve dosyalara yazar,
 *   ardından yöneticilerin load()'u ile RAM'e geri yükler.
 *
 * Yedek formatı:
 * {
 *   "version": 1,
 *   "exported_at": <epoch>,
 *   "config": { ... AppConfig ... },
 *   "users": [ ... ],
 *   "schedules": [ ... ],
 *   "holidays": [ ... ],
 *   "access_log": [ ... ],
 *   "event_log": [ ... ]
 * }
 */
namespace Backup {

  constexpr int kVersion = 1;

  /** Tüm sistem verisini tek JSON'a doldurur. */
  void exportAll(AppConfig& cfg, UserManager& users, TimeSchedule& schedule,
                 HolidayManager& holidays, AccessLog& accessLog, EventLog& events,
                 JsonDocument& doc);

  /**
   * JSON'dan sistem verisini geri yükler.
   * Başarılıysa true; format/parse hatasında false (hiçbir şey yazılmaz).
   */
  bool restoreAll(AppConfig& cfg, UserManager& users, TimeSchedule& schedule,
                  HolidayManager& holidays, AccessLog& accessLog, EventLog& events,
                  JsonDocument& doc);

}  // namespace Backup
