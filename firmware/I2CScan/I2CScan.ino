// I2C 配線チェック用のスケッチ（診断専用）。
// 焼くと、AtomS3R-CAM の候補ピンを総当たりして応答したアドレスを3秒ごとに表示する。
// 例: 0x33 = MLX90640（赤外線）、0x3C = SSD1306（OLED）
// 使い方: Arduino IDE でこのスケッチを書き込み、シリアルモニターを 115200 bps で開く。
// 確認が終わったら firmware/SmartWalkCam を焼き直すこと。
#include <Arduino.h>
#include <Wire.h>

struct Bus {
  const char* name;
  int sda;
  int scl;
  unsigned long hz;
};

Bus buses[] = {
  {"grove",   2,  1, 100000},
  {"grove",   2,  1, 400000},
  {"bottom",  5,  6, 100000},
  {"bottom",  6,  5, 100000},
  {"bottom",  7,  8, 100000},
  {"bottom",  8,  7, 100000},
  {"bottom", 38, 39, 100000},
  {"bottom", 39, 38, 100000},
};

void scanAll() {
  Serial.println();
  Serial.println("=== I2C SCAN START ===");
  for (const Bus& b : buses) {
    Wire.end();
    if (!Wire.begin(b.sda, b.scl, b.hz)) {
      Serial.printf("%s SDA=%d SCL=%d @%luHz: begin failed\n", b.name, b.sda, b.scl, b.hz);
      continue;
    }
    Wire.setClock(b.hz);
    delay(3);
    int found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("%s SDA=%d SCL=%d @%luHz: ACK 0x%02X\n", b.name, b.sda, b.scl, b.hz, addr);
        found++;
      }
    }
    if (!found) {
      Serial.printf("%s SDA=%d SCL=%d @%luHz: no device\n", b.name, b.sda, b.scl, b.hz);
    }
  }
  Serial.println("=== I2C SCAN END ===");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  scanAll();
}

void loop() {
  delay(3000);
  scanAll();
}
