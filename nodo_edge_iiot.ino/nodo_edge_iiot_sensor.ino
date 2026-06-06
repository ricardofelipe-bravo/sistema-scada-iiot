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
 *
 * Procesamiento local (Edge):
 *   - Filtrado de lecturas inválidas del sensor
 *   - Detección local de alarmas sin depender del servidor
 *   - Buffer circular de 20 muestras cuando no hay conexión MQTT
 *   - Reenvío automático del buffer al reconectar
 *   - LED de alarma activado localmente sin necesidad de red
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

// ─── Buffer local (Edge processing) ────────────────────────
// Almacena hasta 20 muestras cuando no hay conexión MQTT
#define BUFFER_SIZE 20
String bufferLocal[BUFFER_SIZE];
int    bufferHead  = 0;
int    bufferCount = 0;

void guardarEnBuffer(const char* payload) {
    bufferLocal[bufferHead] = String(payload);
    bufferHead = (bufferHead + 1) % BUFFER_SIZE;
    if (bufferCount < BUFFER_SIZE) bufferCount++;
    Serial.printf("[BUFFER] %d/%d muestras almacenadas localmente\n",
                  bufferCount, BUFFER_SIZE);
}

void vaciarBuffer() {
    if (bufferCount == 0) return;
    int inicio = (bufferHead - bufferCount + BUFFER_SIZE) % BUFFER_SIZE;
    Serial.printf("[BUFFER] Enviando %d muestras almacenadas...\n", bufferCount);
    for (int i = 0; i < bufferCount; i++) {
        int idx = (inicio + i) % BUFFER_SIZE;
        mqttClient.publish(MQTT_TOPIC, bufferLocal[idx].c_str());
        delay(100);
    }
    bufferCount = 0;
    bufferHead  = 0;
    Serial.println("[BUFFER] Buffer vaciado correctamente");
}

// ─── Filtro de lecturas (Edge processing) ───────────────────
// Promedio móvil de las últimas 3 lecturas válidas
#define FILTRO_N 3
float filtroVoltaje[FILTRO_N]   = {220, 220, 220};
float filtroCorriente[FILTRO_N] = {0, 0, 0};
int   filtroIdx = 0;

float aplicarFiltro(float* arr, float nuevo) {
    arr[filtroIdx % FILTRO_N] = nuevo;
    float suma = 0;
    for (int i = 0; i < FILTRO_N; i++) suma += arr[i];
    return suma / FILTRO_N;
}

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

    int intentos = 0;
    while (!mqttClient.connected() && intentos < 5) {
        Serial.print("Conectando a HiveMQ...");
        if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
            Serial.println("Conectado");
            digitalWrite(LED_MQTT, HIGH);
            vaciarBuffer(); // reenviar muestras almacenadas
        } else {
            Serial.printf("Error: %d — reintento %d/5\n",
                          mqttClient.state(), ++intentos);
            digitalWrite(LED_MQTT, LOW);
            delay(3000);
        }
    }
}

// ─── Leer PZEM, filtrar y publicar ──────────────────────────
void leerFiltrarYPublicar() {
    // Leer sensor
    float voltajeBruto   = pzem.voltage();
    float corrienteBruta = pzem.current();
    float potencia       = pzem.power();
    float energia        = pzem.energy();
    float frecuencia     = pzem.frequency();
    float factorPotencia = pzem.pf();

    // Verificar lectura válida — procesamiento local
    if (isnan(voltajeBruto) || isnan(corrienteBruta)) {
        Serial.println("[ERROR] Sensor no responde — verifica conexiones PZEM-004T");
        digitalWrite(LED_ALARMA, HIGH);
        delay(200);
        digitalWrite(LED_ALARMA, LOW);
        return;
    }

    // Aplicar filtro promedio móvil — reduce ruido del sensor
    float voltaje   = aplicarFiltro(filtroVoltaje,   voltajeBruto);
    float corriente = aplicarFiltro(filtroCorriente, corrienteBruta);
    filtroIdx++;

    // Detección local de alarmas — sin depender del servidor
    bool alarmaVoltaje   = (voltaje   > 228.0);
    bool alarmaCorreinte = (corriente > 5.0);
    bool alarma          = alarmaVoltaje || alarmaCorreinte;
    digitalWrite(LED_ALARMA, alarma ? HIGH : LOW);

    if (alarma) {
        Serial.printf("[ALARMA LOCAL] V=%.1f I=%.2f\n", voltaje, corriente);
    }

    // Timestamp NTP
    struct tm timeinfo;
    char timeStr[20] = "sin_sync";
    if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    // Construir JSON
    StaticJsonDocument<256> doc;
    doc["voltaje"]         = round(voltaje       * 10)   / 10.0;
    doc["corriente"]       = round(corriente      * 100)  / 100.0;
    doc["potencia"]        = round(potencia       * 10)   / 10.0;
    doc["energia"]         = round(energia        * 100)  / 100.0;
    doc["frecuencia"]      = round(frecuencia     * 10)   / 10.0;
    doc["factor_potencia"] = round(factorPotencia * 1000) / 1000.0;
    doc["estado_motor"]    = (corriente > 0.5) ? 1 : 0;
    doc["timestamp"]       = timeStr;

    char buffer[256];
    serializeJson(doc, buffer);

    // Publicar o guardar en buffer local
    if (mqttClient.connected()) {
        mqttClient.publish(MQTT_TOPIC, buffer);
        Serial.println(buffer);
    } else {
        guardarEnBuffer(buffer);
    }
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
    Serial.println("  Edge: filtro + buffer local");
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
    leerFiltrarYPublicar();
    delay(2000);
}
