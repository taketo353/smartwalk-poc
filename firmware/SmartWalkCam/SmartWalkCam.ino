// SmartWalk observation node for AtomS3R-CAM + MLX90640 + SSD1306 OLED.
// - Sends the road surface temperature (thermal ROI mean) to POST /observations every 5 s.
// - Talks to the API either over USB (through usb_bridge.py on the PC) or over Wi-Fi.
//   In Wi-Fi mode it also serves camera images and the thermal frame over HTTP.
// - Estimates the road surface from light reflection in the camera image and sends the
//   label to POST /vision, so the student and university views show it.
// - The button (or "snap" on the serial monitor) takes a snapshot and sends immediately.

#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

#ifndef SMARTWALK_USE_WIFI
#define SMARTWALK_USE_WIFI 0
#endif
#ifndef SMARTWALK_SEND_VISION
#define SMARTWALK_SEND_VISION 1
#endif

#if SMARTWALK_USE_WIFI
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#endif
#include <Wire.h>
#include "esp_camera.h"
#include "img_converters.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MLX90640.h>
#include <Preferences.h>
#include <math.h>

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

#if SMARTWALK_USE_WIFI
WebServer server(80);
#endif
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

// ---------- reflection features ----------
// Features of the centre of the camera image, used to tell dry / wet / ice / snow apart.
//   brightness : mean luminance (0-1). Wet asphalt is darker, snow is brighter.
//   highlight  : share of near-saturated pixels (0-1). Mirror-like (specular) reflection
//                from a water or ice film shows up as glare.
//   texture    : mean luminance step between neighbouring pixels, relative to brightness.
//                A water or ice film smooths the rough asphalt texture.
//   saturation : mean colour saturation (0-1). Snow is white (low saturation).
constexpr int kFeatureCount = 4;
const char* const kFeatureNames[kFeatureCount] = {"brightness", "highlight", "texture", "saturation"};
const float kFeatureScale[kFeatureCount] = {1.0f, 5.0f, 2.0f, 1.0f};  // weights for distance
const char* const kCalLabels[] = {"dry", "wet", "ice", "snow"};
constexpr int kCalCount = 4;

struct Reflection {
  bool valid = false;
  float f[kFeatureCount] = {0, 0, 0, 0};
};

Reflection reflection;             // features of the latest analysed image
String surfaceLabel = "unknown";   // label sent to POST /vision
String labelMethod = "rule";       // "rule" or "calibrated"
float centroid[kCalCount][kFeatureCount];
bool calibrated[kCalCount] = {false, false, false, false};
Preferences prefs;

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

// Analyses the centre half of an RGB565 frame (every 4th pixel).
Reflection analyseFrame(const camera_fb_t* fb) {
  Reflection r;
  if (!fb || fb->format != PIXFORMAT_RGB565) return r;
  const int w = fb->width, h = fb->height, step = 4;
  const int x0 = w / 4, x1 = w * 3 / 4, y0 = h / 4, y1 = h * 3 / 4;
  const int cols = (x1 - x0) / step;
  static uint8_t prevRow[256];
  double sumY = 0, sumSat = 0, sumGrad = 0;
  long n = 0, highlights = 0, gradients = 0;
  for (int yi = 0, y = y0; y < y1; y += step, yi++) {
    uint8_t left = 0;
    for (int xi = 0, x = x0; x < x1 && xi < 256; x += step, xi++) {
      const uint8_t* p = fb->buf + 2 * (y * w + x);  // big-endian RGB565
      uint8_t rr = p[0] & 0xF8;
      uint8_t gg = ((p[0] & 0x07) << 5) | ((p[1] & 0xE0) >> 3);
      uint8_t bb = (p[1] & 0x1F) << 3;
      uint8_t luma = (77 * rr + 150 * gg + 29 * bb) >> 8;
      uint8_t hi = max(rr, max(gg, bb)), lo = min(rr, min(gg, bb));
      sumY += luma;
      sumSat += hi ? (float)(hi - lo) / hi : 0;
      if (luma >= 235) highlights++;
      if (xi > 0) { sumGrad += abs(luma - left); gradients++; }
      if (yi > 0) { sumGrad += abs(luma - prevRow[xi]); gradients++; }
      prevRow[xi] = luma;
      left = luma;
      n++;
    }
  }
  if (n == 0 || cols <= 0) return r;
  float meanY = sumY / n;
  r.f[0] = meanY / 255.0f;
  r.f[1] = (float)highlights / n;
  r.f[2] = gradients ? (sumGrad / gradients) / max(meanY, 1.0f) : 0;
  r.f[3] = sumSat / n;
  r.valid = true;
  return r;
}

bool anyCalibration() {
  for (int i = 0; i < kCalCount; i++) if (calibrated[i]) return true;
  return false;
}

// Nearest calibrated surface, or simple rules when nothing is calibrated.
String classify(const Reflection& r) {
  if (!r.valid) return "unknown";
  if (anyCalibration()) {
    labelMethod = "calibrated";
    int best = -1;
    float bestD = INFINITY;
    for (int i = 0; i < kCalCount; i++) {
      if (!calibrated[i]) continue;
      float d = 0;
      for (int k = 0; k < kFeatureCount; k++) {
        float diff = (r.f[k] - centroid[i][k]) * kFeatureScale[k];
        d += diff * diff;
      }
      if (d < bestD) { bestD = d; best = i; }
    }
    return kCalLabels[best];
  }
  // Without calibration the image alone cannot separate ice from water, so a shiny or
  // smooth surface is reported as "wet" and the server combines it with the temperature.
  labelMethod = "rule";
  float brightness = r.f[0], highlight = r.f[1], texture = r.f[2], saturation = r.f[3];
  if (brightness > 0.70f && saturation < 0.15f && highlight < 0.30f) return "snow";
  if (highlight >= 0.02f || texture < 0.06f) return "wet";
  return "dry";
}

void loadCalibration() {
  prefs.begin("smartwalk", false);
  for (int i = 0; i < kCalCount; i++) {
    calibrated[i] = prefs.getBytes(kCalLabels[i], centroid[i], sizeof(centroid[i])) == sizeof(centroid[i]);
  }
}

bool calibrate(const String& label) {
  if (label == "clear") {
    for (int i = 0; i < kCalCount; i++) {
      calibrated[i] = false;
      prefs.remove(kCalLabels[i]);
    }
    return true;
  }
  for (int i = 0; i < kCalCount; i++) {
    if (label != kCalLabels[i] || !reflection.valid) continue;
    memcpy(centroid[i], reflection.f, sizeof(centroid[i]));
    calibrated[i] = true;
    prefs.putBytes(kCalLabels[i], centroid[i], sizeof(centroid[i]));
    surfaceLabel = classify(reflection);
    return true;
  }
  return false;
}

// Captures a frame, updates the reflection features and label, and optionally returns a JPEG.
uint8_t* observeImage(size_t* len) {
  if (!cameraReady) return nullptr;
  camera_fb_t* fb = esp_camera_fb_get();  // drop a possibly old frame
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (!fb) return nullptr;
  reflection = analyseFrame(fb);
  surfaceLabel = classify(reflection);
  uint8_t* out = nullptr;
  if (len && !frame2jpg(fb, kJpegQuality, &out, len)) {
    free(out);
    out = nullptr;
  }
  esp_camera_fb_return(fb);
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

// Collects one line from the serial port without blocking. Returns true when a line is ready.
bool readSerialLine(String& out) {
  static String buffer;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      out = buffer;
      buffer = "";
      out.trim();
      return out.length() > 0;
    }
    if (buffer.length() < 512) buffer += c;
  }
  return false;
}

void handleCommand(const String& line);

bool linkUp() {
#if SMARTWALK_USE_WIFI
  return WiFi.status() == WL_CONNECTED;
#else
  return true;
#endif
}

#if SMARTWALK_USE_WIFI
int postJson(const char* path, const String& body, String* response) {
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.begin(String(SMARTWALK_API_URL) + path);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  if (response) *response = code > 0 ? http.getString() : "";
  http.end();
  return code;
}
#else
// USB mode: usb_bridge.py on the PC forwards "@POST <path> <json>" to the API and
// answers with "@RESP <status> <json>". Returns -1 when the bridge does not answer.
int postJson(const char* path, const String& body, String* response) {
  Serial.printf("@POST %s %s\n", path, body.c_str());
  uint32_t start = millis();
  String line;
  while (millis() - start < 2 * kHttpTimeoutMs) {
    if (readSerialLine(line)) {
      if (line.startsWith("@RESP ")) {
        int space = line.indexOf(' ', 6);
        int code = line.substring(6, space < 0 ? line.length() : space).toInt();
        if (response) *response = space < 0 ? "" : line.substring(space + 1);
        return code;
      }
      handleCommand(line);
    }
    delay(1);
  }
  return -1;
}
#endif

void postVision() {
#if SMARTWALK_SEND_VISION
  if (!linkUp()) return;
  observeImage(nullptr);
  String body = String("{\"location_id\":\"") + kLocationId +
                "\",\"image_id\":\"atoms3r-cam-reflection\",\"label\":\"" + surfaceLabel +
                "\",\"vision_source\":\"model\",\"image_source\":\"live\"}";
  int code = postJson("/vision", body, nullptr);
  Serial.printf("POST /vision label=%s (%s) -> %d\n", surfaceLabel.c_str(), labelMethod.c_str(), code);
#endif
}

void postObservation() {
  lastPostAt = millis();
  if (!linkUp()) {
    postResult = "no WiFi";
    return;
  }
  float t = surfaceTemperature();
  String value = isfinite(t) ? String(t, 2) : String("null");
  String body = String("{\"location_id\":\"") + kLocationId +
                "\",\"surface_temperature_c\":" + value +
                ",\"temperature_source\":\"sensor\"}";
  String response;
  int code = postJson("/observations", body, &response);
  if (code == 200) postResult = "OK";
  else if (code < 0) postResult = SMARTWALK_USE_WIFI ? "no API" : "no bridge";
  else postResult = "ERR " + String(code);
  if (code == 200) risk = jsonString(response, "risk");
  Serial.printf("POST %s -> %d risk=%s\n", body.c_str(), code, risk.c_str());
}

// ---------- HTTP server (Wi-Fi mode) ----------

#if SMARTWALK_USE_WIFI
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
              "/reflection.json reflection features and label\n"
              "/calibrate?label=dry|wet|ice|snow|clear\n"
              "IP " + ip + "\n");
}

void handleCapture() {
  size_t len = 0;
  uint8_t* jpg = observeImage(&len);
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
#endif

String thermalJson() {
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
  return s;
}

String reflectionJson() {
  String s = "{\"label\":\"" + surfaceLabel + "\",\"method\":\"" + labelMethod + "\",\"features\":";
  if (reflection.valid) {
    s += "{";
    for (int k = 0; k < kFeatureCount; k++) {
      if (k) s += ",";
      s += String("\"") + kFeatureNames[k] + "\":" + String(reflection.f[k], 4);
    }
    s += "}";
  } else {
    s += "null";
  }
  s += ",\"calibration\":{";
  bool first = true;
  for (int i = 0; i < kCalCount; i++) {
    if (!calibrated[i]) continue;
    if (!first) s += ",";
    first = false;
    s += String("\"") + kCalLabels[i] + "\":[";
    for (int k = 0; k < kFeatureCount; k++) {
      if (k) s += ",";
      s += String(centroid[i][k], 4);
    }
    s += "]";
  }
  s += "}}";
  return s;
}

#if SMARTWALK_USE_WIFI
void handleThermal() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", thermalJson());
}

void handleReflection() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", reflectionJson());
}

// Point the camera at a known surface, then open /calibrate?label=dry (or wet, ice, snow).
void handleCalibrate() {
  observeImage(nullptr);
  if (!calibrate(server.arg("label"))) {
    server.send(400, "text/plain", "use label=dry|wet|ice|snow|clear with the camera working");
    return;
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", reflectionJson());
}
#endif

// ---------- snapshot button ----------

void takeSnapshot() {
  size_t len = 0;
  uint8_t* jpg = observeImage(&len);
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
#if !SMARTWALK_USE_WIFI
  // USB mode: hand the picture and the thermal frame to usb_bridge.py, which saves them.
  if (jpg) {
    Serial.printf("@JPEG %u\n", (unsigned)len);
    Serial.write(jpg, len);
    Serial.println();
  }
  Serial.print("@THERMAL ");
  Serial.println(thermalJson());
#endif
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

void handleCommand(const String& line) {
  if (line == "snap") {
    takeSnapshot();
  } else if (line.startsWith("cal ")) {
    observeImage(nullptr);
    Serial.println(calibrate(line.substring(4)) ? reflectionJson() : "cal failed");
  } else if (line == "info") {
    observeImage(nullptr);
    Serial.println(reflectionJson());
  } else if (line.length()) {
    Serial.println("Commands: snap, info, cal dry|wet|ice|snow|clear");
  }
}

void checkSerial() {
  String line;
  if (readSerialLine(line)) handleCommand(line);
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
  oled.printf("Img %s  API %s", surfaceLabel.c_str(), postResult.c_str());
  oled.setCursor(0, 52);
#if SMARTWALK_USE_WIFI
  if (WiFi.status() == WL_CONNECTED) oled.print(WiFi.localIP());
  else oled.print("WiFi connecting");
#else
  oled.print("USB bridge");
#endif
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
  loadCalibration();

#if SMARTWALK_USE_WIFI
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SMARTWALK_WIFI_SSID, SMARTWALK_WIFI_PASSWORD);

  server.on("/", handleRoot);
  server.on("/capture.jpg", handleCapture);
  server.on("/snapshot.jpg", handleSnapshot);
  server.on("/thermal.json", handleThermal);
  server.on("/reflection.json", handleReflection);
  server.on("/calibrate", handleCalibrate);
  server.begin();
#endif
}

void loop() {
  static uint32_t thermalReadAt = 0, oledAt = 0;
#if SMARTWALK_USE_WIFI
  static bool wasConnected = false;
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.print("WiFi connected: http://");
    Serial.println(WiFi.localIP());
  }
  wasConnected = connected;
  server.handleClient();
#endif
  checkButton();
  checkSerial();

  uint32_t now = millis();
  if (now - thermalReadAt >= kThermalIntervalMs) {
    thermalReadAt = now;
    readThermal();
  }
  if (postNow || now - lastPostAt >= kPostIntervalMs) {
    postNow = false;
    postVision();
    postObservation();
  }
  if (now - oledAt >= 500) {
    oledAt = now;
    drawOled();
  }
}
