// =====================================================================
//  RX-8 (SE3P, 2003-2008) Gösterge Paneli Simülatörü - AYARLAR
//  Bu dosyadaki değerleri kendi donanımına göre değiştir.
// =====================================================================
#pragma once

// ---------------- Kart seçimi ------------------------------------------
// Waveshare ESP32-S3-Touch-LCD-2.1 kullanıyorsan: PlatformIO'da "ws_lcd21"
// ortamı bunu kendisi tanımlar. Arduino IDE'de aşağıdaki satırı aç.
// #define BOARD_WS_TOUCH_LCD_21

// ---------------- Wi-Fi (ESP32 kendi ağını açar) ----------------------
#define WIFI_AP_SSID      "RX8-Cluster"
#define WIFI_AP_PASS      "rx8rotary"      // en az 8 karakter
// Panel adresi: http://192.168.4.1

// ---------------- Pin haritası -----------------------------------------
// Kart otomatik algılanır: ESP32-S3 için ayrı, klasik ESP32 için ayrı pinler.
// -1 yazarsan o çıkış devre dışı kalır.
//
// CAN: ESP32 dahili TWAI + SN65HVD230 (3.3V). TJA1050 kullanırsan 5V ile
// besle ve RXD çıkışını bölücüyle (1k/2k) 3.3V'a düşür.
//
// Kablolu lambalar: "toprağa çekilen" girişler ULN2803 ile sürülür
// (kapı 2E, el freni 2M, fren hidroliği 1D, kemer 2A, uzun far 2D).
// Sinyaller (2G sol, 2I sağ) +12V ister: röle modülü / high-side P-MOSFET.
#if defined(BOARD_WS_TOUCH_LCD_21)
  // Waveshare ESP32-S3-Touch-LCD-2.1: ekran, dokunmatik ve SD kart neredeyse
  // bütün GPIO'ları kullanıyor. CAN, 12PIN SH1.0 soketindeki D-/D+ (GPIO19/20)
  // pinlerinden çıkar; kart UART (CH343) Type-C portundan programlanır.
  // Boş pin kalmadığı için kablolu lamba ve yakıt çıkışı yok.
  #define PIN_CAN_TX        20     // D+  (açılışta yüksek = hat boş)
  #define PIN_CAN_RX        19     // D-
  #define PIN_DOOR          -1
  #define PIN_PARK_BRAKE    -1
  #define PIN_BRAKE_FLUID   -1
  #define PIN_SEATBELT      -1
  #define PIN_HIGH_BEAM     -1
  #define PIN_TURN_LEFT     -1
  #define PIN_TURN_RIGHT    -1
  #define PIN_IGNITION_RELAY -1
  #define PIN_STATUS_LED    -1
  #define PIN_LCD_BL        6      // ekran kullanılmıyor, arka ışık kapalı tutulur
  static const int FUEL_PINS[5] = {-1, -1, -1, -1, -1};
#elif CONFIG_IDF_TARGET_ESP32S3
  // ESP32-S3: YD-ESP32-23 (VCC-GND, N16R8) / DevKitC-1. Bütün pinler J1 sırasında.
  // 19/20 USB, 26-37 flash/PSRAM, 0/3/45/46 strapping: KULLANMA
  #define PIN_CAN_TX        5
  #define PIN_CAN_RX        4
  #define PIN_DOOR          6
  #define PIN_PARK_BRAKE    7
  #define PIN_BRAKE_FLUID   15
  #define PIN_SEATBELT      16
  #define PIN_HIGH_BEAM     17
  #define PIN_TURN_LEFT     18
  #define PIN_TURN_RIGHT    8
  #define PIN_IGNITION_RELAY -1
  #define PIN_STATUS_LED    -1
  #define PIN_STATUS_RGB    48     // WS2812; YD-ESP32-23'te "RGB" lehim köprüsü kapalı olmalı (v1.1 DevKitC-1'de 38)
  static const int FUEL_PINS[5] = {9, 10, 11, 12, 13};      // bit0..bit4
#else
  // Klasik ESP32 (DevKit v1 / WROOM-32)
  #define PIN_CAN_TX        5
  #define PIN_CAN_RX        4
  #define PIN_DOOR          13
  #define PIN_PARK_BRAKE    14
  #define PIN_BRAKE_FLUID   27
  #define PIN_SEATBELT      26
  #define PIN_HIGH_BEAM     25
  #define PIN_TURN_LEFT     32
  #define PIN_TURN_RIGHT    33
  #define PIN_IGNITION_RELAY -1
  #define PIN_STATUS_LED    2
  static const int FUEL_PINS[5] = {16, 17, 18, 19, 21};     // bit0..bit4
#endif

#ifndef PIN_LCD_BL
  #define PIN_LCD_BL        -1
#endif
#ifndef PIN_STATUS_RGB
  #define PIN_STATUS_RGB    -1     // adreslenebilir RGB durum LED'i
#endif

#define TURN_ACTIVE_LEVEL HIGH  // röle modülün LOW tetiklemeliyse LOW yap
#define IGN_ACTIVE_LEVEL  HIGH

// ---------------- Yakıt göstergesi (opsiyonel) -------------------------
// Yakıt şamandırası rezistif; CAN ile değil 2R-2T (ana) ve 2P-2T (yan)
// arasındaki dirençle okunuyor. Seri bağlı 5 direnç, her biri bir
// PhotoMOS röle (AQY212, G3VM-61 vb.) ile kısa devre ediliyor.
// ÖNCE potansiyometre ile elle kalibrasyon yap (README'ye bak).
#define FUEL_LADDER_ENABLED 0
static const float FUEL_RES[5]   = {10, 20, 39, 82, 150};  // ohm, bit sırasına göre
#define FUEL_PHOTOMOS_ON  HIGH
// Kalibrasyon: potla ölçtüğün değerleri yaz
#define FUEL_OHM_EMPTY    280.0f   // ibre E (boş) iken ölçülen direnç
#define FUEL_OHM_FULL      10.0f   // ibre F (dolu) iken ölçülen direnç

#if defined(BOARD_WS_TOUCH_LCD_21) && FUEL_LADDER_ENABLED
  #error "Waveshare LCD 2.1 kartında yakıt merdiveni için boş pin yok"
#endif

// ---------------- CAN kodlama sabitleri --------------------------------
// RPM çarpanı: Kaynaklara göre 3.85 (DaveBlackH) ya da 4.0. Web panelinden
// de değiştirilebilir; takometre doğru göstermiyorsa buradan ayarla.
#define DEFAULT_RPM_FACTOR  3.85f
