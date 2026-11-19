#include <Arduino.h>
#include <HTTPClient.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <cJSON.h>
#include <esp32-hal-rgb-led.h>
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

enum class Risk { Low, Caution, High, Unknown };

struct RiskUpdate {
  Risk risk;
  uint32_t receivedAt;
};

struct DemoInput {
  bool paused;
  bool sensorMode;
  float temperatureC;
};

constexpr uint32_t kPollIntervalMs = 5000;
constexpr uint32_t kStaleAfterMs = 20000;
constexpr uint16_t kHttpTimeoutMs = 1500;
constexpr char kLocationId[] = "kakuma-slope-01";

M5GFX display;
QueueHandle_t demoQueue;
QueueHandle_t riskQueue;
DemoInput demoInput{false, false, 4.0f};
RiskUpdate latestRisk{Risk::Unknown, 0};
Risk displayedRisk = Risk::Unknown;

// Replace this function with the adapter for the actual surface sensor.
// Return NAN if a reading is unavailable or invalid.
float readSurfaceTemperatureC() {
  return NAN;
}

void showRisk(Risk risk) {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  const char* label;
  switch (risk) {
    case Risk::Low:     green = 255;           label = "LOW"; break;
    case Risk::Caution: red = green = 255;     label = "CAUTION"; break;
    case Risk::High:    red = 255;             label = "HIGH"; break;
    default:           blue = 255;            label = "UNKNOWN"; break;
  }

  display.fillScreen(display.color565(red, green, blue));
  display.setTextColor(green ? TFT_BLACK : TFT_WHITE);
  display.setTextDatum(textdatum_t::middle_center);
  display.drawString(label, display.width() / 2, display.height() / 2);

  if (SMARTWALK_RGB_PIN >= 0) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    rgbLedWrite(SMARTWALK_RGB_PIN, red / 8, green / 8, blue / 8);
#else
    neopixelWrite(SMARTWALK_RGB_PIN, red / 8, green / 8, blue / 8);
#endif
  }
  displayedRisk = risk;
  Serial.printf("Display: %s\n", label);
}

bool parseRisk(const String& body, Risk& risk) {
  cJSON* object = cJSON_ParseWithOpts(body.c_str(), nullptr, true);
  const cJSON* field = cJSON_GetObjectItemCaseSensitive(object, "risk");
  const char* value = cJSON_GetStringValue(field);
  bool recognized = true;
  if (value && strcmp(value, "low") == 0) {
    risk = Risk::Low;
  } else if (value && strcmp(value, "caution") == 0) {
    risk = Risk::Caution;
  } else if (value && strcmp(value, "high") == 0) {
    risk = Risk::High;
  } else if (value && strcmp(value, "unknown") == 0) {
    risk = Risk::Unknown;
  } else {
    recognized = false;
  }
  cJSON_Delete(object);
  return recognized;
}

bool beginRequest(HTTPClient& http, WiFiClient& client, const String& baseUrl,
                  const char* path) {
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  return http.begin(client, baseUrl + path);
}

void fetchStatus(const String& baseUrl) {
  WiFiClient client;
  HTTPClient http;
  if (!beginRequest(http, client, baseUrl, "/status")) {
    Serial.println("GET /status: could not start request");
    return;
  }

  const int code = http.GET();
  Risk risk;
  if (code >= 200 && code < 300 && parseRisk(http.getString(), risk)) {
    const RiskUpdate update{risk, millis()};
    xQueueOverwrite(riskQueue, &update);
    Serial.printf("GET /status: %d\n", code);
  } else {
    Serial.printf("GET /status: no usable risk (HTTP %d)\n", code);
  }
  http.end();
}

void postObservation(const String& baseUrl, const DemoInput& input) {
  const float temperatureC = input.sensorMode ? readSurfaceTemperatureC() : input.temperatureC;
  cJSON* observation = cJSON_CreateObject();
  if (!observation) {
    return;
  }
  const char* source = input.sensorMode ? "sensor" : "demo";
  // cJSON serializes non-finite numbers as JSON null.
  const bool populated = cJSON_AddStringToObject(observation, "location_id", kLocationId) &&
      cJSON_AddNumberToObject(observation, "surface_temperature_c", temperatureC) &&
      cJSON_AddStringToObject(observation, "temperature_source", source);
  char payload[192];
  const bool serialized = populated && cJSON_PrintPreallocated(observation, payload, sizeof(payload), false);
  cJSON_Delete(observation);
  if (!serialized) {
    Serial.println("POST /observations: could not encode observation");
    return;
  }

  WiFiClient client;
  HTTPClient http;
  if (!beginRequest(http, client, baseUrl, "/observations")) {
    Serial.println("POST /observations: could not start request");
    return;
  }
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(reinterpret_cast<uint8_t*>(payload), strlen(payload));
  Serial.printf("POST /observations: %d %s\n", code, payload);
  http.end();
}

void runNetwork(void*) {
  String baseUrl = SMARTWALK_API_URL;
  while (baseUrl.endsWith("/")) {
    baseUrl.remove(baseUrl.length() - 1);
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SMARTWALK_WIFI_SSID, SMARTWALK_WIFI_PASSWORD);
  uint32_t lastPost = millis() - kPollIntervalMs;
  uint32_t lastGet = lastPost;
  bool connected = false;

  for (;;) {
    const bool online = WiFi.status() == WL_CONNECTED;
    if (online != connected) {
      Serial.println(online ? "Wi-Fi connected" : "Wi-Fi disconnected");
      connected = online;
    }
    if (online) {
      const uint32_t now = millis();
      if (now - lastGet >= kPollIntervalMs) {
        lastGet = now;
        fetchStatus(baseUrl);
      }

      DemoInput input;
      const uint32_t postNow = millis();
      if (postNow - lastPost >= kPollIntervalMs &&
          xQueuePeek(demoQueue, &input, 0) == pdTRUE && !input.paused) {
        lastPost = postNow;
        postObservation(baseUrl, input);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void printHelp() {
  Serial.println("Commands: temp <degrees C|missing>, sensor, pause, resume, help");
}

void handleCommand(String command) {
  command.trim();
  if (command == "pause") {
    demoInput.paused = true;
    Serial.println("Observation POST paused");
  } else if (command == "resume") {
    demoInput.paused = false;
    Serial.println("Observation POST resumed");
  } else if (command == "sensor") {
    demoInput.sensorMode = true;
    Serial.println("Temperature source: sensor");
  } else if (command.startsWith("temp ")) {
    String value = command.substring(5);
    value.trim();
    float temperatureC = NAN;
    if (value != "missing") {
      char* end;
      temperatureC = strtof(value.c_str(), &end);
      if (end == value.c_str() || *end != '\0' || !isfinite(temperatureC)) {
        Serial.println("Enter a finite temperature or 'temp missing'");
        return;
      }
    }
    demoInput.sensorMode = false;
    demoInput.temperatureC = temperatureC;
    Serial.printf("Demo temperature: %s\n", value.c_str());
  } else {
    if (command.length() > 0) {
      printHelp();
    }
    return;
  }

  if (demoQueue) {
    xQueueOverwrite(demoQueue, &demoInput);
  }
}

void readSerial() {
  static String command;
  static bool tooLong = false;
  while (Serial.available()) {
    const char next = Serial.read();
    if (next == '\n' || next == '\r') {
      if (!tooLong) {
        handleCommand(command);
      }
      command = "";
      tooLong = false;
    } else if (command.length() < 64) {
      command += next;
    } else {
      tooLong = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  display.begin();
  display.setTextSize(2);
  showRisk(Risk::Unknown);
  printHelp();

  if (SMARTWALK_WIFI_SSID[0] == '\0' || !String(SMARTWALK_API_URL).startsWith("http://")) {
    Serial.println("Set the Wi-Fi SSID and HTTP API URL in config.local.h");
    return;
  }
  demoQueue = xQueueCreate(1, sizeof(DemoInput));
  riskQueue = xQueueCreate(1, sizeof(RiskUpdate));
  if (!demoQueue || !riskQueue) {
    Serial.println("Could not allocate network queues");
    return;
  }
  xQueueOverwrite(demoQueue, &demoInput);
  if (xTaskCreate(runNetwork, "SmartWalk network", 8192, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("Could not start network task");
  }
}

void loop() {
  readSerial();
  if (riskQueue) {
    xQueueReceive(riskQueue, &latestRisk, 0);
  }
  if (millis() - latestRisk.receivedAt >= kStaleAfterMs) {
    latestRisk.risk = Risk::Unknown;
  }
  if (latestRisk.risk != displayedRisk) {
    showRisk(latestRisk.risk);
  }
  delay(10);
}
