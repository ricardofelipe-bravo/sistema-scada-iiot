/*
 * ============================================================
 *  NODO EDGE IIoT — VERSIÓN SIMULACIÓN (sin sensor físico)
 * ============================================================
 * Uso: pruebas del sistema completo sin hardware de medición.
 * Simula un motor industrial 1HP / 220V con comportamiento
 * realista — arranque, carga nominal, calentamiento y alarmas.
 *
 * Para pasar a producción: usar nodo_edge_iiot_sensor.ino
 *
 * Hardware requerido:
 *   - ESP32 DevKit 38 pines
 *   - LEDs: WiFi (GPIO23), MQTT (GPIO22), Alarma (GPIO21)
 *
 * Librerías requeridas:
 *   - WiFiManager     (tzapu)
 *   - PubSubClient    (Nick O'Leary)
 *   - ArduinoJson     (Benoit Blanchon)
 * ============================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <time.h>

// ─── HiveMQ Cloud ───────────────────────────────────────────
const char* MQTT_HOST      = "512b0b58cfc548398f97951eeb91530e.s1.eu.hivemq.cloud";
const int   MQTT_PORT      = 8883;
const char* MQTT_USER      = "edgecloud";
const char* MQTT_PASS      = "Esp32cloud";
const char* MQTT_TOPIC     = "industrial/motor/variables";
const char* MQTT_CLIENT_ID = "ESP32_Motor_01_SIM";

// ─── NTP ────────────────────────────────────────────────────
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = -18000;
const int   DST_OFFSET = 0;

// ─── LEDs ───────────────────────────────────────────────────
#define LED_WIFI   23
#define LED_MQTT   22
#define LED_ALARMA 21

WiFiClientSecure espClient;
PubSubClient     mqttClient(espClient);

// ─── Estado simulado ────────────────────────────────────────
bool  estado_motor = false;
float temperatura  = 25.0;
int   ciclo        = 0;

// ─── Portal Cautivo ─────────────────────────────────────────
void iniciarPortalCautivo() {
    WiFiManager wifiManager;

    wifiManager.setAPCallback([](WiFiManager* mgr) {
        Serial.println("==============================");
        Serial.println("  PORTAL DE CONFIGURACION");
        Serial.println("  Red WiFi:   NodoEdge_Config");
        Serial.println("  Contrasena: nodo1234");
        Serial.println("  IP portal:  192.168.4.1");
        Serial.println("  MODO: SIMULACION");
        Serial.println("==============================");
        for (int i = 0; i < 20; i++) {
            digitalWrite(LED_WIFI, HIGH); delay(100);
            digitalWrite(LED_WIFI, LOW);  delay(100);
        }
    });

    wifiManager.setConfigPortalTimeout(180);

    if (!wifiManager.autoConnect("NodoEdge_Config", "nodo1234")) {
        Serial.println("Timeout portal — reiniciando...");
        delay(3000);
        ESP.restart();
    }

    digitalWrite(LED_WIFI, HIGH);
    Serial.println("WiFi conectado: " + WiFi.localIP().toString());
}

// ─── NTP ────────────────────────────────────────────────────
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
    Serial.println("\nNTP OK: " + String(buf));
}

// ─── MQTT ───────────────────────────────────────────────────
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

// ─── Simular y publicar ─────────────────────────────────────
void simularYPublicar() {
    ciclo++;

    // Motor cambia cada 60 ciclos (120 seg)
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
    } else {
        corriente       = 0.3  + random(-5, 5)    / 100.0;
        factor_potencia = 0.92 + random(-3, 3)    / 1000.0;
        temperatura     = max(25.0f, temperatura - random(1, 3) / 10.0f);
    }

    float potencia = voltaje * corriente * factor_potencia;

    bool alarma = (voltaje > 228.0 || corriente > 5.0 || temperatura > 65.0);
    digitalWrite(LED_ALARMA, alarma ? HIGH : LOW);

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

// ─── Setup ──────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(0));

    pinMode(LED_WIFI,   OUTPUT);
    pinMode(LED_MQTT,   OUTPUT);
    pinMode(LED_ALARMA, OUTPUT);
    digitalWrite(LED_WIFI,   LOW);
    digitalWrite(LED_MQTT,   LOW);
    digitalWrite(LED_ALARMA, LOW);

    Serial.println("\n============================");
    Serial.println("  NODO EDGE IIoT — SIMULACION");
    Serial.println("  Motor 1HP / 220V simulado");
    Serial.println("============================");

    iniciarPortalCautivo();
    sincronizarNTP();
    conectarMQTT();
}

// ─── Loop ───────────────────────────────────────────────────
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
    simularYPublicar();
    delay(2000);
}
