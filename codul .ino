#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#define DEBUG 1


// ====== WiFi (Wokwi) ======
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// ====== MQTT Broker (public) ======
// Poți folosi și alt broker; pentru demo e ok.
const char* MQTT_HOST = "test.mosquitto.org";
const uint16_t MQTT_PORT = 1883;

// ====== MQTT Topics ======
const char* TOPIC_TELEMETRY = "irrigation/demo/telemetry";
const char* TOPIC_CMD       = "irrigation/demo/cmd";
const char* TOPIC_STATUS    = "irrigation/demo/status";

// ====== Pins (Wokwi) ======
const int PIN_POT  = 34;  // ADC
const int PIN_PUMP = 2;   // LED builtin pe multe plăci ESP32 (în Wokwi merge ok)

// ====== Timing ======
unsigned long lastPublishMs = 0;
const unsigned long PUBLISH_INTERVAL_MS = 3000;

// ====== Irrigation state ======
bool pumpOn = false;
unsigned long pumpUntilMs = 0;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

static void publishStatus(const char* state, int remainingSec = -1) {
  StaticJsonDocument<256> doc;
  doc["pump"] = state;
  if (remainingSec >= 0) doc["remainingSec"] = remainingSec;
  doc["ts"] = (long) (millis() / 1000);

  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(TOPIC_STATUS, buf, n);
}

static void setPump(bool on, unsigned long durationMs = 0) {
  pumpOn = on;
  digitalWrite(PIN_PUMP, on ? HIGH : LOW);

  Serial.print("PUMP ");
Serial.println(on ? "ON" : "OFF");


  if (on && durationMs > 0) {
    pumpUntilMs = millis() + durationMs;
    publishStatus("ON", (int)(durationMs / 1000));
  } else {
    pumpUntilMs = 0;
    publishStatus("OFF");
  }
}

static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  // Copiem payload într-un buffer terminat cu \0
  char msg[512];
  unsigned int n = (length < sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
  memcpy(msg, payload, n);
  msg[n] = '\0';

  Serial.print("Received on ");
Serial.print(topic);
Serial.print(": ");
Serial.println(msg);
Serial.println(n ? "ON" : "OFF");



  // Așteptăm JSON de forma: {"action":"START","duration":10}
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) {
    // Dacă nu e JSON, ignorăm
    return;
  }

  const char* action = doc["action"] | "";
  int durationSec = doc["duration"] | 0;

  if (strcmp(action, "START") == 0 && durationSec > 0) {
    setPump(true, (unsigned long)durationSec * 1000UL);
  } else if (strcmp(action, "STOP") == 0) {
    setPump(false);
  }
}

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi OK, IP=");
  Serial.println(WiFi.localIP());
}


static void connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  

  while (!mqtt.connected()) {
    // client id randomizat ca să nu se ciocnească mai mulți
    String clientId = "esp32-irrigation-" + String((uint32_t)ESP.getEfuseMac(), HEX);
   if (mqtt.connect(clientId.c_str())) {
  Serial.println("MQTT connected");
  mqtt.subscribe(TOPIC_CMD);
  Serial.print("Subscribed to: ");
  Serial.println(TOPIC_CMD);
  publishStatus("OFF");
} else {
  delay(500);
}


  }
}

static int readSoilPercent() {
  // Potențiometru 0..4095 -> 0..100
  int raw = analogRead(PIN_POT);
  int pct = (int)lround((raw / 4095.0) * 100.0);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

static void publishTelemetry(int soilPct) {
  StaticJsonDocument<256> doc;
  doc["soilPct"] = soilPct;               // umiditate simulată
  doc["pumpOn"] = pumpOn;
  doc["ts"] = (long)(millis() / 1000);

  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(TOPIC_TELEMETRY, buf, n);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("SERIAL OK - BOOT");

  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_PUMP, LOW);

  analogReadResolution(12);

  connectWiFi();

  Serial.println("Connecting MQTT...");
  connectMQTT();
  Serial.println("MQTT OK");
}


void loop() {

  //Serial.println("SERIAL TEST: tick");
  delay(1000);
  
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  // Oprire automată când expiră durata
  if (pumpOn && pumpUntilMs > 0 && millis() >= pumpUntilMs) {
    setPump(false);
  }

  // Publish telemetrie
  if (millis() - lastPublishMs >= PUBLISH_INTERVAL_MS) {
    lastPublishMs = millis();
    int soil = readSoilPercent();
    publishTelemetry(soil);
  }


}
