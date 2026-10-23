#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClient.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <ArduinoJson.h>
#include <NewPing.h>
#include <Adafruit_NeoPixel.h>
#include "lab_page.h"

const char *ssid = "your-ssid";
const char *password = "your-password";

const int TRIG_PIN = 4;
const int ECHO_PIN = 5;
const int LED_PIN = 14;
const int NUM_LEDS = 8;
const int MAX_DISTANCE_CM = 400;

NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE_CM);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

WebServer server(80);

struct LedState {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t brightness;
};

LedState leds[NUM_LEDS];

void sendJson(int code, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

void sendError(int code, const char *message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(code, doc);
}

void addLed(JsonObject obj, int id) {
  obj["id"] = id;
  obj["r"] = leds[id].r;
  obj["g"] = leds[id].g;
  obj["b"] = leds[id].b;
  obj["brightness"] = leds[id].brightness;
}

void showLeds() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t r = leds[i].r * leds[i].brightness / 255;
    uint8_t g = leds[i].g * leds[i].brightness / 255;
    uint8_t b = leds[i].b * leds[i].brightness / 255;
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

bool readField(JsonDocument &doc, const char *key, uint8_t &value) {
  if (!doc[key].is<int>()) return false;
  int v = doc[key].as<int>();
  if (v < 0 || v > 255) return false;
  value = v;
  return true;
}

void handleRoot() {
  server.send_P(200, "text/html", LAB_PAGE);
}

void handleDistance() {
  unsigned int echoTime = sonar.ping();
  JsonDocument doc;
  if (echoTime == NO_ECHO) {
    doc["distance_cm"] = nullptr;
    doc["valid"] = false;
  } else {
    doc["distance_cm"] = round(echoTime * 10.0 / US_ROUNDTRIP_CM) / 10.0;
    doc["valid"] = true;
  }
  server.sendHeader("Cache-Control", "no-store");
  sendJson(200, doc);
}

void handleGetLeds() {
  JsonDocument doc;
  JsonArray arr = doc["leds"].to<JsonArray>();
  for (int i = 0; i < NUM_LEDS; i++) {
    addLed(arr.add<JsonObject>(), i);
  }
  sendJson(200, doc);
}

void handlePutLed() {
  String idStr = server.pathArg(0);
  int id = idStr.toInt();
  if (idStr.length() == 0 || String(id) != idStr || id < 0 || id >= NUM_LEDS) {
    sendError(404, "Unknown LED id");
    return;
  }

  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err || !body.is<JsonObject>()) {
    sendError(400, "Malformed JSON body");
    return;
  }

  LedState next;
  if (!readField(body, "r", next.r) || !readField(body, "g", next.g) || !readField(body, "b", next.b) || !readField(body, "brightness", next.brightness)) {
    sendError(400, "r, g, b and brightness must be integers from 0 to 255");
    return;
  }

  leds[id] = next;
  showLeds();

  JsonDocument doc;
  addLed(doc.to<JsonObject>(), id);
  sendJson(200, doc);
}

void handleNotFound() {
  sendError(404, "Not found");
}

void setup(void) {
  Serial.begin(115200);
  strip.begin();
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = {0, 0, 0, 128};
  }
  showLeds();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  int numberOfTries = 20;
  while (WiFi.status() != WL_CONNECTED && numberOfTries > 0) {
    delay(500);
    Serial.print(".");
    numberOfTries--;
  }
  Serial.println("");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi!");
    return;
  }
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/distance", HTTP_GET, handleDistance);
  server.on("/api/leds", HTTP_GET, handleGetLeds);
  server.on(UriBraces("/api/leds/{}"), HTTP_PUT, handlePutLed);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

unsigned long lastShow = 0;

void loop(void) {
  server.handleClient();
  if (millis() - lastShow >= 100) {
    lastShow = millis();
    showLeds();
  }
  delay(2);
}
