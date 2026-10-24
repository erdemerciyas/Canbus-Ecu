// =====================================================================
//  Mazda RX-8 (SE3P 2003-2008) Gösterge Paneli Simülatörü - ESP32
//
//  - ESP32 dahili TWAI (CAN) denetleyicisi, 500 kbit/s HS-CAN
//  - PCM (motor beyni) yerine geçip cluster'ın beklediği mesajları yollar:
//      0x201  RPM / hız / gaz pedalı
//      0x420  hararet / yağ basıncı / MIL, şarj, yağ, hararet uyarıları
//      0x212  ABS / DSC / TCS / fren uyarıları
//      0x203 0x215 0x231 0x240 0x620 0x630 0x650 0x300  "her şey yolunda" mesajları
//  - 0x047 immobilizer sorgusuna PCM gibi 0x041 ile cevap verir (araçta lazım)
//  - Kablolu lambalar (sinyal, uzun far, kapı, el freni...) GPIO ile
//  - Opsiyonel: yakıt göstergesi için direnç merdiveni
//  - Wi-Fi web paneli: http://192.168.4.1
//
//  CAN kodlamaları büyük ölçüde DaveBlackH/MazdaRX8Arduino ve
//  xsimulator.net topluluğunun tersine mühendislik çalışmasına dayanır.
//  Araçtan araca küçük farklar olabilir - web panelinden ayarlanabilir.
//
//  Arduino IDE: Kart = "ESP32 Dev Module" veya "ESP32S3 Dev Module"
//  (S3 için Tools > USB CDC On Boot = Enabled önerilir). Core 2.x veya 3.x.
//  Waveshare ESP32-S3-Touch-LCD-2.1: config.h'de BOARD_WS_TOUCH_LCD_21 aç,
//  USB CDC On Boot = Disabled seç, UART Type-C portundan yükle.
//  Harici kütüphane gerekmez.
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "driver/twai.h"
#if CONFIG_IDF_TARGET_ESP32S3
  #include "soc/usb_serial_jtag_reg.h"
#endif
#include "config.h"
#include "web_page.h"

// Log: S3'te USB-CDC açıksa hem USB hem UART0 portuna yaz
#if ARDUINO_USB_CDC_ON_BOOT
  #define LOGF(...)  do { Serial.printf(__VA_ARGS__); Serial0.printf(__VA_ARGS__); } while (0)
#else
  #define LOGF(...)  Serial.printf(__VA_ARGS__)
#endif
#define LOGLN(x) LOGF("%s\n", x)

// ---------------------------------------------------------------------
//  Gösterge durumu (web panelinden / ileride ECU'dan değişir)
// ---------------------------------------------------------------------
struct ClusterState {
  // 0x201 - PCM
  float rpm        = 1000;     // dev/dk
  float speedKmh   = 0;        // km/s
  float throttle   = 0;        // %
  float rpmFactor  = DEFAULT_RPM_FACTOR;

  // 0x420 - MIL / motor durumu
  uint8_t engTemp     = 145;   // ham değer, ~145 = ibre ortada
  uint8_t oilPressure = 1;     // 0 = düşük, >=1 = normal
  bool checkEngine      = false;
  bool checkEngineBlink = false;
  bool lowCoolant       = false;
  bool batteryCharge    = false;
  bool oilWarning       = false;
  bool odoAuto          = false;  // deneysel: hızdan odometre sayacı üret
  float odoTicksPerKm   = 1000;   // deneysel ölçek
  uint8_t odo           = 0;

  // 0x212 - DSC / ABS
  bool dscOff     = false;
  bool absWarn    = false;
  bool brakeWarn  = false;
  bool tcsBlink   = false;   // TCS/DSC lambası yanıp söner (müdahale)
  bool tcsOff     = false;

  // Kablolu girişler
  bool turnLeft   = false;
  bool turnRight  = false;
  bool hazard     = false;
  bool highBeam   = false;
  bool door       = false;
  bool parkBrake  = false;
  bool brakeFluid = false;
  bool seatbelt   = false;
  bool ignition   = true;

  // Yakıt
  float fuelPct   = 50;
  int   fuelOhmOverride = -1;   // >=0 ise ham direnç (kalibrasyon için)

  // Seçenekler
  bool canEnabled    = true;
  bool sendKeepalive = true;
  bool sendDSC       = true;
  bool sendEPS       = true;
  bool immoResponder = true;
  uint8_t abs620b6   = 4;      // 0x620 byte6: araca göre 2,3 veya 4

  // Demo
  uint8_t demoMode = 0;        // 0 kapalı, 1 tek ibre süpürme, 2 sürüş demosu
} S;

// Demo öncesi değerleri saklamak için
static ClusterState saved;
static uint32_t demoStart = 0;

// ---------------------------------------------------------------------
//  CAN istatistikleri ve RX kaydı
// ---------------------------------------------------------------------
struct RxEntry { uint32_t id; uint8_t len; uint8_t data[8]; uint32_t count; uint32_t lastMs; };
static const int RX_MAX = 40;
static RxEntry rxLog[RX_MAX];
static int rxLogN = 0;
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t txOk = 0, txFail = 0, rxTotal = 0, immoReplies = 0;
static volatile bool canDriverOk = false;

WebServer server(80);

// ---------------------------------------------------------------------
//  CAN yardımcıları
// ---------------------------------------------------------------------
static bool canSend(uint32_t id, const uint8_t *data, uint8_t len) {
  if (!canDriverOk) return false;
  twai_message_t m = {};
  m.identifier = id;
  m.extd = 0;
  m.rtr = 0;
  m.data_length_code = len;
  memcpy(m.data, data, len);
  if (twai_transmit(&m, 0) == ESP_OK) { txOk++; return true; }
  txFail++;
  return false;
}

static void logRx(const twai_message_t &m) {
  rxTotal++;
  portENTER_CRITICAL(&rxMux);
  int i = 0;
  for (; i < rxLogN; i++) if (rxLog[i].id == m.identifier) break;
  if (i == rxLogN) {
    if (rxLogN < RX_MAX) rxLogN++;
    else i = RX_MAX - 1;   // dolu: sonuncunun üstüne yaz
    rxLog[i].count = 0;
    LOGF("[CAN RX] yeni ID 0x%03lX\n", (unsigned long)m.identifier);
  }
  rxLog[i].id = m.identifier;
  rxLog[i].len = m.data_length_code;
  memcpy(rxLog[i].data, m.data, 8);
  rxLog[i].count++;
  rxLog[i].lastMs = millis();
  portEXIT_CRITICAL(&rxMux);
}

// ---------------------------------------------------------------------
//  Mesaj içerikleri
// ---------------------------------------------------------------------
// Sabit "PCM burada, her şey yolunda" mesajları (DaveBlackH)
static const uint8_t K203[7] = {19, 19, 19, 19, 175, 3, 19};      // traction control ile ilgili
static const uint8_t K215[8] = {2, 45, 2, 45, 2, 42, 6, 129};
static const uint8_t K231[5] = {15, 0, 255, 255, 0};
static const uint8_t K240[8] = {4, 0, 40, 0, 2, 55, 6, 129};
static const uint8_t K630[8] = {8, 0, 0, 0, 0, 0, 106, 106};      // AT/MT, jant ölçüsü
static const uint8_t K650[1] = {0};
static const uint8_t K300[1] = {0};                               // EPS (direksiyon) lambası söner

// Immobilizer / KCM cevapları
static const uint8_t IMMO_A[8] = {7, 12, 48, 242, 23, 0, 0, 0};
static const uint8_t IMMO_B[8] = {129, 127, 0, 0, 0, 0, 0, 0};

static void send201() {
  uint16_t r = (uint16_t)constrain(S.rpm * S.rpmFactor, 0.0f, 65535.0f);
  uint16_t v = (uint16_t)constrain(S.speedKmh * 100.0f + 10000.0f, 0.0f, 65535.0f);
  uint8_t d[8];
  d[0] = r >> 8;  d[1] = r & 0xFF;
  d[2] = 0xFF;    d[3] = 0xFF;
  d[4] = v >> 8;  d[5] = v & 0xFF;
  d[6] = (uint8_t)constrain(S.throttle * 2.0f, 0.0f, 200.0f);   // %0.5 adım
  d[7] = 0xFF;
  canSend(0x201, d, 8);
}

static void send420() {
  uint8_t d[7] = {0};
  d[0] = S.engTemp;
  d[1] = S.odo;
  d[4] = S.oilPressure;
  if (S.checkEngine)      d[5] |= 0x40;
  if (S.checkEngineBlink) d[5] |= 0x80;
  if (S.lowCoolant)       d[6] |= 0x02;
  if (S.batteryCharge)    d[6] |= 0x40;
  if (S.oilWarning)       d[6] |= 0x80;
  canSend(0x420, d, 7);
}

static void send212() {
  uint8_t d[7] = {0};
  if (S.dscOff)    d[3] |= 0x04;
  if (S.absWarn)   d[4] |= 0x08;
  if (S.brakeWarn) d[4] |= 0x40;
  if (S.tcsBlink)  d[5] |= 0x20;
  if (S.tcsOff)    d[5] |= 0x10;
  canSend(0x212, d, 7);
}

static void sendKeepalives() {
  canSend(0x203, K203, 7);
  canSend(0x215, K215, 8);
  canSend(0x231, K231, 5);
  canSend(0x240, K240, 8);
  uint8_t k620[7] = {0, 0, 0, 0, 16, 0, S.abs620b6};             // ABS lambası için
  canSend(0x620, k620, 7);
  canSend(0x630, K630, 8);
  canSend(0x650, K650, 1);
}

// ---------------------------------------------------------------------
//  FreeRTOS görevleri
// ---------------------------------------------------------------------
static void canTxTask(void *) {
  TickType_t last = xTaskGetTickCount();
  uint32_t tick = 0;
  float odoAcc = 0;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
    tick++;
    if (!S.canEnabled || !S.ignition) continue;

    send201();                                  // 20 ms - ibre akıcı olsun
    if (tick % 5 == 0) {                        // 100 ms
      if (S.odoAuto) {
        odoAcc += S.speedKmh / 3600.0f * 0.1f * S.odoTicksPerKm;  // 0.1 s'de katedilen
        while (odoAcc >= 1.0f) { S.odo++; odoAcc -= 1.0f; }
      }
      send420();
      if (S.sendDSC) send212();
      if (S.sendKeepalive) sendKeepalives();
      if (S.sendEPS) canSend(0x300, K300, 1);
    }
  }
}

static void canRxTask(void *) {
  twai_message_t m;
  for (;;) {
    if (!canDriverOk) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    if (twai_receive(&m, pdMS_TO_TICKS(100)) != ESP_OK) continue;
    logRx(m);
    // Keyless/immobilizer modülü PCM ile konuşmak istiyor (0x047 -> 0x041)
    if (S.immoResponder && m.identifier == 0x047 && m.data_length_code >= 3) {
      if (m.data[1] == 127 && m.data[2] == 2)   { canSend(0x041, IMMO_A, 8); immoReplies++; }
      if (m.data[1] == 92  && m.data[2] == 244) { canSend(0x041, IMMO_B, 8); immoReplies++; }
    }
  }
}

static void canAlertTask(void *) {
  uint32_t alerts;
  for (;;) {
    if (!canDriverOk) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    if (twai_read_alerts(&alerts, pdMS_TO_TICKS(500)) != ESP_OK) continue;
    if (alerts & TWAI_ALERT_ERR_PASS) LOGLN("[CAN] Error-passive (ACK yok? cluster beslemesi / CAN H-L kontrol et)");
    if (alerts & TWAI_ALERT_BUS_OFF) {
      LOGLN("[CAN] BUS-OFF -> kurtarma başlatılıyor");
      twai_initiate_recovery();
    }
    if (alerts & TWAI_ALERT_BUS_RECOVERED) {
      LOGLN("[CAN] Bus kurtarıldı, yeniden başlatılıyor");
      twai_start();
    }
  }
}

static bool canInit() {
#if CONFIG_IDF_TARGET_ESP32S3
  // GPIO19/20 dahili USB PHY'ye bağlı; CAN bu pinlerdeyse USB'yi pinlerden ayır
  if (PIN_CAN_TX == 19 || PIN_CAN_TX == 20 || PIN_CAN_RX == 19 || PIN_CAN_RX == 20)
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
#endif
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
  g.tx_queue_len = 32;
  g.rx_queue_len = 64;
  g.alerts_enabled = TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) { LOGLN("[CAN] driver kurulamadı"); return false; }
  if (twai_start() != ESP_OK) { LOGLN("[CAN] başlatılamadı"); return false; }
  LOGLN("[CAN] 500 kbit/s hazır");
  return true;
}

// ---------------------------------------------------------------------
//  Kablolu çıkışlar ve yakıt merdiveni
// ---------------------------------------------------------------------
static void outPin(int pin, bool on, int activeLevel = HIGH) {
  if (pin < 0) return;
  digitalWrite(pin, on ? activeLevel : !activeLevel);
}

static void initOutputs() {
  const int pins[] = {PIN_DOOR, PIN_PARK_BRAKE, PIN_BRAKE_FLUID, PIN_SEATBELT, PIN_HIGH_BEAM,
                      PIN_TURN_LEFT, PIN_TURN_RIGHT, PIN_IGNITION_RELAY, PIN_STATUS_LED};
  for (int p : pins) if (p >= 0) pinMode(p, OUTPUT);
  if (PIN_LCD_BL >= 0) { pinMode(PIN_LCD_BL, OUTPUT); digitalWrite(PIN_LCD_BL, LOW); }
  outPin(PIN_TURN_LEFT, false, TURN_ACTIVE_LEVEL);
  outPin(PIN_TURN_RIGHT, false, TURN_ACTIVE_LEVEL);
#if FUEL_LADDER_ENABLED
  for (int i = 0; i < 5; i++) pinMode(FUEL_PINS[i], OUTPUT);
#endif
}

static float fuelTargetOhm() {
  if (S.fuelOhmOverride >= 0) return S.fuelOhmOverride;
  float p = constrain(S.fuelPct, 0.0f, 100.0f) / 100.0f;
  return FUEL_OHM_EMPTY + (FUEL_OHM_FULL - FUEL_OHM_EMPTY) * p;
}

// Seri dirençlerden hangileri kalsın? (en yakın toplamı seç)
static uint8_t fuelCode = 0;
static float fuelActualOhm = 0;
static void updateFuel() {
  float target = fuelTargetOhm();
  float bestErr = 1e9; uint8_t best = 0;
  for (uint8_t c = 0; c < 32; c++) {          // c bit=1 -> direnç devrede (PhotoMOS açık değil)
    float r = 0;
    for (int i = 0; i < 5; i++) if (c & (1 << i)) r += FUEL_RES[i];
    float e = fabsf(r - target);
    if (e < bestErr) { bestErr = e; best = c; fuelActualOhm = r; }
  }
  fuelCode = best;
#if FUEL_LADDER_ENABLED
  for (int i = 0; i < 5; i++) {
    bool bypass = !(best & (1 << i));           // direnç devre dışı -> PhotoMOS kısa devre
    digitalWrite(FUEL_PINS[i], bypass ? FUEL_PHOTOMOS_ON : !FUEL_PHOTOMOS_ON);
  }
#endif
}

static void updateOutputs() {
  bool blinkOn = (millis() / 350) % 2 == 0;     // ~1.4 Hz flaşör
  bool l = (S.turnLeft || S.hazard) && blinkOn;
  bool r = (S.turnRight || S.hazard) && blinkOn;
  outPin(PIN_TURN_LEFT,  l, TURN_ACTIVE_LEVEL);
  outPin(PIN_TURN_RIGHT, r, TURN_ACTIVE_LEVEL);
  outPin(PIN_HIGH_BEAM,  S.highBeam);
  outPin(PIN_DOOR,       S.door);
  outPin(PIN_PARK_BRAKE, S.parkBrake);
  outPin(PIN_BRAKE_FLUID,S.brakeFluid);
  outPin(PIN_SEATBELT,   S.seatbelt);
  outPin(PIN_IGNITION_RELAY, S.ignition, IGN_ACTIVE_LEVEL);
  updateFuel();

  // Durum LED'i: CAN sağlıklıysa yavaş, sorun varsa hızlı yanıp söner
  twai_status_info_t st;
  bool healthy = canDriverOk && twai_get_status_info(&st) == ESP_OK &&
                 st.state == TWAI_STATE_RUNNING && st.tx_error_counter < 96;
  bool ledOn = (millis() / (healthy ? 1000 : 150)) % 2;
  outPin(PIN_STATUS_LED, ledOn);
#if PIN_STATUS_RGB >= 0
  // RGB: sağlıklıysa yeşil yavaş, sorun varsa kırmızı hızlı yanıp söner
  static int lastRgb = -1;
  int rgb = ledOn ? (healthy ? 1 : 2) : 0;
  if (rgb != lastRgb) {
    neopixelWrite(PIN_STATUS_RGB, rgb == 2 ? 20 : 0, rgb == 1 ? 20 : 0, 0);
    lastRgb = rgb;
  }
#endif
}

// ---------------------------------------------------------------------
//  Demo / gösteri modları
//  1 ibre süpürme  2 sürüş  3 hoşgeldin  4 hoşçakal  5 ışık şovu
//  6 gazlama (devir kesici)  7 soğuk çalıştırma
// ---------------------------------------------------------------------
// Animasyonlarda sırayla yakılan sabit lambalar
static bool ClusterState::*const LAMPS[] = {
  &ClusterState::checkEngine, &ClusterState::batteryCharge, &ClusterState::oilWarning,
  &ClusterState::lowCoolant,  &ClusterState::absWarn,       &ClusterState::brakeWarn,
  &ClusterState::dscOff,      &ClusterState::tcsOff,        &ClusterState::highBeam,
  &ClusterState::door,        &ClusterState::parkBrake,     &ClusterState::seatbelt,
  &ClusterState::brakeFluid};
static const int N_LAMPS = sizeof(LAMPS) / sizeof(LAMPS[0]);
static bool ClusterState::*const BLINKERS[] = {
  &ClusterState::checkEngineBlink, &ClusterState::tcsBlink,
  &ClusterState::turnLeft, &ClusterState::turnRight, &ClusterState::hazard};

static bool demoUsesLamps(uint8_t m) { return m >= 3 && m != 6; }
static float seg(uint32_t t, uint32_t a, uint32_t b) { return t <= a ? 0 : t >= b ? 1 : (float)(t - a) / (b - a); }
static float ease(float x) { return x * x * (3 - 2 * x); }
static float lerp(float a, float b, float k) { return a + (b - a) * k; }

// k: 0 = tüm ibreler dipte, 1 = tepede
static void setGauges(float k) {
  S.rpm = 9000 * k; S.speedKmh = 280 * k; S.throttle = 100 * k;
  S.fuelPct = 100 * k; S.engTemp = 60 + (uint8_t)(170 * k);
}

static void setAllLamps(bool on) {
  for (auto l : LAMPS) S.*l = on;
  for (auto l : BLINKERS) S.*l = false;
}

static void stopDemo() {
  if (S.demoMode == 0) return;
  // Sadece demo'nun değiştirdiği alanları geri yükle
  S.rpm = saved.rpm; S.speedKmh = saved.speedKmh; S.throttle = saved.throttle;
  S.engTemp = saved.engTemp; S.fuelPct = saved.fuelPct;
  if (demoUsesLamps(S.demoMode)) {
    for (auto l : LAMPS) S.*l = saved.*l;
    for (auto l : BLINKERS) S.*l = saved.*l;
  }
  S.demoMode = 0;
}

static void startDemo(uint8_t mode) {
  stopDemo();
  saved = S;
  if ((mode == 3 || mode == 7) && saved.rpm < 600) saved.rpm = 850;   // motor çalışmış olarak biter
  S.demoMode = mode;
  demoStart = millis();
  if (demoUsesLamps(mode)) setAllLamps(false);
}

static void runDemo() {
  uint32_t t = millis() - demoStart;
  if (S.demoMode == 1) {                         // ibre süpürme: 1.5 s yukarı, 1.5 s aşağı
    setGauges(t < 1500 ? t / 1500.0f : (t < 3000 ? (3000 - t) / 1500.0f : 0));
    if (t >= 3200) stopDemo();
  } else if (S.demoMode == 3) {                  // hoşgeldin: lamba testi, süpürme, dörtlü, marş
    for (int i = 0; i < N_LAMPS; i++)            // önce hepsi yanar, ibreler kalkarken sırayla söner
      S.*LAMPS[i] = t < (uint32_t)(700 + i * 1500 / N_LAMPS);
    S.hazard = t >= 2200 && t < 3600;
    if (t < 4500) setGauges(ease(seg(t, 700, 2200)) * (1 - ease(seg(t, 2700, 4500))));
    else {                                       // marş: devir parlar, ibreler son değerlere oturur
      float k = ease(seg(t, 4500, 6000));
      S.rpm = saved.rpm * k + 1500 * sinf(PI * k);
      S.speedKmh = saved.speedKmh * k; S.throttle = saved.throttle * k;
      S.fuelPct = saved.fuelPct * k; S.engTemp = (uint8_t)lerp(60, saved.engTemp, k);
    }
    if (t >= 6000) stopDemo();
  } else if (S.demoMode == 4) {                  // hoşçakal: ibreler iner, dörtlü, lambalar göz kırpar
    float k = 1 - ease(seg(t, 0, 2500));
    S.rpm = saved.rpm * k; S.speedKmh = saved.speedKmh * k; S.throttle = saved.throttle * k;
    S.hazard = t < 1400;
    bool flash = (t >= 3300 && t < 3500) || (t >= 3700 && t < 3900);
    for (auto l : LAMPS) S.*l = flash;
    if (t >= 2500 && t < 3300) S.batteryCharge = S.oilWarning = true;   // motor durdu, kontak açık
    if (t >= 4200) { stopDemo(); S.rpm = S.speedKmh = S.throttle = 0; }
  } else if (S.demoMode == 5) {                  // ışık şovu: 3 desen döner, ibreler dalgalanır
    uint32_t c = t % 12000;
    for (int i = 0; i < N_LAMPS; i++) {
      bool on;
      if (c < 4000)      on = i == (int)(t / 110 % N_LAMPS);             // kayan ışık
      else if (c < 8000) on = (i & 1) == (int)(t / 250 % 2);             // tek/çift flaş
      else { int n = t / 90 % (2 * N_LAMPS); on = i < (n < N_LAMPS ? n : 2 * N_LAMPS - n); }  // dolan bar
      S.*LAMPS[i] = on;
    }
    S.turnLeft = t / 1400 % 2 == 0; S.turnRight = !S.turnLeft;
    float w = t / 1000.0f * PI;                  // 2 s periyot
    S.rpm = 4500 + 4000 * sinf(w);
    S.speedKmh = 140 - 130 * sinf(w);
    S.throttle = S.rpm / 90;
    S.engTemp = (uint8_t)(145 + 60 * sinf(w / 1.5f));
    S.fuelPct = 50 + 50 * sinf(w / 2);
  } else if (S.demoMode == 6) {                  // gazlama: rölanti -> devir kesici -> rölanti, 5 s döngü
    uint32_t c = t % 5000;
    float idle = 850 + 25 * sinf(t / 90.0f);
    S.speedKmh = 0;
    if (c < 1000)      { S.rpm = idle; S.throttle = 0; }
    else if (c < 1700) { S.rpm = lerp(idle, 8700, ease(seg(c, 1000, 1700))); S.throttle = 100; }
    else if (c < 3000) { S.rpm = c / 60 % 2 ? 9000 : 8600; S.throttle = 100; }
    else               { S.rpm = lerp(8700, idle, ease(seg(c, 3000, 4200))); S.throttle = 0; }
  } else if (S.demoMode == 7) {                  // soğuk çalıştırma: marş, yüksek rölanti, ısınma
    S.speedKmh = 0; S.throttle = 0;
    S.batteryCharge = S.oilWarning = t < 1500;
    if (t < 1200) S.rpm = 250 + 80 * sinf(t / 25.0f);                     // marş basıyor
    else S.rpm = lerp(1800, saved.rpm, ease(seg(t, 1200, 15000))) + 600 * expf(-(t - 1200) / 250.0f);
    S.engTemp = (uint8_t)lerp(60, saved.engTemp, ease(seg(t, 1200, 15000)));
    if (t >= 15000) stopDemo();
  } else if (S.demoMode == 2) {                  // basit sürüş: vites atarak hızlan, sonra yavaşla
    // 6 ileri manuel, her viteste 1000 dev/dk başına yaklaşık km/s
    static const float kmhPer1000[6] = {9.4f, 15.6f, 21.5f, 29.8f, 35.5f, 42.3f};
    static float v = 0;
    static uint32_t lastMs = 0, lastStart = 0;
    if (lastStart != demoStart) { lastStart = demoStart; v = 0; lastMs = 0; }
    uint32_t now = millis();
    float dt = lastMs ? (now - lastMs) / 1000.0f : 0; lastMs = now;
    float cycle = (t % 40000) / 1000.0f;         // 40 s döngü: 25 s gaz, 15 s fren
    if (cycle < 25) { S.throttle = 85; v += (12.0f - v * 0.045f) * dt; }
    else            { S.throttle = 0;  v -= 14.0f * dt; }
    v = constrain(v, 0.0f, 250.0f);
    int gear = 0;
    while (gear < 5 && v / kmhPer1000[gear] * 1000.0f > 8000.0f) gear++;
    float rpm = v / kmhPer1000[gear] * 1000.0f;
    S.speedKmh = v;
    S.rpm = max(rpm, 850.0f);
    S.engTemp = 145;
  }
}

// ---------------------------------------------------------------------
//  Web sunucusu
// ---------------------------------------------------------------------
static bool argBool(const String &v) { return v == "1" || v == "true" || v == "on"; }

static void applyParam(const String &k, const String &v) {
  float f = v.toFloat();
  bool b = argBool(v);
  if      (k == "rpm")        S.rpm = constrain(f, 0, 12000);
  else if (k == "speed")      S.speedKmh = constrain(f, 0, 320);
  else if (k == "throttle")   S.throttle = constrain(f, 0, 100);
  else if (k == "rpmFactor")  S.rpmFactor = constrain(f, 1, 10);
  else if (k == "temp")       S.engTemp = constrain((int)f, 0, 255);
  else if (k == "oil")        S.oilPressure = constrain((int)f, 0, 255);
  else if (k == "fuel")       { S.fuelPct = constrain(f, 0, 100); S.fuelOhmOverride = -1; }
  else if (k == "fuelOhm")    S.fuelOhmOverride = (int)f;
  else if (k == "odo")        S.odo = (uint8_t)f;
  else if (k == "odoAuto")    S.odoAuto = b;
  else if (k == "odoTicks")   S.odoTicksPerKm = constrain(f, 1, 100000);
  else if (k == "checkEngine")S.checkEngine = b;
  else if (k == "ceBlink")    S.checkEngineBlink = b;
  else if (k == "lowCoolant") S.lowCoolant = b;
  else if (k == "charge")     S.batteryCharge = b;
  else if (k == "oilWarn")    S.oilWarning = b;
  else if (k == "dscOff")     S.dscOff = b;
  else if (k == "abs")        S.absWarn = b;
  else if (k == "brakeWarn")  S.brakeWarn = b;
  else if (k == "tcsBlink")   S.tcsBlink = b;
  else if (k == "tcsOff")     S.tcsOff = b;
  else if (k == "turnL")      S.turnLeft = b;
  else if (k == "turnR")      S.turnRight = b;
  else if (k == "hazard")     S.hazard = b;
  else if (k == "highBeam")   S.highBeam = b;
  else if (k == "door")       S.door = b;
  else if (k == "parkBrake")  S.parkBrake = b;
  else if (k == "brakeFluid") S.brakeFluid = b;
  else if (k == "seatbelt")   S.seatbelt = b;
  else if (k == "ignition")   { if (b && !S.ignition) startDemo(3); S.ignition = b; }   // kontak açılınca hoşgeldin
  else if (k == "canEnabled") S.canEnabled = b;
  else if (k == "keepalive")  S.sendKeepalive = b;
  else if (k == "sendDSC")    S.sendDSC = b;
  else if (k == "sendEPS")    S.sendEPS = b;
  else if (k == "immo")       S.immoResponder = b;
  else if (k == "abs620")     S.abs620b6 = (uint8_t)f;
  else if (k == "demo")       { if ((int)f == 0) stopDemo(); else startDemo((uint8_t)f); }
  else if (k == "lampTest") {
    S.checkEngine = S.lowCoolant = S.batteryCharge = S.oilWarning = b;
    S.dscOff = S.absWarn = S.brakeWarn = S.tcsOff = b;
    S.highBeam = S.door = S.parkBrake = S.hazard = S.seatbelt = b;
  }
  else if (k == "reset") {
    ClusterState d; d.rpmFactor = S.rpmFactor; S = d;
  }
}

static void handleSet() {
  for (int i = 0; i < server.args(); i++) applyParam(server.argName(i), server.arg(i));
  server.send(200, "text/plain", "ok");
}

static const char *twaiStateName(twai_state_t s) {
  switch (s) {
    case TWAI_STATE_STOPPED:    return "DURDU";
    case TWAI_STATE_RUNNING:    return "CALISIYOR";
    case TWAI_STATE_BUS_OFF:    return "BUS-OFF";
    case TWAI_STATE_RECOVERING: return "KURTARILIYOR";
    default: return "?";
  }
}

static void handleState() {
  twai_status_info_t st = {};
  bool ok = canDriverOk && twai_get_status_info(&st) == ESP_OK;
  String j; j.reserve(1200);
  j += "{";
  j += "\"rpm\":" + String(S.rpm, 0);
  j += ",\"speed\":" + String(S.speedKmh, 1);
  j += ",\"throttle\":" + String(S.throttle, 0);
  j += ",\"rpmFactor\":" + String(S.rpmFactor, 3);
  j += ",\"temp\":" + String(S.engTemp);
  j += ",\"oil\":" + String(S.oilPressure);
  j += ",\"fuel\":" + String(S.fuelPct, 0);
  j += ",\"fuelOhm\":" + String(S.fuelOhmOverride);
  j += ",\"fuelActual\":" + String(fuelActualOhm, 0);
  j += ",\"fuelLadder\":" + String(FUEL_LADDER_ENABLED);
  j += ",\"gpioLamps\":" + String(PIN_TURN_LEFT >= 0 || PIN_DOOR >= 0 || PIN_HIGH_BEAM >= 0);
  j += ",\"odo\":" + String(S.odo);
  j += ",\"odoAuto\":" + String(S.odoAuto);
  j += ",\"odoTicks\":" + String(S.odoTicksPerKm, 0);
  j += ",\"checkEngine\":" + String(S.checkEngine);
  j += ",\"ceBlink\":" + String(S.checkEngineBlink);
  j += ",\"lowCoolant\":" + String(S.lowCoolant);
  j += ",\"charge\":" + String(S.batteryCharge);
  j += ",\"oilWarn\":" + String(S.oilWarning);
  j += ",\"dscOff\":" + String(S.dscOff);
  j += ",\"abs\":" + String(S.absWarn);
  j += ",\"brakeWarn\":" + String(S.brakeWarn);
  j += ",\"tcsBlink\":" + String(S.tcsBlink);
  j += ",\"tcsOff\":" + String(S.tcsOff);
  j += ",\"turnL\":" + String(S.turnLeft);
  j += ",\"turnR\":" + String(S.turnRight);
  j += ",\"hazard\":" + String(S.hazard);
  j += ",\"highBeam\":" + String(S.highBeam);
  j += ",\"door\":" + String(S.door);
  j += ",\"parkBrake\":" + String(S.parkBrake);
  j += ",\"brakeFluid\":" + String(S.brakeFluid);
  j += ",\"seatbelt\":" + String(S.seatbelt);
  j += ",\"ignition\":" + String(S.ignition);
  j += ",\"canEnabled\":" + String(S.canEnabled);
  j += ",\"keepalive\":" + String(S.sendKeepalive);
  j += ",\"sendDSC\":" + String(S.sendDSC);
  j += ",\"sendEPS\":" + String(S.sendEPS);
  j += ",\"immo\":" + String(S.immoResponder);
  j += ",\"abs620\":" + String(S.abs620b6);
  j += ",\"demo\":" + String(S.demoMode);
  j += ",\"can\":{";
  j += "\"state\":\"" + String(ok ? twaiStateName(st.state) : "YOK") + "\"";
  j += ",\"txErr\":" + String(ok ? st.tx_error_counter : 0);
  j += ",\"rxErr\":" + String(ok ? st.rx_error_counter : 0);
  j += ",\"busErr\":" + String(ok ? st.bus_error_count : 0);
  j += ",\"txQueue\":" + String(ok ? st.msgs_to_tx : 0);
  j += ",\"txOk\":" + String(txOk);
  j += ",\"txFail\":" + String(txFail);
  j += ",\"rx\":" + String(rxTotal);
  j += ",\"immo\":" + String(immoReplies);
  j += "}}";
  server.send(200, "application/json", j);
}

static void handleCanLog() {
  String j = "[";
  portENTER_CRITICAL(&rxMux);
  RxEntry copy[RX_MAX]; int n = rxLogN;
  memcpy(copy, rxLog, sizeof(RxEntry) * n);
  portEXIT_CRITICAL(&rxMux);
  uint32_t now = millis();
  for (int i = 0; i < n; i++) {
    char buf[96];
    char hex[25] = {0};
    for (int b = 0; b < copy[i].len; b++) sprintf(hex + b * 3, "%02X ", copy[i].data[b]);
    snprintf(buf, sizeof(buf), "%s{\"id\":\"%03lX\",\"d\":\"%s\",\"n\":%lu,\"age\":%lu}",
             i ? "," : "", (unsigned long)copy[i].id, hex,
             (unsigned long)copy[i].count, (unsigned long)(now - copy[i].lastMs));
    j += buf;
  }
  j += "]";
  server.send(200, "application/json", j);
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(115200);
#endif
  delay(200);
  LOGLN("\n=== RX-8 Cluster Simulator ===");

  initOutputs();
  canDriverOk = canInit();

  xTaskCreatePinnedToCore(canTxTask,    "canTx",    4096, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(canRxTask,    "canRx",    4096, nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(canAlertTask, "canAlert", 3072, nullptr, 3, nullptr, 1);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  LOGF("Wi-Fi: %s  ->  http://%s\n", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/set", handleSet);
  server.on("/state", handleState);
  server.on("/can", handleCanLog);
  server.begin();

  startDemo(3);   // açılışta hoşgeldin animasyonu
}

void loop() {
  server.handleClient();
  if (S.demoMode) runDemo();
  updateOutputs();

  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    twai_status_info_t st = {};
    if (canDriverOk) twai_get_status_info(&st);
    LOGF("[durum] rpm=%.0f hiz=%.0f temp=%u | CAN %s txOk=%lu txErr=%lu rx=%lu\n",
                  S.rpm, S.speedKmh, S.engTemp,
                  canDriverOk ? twaiStateName(st.state) : "YOK",
                  (unsigned long)txOk, (unsigned long)st.tx_error_counter, (unsigned long)rxTotal);
  }
  delay(2);
}
