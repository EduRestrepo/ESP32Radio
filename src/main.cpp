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
String currentHostname = TINYRADIO_HOSTNAME;
int currentVolume = DEFAULT_VOLUME;
String currentStationId = "1";
String currentStationUrl = "";
bool isPlaying = false;
bool isApMode = false;
unsigned long bootTime = 0;

// Control para apagar el SoftAP tras mostrar la IP asignada al usuario
bool pendingApShutdown = false;
unsigned long apShutdownTimestamp = 0;

// Control del botón físico BOOT (GPIO 9) para reset de fábrica de WiFi
unsigned long bootBtnPressStart = 0;
bool bootBtnPressed = false;

// Prototipos
void initFileSystem();
void loadConfiguration();
void saveConfiguration();
void resetWiFiConfiguration();
void checkResetButton();
String sanitizeHostname(const String &raw);
void setupWiFi();
void startSoftAP();
void startMDNS();
void setupWebServer();
void setupCaptivePortalRoutes();
void setupAudio();
void handleCaptiveRedirect();
void handleNotFound();

// ---------------- SETUP ----------------
void setup() {
  DEBUG_SERIAL.begin(115200);
  delay(1000);

  // Configurar botón físico BOOT (GPIO 9) para restablecer WiFi (mantener 3s)
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

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

  // 5. Iniciar mDNS si está conectado a red local
  if (!isApMode) {
    startMDNS();
  }

  // 6. Configurar servidor HTTP y Portal Cautivo
  setupWebServer();

  DEBUG_SERIAL.println("[OK] Sistema listo. (Mantén pulsado BOOT 3s en cualquier momento para restablecer WiFi)");
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

  // Apagar SoftAP de forma diferida tras haber entregado la IP asignada al navegador
  if (pendingApShutdown && (millis() - apShutdownTimestamp >= 25000)) {
    pendingApShutdown = false;
    isApMode = false;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    DEBUG_SERIAL.printf("[WiFi] Punto de acceso temporal cerrado. Activo en http://%s y http://%s.local\n",
      WiFi.localIP().toString().c_str(), currentHostname.c_str());
  }

  // Comprobar si el usuario mantiene presionado el botón BOOT (GPIO 9) durante 3 seg para Reset WiFi
  checkResetButton();
}

// ---------------- BOTÓN DE RESET Y UTILIDADES ----------------
void checkResetButton() {
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (!bootBtnPressed) {
      bootBtnPressed = true;
      bootBtnPressStart = millis();
      DEBUG_SERIAL.println("[Botón BOOT] Presionado... mantén 3 segundos para restablecer el WiFi.");
    } else if (millis() - bootBtnPressStart >= RESET_HOLD_TIME_MS) {
      DEBUG_SERIAL.println("\n=================================================");
      DEBUG_SERIAL.println("  🔄 RESET DE WIFI ACTIVADO POR BOTÓN BOOT (3s)  ");
      DEBUG_SERIAL.println("=================================================");
      resetWiFiConfiguration();
    }
  } else {
    bootBtnPressed = false;
  }
}

void resetWiFiConfiguration() {
  audio.stopSong();
  currentSSID = "";
  currentPass = "";
  saveConfiguration();
  delay(500);
  ESP.restart();
}

String sanitizeHostname(const String &raw) {
  String clean = "";
  for (size_t i = 0; i < raw.length() && clean.length() < 24; i++) {
    char c = tolower(raw[i]);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
      clean += c;
    } else if (c == ' ' || c == '_') {
      clean += '-';
    }
  }
  while (clean.startsWith("-")) clean.remove(0, 1);
  while (clean.endsWith("-")) clean.remove(clean.length() - 1);
  if (clean.length() == 0) clean = TINYRADIO_HOSTNAME;
  return clean;
}

void startMDNS() {
  MDNS.end();
  if (MDNS.begin(currentHostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    DEBUG_SERIAL.printf("[mDNS] Disponible en http://%s.local (IP: %s)\n",
      currentHostname.c_str(), WiFi.localIP().toString().c_str());
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
        currentHostname = sanitizeHostname(doc["hostname"] | TINYRADIO_HOSTNAME);
        currentVolume = doc["volume"] | DEFAULT_VOLUME;
        currentStationId = doc["currentStationId"] | "1";
        DEBUG_SERIAL.printf("[Config] SSID: '%s', Hostname: '%s'\n", currentSSID.c_str(), currentHostname.c_str());
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
    doc["hostname"] = currentHostname;
    doc["volume"] = currentVolume;
    doc["currentStationId"] = currentStationId;
    serializeJson(doc, f);
    f.close();
    DEBUG_SERIAL.println("[Config] Guardada en LittleFS.");
  }
}

// ---------------- CONEXIÓN WIFI Y SOFTAP ----------------
void startSoftAP() {
  isApMode = true;
  WiFi.mode(WIFI_AP_STA);
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

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(currentHostname.c_str());

  if (currentSSID.length() > 0) {
    DEBUG_SERIAL.printf("[WiFi] Intentando conectar a '%s' como '%s'...\n", currentSSID.c_str(), currentHostname.c_str());
    WiFi.begin(currentSSID.c_str(), currentPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(300);
      DEBUG_SERIAL.print(".");
      checkResetButton();
    }
    DEBUG_SERIAL.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    isApMode = false;
    DEBUG_SERIAL.printf("[WiFi] ¡Conectado! IP asignada: http://%s (o http://%s.local)\n",
      WiFi.localIP().toString().c_str(), currentHostname.c_str());
  } else {
    startSoftAP();
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
    doc["hostname"] = currentHostname;
    doc["ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : DEFAULT_AP_SSID;
    doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

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

  // REST API: Guardar credenciales WiFi + Hostname, probar conexión y devolver IP asignada
  server.on("/api/wifi", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    JsonDocument req;
    deserializeJson(req, server.arg("plain"));
    String newSSID = req["ssid"] | "";
    String newPass = req["password"] | "";
    String newHostname = sanitizeHostname(req["hostname"] | currentHostname);

    if (newSSID.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"Empty SSID\"}");
      return;
    }

    currentHostname = newHostname;
    WiFi.setHostname(currentHostname.c_str());

    // Mantener el AP encendido mientras intentamos conectar al router (WIFI_AP_STA)
    if (isApMode) {
      WiFi.mode(WIFI_AP_STA);
    }
    WiFi.disconnect(false);
    delay(100);

    DEBUG_SERIAL.printf("[WiFi] Probando conexión a '%s' con nombre '%s'...\n", newSSID.c_str(), currentHostname.c_str());
    WiFi.begin(newSSID.c_str(), newPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
      delay(250);
      if (isApMode) {
        dnsServer.processNextRequest();
      }
      DEBUG_SERIAL.print(".");
    }
    DEBUG_SERIAL.println();

    if (WiFi.status() == WL_CONNECTED) {
      currentSSID = newSSID;
      currentPass = newPass;
      saveConfiguration();
      startMDNS();

      String assignedIP = WiFi.localIP().toString();
      DEBUG_SERIAL.printf("[WiFi] ¡Conectado! IP asignada por el router: %s (http://%s.local)\n",
        assignedIP.c_str(), currentHostname.c_str());

      // Si estábamos en portal cautivo, apagar el AP dentro de 25s para que el usuario vea/copie la IP primero
      if (isApMode) {
        pendingApShutdown = true;
        apShutdownTimestamp = millis();
      }

      JsonDocument res;
      res["success"] = true;
      res["connected"] = true;
      res["ip"] = assignedIP;
      res["hostname"] = currentHostname;
      res["mdns"] = String("http://") + currentHostname + ".local";
      String out;
      serializeJson(res, out);
      server.send(200, "application/json", out);
    } else {
      DEBUG_SERIAL.println("[WiFi] Falló la conexión. Se mantiene el Portal Cautivo activo.");
      JsonDocument res;
      res["success"] = false;
      res["connected"] = false;
      res["error"] = "No se pudo conectar a la red WiFi. Verifica la contraseña e inténtalo de nuevo.";
      String out;
      serializeJson(res, out);
      server.send(200, "application/json", out);
    }
  });

  // REST API: Restablecer configuración WiFi (volver a Modo Configuración / Portal Cautivo)
  server.on("/api/wifi/reset", HTTP_POST, []() {
    server.send(200, "application/json", "{\"success\":true,\"msg\":\"Restableciendo WiFi y activando portal cautivo...\"}");
    delay(600);
    resetWiFiConfiguration();
  });

  // REST API: Información de hardware
  server.on("/api/hardware-info", HTTP_GET, []() {
    JsonDocument doc;
    doc["chip"] = "ESP32-C3";
    doc["cpuFreq"] = ESP.getCpuFreqMHz();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["uptime"] = (millis() - bootTime) / 1000;
    doc["isApMode"] = isApMode;
    doc["hostname"] = currentHostname;
    doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    doc["ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : DEFAULT_AP_SSID;
    doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

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
