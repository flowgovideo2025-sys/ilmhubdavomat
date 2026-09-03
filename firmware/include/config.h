#pragma once

// ============================================
// ILMHUB SMART ATTENDANCE CONFIG
// ============================================

// Device
#define DEVICE_CODE "ILMHUB-UYCHI-01"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// ============================================
// WiFi
// ============================================

// WiFiManager ishlatiladi.
// Bu qiymatlar faqat kerak bo'lsa fallback sifatida.
// Asosiy WiFi konfiguratsiyasi WiFiManager orqali saqlanadi.

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

// ============================================
// BACKEND SERVER
// ============================================

// Kompyuteringizning LAN IP manzili
#ifndef API_BASE_URL
#define API_BASE_URL "https://ilmhubdavomat.onrender.com"
#endif
#ifndef DEVICE_API_KEY
#define DEVICE_API_KEY "YOUR_DEVICE_API_KEY"
#endif

// ============================================
// R503
// ============================================

// R503 YELLOW/TX -> ESP D6/RX
// R503 GREEN/RX  -> ESP D5/TX

#define FINGERPRINT_RX D6
#define FINGERPRINT_TX D5

// ============================================
// BUZZER
// ============================================

#define BUZZER_PIN D7

// ============================================
// OLED I2C
// ============================================

#define OLED_SDA D2
#define OLED_SCL D1

// ============================================
// WEB SERVER
// ============================================

#define WEB_PORT 80

// ============================================
// TIMERS
// ============================================

#define HEARTBEAT_INTERVAL 10000UL
#define QUEUE_RETRY_INTERVAL 15000UL
#define WIFI_RETRY_INTERVAL 10000UL

// Maximum offline attendance queue
#define MAX_QUEUE_ITEMS 40

// LittleFS queue file
#define QUEUE_FILE "/attendance-queue.json"