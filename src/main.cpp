/**
 * TinyRadio - ESP32-C3 Internet Radio Firmware
 * 
 * Features:
 * - Portal Cautivo automático para configuración WiFi (Android, iOS/macOS, Windows)
 * - LittleFS web server con interfaz moderna responsiva
 * - Salida I2S digital con decodificación MP3/AAC (MAX98357A / PCM5102A)
 * - Reproducción dual (Audio Web en el navegador / Hardware en ESP32)
 * - REST API para estaciones, volumen, escaneo WiFi, telemetría
 * - Descubrimiento mDNS en http://tinyradio.local
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include "Audio.h"
#include "config.h"

// ---------------- OBJETOS GLOBALES ----------------
WebServer server(80);
DNSServer dnsServer;
Audio audio;

IPAddress apIP(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, AP_IP_OCTET_4);
IPAddress apNetmask(255, 255, 255, 0);

// Estado del dispositivo
String currentSSID = "";
String currentPass = "";
int currentVolume = DEFAULT_VOLUME;
String currentStationId = "1";
String currentStationUrl = "";
bool isPlaying = false;
bool isApMode = false;
unsigned long bootTime = 0;

// Prototipos
void initFileSystem();
void loadConfiguration();
void saveConfiguration();
void setupWiFi();
void setupWebServer();
void setupCaptivePortalRoutes();
void setupAudio();
void handleCaptiveRedirect();
void handleNotFound();

// ---------------- SETUP ----------------
void setup() {
  DEBUG_SERIAL.begin(115200);
  delay(1000);

  DEBUG_SERIAL.println("\n========================================");
  DEBUG_SERIAL.println("  📻 TinyRadio - ESP32-C3 Starting...   ");
  DEBUG_SERIAL.println("========================================");

  bootTime = millis();

  // 1. Montar sistema de archivos LittleFS
  initFileSystem();

  // 2. Cargar configuración previa
  loadConfiguration();

  // 3. Configurar I2S
  setupAudio();

  // 4. Conectar a WiFi o activar Portal Cautivo SoftAP
  setupWiFi();

  // 5. Iniciar mDNS
  if (!isApMode) {
    if (MDNS.begin(TINYRADIO_HOSTNAME)) {
      DEBUG_SERIAL.println("[mDNS] Disponible en http://tinyradio.local");
      MDNS.addService("http", "tcp", 80);
    }
  }

  // 6. Configurar servidor HTTP y Portal Cautivo
  setupWebServer();

  DEBUG_SERIAL.println("[OK] Sistema listo.");
}

// ---------------- LOOP ----------------
void loop() {
  // Decodificación de streaming de audio
  audio.loop();

  // Atender peticiones web
  server.handleClient();

  // Si estamos en modo Punto de Acceso, atender redirecciones DNS del portal cautivo
  if (isApMode) {
    dnsServer.processNextRequest();
  }
}

// ---------------- AUDIO I2S ----------------
void setupAudio() {
  DEBUG_SERIAL.printf("[Audio] I2S Pins: BCLK=%d, LRC=%d, DOUT=%d\n", I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
  audio.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);

  int mappedVol = map(currentVolume, 0, 100, 0, 21);
  audio.setVolume(mappedVol);
  audio.setConnectionTimeout(2500, 4000);
}

void audio_info(const char *info) {
  DEBUG_SERIAL.printf("[Audio] %s\n", info);
}

void audio_showstation(const char *info) {
  DEBUG_SERIAL.printf("[Estación] %s\n", info);
}

void audio_eof_mp3(const char *info) {
  DEBUG_SERIAL.printf("[Fin stream] %s\n", info);
  isPlaying = false;
}

// ---------------- FILE SYSTEM ----------------
void initFileSystem() {
  if (!LittleFS.begin(true)) {
    DEBUG_SERIAL.println("[LittleFS] Error al montar.");
  } else {
    DEBUG_SERIAL.printf("[LittleFS] Montado. Espacio total: %u, usado: %u\n", 
      LittleFS.totalBytes(), LittleFS.usedBytes());
  }
}

void loadConfiguration() {
  if (LittleFS.exists(CONFIG_FILE)) {
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        currentSSID = doc["ssid"] | "";
        currentPass = doc["password"] | "";
        currentVolume = doc["volume"] | DEFAULT_VOLUME;
        currentStationId = doc["currentStationId"] | "1";
        DEBUG_SERIAL.printf("[Config] SSID guardado: '%s'\n", currentSSID.c_str());
      }
      f.close();
    }
  }
}

void saveConfiguration() {
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (f) {
    JsonDocument doc;
    doc["ssid"] = currentSSID;
    doc["password"] = currentPass;
    doc["volume"] = currentVolume;
    doc["currentStationId"] = currentStationId;
    serializeJson(doc, f);
    f.close();
    DEBUG_SERIAL.println("[Config] Guardada en LittleFS.");
  }
}

// ---------------- CONEXIÓN WIFI Y SOFTAP ----------------
void setupWiFi() {
  WiFi.mode(WIFI_STA);

  if (currentSSID.length() > 0) {
    DEBUG_SERIAL.printf("[WiFi] Intentando conectar a: %s\n", currentSSID.c_str());
    WiFi.begin(currentSSID.c_str(), currentPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(300);
      DEBUG_SERIAL.print(".");
    }
    DEBUG_SERIAL.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    isApMode = false;
    DEBUG_SERIAL.printf("[WiFi] ¡Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    // Activar SoftAP y Portal Cautivo (Red abierta sin contraseña)
    isApMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, apNetmask);
    if (strlen(DEFAULT_AP_PASS) > 0) {
      WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASS);
    } else {
      WiFi.softAP(DEFAULT_AP_SSID);
    }

    // Servidor DNS para capturar cualquier dominio (* -> 192.168.4.1)
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);

    DEBUG_SERIAL.println("=================================================");
    DEBUG_SERIAL.println("  📶 PORTAL CAUTIVO ACTIVADO                    ");
    DEBUG_SERIAL.printf("  1. Conéctate a la red WiFi abierta: '%s'\n", DEFAULT_AP_SSID);
    DEBUG_SERIAL.printf("  2. Abre en el navegador: http://%s\n", apIP.toString().c_str());
    DEBUG_SERIAL.println("=================================================");
  }
}

// Redirección para el portal cautivo
void handleCaptiveRedirect() {
  server.sendHeader("Location", String("http://") + apIP.toString() + "/", true);
  server.send(302, "text/plain", "Redirigiendo a TinyRadio...");
}

// ---------------- RUTAS Y SERVIDOR WEB ----------------
void setupWebServer() {
  // Probes de detección de Portal Cautivo para Android, iOS y Windows
  setupCaptivePortalRoutes();

  // Archivos estáticos de LittleFS
  server.on("/", HTTP_GET, []() {
    if (LittleFS.exists("/index.html")) {
      File file = LittleFS.open("/index.html", "r");
      server.streamFile(file, "text/html");
      file.close();
    } else {
      server.send(200, "text/html", "<h2>TinyRadio: Sube los archivos data/ a LittleFS</h2>");
    }
  });

  server.on("/style.css", HTTP_GET, []() {
    File file = LittleFS.open("/style.css", "r");
    server.streamFile(file, "text/css");
    file.close();
  });

  server.on("/app.js", HTTP_GET, []() {
    File file = LittleFS.open("/app.js", "r");
    server.streamFile(file, "application/javascript");
    file.close();
  });

  server.on("/stations.json", HTTP_GET, []() {
    File file = LittleFS.open(STATIONS_FILE, "r");
    server.streamFile(file, "application/json");
    file.close();
  });

  // REST API: Estaciones
  server.on("/api/stations", HTTP_GET, []() {
    if (LittleFS.exists(STATIONS_FILE)) {
      File file = LittleFS.open(STATIONS_FILE, "r");
      server.streamFile(file, "application/json");
      file.close();
    } else {
      server.send(404, "application/json", "[]");
    }
  });

  // REST API: Añadir estación
  server.on("/api/stations", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    JsonDocument newStation;
    if (deserializeJson(newStation, server.arg("plain"))) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    JsonDocument doc;
    if (LittleFS.exists(STATIONS_FILE)) {
      File f = LittleFS.open(STATIONS_FILE, "r");
      deserializeJson(doc, f);
      f.close();
    }
    JsonArray arr = doc.as<JsonArray>();
    arr.add(newStation);

    File fw = LittleFS.open(STATIONS_FILE, "w");
    serializeJson(doc, fw);
    fw.close();

    server.send(200, "application/json", "{\"success\":true}");
  });

  // REST API: Alternar favoritos
  server.on("/api/favorites/toggle", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    JsonDocument req;
    deserializeJson(req, server.arg("plain"));
    String id = req["id"] | "";
    bool fav = req["favorite"] | false;

    if (id.length() > 0 && LittleFS.exists(STATIONS_FILE)) {
      JsonDocument doc;
      File f = LittleFS.open(STATIONS_FILE, "r");
      deserializeJson(doc, f);
      f.close();

      for (JsonObject st : doc.as<JsonArray>()) {
        if (st["id"].as<String>() == id) {
          st["favorite"] = fav;
          break;
        }
      }

      File fw = LittleFS.open(STATIONS_FILE, "w");
      serializeJson(doc, fw);
      fw.close();
    }
    server.send(200, "application/json", "{\"success\":true}");
  });

  // REST API: Reproducción en ESP32
  server.on("/api/play", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    JsonDocument req;
    deserializeJson(req, server.arg("plain"));
    String url = req["url"] | "";
    String id = req["id"] | "";

    if (url.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"Missing URL\"}");
      return;
    }

    currentStationUrl = url;
    currentStationId = id;
    DEBUG_SERIAL.printf("[Radio] Reproduciendo: %s\n", url.c_str());
    audio.connecttohost(url.c_str());
    isPlaying = true;

    server.send(200, "application/json", "{\"success\":true,\"playing\":true}");
  });

  // REST API: Detener reproducción
  server.on("/api/stop", HTTP_POST, []() {
    audio.stopSong();
    isPlaying = false;
    server.send(200, "application/json", "{\"success\":true,\"playing\":false}");
  });

  // REST API: Volumen
  server.on("/api/volume", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    JsonDocument req;
    deserializeJson(req, server.arg("plain"));
    int vol = req["volume"] | currentVolume;
    currentVolume = constrain(vol, 0, 100);

    audio.setVolume(map(currentVolume, 0, 100, 0, 21));
    saveConfiguration();

    server.send(200, "application/json", "{\"success\":true,\"volume\":" + String(currentVolume) + "}");
  });

  // REST API: Estado
  server.on("/api/status", HTTP_GET, []() {
    JsonDocument doc;
    doc["isPlaying"] = audio.isRunning();
    doc["currentStationId"] = currentStationId;
    doc["volume"] = currentVolume;
    doc["isApMode"] = isApMode;
    doc["ssid"] = isApMode ? DEFAULT_AP_SSID : WiFi.SSID();
    doc["ip"] = isApMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // REST API: Escanear redes WiFi
  server.on("/api/scan-wifi", HTTP_GET, []() {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
      JsonObject net = arr.add<JsonObject>();
      net["ssid"] = WiFi.SSID(i);
      net["rssi"] = WiFi.RSSI(i);
      net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // REST API: Guardar credenciales WiFi y reiniciar
  server.on("/api/wifi", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    JsonDocument req;
    deserializeJson(req, server.arg("plain"));
    String newSSID = req["ssid"] | "";
    String newPass = req["password"] | "";

    if (newSSID.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"Empty SSID\"}");
      return;
    }

    currentSSID = newSSID;
    currentPass = newPass;
    saveConfiguration();

    server.send(200, "application/json", "{\"success\":true,\"msg\":\"Reiniciando ESP32...\"}");
    delay(1000);
    ESP.restart();
  });

  // REST API: Información de hardware
  server.on("/api/hardware-info", HTTP_GET, []() {
    JsonDocument doc;
    doc["chip"] = "ESP32-C3";
    doc["cpuFreq"] = ESP.getCpuFreqMHz();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["uptime"] = (millis() - bootTime) / 1000;
    doc["isApMode"] = isApMode;
    doc["rssi"] = isApMode ? 0 : WiFi.RSSI();
    doc["ssid"] = isApMode ? DEFAULT_AP_SSID : WiFi.SSID();
    doc["ip"] = isApMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.onNotFound(handleNotFound);

  server.begin();
  DEBUG_SERIAL.println("[WebServer] Servidor HTTP activo.");
}

// Probes específicos para que Android, iPhone/iPad y Windows abran la pantalla de inicio de sesión
void setupCaptivePortalRoutes() {
  // Android
  server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);
  server.on("/gen_204", HTTP_GET, handleCaptiveRedirect);

  // iOS / macOS
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect);
  server.on("/canonical.html", HTTP_GET, handleCaptiveRedirect);
  server.on("/library/test/success.html", HTTP_GET, handleCaptiveRedirect);
  server.on("/success.txt", HTTP_GET, handleCaptiveRedirect);

  // Windows
  server.on("/ncsi.txt", HTTP_GET, handleCaptiveRedirect);
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
}

void handleNotFound() {
  if (isApMode) {
    handleCaptiveRedirect();
    return;
  }
  server.send(404, "text/plain", "404 Not Found");
}
