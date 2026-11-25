#pragma once

// Copy this file to config.local.h and fill in the values.
#define SMARTWALK_WIFI_SSID ""
#define SMARTWALK_WIFI_PASSWORD ""
#define SMARTWALK_API_URL "http://192.0.2.1:8000"

// I2C bus for MLX90640 and the OLED. Use the same pins as the Renpin sketch.
#define SMARTWALK_I2C_SDA 2
#define SMARTWALK_I2C_SCL 1
#define SMARTWALK_OLED_ADDR 0x3C

// GPIO of the snapshot button (active LOW). -1 disables the button.
#define SMARTWALK_BUTTON_PIN -1

// Thermal pixels averaged as the road surface (MLX90640 is 32 columns x 24 rows).
#define SMARTWALK_ROI_COL_START 8
#define SMARTWALK_ROI_COL_END 23
#define SMARTWALK_ROI_ROW_START 6
#define SMARTWALK_ROI_ROW_END 17

// 1: estimate the surface from light reflection and send it to POST /vision.
// Set 0 when the vision PC (RSPNet) sends the labels instead.
#define SMARTWALK_SEND_VISION 1
