// SmartWalk observation node for AtomS3R-CAM + MLX90640 + SSD1306 OLED.
// - Sends the road surface temperature (thermal ROI mean) to POST /observations every 5 s.
// - Serves camera images and the thermal frame over HTTP for the vision side.
// - The button (or "snap" on the serial monitor) takes a snapshot and sends immediately.

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "esp_camera.h"
#include "img_converters.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MLX90640.h>
#include <math.h>

#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

constexpr char kLocationId[] = "kakuma-slope-01";
constexpr uint32_t kPostIntervalMs = 5000;
constexpr uint32_t kThermalIntervalMs = 1000;
constexpr uint32_t kThermalStaleMs = 5000;
constexpr uint16_t kHttpTimeoutMs = 1500;
constexpr uint8_t kJpegQuality = 80;
constexpr int kCols = 32;
constexpr int kRows = 24;

// AtomS3R-CAM camera (GC0308) pins. GPIO18 LOW powers the camera.
constexpr int kCamPowerPin = 18;
constexpr int kCamXclk = 21, kCamSda = 12, kCamScl = 9;
constexpr int kCamVsync = 10, kCamHref = 14, kCamPclk = 40;
constexpr int kCamData[8] = {3, 42, 46, 48, 4, 17, 11, 13};  // D0..D7

WebServer server(80);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Adafruit_MLX90640 mlx;

float frame[kRows * kCols];
bool cameraReady = false, thermalReady = false, oledReady = false;
uint32_t thermalAt = 0;          // millis() of the last good thermal frame (0 = none)
float roiMean = NAN, roiMin = NAN, roiMax = NAN;
uint8_t* snapJpg = nullptr;      // last snapshot taken with the button
size_t snapLen = 0;
uint32_t snapCount = 0;
uint32_t lastPostAt = 0;
bool postNow = false;
String risk = "-";               // risk from the last API response
String postResult = "-";

// ---------- camera ----------

bool initCamera() {
  pinMode(kCamPowerPin, OUTPUT);
  digitalWrite(kCamPowerPin, LOW);
  delay(500);
  camera_config_t c = {};
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.pin_xclk = kCamXclk;
  c.pin_sccb_sda = kCamSda;
  c.pin_sccb_scl = kCamScl;
  c.pin_d0 = kCamData[0]; c.pin_d1 = kCamData[1]; c.pin_d2 = kCamData[2]; c.pin_d3 = kCamData[3];
  c.pin_d4 = kCamData[4]; c.pin_d5 = kCamData[5]; c.pin_d6 = kCamData[6]; c.pin_d7 = kCamData[7];
  c.pin_vsync = kCamVsync;
  c.pin_href = kCamHref;
  c.pin_pclk = kCamPclk;
  c.xclk_freq_hz = 20000000;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_RGB565;  // GC0308 has no JPEG encoder; convert in software
  c.frame_size = FRAMESIZE_VGA;
  c.jpeg_quality = 12;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  c.sccb_i2c_port = 1;                // Wire (port 0) is used by MLX90640 and the OLED
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

// Takes a fresh picture and returns a malloc'd JPEG (caller frees). nullptr on failure.
uint8_t* captureJpeg(size_t* len) {
  if (!cameraReady) return nullptr;
  camera_fb_t* fb = esp_camera_fb_get();  // drop a possibly old frame
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (!fb) return nullptr;
  uint8_t* out = nullptr;
  bool ok = frame2jpg(fb, kJpegQuality, &out, len);
  esp_camera_fb_return(fb);
  if (!ok) {
    free(out);
    return nullptr;
  }
  return out;
}

// ---------- thermal ----------

void readThermal() {
  if (!thermalReady || mlx.getFrame(frame) != 0) return;
  float sum = 0, lo = INFINITY, hi = -INFINITY;
  int n = 0;
  for (int r = SMARTWALK_ROI_ROW_START; r <= SMARTWALK_ROI_ROW_END; r++) {
    for (int col = SMARTWALK_ROI_COL_START; col <= SMARTWALK_ROI_COL_END; col++) {
      float t = frame[r * kCols + col];
      if (!isfinite(t) || t < -40 || t > 150) continue;
      sum += t;
      lo = min(lo, t);
      hi = max(hi, t);
      n++;
    }
  }
  if (n == 0) return;
  roiMean = sum / n;
  roiMin = lo;
  roiMax = hi;
  thermalAt = millis();
}

float surfaceTemperature() {
  if (thermalAt == 0 || millis() - thermalAt > kThermalStaleMs) return NAN;
  return roiMean;
}

// ---------- API ----------

String jsonString(const String& body, const char* key) {
  String pattern = String("\"") + key + "\"";
  int i = body.indexOf(pattern);
  if (i < 0) return "";
  i = body.indexOf(':', i + pattern.length());
  int start = body.indexOf('"', i);
  if (i < 0 || start < 0) return "";
  int end = body.indexOf('"', start + 1);
  return end < 0 ? "" : body.substring(start + 1, end);
}

void postObservation() {
  lastPostAt = millis();
  if (WiFi.status() != WL_CONNECTED) {
    postResult = "no WiFi";
    return;
  }
  float t = surfaceTemperature();
  String value = isfinite(t) ? String(t, 2) : String("null");
  String body = String("{\"location_id\":\"") + kLocationId +
                "\",\"surface_temperature_c\":" + value +
                ",\"temperature_source\":\"sensor\"}";
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.begin(String(SMARTWALK_API_URL) + "/observations");
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  String response = code > 0 ? http.getString() : "";
  http.end();
  postResult = code == 200 ? "OK" : "ERR " + String(code);
  if (code == 200) risk = jsonString(response, "risk");
  Serial.printf("POST %s -> %d risk=%s\n", body.c_str(), code, risk.c_str());
}

// ---------- HTTP server ----------

void sendJpeg(const uint8_t* jpg, size_t len) {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(len);
  server.send(200, "image/jpeg", "");
  server.client().write(jpg, len);
}

void handleRoot() {
  String ip = WiFi.localIP().toString();
  server.send(200, "text/plain; charset=utf-8",
              "SmartWalk observation node\n"
              "/capture.jpg  fresh camera image\n"
              "/snapshot.jpg last button snapshot\n"
              "/thermal.json thermal frame and ROI\n"
              "IP " + ip + "\n");
}

void handleCapture() {
  size_t len = 0;
  uint8_t* jpg = captureJpeg(&len);
  if (!jpg) {
    server.send(503, "text/plain", "camera unavailable");
    return;
  }
  sendJpeg(jpg, len);
  free(jpg);
}

void handleSnapshot() {
  if (!snapJpg) {
    server.send(404, "text/plain", "no snapshot yet");
    return;
  }
  sendJpeg(snapJpg, snapLen);
}

void handleThermal() {
  float t = surfaceTemperature();
  String s;
  s.reserve(6500);
  s += "{\"cols\":32,\"rows\":24,\"roi\":{\"col_start\":" + String(SMARTWALK_ROI_COL_START) +
       ",\"col_end\":" + String(SMARTWALK_ROI_COL_END) +
       ",\"row_start\":" + String(SMARTWALK_ROI_ROW_START) +
       ",\"row_end\":" + String(SMARTWALK_ROI_ROW_END) + "}";
  s += ",\"surface_temperature_c\":" + (isfinite(t) ? String(t, 2) : String("null"));
  s += ",\"roi_min_c\":" + (isfinite(t) ? String(roiMin, 2) : String("null"));
  s += ",\"roi_max_c\":" + (isfinite(t) ? String(roiMax, 2) : String("null"));
  s += ",\"frame\":[";
  for (int i = 0; i < kRows * kCols; i++) {
    if (i) s += ',';
    s += isfinite(frame[i]) ? String(frame[i], 1) : String("null");
  }
  s += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", s);
}

// ---------- snapshot button ----------

void takeSnapshot() {
  size_t len = 0;
  uint8_t* jpg = captureJpeg(&len);
  if (jpg) {
    free(snapJpg);
    snapJpg = jpg;
    snapLen = len;
    snapCount++;
    Serial.printf("Snapshot #%u (%u bytes)\n", snapCount, (unsigned)len);
  } else {
    Serial.println("Snapshot failed");
  }
  readThermal();
  postNow = true;
}

void checkButton() {
#if SMARTWALK_BUTTON_PIN >= 0
  static bool last = HIGH;
  static uint32_t changedAt = 0;
  bool now = digitalRead(SMARTWALK_BUTTON_PIN);
  if (now != last && millis() - changedAt > 50) {
    changedAt = millis();
    last = now;
    if (now == LOW) takeSnapshot();
  }
#endif
}

void checkSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line == "snap") takeSnapshot();
  else if (line.length()) Serial.println("Commands: snap");
}

// ---------- OLED ----------

void drawOled() {
  if (!oledReady) return;
  float t = surfaceTemperature();
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("RISK");
  oled.setTextSize(2);
  oled.setCursor(0, 10);
  oled.print(risk);
  oled.setTextSize(1);
  oled.setCursor(0, 30);
  if (isfinite(t)) oled.printf("Road %.1fC (%.1f~%.1f)", t, roiMin, roiMax);
  else oled.print("Road --");
  oled.setCursor(0, 41);
  oled.printf("API %s  snap %u", postResult.c_str(), snapCount);
  oled.setCursor(0, 52);
  if (WiFi.status() == WL_CONNECTED) oled.print(WiFi.localIP());
  else oled.print("WiFi connecting");
  oled.display();
}

// ---------- main ----------

void setup() {
  Serial.begin(115200);
  delay(300);
#if SMARTWALK_BUTTON_PIN >= 0
  pinMode(SMARTWALK_BUTTON_PIN, INPUT_PULLUP);
#endif
  Wire.begin(SMARTWALK_I2C_SDA, SMARTWALK_I2C_SCL);
  Wire.setClock(400000);

  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, SMARTWALK_OLED_ADDR);
  if (!oledReady) Serial.println("OLED not found");
  drawOled();

  thermalReady = mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire);
  if (thermalReady) {
    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_18BIT);
    mlx.setRefreshRate(MLX90640_4_HZ);
  } else {
    Serial.println("MLX90640 not found");
  }

  cameraReady = initCamera();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SMARTWALK_WIFI_SSID, SMARTWALK_WIFI_PASSWORD);

  server.on("/", handleRoot);
  server.on("/capture.jpg", handleCapture);
  server.on("/snapshot.jpg", handleSnapshot);
  server.on("/thermal.json", handleThermal);
  server.begin();
}

void loop() {
  static bool wasConnected = false;
  static uint32_t thermalReadAt = 0, oledAt = 0;
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.print("WiFi connected: http://");
    Serial.println(WiFi.localIP());
  }
  wasConnected = connected;

  server.handleClient();
  checkButton();
  checkSerial();

  uint32_t now = millis();
  if (now - thermalReadAt >= kThermalIntervalMs) {
    thermalReadAt = now;
    readThermal();
  }
  if (postNow || now - lastPostAt >= kPostIntervalMs) {
    postNow = false;
    postObservation();
  }
  if (now - oledAt >= 500) {
    oledAt = now;
    drawOled();
  }
}
