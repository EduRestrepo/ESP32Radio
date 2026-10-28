#pragma once
#include <Arduino.h>

// ==========================================
// TinyRadio - Configuración General
// ==========================================

#define TINYRADIO_VERSION "1.0.0"
#define TINYRADIO_HOSTNAME "tinyradio"

// Modo Punto de Acceso (AP) y Portal Cautivo
#define DEFAULT_AP_SSID "TinyRadio-Setup"
#define DEFAULT_AP_PASS "" // Red abierta (sin contraseña) para la primera configuración
#define AP_IP_OCTET_1 192
#define AP_IP_OCTET_2 168
#define AP_IP_OCTET_3 4
#define AP_IP_OCTET_4 1

// Pines I2S para DAC (ej. MAX98357A / PCM5102A) en ESP32-C3 Mini
#define I2S_BCLK_PIN    6   // Bit Clock (BCLK)
#define I2S_LRC_PIN     7   // Left/Right Clock (LRC / WS)
#define I2S_DOUT_PIN    8   // Data Out (DIN)

// Botón físico BOOT en ESP32-C3 (GPIO 9) para restablecer WiFi (mantener 3 seg)
#define RESET_BUTTON_PIN 9
#define RESET_HOLD_TIME_MS 3000

// Rutas LittleFS
#define STATIONS_FILE   "/stations.json"
#define CONFIG_FILE     "/config.json"

// Volumen por defecto (0 a 100)
#define DEFAULT_VOLUME  70

#define DEBUG_SERIAL    Serial
