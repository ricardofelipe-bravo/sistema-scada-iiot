#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <WiFiManager.h>

// HiveMQ Cloud
const char* MQTT_HOST      = "512b0b58cfc548398f97951eeb91530e.s1.eu.hivemq.cloud";
const int   MQTT_PORT      = 8883;
const char* MQTT_USER      = "edgecloud";
const char* MQTT_PASS      = "Esp32cloud";
const char* MQTT_TOPIC     = "industrial/motor/variables";
const char* MQTT_CLIENT_ID = "ESP32_Motor_01";

// NTP
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = -18000;
const int   DST_OFFSET = 0;

// LEDs
#define LED_WIFI   23
#define LED_MQTT   22
#define LED_ALARMA 21

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

bool  estado_motor = false;
float temperatura  = 25.0;
int   ciclo        = 0;

// ─── Portal Cautivo ─────────────────────────────────────
void iniciarPortalCautivo() {
    WiFiManager wifiManager;
    
    // Personalizar página del portal
    wifiManager.setAPCallback([](WiFiManager* mgr) {
        Serial.println("Portal cautivo activo");
        Serial.println("Conectate a: NodoEdge_Config");
        Serial.println("IP: 192.168.4.1");
        // Parpadeo rápido LED WiFi = portal activo
        for (int i = 0; i < 10; i++) {
            digitalWrite(LED_WIFI, HIGH);
            delay(100);
            digitalWrite(LED_WIFI, LOW);
            delay(100);
        }
    });

    // Timeout 3 minutos para configurar
    wifiManager.setConfigPortalTimeout(180);

    // Intenta conectar con credenciales guardadas
    // Si falla, levanta portal con nombre "NodoEdge_Config"
    if (!wifiManager.autoConnect("NodoEdge_Config", "nodo1234")) {
        Serial.println("Timeout portal cautivo — reiniciando");
        delay(3000);
        ESP.restart();
    }

    Serial.println("WiFi conectado via portal: " + WiFi.localIP().toString());
    digitalWrite(LED_WIFI, HIGH);
}

// ─── NTP ────────────────────────────────────────────────
void sincronizarNTP() {
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    Serial.print("Sincronizando NTP");
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) {
        Serial.print(".");
        delay(500);
    }
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.println("\nNTP sincronizado: " + String(buf));
}

// ─── MQTT ───────────────────────────────────────────────
void conectarMQTT() {
    espClient.setInsecure();
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setBufferSize(512);

    while (!mqttClient.connected()) {
        Serial.print("Conectando a HiveMQ...");
        if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
            Serial.println("Conectado");
            digitalWrite(LED_MQTT, HIGH);
        } else {
            Serial.print("Error: ");
            Serial.println(mqttClient.state());
            digitalWrite(LED_MQTT, LOW);
            delay(3000);
        }
    }
}

// ─── Publicar variables ─────────────────────────────────
void publicarVariables() {
    ciclo++;

    if (ciclo % 60 == 0) {
        estado_motor = !estado_motor;
        Serial.println(estado_motor ? ">>> Motor ON <<<" : ">>> Motor OFF <<<");
    }

    float voltaje = 220.0 + random(-30, 30) / 10.0;
    float corriente, factor_potencia;

    if (estado_motor) {
        corriente       = 4.5  + random(-10, 10)  / 100.0;
        factor_potencia = 0.78 + random(-5,  5)   / 1000.0;
        temperatura     = min(80.0f, temperatura + random(2, 5) / 10.0f);
        digitalWrite(LED_ALARMA, temperatura > 65 ? HIGH : LOW);
    } else {
        corriente       = 0.3  + random(-5, 5)    / 100.0;
        factor_potencia = 0.92 + random(-3, 3)    / 1000.0;
        temperatura     = max(25.0f, temperatura - random(1, 3) / 10.0f);
        digitalWrite(LED_ALARMA, LOW);
    }

    float potencia = voltaje * corriente * factor_potencia;

    struct tm timeinfo;
    char timeStr[20] = "sin_sync";
    if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    StaticJsonDocument<256> doc;
    doc["voltaje"]         = round(voltaje         * 10)   / 10.0;
    doc["corriente"]       = round(corriente        * 100)  / 100.0;
    doc["potencia"]        = round(potencia         * 10)   / 10.0;
    doc["factor_potencia"] = round(factor_potencia  * 1000) / 1000.0;
    doc["temperatura"]     = round(temperatura      * 10)   / 10.0;
    doc["estado_motor"]    = estado_motor ? 1 : 0;
    doc["timestamp"]       = timeStr;

    char buffer[256];
    serializeJson(doc, buffer);
    mqttClient.publish(MQTT_TOPIC, buffer);
    Serial.println(buffer);
}

// ─── Setup ──────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(0));

    pinMode(LED_WIFI,   OUTPUT);
    pinMode(LED_MQTT,   OUTPUT);
    pinMode(LED_ALARMA, OUTPUT);

    digitalWrite(LED_WIFI,   LOW);
    digitalWrite(LED_MQTT,   LOW);
    digitalWrite(LED_ALARMA, LOW);

    iniciarPortalCautivo();  // WiFi con portal cautivo
    sincronizarNTP();
    conectarMQTT();
}

// ─── Loop ───────────────────────────────────────────────
void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        digitalWrite(LED_WIFI, LOW);
        iniciarPortalCautivo();
    }
    if (!mqttClient.connected()) {
        digitalWrite(LED_MQTT, LOW);
        conectarMQTT();
    }
    mqttClient.loop();
    publicarVariables();
    delay(2000);
}