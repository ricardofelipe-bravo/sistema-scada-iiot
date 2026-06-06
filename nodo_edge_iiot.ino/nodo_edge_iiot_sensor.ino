/*
 * ============================================================
 *  NODO EDGE IIoT — VERSIÓN PRODUCCIÓN (con sensor PZEM-004T)
 * ============================================================
 * Hardware requerido:
 *   - ESP32 DevKit 38 pines
 *   - Sensor PZEM-004T (conectado a GPIO16/GPIO17)
 *   - LEDs: WiFi (GPIO23), MQTT (GPIO22), Alarma (GPIO21)
 *
 * Librerías requeridas:
 *   - WiFiManager     (tzapu)
 *   - PubSubClient    (Nick O'Leary)
 *   - ArduinoJson     (Benoit Blanchon)
 *   - PZEM004Tv30     (Jakub Mandula)
 *
 * Conexión PZEM-004T:
 *   PZEM TX  →  ESP32 GPIO16 (RX2)
 *   PZEM RX  →  ESP32 GPIO17 (TX2)
 *   PZEM VCC →  ESP32 VIN (5V)
 *   PZEM GND →  ESP32 GND
 * ============================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <PZEM004Tv30.h>
#include <time.h>

// ─── HiveMQ Cloud ───────────────────────────────────────────
const char* MQTT_HOST      = "512b0b58cfc548398f97951eeb91530e.s1.eu.hivemq.cloud";
const int   MQTT_PORT      = 8883;
const char* MQTT_USER      = "edgecloud";
const char* MQTT_PASS      = "Esp32cloud";
const char* MQTT_TOPIC     = "industrial/motor/variables";
const char* MQTT_CLIENT_ID = "ESP32_Motor_01";

// ─── NTP ────────────────────────────────────────────────────
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = -18000;  // Colombia UTC-5
const int   DST_OFFSET = 0;

// ─── LEDs ───────────────────────────────────────────────────
#define LED_WIFI   23
#define LED_MQTT   22
#define LED_ALARMA 21

// ─── PZEM-004T (Serial2: RX=16, TX=17) ─────────────────────
PZEM004Tv30 pzem(Serial2, 16, 17);

WiFiClientSecure espClient;
PubSubClient     mqttClient(espClient);

// ─── Portal Cautivo ─────────────────────────────────────────
void iniciarPortalCautivo() {
    WiFiManager wifiManager;

    wifiManager.setAPCallback([](WiFiManager* mgr) {
        Serial.println("==============================");
        Serial.println("  PORTAL DE CONFIGURACION");
        Serial.println("  Red WiFi:   NodoEdge_Config");
        Serial.println("  Contrasena: nodo1234");
        Serial.println("  IP portal:  192.168.4.1");
        Serial.println("==============================");
        // Parpadeo rapido = portal activo
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

// ─── Leer PZEM y publicar ───────────────────────────────────
void leerYPublicar() {
    // Leer sensor
    float voltaje         = pzem.voltage();
    float corriente       = pzem.current();
    float potencia        = pzem.power();
    float energia         = pzem.energy();
    float frecuencia      = pzem.frequency();
    float factor_potencia = pzem.pf();

    // Verificar lectura valida
    if (isnan(voltaje) || isnan(corriente)) {
        Serial.println("[ERROR] Sensor no responde — verifica conexiones PZEM-004T");
        digitalWrite(LED_ALARMA, HIGH);
        delay(500);
        digitalWrite(LED_ALARMA, LOW);
        return;
    }

    // Alarma si hay sobretemperatura o sobrevoltaje
    bool alarma = (voltaje > 228.0 || corriente > 5.0);
    digitalWrite(LED_ALARMA, alarma ? HIGH : LOW);

    // Timestamp NTP
    struct tm timeinfo;
    char timeStr[20] = "sin_sync";
    if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    // JSON
    StaticJsonDocument<256> doc;
    doc["voltaje"]         = round(voltaje         * 10)   / 10.0;
    doc["corriente"]       = round(corriente        * 100)  / 100.0;
    doc["potencia"]        = round(potencia         * 10)   / 10.0;
    doc["energia"]         = round(energia          * 100)  / 100.0;
    doc["frecuencia"]      = round(frecuencia       * 10)   / 10.0;
    doc["factor_potencia"] = round(factor_potencia  * 1000) / 1000.0;
    doc["estado_motor"]    = (corriente > 0.5) ? 1 : 0;
    doc["timestamp"]       = timeStr;

    char buffer[256];
    serializeJson(doc, buffer);
    mqttClient.publish(MQTT_TOPIC, buffer);
    Serial.println(buffer);
}

// ─── Setup ──────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(LED_WIFI,   OUTPUT);
    pinMode(LED_MQTT,   OUTPUT);
    pinMode(LED_ALARMA, OUTPUT);
    digitalWrite(LED_WIFI,   LOW);
    digitalWrite(LED_MQTT,   LOW);
    digitalWrite(LED_ALARMA, LOW);

    Serial.println("\n============================");
    Serial.println("  NODO EDGE IIoT — PRODUCCION");
    Serial.println("  Sensor: PZEM-004T");
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
    leerYPublicar();
    delay(2000);
}
