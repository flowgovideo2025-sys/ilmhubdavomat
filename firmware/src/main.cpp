#include <Arduino.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <time.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include "config.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>

// =====================================================
// ILMHUB SMART ATTENDANCE
// ESP8266 + R503 + OLED + BUZZER + WEB
// NAMANGAN / UZBEKISTAN UTC+5
// =====================================================


// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID_VALUE = WIFI_SSID;
const char* WIFI_PASS = WIFI_PASSWORD;

const char* AP_SSID = "ILMHUB_DAVOMAT";
const char* AP_PASS = "ILMHUB_SETUP_PASSWORD";

ESP8266WebServer server(80);

bool routerConnected = false;


// =====================================================
// DEVICE
// =====================================================

const char* DEVICE_CODE_VALUE = DEVICE_CODE;


// =====================================================
// OLED
// =====================================================

#define OLED_SDA D2
#define OLED_SCL D1

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET
);

bool oledOK = false;


// =====================================================
// R503
//
// SoftwareSerial(RX, TX)
//
// R503 GREEN/TX  -> D5
// R503 YELLOW/RX -> D6
// =====================================================

#define R503_RX D5
#define R503_TX D6

SoftwareSerial fingerSerial(
    R503_RX,
    R503_TX
);

Adafruit_Fingerprint finger(
    &fingerSerial
);

bool sensorOK = false;


// =====================================================
// LED
// =====================================================

#define LED1 D7
#define LED2 D0
#define LED3 D4


// =====================================================
// BUZZER
// =====================================================

#define BUZZER D8


// =====================================================
// EEPROM
// =====================================================

#define EEPROM_SIZE 4096

#define MAGIC_VALUE 0x494C4D48UL

struct Student {

    uint32_t magic;

    uint8_t used;

    uint8_t fingerprintID;

    char firstName[24];

    char lastName[32];

    char group[32];

    uint8_t present;

};

Student students[128];


// =====================================================
// ATTENDANCE
// =====================================================

#define MAX_ATTENDANCE 20

struct Attendance {

    char firstName[24];

    char lastName[32];

    char group[32];

    char date[12];

    char time[12];

    char status[10];

};

Attendance attendance[MAX_ATTENDANCE];

int attendanceCount = 0;


// =====================================================
// STATE
// =====================================================

bool fingerLocked = false;

bool enrolling = false;

String enrollMessage = "";

int enrollingID = 0;

unsigned long lastScan = 0;

unsigned long lastNTPCheck = 0;

unsigned long lastOLED = 0;
unsigned long lastHeartbeat = 0;

void sendHeartbeat();

BearSSL::WiFiClientSecure heartbeatClient;

enum BuzzerPattern {
    BUZZER_IDLE,
    BUZZER_SUCCESS_FIRST,
    BUZZER_SUCCESS_SECOND,
    BUZZER_LEAVE,
    BUZZER_UNKNOWN_FIRST,
    BUZZER_UNKNOWN_SECOND,
    BUZZER_UNKNOWN_THIRD
};

BuzzerPattern buzzerPattern = BUZZER_IDLE;
unsigned long buzzerUntil = 0;

void stopBuzzer() {
    noTone(BUZZER);
    buzzerPattern = BUZZER_IDLE;
    buzzerUntil = 0;
}

void serviceBuzzer() {
    if (buzzerPattern == BUZZER_IDLE || millis() < buzzerUntil) {
        return;
    }

    switch (buzzerPattern) {
        case BUZZER_SUCCESS_FIRST:
            tone(BUZZER, 2400);
            buzzerPattern = BUZZER_SUCCESS_SECOND;
            buzzerUntil = millis() + 130;
            break;
        case BUZZER_SUCCESS_SECOND:
            stopBuzzer();
            break;
        case BUZZER_LEAVE:
            stopBuzzer();
            break;
        case BUZZER_UNKNOWN_FIRST:
            noTone(BUZZER);
            buzzerPattern = BUZZER_UNKNOWN_SECOND;
            buzzerUntil = millis() + 80;
            break;
        case BUZZER_UNKNOWN_SECOND:
            tone(BUZZER, 450);
            buzzerPattern = BUZZER_UNKNOWN_THIRD;
            buzzerUntil = millis() + 100;
            break;
        case BUZZER_UNKNOWN_THIRD:
            stopBuzzer();
            break;
        default:
            stopBuzzer();
            break;
    }
}


// =====================================================
// EEPROM ADDRESS
// =====================================================

int studentAddress(int id) {

    return id * sizeof(Student);

}


// =====================================================
// EEPROM LOAD
// =====================================================

void loadStudents() {

    EEPROM.begin(EEPROM_SIZE);

    for (int i = 0; i < 128; i++) {

        EEPROM.get(
            studentAddress(i),
            students[i]
        );

        if (
            students[i].magic != MAGIC_VALUE
        ) {

            memset(
                &students[i],
                0,
                sizeof(Student)
            );

        }

    }

    Serial.println("EEPROM STUDENTS LOADED");
}


// =====================================================
// EEPROM SAVE ONE
// =====================================================

void saveStudent(int id) {

    if (id < 0 || id > 127) {
        return;
    }

    students[id].magic = MAGIC_VALUE;

    EEPROM.put(
        studentAddress(id),
        students[id]
    );

    EEPROM.commit();
}


// =====================================================
// FIND FREE FINGERPRINT ID
// =====================================================

int findFreeID() {

    for (int i = 1; i < 128; i++) {

        if (!students[i].used) {

            return i;

        }

    }

    return -1;
}


// =====================================================
// FIND STUDENT BY FINGER ID
// =====================================================

Student* findStudent(int id) {

    if (
        id < 1 ||
        id > 127
    ) {

        return nullptr;

    }

    if (
        !students[id].used
    ) {

        return nullptr;

    }

    return &students[id];
}


// =====================================================
// TIME SETUP
// Uzbekistan UTC+5
// =====================================================

void setupTime() {

    configTime(
        5 * 3600,
        0,
        "pool.ntp.org",
        "time.nist.gov",
        "time.google.com"
    );

    Serial.println(
        "NTP TIME STARTED"
    );
}


// =====================================================
// GET DATE
// =====================================================

String getDate() {

    time_t now = time(nullptr);

    if (now < 100000) {

        return "----/--/--";

    }

    struct tm* t =
        localtime(&now);

    char buffer[16];

    snprintf(
        buffer,
        sizeof(buffer),
        "%04d-%02d-%02d",
        t->tm_year + 1900,
        t->tm_mon + 1,
        t->tm_mday
    );

    return String(buffer);
}


// =====================================================
// GET TIME
// =====================================================

String getTime() {

    time_t now = time(nullptr);

    if (now < 100000) {

        return "--:--:--";

    }

    struct tm* t =
        localtime(&now);

    char buffer[16];

    snprintf(
        buffer,
        sizeof(buffer),
        "%02d:%02d:%02d",
        t->tm_hour,
        t->tm_min,
        t->tm_sec
    );

    return String(buffer);
}


// =====================================================
// TIME READY
// =====================================================

bool timeReady() {

    time_t now = time(nullptr);

    return now > 1700000000;
}


// =====================================================
// OLED BASIC
// =====================================================

void oledText(
    String line1,
    String line2 = "",
    String line3 = ""
) {

    if (!oledOK) {
        return;
    }

    display.clearDisplay();

    display.setTextColor(
        SSD1306_WHITE
    );

    display.setTextSize(1);

    display.setCursor(
        0,
        0
    );

    display.println(
        line1
    );

    display.setCursor(
        0,
        20
    );

    display.println(
        line2
    );

    display.setCursor(
        0,
        40
    );

    display.println(
        line3
    );

    display.display();
}


// =====================================================
// OLED READY
// =====================================================

void oledReady() {

    if (!oledOK) {
        return;
    }

    display.clearDisplay();

    display.setTextColor(
        SSD1306_WHITE
    );

    display.setTextSize(2);

    display.setCursor(
        20,
        0
    );

    display.println(
        "ILMHUB"
    );

    display.setTextSize(1);

    display.setCursor(
        0,
        28
    );

    display.println(
        "Barmoqni qoying"
    );

    display.setCursor(
        0,
        45
    );

    display.println(
        "Davomat tayyor"
    );

    display.display();
}


// =====================================================
// OLED SUCCESS
// =====================================================

void oledSuccess(
    String name,
    String status
) {

    if (!oledOK) {
        return;
    }

    display.clearDisplay();

    display.setTextColor(
        SSD1306_WHITE
    );

    display.setTextSize(1);

    display.setCursor(
        0,
        0
    );

    display.println(
        "BARMОQ TANILDI"
    );

    display.setTextSize(2);

    display.setCursor(
        0,
        17
    );

    display.println(
        name
    );

    display.setTextSize(2);

    display.setCursor(
        0,
        45
    );

    display.println(
        status
    );

    display.display();
}


// =====================================================
// OLED UNKNOWN
// =====================================================

void oledUnknown() {

    oledText(
        "UNKNOWN FINGER",
        "Royxatda mavjud emas",
        "Qayta urinib koring"
    );
}


// =====================================================
// BUZZER SUCCESS
// =====================================================

void buzzerSuccess() {
    stopBuzzer();
    tone(BUZZER, 1800);
    buzzerPattern = BUZZER_SUCCESS_FIRST;
    buzzerUntil = millis() + 100;
}


// =====================================================
// BUZZER KETDI
// =====================================================

void buzzerLeave() {
    stopBuzzer();
    tone(BUZZER, 1100);
    buzzerPattern = BUZZER_LEAVE;
    buzzerUntil = millis() + 150;
}


// =====================================================
// BUZZER UNKNOWN
// =====================================================

void buzzerUnknown() {
    stopBuzzer();
    tone(BUZZER, 450);
    buzzerPattern = BUZZER_UNKNOWN_FIRST;
    buzzerUntil = millis() + 100;
}


// =====================================================
// ADD ATTENDANCE
// =====================================================

void addAttendance(
    Student* s,
    const char* status
) {

    if (s == nullptr) {
        return;
    }

    if (
        attendanceCount >= MAX_ATTENDANCE
    ) {

        for (
            int i = 1;
            i < MAX_ATTENDANCE;
            i++
        ) {

            attendance[i - 1] =
                attendance[i];

        }

        attendanceCount =
            MAX_ATTENDANCE - 1;
    }

    memset(
        &attendance[attendanceCount],
        0,
        sizeof(Attendance)
    );

    strncpy(
        attendance[attendanceCount].firstName,
        s->firstName,
        sizeof(attendance[attendanceCount].firstName) - 1
    );

    strncpy(
        attendance[attendanceCount].lastName,
        s->lastName,
        sizeof(attendance[attendanceCount].lastName) - 1
    );

    strncpy(
        attendance[attendanceCount].group,
        s->group,
        sizeof(attendance[attendanceCount].group) - 1
    );

    String date = getDate();

    String tim = getTime();

    strncpy(
        attendance[attendanceCount].date,
        date.c_str(),
        sizeof(attendance[attendanceCount].date) - 1
    );

    strncpy(
        attendance[attendanceCount].time,
        tim.c_str(),
        sizeof(attendance[attendanceCount].time) - 1
    );

    strncpy(
        attendance[attendanceCount].status,
        status,
        sizeof(attendance[attendanceCount].status) - 1
    );

    attendanceCount++;
}


// =====================================================
// HTML ESCAPE
// =====================================================

String esc(String s) {

    s.replace(
        "&",
        "&amp;"
    );

    s.replace(
        "<",
        "&lt;"
    );

    s.replace(
        ">",
        "&gt;"
    );

    s.replace(
        "\"",
        "&quot;"
    );

    s.replace(
        "'",
        "&#39;"
    );

    return s;
}


// =====================================================
// STUDENT CARDS
// =====================================================

String studentCards() {

    String h;

    h.reserve(10000);

    for (int i = 1; i < 128; i++) {

        if (!students[i].used) {
            continue;
        }

        h += "<div class='student'>";

        h += "<div class='studentName'>";

        h += esc(
            String(students[i].firstName) +
            " " +
            String(students[i].lastName)
        );

        h += "</div>";

        h += "<div class='group'>";

        h += esc(
            String(students[i].group)
        );

        h += "</div>";

        h += "<div class='fid'>";

        h += "Fingerprint ID: ";

        h += String(i);

        h += "</div>";

        if (
            students[i].present
        ) {

            h +=
                "<span class='present'>● KELDI</span>";

        } else {

            h +=
                "<span class='absent'>● KETDI</span>";

        }

        h += "</div>";
    }

    return h;
}


// =====================================================
// ATTENDANCE TABLE
// =====================================================

String attendanceTable() {

    String h;

    h.reserve(15000);

    h +=
        "<table>"
        "<thead>"
        "<tr>"
        "<th>#</th>"
        "<th>O'quvchi</th>"
        "<th>Guruh</th>"
        "<th>Sana</th>"
        "<th>Vaqt</th>"
        "<th>Holat</th>"
        "</tr>"
        "</thead>"
        "<tbody>";

    if (
        attendanceCount == 0
    ) {

        h +=
            "<tr>"
            "<td colspan='6' class='empty'>"
            "Hali davomat yo'q"
            "</td>"
            "</tr>";

    } else {

        for (
            int i = attendanceCount - 1;
            i >= 0;
            i--
        ) {

            h += "<tr>";

            h += "<td>";
            h += String(i + 1);
            h += "</td>";

            h += "<td>";

            h += esc(
                String(attendance[i].firstName) +
                " " +
                String(attendance[i].lastName)
            );

            h += "</td>";

            h += "<td>";

            h += esc(
                String(attendance[i].group)
            );

            h += "</td>";

            h += "<td>";

            h += attendance[i].date;

            h += "</td>";

            h += "<td>";

            h += attendance[i].time;

            h += "</td>";

            h += "<td>";

            if (
                String(attendance[i].status) ==
                "KELDI"
            ) {

                h +=
                    "<span class='present'>KELDI</span>";

            } else {

                h +=
                    "<span class='left'>KETDI</span>";

            }

            h += "</td>";

            h += "</tr>";
        }
    }

    h +=
        "</tbody>"
        "</table>";

    return h;
}


// =====================================================
// MAIN WEB PAGE
// NO META REFRESH
// =====================================================

String webPage() {

    String h;

    h.reserve(30000);

    int total = 0;

    int present = 0;

    for (
        int i = 1;
        i < 128;
        i++
    ) {

        if (
            students[i].used
        ) {

            total++;

            if (
                students[i].present
            ) {
                present++;
            }
        }
    }

    h +=
        "<!DOCTYPE html>"
        "<html lang='uz'>"
        "<head>"

        "<meta charset='UTF-8'>"

        "<meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"

        "<title>ILMHUB SMART ATTENDANCE</title>"

        "<style>"

        "*{box-sizing:border-box}"

        "body{"
        "margin:0;"
        "font-family:Arial,sans-serif;"
        "background:#eef2f7;"
        "color:#172033;"
        "}"

        ".header{"
        "background:#111827;"
        "color:white;"
        "padding:25px;"
        "}"

        ".container{"
        "max-width:1200px;"
        "margin:auto;"
        "padding:20px;"
        "}"

        ".header h1{"
        "margin:0;"
        "font-size:28px;"
        "}"

        ".online{"
        "display:inline-block;"
        "margin-top:10px;"
        "padding:7px 14px;"
        "border-radius:20px;"
        "background:#16a34a;"
        "font-weight:bold;"
        "}"

        ".cards{"
        "display:grid;"
        "grid-template-columns:"
        "repeat(auto-fit,minmax(200px,1fr));"
        "gap:15px;"
        "margin-bottom:20px;"
        "}"

        ".stat{"
        "background:white;"
        "border-radius:15px;"
        "padding:20px;"
        "box-shadow:0 3px 12px rgba(0,0,0,.07);"
        "}"

        ".number{"
        "font-size:32px;"
        "font-weight:bold;"
        "}"

        ".section{"
        "background:white;"
        "padding:20px;"
        "border-radius:15px;"
        "margin-bottom:20px;"
        "box-shadow:0 3px 12px rgba(0,0,0,.07);"
        "}"

        "input{"
        "width:100%;"
        "padding:12px;"
        "margin:6px 0 12px;"
        "border:1px solid #d1d5db;"
        "border-radius:9px;"
        "font-size:15px;"
        "}"

        "button{"
        "border:0;"
        "padding:13px 20px;"
        "border-radius:9px;"
        "background:#2563eb;"
        "color:white;"
        "font-weight:bold;"
        "cursor:pointer;"
        "}"

        "button:hover{"
        "opacity:.9"
        "}"

        ".studentGrid{"
        "display:grid;"
        "grid-template-columns:"
        "repeat(auto-fit,minmax(220px,1fr));"
        "gap:12px;"
        "}"

        ".student{"
        "border:1px solid #e5e7eb;"
        "padding:16px;"
        "border-radius:12px;"
        "}"

        ".studentName{"
        "font-size:20px;"
        "font-weight:bold;"
        "}"

        ".group{"
        "margin-top:5px;"
        "color:#4b5563;"
        "}"

        ".fid{"
        "font-size:12px;"
        "color:#6b7280;"
        "margin:8px 0;"
        "}"

        ".present{"
        "color:#16a34a;"
        "font-weight:bold;"
        "}"

        ".absent,.left{"
        "color:#dc2626;"
        "font-weight:bold;"
        "}"

        ".tableWrap{"
        "overflow:auto;"
        "}"

        "table{"
        "width:100%;"
        "border-collapse:collapse;"
        "}"

        "th{"
        "background:#111827;"
        "color:white;"
        "padding:12px;"
        "text-align:left;"
        "}"

        "td{"
        "padding:12px;"
        "border-bottom:1px solid #e5e7eb;"
        "}"

        ".empty{"
        "text-align:center;"
        "padding:30px;"
        "color:#6b7280;"
        "}"

        ".statusBox{"
        "padding:12px;"
        "background:#f3f4f6;"
        "border-radius:10px;"
        "margin-top:10px;"
        "}"

        ".successBox{"
        "background:#dcfce7;"
        "color:#166534;"
        "}"

        ".errorBox{"
        "background:#fee2e2;"
        "color:#991b1b;"
        "}"

        ".time{"
        "font-size:18px;"
        "font-weight:bold;"
        "}"

        "@media(max-width:600px){"
        ".container{padding:10px}"
        ".header{padding:18px}"
        "}"

        "</style>"

        "</head>"

        "<body>";

    // =================================================
    // HEADER
    // =================================================

    h +=
        "<div class='header'>"
        "<div class='container'>"

        "<h1>ILMHUB SMART ATTENDANCE</h1>"

        "<span class='online'>"
        "● TIZIM ISHLAYAPTI"
        "</span>"

        "<p>"
        "ESP8266 + R503 + OLED"
        "</p>"

        "</div>"
        "</div>";

    h +=
        "<div class='container'>";

    // =================================================
    // STATS
    // =================================================

    h +=
        "<div class='cards'>"

        "<div class='stat'>"
        "<div>O'quvchilar</div>"
        "<div class='number'>";

    h += String(total);

    h +=
        "</div>"
        "</div>"

        "<div class='stat'>"
        "<div>Bugun KELDI</div>"
        "<div class='number'>";

    h += String(present);

    h +=
        "</div>"
        "</div>"

        "<div class='stat'>"
        "<div>R503</div>"
        "<div class='number'>";

    h += sensorOK ? "OK" : "ERROR";

    h +=
        "</div>"
        "</div>"

        "<div class='stat'>"
        "<div>Vaqt</div>"
        "<div class='time' id='clock'>";

    h += getDate();
    h += " ";
    h += getTime();

    h +=
        "</div>"
        "</div>"

        "</div>";

    // =================================================
    // REGISTER
    // =================================================

    h +=
        "<div class='section'>"

        "<h2>➕ Yangi o'quvchi ro'yxatdan o'tkazish</h2>"

        "<p>"
        "Ism, familiya va guruhni kiriting. "
        "Keyin barmoqni sensorning ustiga qo'ying."
        "</p>"

        "<form method='POST' action='/register' "
        "onsubmit='startRegister()'>"

        "<label>Ism</label>"

        "<input "
        "id='firstName' "
        "name='firstName' "
        "required "
        "autocomplete='off'>"

        "<label>Familiya</label>"

        "<input "
        "id='lastName' "
        "name='lastName' "
        "required "
        "autocomplete='off'>"

        "<label>Guruh</label>"

        "<input "
        "id='group' "
        "name='group' "
        "required "
        "autocomplete='off'>"

        "<button type='submit'>"
        "BARMОQNI REGISTER QILISH"
        "</button>"

        "</form>"

        "<div id='registerStatus' "
        "class='statusBox'>"
        "Tayyor."
        "</div>"

        "</div>";

    // =================================================
    // STUDENTS
    // =================================================

    h +=
        "<div class='section'>"

        "<h2>👨‍🎓 O'quvchilar</h2>"

        "<div class='studentGrid'>";

    h += studentCards();

    h +=
        "</div>"
        "</div>";

    // =================================================
    // ATTENDANCE
    // =================================================

    h +=
        "<div class='section'>"

        "<h2>📋 Davomat jadvali</h2>"

        "<div class='tableWrap'>";

    h += attendanceTable();

    h +=
        "</div>"
        "</div>";

    // =================================================
    // DEVICE INFO
    // =================================================

    h +=
        "<div class='section'>"

        "<h2>⚙️ Qurilma</h2>"

        "<p><b>Device:</b> ";

    h += DEVICE_CODE_VALUE;

    h +=
        "</p>"

        "<p><b>IP:</b> ";

    if (
        routerConnected
    ) {

        h += WiFi.localIP().toString();

    } else {

        h += "192.168.4.1";

    }

    h +=
        "</p>"

        "<p><b>WiFi:</b> ";

    h += routerConnected ?
        "CONNECTED" :
        "LOCAL AP";

    h +=
        "</p>"

        "<p><b>R503:</b> ";

    h += sensorOK ?
        "READY" :
        "ERROR";

    h +=
        "</p>"

        "<p><b>Time zone:</b> Uzbekistan UTC+5</p>"

        "</div>";

    h +=
        "</div>";

    // =================================================
    // JAVASCRIPT
    // =================================================

    h +=
        "<script>"

        "function startRegister(){"

        "document.getElementById('registerStatus').innerHTML="
        "'⏳ Barmoq register qilinmoqda. Sensorni kuting...';"

        "}"

        "function updateClock(){"

        "fetch('/api/time')"

        ".then(r=>r.text())"

        ".then(t=>{"
        "document.getElementById('clock').innerHTML=t;"
        "})"

        ".catch(()=>{});"

        "}"

        "setInterval(updateClock,1000);"

        "</script>";

    h +=
        "</body>"
        "</html>";

    return h;
}


// =====================================================
// ROOT
// =====================================================

void handleRoot() {

    server.send(
        200,
        "text/html; charset=utf-8",
        webPage()
    );
}


// =====================================================
// TIME API
// =====================================================

void handleTime() {

    String result =
        getDate() +
        " " +
        getTime();

    server.send(
        200,
        "text/plain; charset=utf-8",
        result
    );
}


// =====================================================
// REGISTER MESSAGE PAGE
// =====================================================

void registerResult(
    String message,
    bool success
) {

    String h;

    h +=
        "<!DOCTYPE html>"
        "<html lang='uz'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"
        "<title>ILMHUB Register</title>"
        "<style>"
        "body{font-family:Arial;"
        "background:#eef2f7;"
        "padding:30px}"
        ".box{max-width:600px;"
        "margin:auto;"
        "background:white;"
        "padding:30px;"
        "border-radius:18px;"
        "text-align:center}"
        ".ok{color:#16a34a}"
        ".err{color:#dc2626}"
        "a{display:inline-block;"
        "margin-top:20px;"
        "padding:12px 20px;"
        "background:#2563eb;"
        "color:white;"
        "text-decoration:none;"
        "border-radius:8px}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='box'>";

    if (success) {

        h +=
            "<h1 class='ok'>✓ REGISTER SUCCESS</h1>";

    } else {

        h +=
            "<h1 class='err'>✗ REGISTER XATO</h1>";

    }

    h +=
        "<h2>";

    h += esc(message);

    h +=
        "</h2>"
        "<a href='/'>← Davomatga qaytish</a>"
        "</div>"
        "</body>"
        "</html>";

    server.send(
        200,
        "text/html; charset=utf-8",
        h
    );
}


// =====================================================
// GET FORM VALUE
// =====================================================

String formValue(
    const char* name
) {

    if (
        !server.hasArg(name)
    ) {

        return "";

    }

    String value =
        server.arg(name);

    value.trim();

    return value;
}


// =====================================================
// ENROLL FINGERPRINT
// =====================================================

bool enrollFingerprint(
    int id
) {

    enrolling = true;

    enrollingID = id;

    enrollMessage =
        "Barmoqni sensor ustiga qo'ying...";

    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "FINGERPRINT REGISTER"
    );

    Serial.print(
        "ID: "
    );

    Serial.println(id);

    Serial.println(
        "PUT FINGER"
    );


    // -------------------------------------------------
    // FIRST IMAGE
    // -------------------------------------------------

    int p = -1;

    unsigned long start =
        millis();

    while (
        p != FINGERPRINT_OK
    ) {

        server.handleClient();

        yield();

        p =
            finger.getImage();

        if (
            p == FINGERPRINT_OK
        ) {

            Serial.println(
                "IMAGE 1 OK"
            );

            enrollMessage =
                "✓ 1-barmoq rasmi olindi";

            break;

        }

        if (
            millis() - start > 30000
        ) {

            enrollMessage =
                "Timeout: barmoq topilmadi";

            enrolling = false;

            return false;

        }

        delay(40);
    }


    // -------------------------------------------------
    // IMAGE TO TEMPLATE 1
    // -------------------------------------------------

    p =
        finger.image2Tz(1);

    if (
        p != FINGERPRINT_OK
    ) {

        Serial.print(
            "IMAGE2TZ ERROR: "
        );

        Serial.println(p);

        enrollMessage =
            "Barmoq tasvirini qayta ishlashda xato";

        enrolling = false;

        return false;
    }


    // -------------------------------------------------
    // REMOVE FINGER
    // -------------------------------------------------

    enrollMessage =
        "Barmoqni olib tashlang...";

    Serial.println(
        "REMOVE FINGER"
    );

    delay(700);

    start =
        millis();

    while (true) {

        server.handleClient();

        yield();

        p =
            finger.getImage();

        if (
            p == FINGERPRINT_NOFINGER
        ) {

            break;

        }

        if (
            millis() - start > 10000
        ) {

            enrollMessage =
                "Barmoq olib tashlanmadi";

            enrolling = false;

            return false;

        }

        delay(40);
    }


    // -------------------------------------------------
    // SECOND IMAGE
    // -------------------------------------------------

    enrollMessage =
        "Xuddi shu barmoqni yana qo'ying";

    Serial.println(
        "PUT SAME FINGER"
    );

    p = -1;

    start =
        millis();

    while (
        p != FINGERPRINT_OK
    ) {

        server.handleClient();

        yield();

        p =
            finger.getImage();

        if (
            p == FINGERPRINT_OK
        ) {

            Serial.println(
                "IMAGE 2 OK"
            );

            break;

        }

        if (
            millis() - start > 30000
        ) {

            enrollMessage =
                "Ikkinchi barmoq topilmadi";

            enrolling = false;

            return false;

        }

        delay(40);
    }


    // -------------------------------------------------
    // TEMPLATE 2
    // -------------------------------------------------

    p =
        finger.image2Tz(2);

    if (
        p != FINGERPRINT_OK
    ) {

        Serial.print(
            "IMAGE2TZ(2) ERROR: "
        );

        Serial.println(p);

        enrollMessage =
            "Ikkinchi tasvirda xato";

        enrolling = false;

        return false;
    }


    // -------------------------------------------------
    // CREATE MODEL
    // -------------------------------------------------

    enrollMessage =
        "Fingerprint modeli yaratilmoqda...";

    p =
        finger.createModel();

    if (
        p != FINGERPRINT_OK
    ) {

        if (
            p == FINGERPRINT_ENROLLMISMATCH
        ) {

            enrollMessage =
                "Ikki barmoq mos kelmadi";

        } else {

            enrollMessage =
                "Fingerprint modelida xato";

        }

        enrolling = false;

        return false;
    }


    // -------------------------------------------------
    // STORE MODEL
    // -------------------------------------------------

    p =
        finger.storeModel(id);

    if (
        p != FINGERPRINT_OK
    ) {

        Serial.print(
            "STORE ERROR: "
        );

        Serial.println(p);

        enrollMessage =
            "R503 xotirasiga saqlashda xato";

        enrolling = false;

        return false;
    }


    Serial.println(
        "FINGERPRINT STORED"
    );

    Serial.print(
        "ID: "
    );

    Serial.println(id);

    enrollMessage =
        "✓ Fingerprint saqlandi";

    enrolling = false;

    return true;
}


// =====================================================
// REGISTER HANDLER
// =====================================================

void handleRegister() {

    if (enrolling) {

        registerResult(
            "Hozir boshqa fingerprint register qilinmoqda.",
            false
        );

        return;
    }


    if (!sensorOK) {

        registerResult(
            "R503 sensor tayyor emas.",
            false
        );

        return;
    }


    String firstName =
        formValue("firstName");

    String lastName =
        formValue("lastName");

    String group =
        formValue("group");


    if (
        firstName.length() == 0 ||
        lastName.length() == 0 ||
        group.length() == 0
    ) {

        registerResult(
            "Ism, familiya va guruhni to'liq kiriting.",
            false
        );

        return;
    }


    int id =
        findFreeID();


    if (
        id < 1
    ) {

        registerResult(
            "Bo'sh fingerprint ID qolmagan.",
            false
        );

        return;
    }


    // -------------------------------------------------
    // ENROLL
    // -------------------------------------------------

    bool ok =
        enrollFingerprint(id);


    if (!ok) {

        registerResult(
            enrollMessage,
            false
        );

        return;
    }


    // -------------------------------------------------
    // SAVE STUDENT
    // -------------------------------------------------

    memset(
        &students[id],
        0,
        sizeof(Student)
    );

    students[id].magic =
        MAGIC_VALUE;

    students[id].used =
        1;

    students[id].fingerprintID =
        id;

    students[id].present =
        0;


    strncpy(
        students[id].firstName,
        firstName.c_str(),
        sizeof(students[id].firstName) - 1
    );

    strncpy(
        students[id].lastName,
        lastName.c_str(),
        sizeof(students[id].lastName) - 1
    );

    strncpy(
        students[id].group,
        group.c_str(),
        sizeof(students[id].group) - 1
    );


    saveStudent(id);


    String success =
        "O'quvchi: " +
        firstName +
        " " +
        lastName +
        " | Guruh: " +
        group +
        " | Fingerprint ID: " +
        String(id);


    registerResult(
        success,
        true
    );
}


// =====================================================
// STATUS API
// =====================================================

void handleState() {

    String h;

    h += "{";

    h += "\"sensor\":";

    h += sensorOK ?
        "true" :
        "false";

    h += ",";

    h += "\"enrolling\":";

    h += enrolling ?
        "true" :
        "false";

    h += ",";

    h += "\"message\":\"";

    String msg =
        enrollMessage;

    msg.replace(
        "\"",
        "\\\""
    );

    h += msg;

    h += "\"";

    h += "}";

    server.send(
        200,
        "application/json",
        h
    );
}


// =====================================================
// RESET FINGER LOCK
// =====================================================

void waitFingerRemoved() {

    unsigned long start =
        millis();

    while (
        millis() - start < 5000
    ) {

        server.handleClient();
        serviceBuzzer();

        uint8_t p =
            finger.getImage();

        if (
            p == FINGERPRINT_NOFINGER
        ) {

            fingerLocked = false;

            return;
        }

        delay(30);
    }

    fingerLocked = false;
}


// =====================================================
// ATTENDANCE SCAN
// =====================================================

void scanFingerprint() {

    if (
        !sensorOK ||
        enrolling
    ) {

        return;
    }


    uint8_t p =
        finger.getImage();


    // -------------------------------------------------
    // NO FINGER
    // -------------------------------------------------

    if (
        p == FINGERPRINT_NOFINGER
    ) {

        fingerLocked = false;

        return;
    }


    // -------------------------------------------------
    // PREVENT REPEAT
    // -------------------------------------------------

    if (
        fingerLocked
    ) {

        return;
    }


    // -------------------------------------------------
    // IMAGE ERROR
    // -------------------------------------------------

    if (
        p != FINGERPRINT_OK
    ) {

        return;
    }


    fingerLocked =
        true;

    lastScan =
        millis();


    Serial.println();
    Serial.println(
        "=============================="
    );

    Serial.println(
        "FINGER DETECTED"
    );


    // -------------------------------------------------
    // IMAGE -> TEMPLATE
    // -------------------------------------------------

    p =
        finger.image2Tz();

    if (
        p != FINGERPRINT_OK
    ) {

        Serial.print(
            "IMAGE2TZ ERROR: "
        );

        Serial.println(p);

        oledUnknown();

        buzzerUnknown();

        waitFingerRemoved();

        oledReady();

        return;
    }


    // -------------------------------------------------
    // SEARCH
    // -------------------------------------------------

    p =
        finger.fingerSearch();


    // -------------------------------------------------
    // FOUND
    // -------------------------------------------------

    if (
        p == FINGERPRINT_OK
    ) {

        int id =
            finger.fingerID;

        int confidence =
            finger.confidence;


        Serial.println(
            "=============================="
        );

        Serial.println(
            "FINGER FOUND!"
        );

        Serial.print(
            "ID: "
        );

        Serial.println(id);

        Serial.print(
            "Confidence: "
        );

        Serial.println(confidence);


        Student* s =
            findStudent(id);


        // ------------------------------------------------
        // FINGERPRINT EXISTS BUT STUDENT DOES NOT
        // ------------------------------------------------

        if (
            s == nullptr
        ) {

            Serial.println(
                "ID EXISTS BUT STUDENT NOT FOUND"
            );

            oledUnknown();

            buzzerUnknown();

            waitFingerRemoved();

            oledReady();

            return;
        }


        // ------------------------------------------------
        // KELDI
        // ------------------------------------------------

        if (
            !s->present
        ) {

            s->present =
                1;

            saveStudent(id);


            addAttendance(
                s,
                "KELDI"
            );


            Serial.print(
                "KELDI: "
            );

            Serial.print(
                s->firstName
            );

            Serial.print(
                " "
            );

            Serial.println(
                s->lastName
            );


            oledSuccess(
                String(s->firstName),
                "KELDI"
            );


            buzzerSuccess();

        }


        // ------------------------------------------------
        // KETDI
        // ------------------------------------------------

        else {

            s->present =
                0;

            saveStudent(id);


            addAttendance(
                s,
                "KETDI"
            );


            Serial.print(
                "KETDI: "
            );

            Serial.print(
                s->firstName
            );

            Serial.print(
                " "
            );

            Serial.println(
                s->lastName
            );


            oledSuccess(
                String(s->firstName),
                "KETDI"
            );


            buzzerLeave();

        }


        delay(500);


        // Wait until finger removed.
        waitFingerRemoved();


        oledReady();

        Serial.println(
            "READY"
        );

        return;
    }


    // -------------------------------------------------
    // NOT FOUND
    // -------------------------------------------------

    if (
        p == FINGERPRINT_NOTFOUND
    ) {

        Serial.println(
            "UNKNOWN FINGERPRINT"
        );


        oledUnknown();

        buzzerUnknown();


        waitFingerRemoved();

        oledReady();

        return;
    }


    // -------------------------------------------------
    // SEARCH ERROR
    // -------------------------------------------------

    Serial.print(
        "SEARCH ERROR: "
    );

    Serial.println(p);


    noTone(BUZZER);

    fingerLocked =
        false;
}


// =====================================================
// WIFI
// =====================================================

void connectWiFi() {

    WiFi.mode(
        WIFI_STA
    );

    WiFi.begin(
        WIFI_SSID_VALUE,
        WIFI_PASS
    );


    Serial.println();
    Serial.println(
        "WIFI CONNECTING..."
    );


    int attempts = 0;


    while (
        WiFi.status() != WL_CONNECTED &&
        attempts < 30
    ) {

        delay(500);

        Serial.print(".");

        attempts++;
    }


    Serial.println();


    if (
        WiFi.status() == WL_CONNECTED
    ) {

        routerConnected =
            true;


        Serial.println(
            "WIFI CONNECTED"
        );

        Serial.print(
            "IP: "
        );

        Serial.println(
            WiFi.localIP()
        );


        setupTime();


        return;
    }


    // -------------------------------------------------
    // LOCAL AP
    // -------------------------------------------------

    Serial.println(
        "ROUTER WIFI FAILED"
    );

    Serial.println(
        "STARTING LOCAL AP"
    );


    routerConnected =
        false;


    WiFi.mode(
        WIFI_AP
    );


    WiFi.softAP(
        AP_SSID,
        AP_PASS
    );


    delay(500);


    Serial.println(
        "LOCAL AP READY"
    );

    Serial.print(
        "SSID: "
    );

    Serial.println(
        AP_SSID
    );

    Serial.print(
        "PASSWORD: "
    );

    Serial.println(
        AP_PASS
    );

    Serial.println(
        "IP: 192.168.4.1"
    );


    // Even AP mode can try NTP later
    setupTime();
}


// =====================================================
// SETUP OLED
// =====================================================

void setupOLED() {

    Wire.begin(
        OLED_SDA,
        OLED_SCL
    );


    if (
        display.begin(
            SSD1306_SWITCHCAPVCC,
            0x3C
        )
    ) {

        oledOK =
            true;

        oledText(
            "ILMHUB",
            "Smart Attendance",
            "Starting..."
        );

        Serial.println(
            "OLED OK"
        );

    } else {

        oledOK =
            false;

        Serial.println(
            "OLED ERROR"
        );
    }
}


// =====================================================
// SETUP R503
// =====================================================

void setupFingerprint() {

    Serial.println();
    Serial.println(
        "R503 STARTING..."
    );


    fingerSerial.begin(
        57600
    );


    delay(1000);


    if (
        finger.verifyPassword()
    ) {

        sensorOK =
            true;


        Serial.println(
            "R503 READY!"
        );


        finger.getTemplateCount();


        Serial.print(
            "Templates: "
        );

        Serial.println(
            finger.templateCount
        );

    } else {

        sensorOK =
            false;

        Serial.println(
            "R503 NOT FOUND!"
        );

    }
}


// =====================================================
// WEB ROUTES
// =====================================================

void setupWeb() {

    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );


    server.on(
        "/time",
        HTTP_GET,
        handleTime
    );


    server.on(
        "/api/time",
        HTTP_GET,
        handleTime
    );


    server.on(
        "/api/state",
        HTTP_GET,
        handleState
    );


    server.on(
        "/register",
        HTTP_POST,
        handleRegister
    );


    server.begin();


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "WEB SERVER READY"
    );

    if (
        routerConnected
    ) {

        Serial.print(
            "OPEN: http://"
        );

        Serial.println(
            WiFi.localIP()
        );

    } else {

        Serial.println(
            "OPEN: http://192.168.4.1"
        );
    }

    Serial.println(
        "================================"
    );
}


// =====================================================
// SETUP
// =====================================================

void setup() {

    Serial.begin(
        115200
    );


    delay(1000);


    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "ILMHUB SMART ATTENDANCE"
    );

    Serial.println(
        "ESP8266 + R503"
    );

    Serial.println(
        "========================================"
    );


    // -------------------------------------------------
    // GPIO
    // -------------------------------------------------

    pinMode(
        LED1,
        OUTPUT
    );

    pinMode(
        LED2,
        OUTPUT
    );

    pinMode(
        LED3,
        OUTPUT
    );

    pinMode(
        BUZZER,
        OUTPUT
    );


    digitalWrite(
        LED1,
        LOW
    );

    digitalWrite(
        LED2,
        LOW
    );

    digitalWrite(
        LED3,
        LOW
    );

    noTone(BUZZER);


    // -------------------------------------------------
    // EEPROM
    // -------------------------------------------------

    loadStudents();


    // -------------------------------------------------
    // OLED
    // -------------------------------------------------

    setupOLED();


    // -------------------------------------------------
    // R503
    // -------------------------------------------------

    setupFingerprint();


    // -------------------------------------------------
    // WIFI
    // -------------------------------------------------

    connectWiFi();


    // -------------------------------------------------
    // WEB
    // -------------------------------------------------

    setupWeb();


    // -------------------------------------------------
    // READY
    // -------------------------------------------------

    oledReady();


    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "SYSTEM READY"
    );

    Serial.print(
        "DEVICE: "
    );

    Serial.println(
        DEVICE_CODE_VALUE
    );


    if (
        routerConnected
    ) {

        Serial.print(
            "WEB: http://"
        );

        Serial.println(
            WiFi.localIP()
        );

    } else {

        Serial.println(
            "WEB: http://192.168.4.1"
        );
    }


    Serial.println(
        "========================================"
    );
}


// =====================================================
// LOOP
// =====================================================

void loop() {

    // Web requests FIRST
    server.handleClient();
    serviceBuzzer();

    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
        lastHeartbeat = millis();
        sendHeartbeat();
    }

    yield();


    // NTP retry
    if (
        routerConnected &&
        millis() - lastNTPCheck > 60000
    ) {

        lastNTPCheck =
            millis();

        if (
            !timeReady()
        ) {

            setupTime();
        }
    }


    // Fingerprint
    scanFingerprint();


    yield();
}

void sendHeartbeat() {
    if (!routerConnected || WiFi.status() != WL_CONNECTED) {
        return;
    }

    HTTPClient http;
    String url = String(API_BASE_URL) + "/api/device/heartbeat";
    heartbeatClient.setInsecure();
    heartbeatClient.setBufferSizes(1024, 1024);
    if (!http.begin(heartbeatClient, url)) {
        Serial.println("BACKEND HTTP INIT ERROR");
        return;
    }
    http.useHTTP10(true);
    http.setReuse(false);
    http.setTimeout(60000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + DEVICE_API_KEY);
    http.addHeader("X-Device-Code", DEVICE_CODE_VALUE);
    String body = String("{\"deviceCode\":\"") + DEVICE_CODE_VALUE +
                  "\",\"ipAddress\":\"" + WiFi.localIP().toString() +
                  "\",\"firmwareVersion\":\"1.0.0\"}";
    int status = http.POST(body);
    Serial.print("HEARTBEAT HTTP: ");
    Serial.println(status);
    if (status < 0) {
        Serial.println(http.errorToString(status));
    }
    http.end();
    heartbeatClient.stop();
}