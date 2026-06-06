# Sistema SCADA con Nodo Edge IIoT para Monitoreo de Variables Eléctricas

![Dashboard](docs/dashboard_alarma.jpg)

Sistema de monitoreo industrial en tiempo real que integra un nodo Edge basado en **ESP32**, comunicación segura **MQTT/TLS**, protocolo industrial **Modbus TCP** y un dashboard **SCADA web** desarrollado con Python y Flask. Diseñado para supervisión de variables eléctricas en motores industriales con detección automática de alarmas y notificaciones por email.

---

## Arquitectura del Sistema

```
ESP32 (Nodo Edge)
    │
    │  MQTT/TLS (HiveMQ Cloud)
    ▼
Python — monitor.py
    │
    ├── SQLite (histórico)
    │
    └── Flask — app.py
              │
              ▼
        Dashboard Web
        (Chart.js — tiempo real)
              │
              ▼
        Alertas Email (SMTP)
```

---

## Características

- **Nodo Edge ESP32** con adquisición de variables eléctricas, portal cautivo para configuración WiFi sin reprogramar, sincronización horaria NTP y LEDs indicadores de estado
- **Comunicación MQTT/TLS** sobre HiveMQ Cloud con publicación de JSON estructurado cada 2 segundos
- **Dashboard SCADA web** con gauges estilo velocímetro, gráficas de tendencia en tiempo real, panel de alarmas y historial de eventos
- **Base de datos SQLite** para almacenamiento histórico de mediciones
- **Sistema de alarmas** con detección por flanco ascendente y notificación automática por email
- **PCB diseñada en KiCad 8** para integración con sensor PZEM-004T (voltaje, corriente, potencia, factor de potencia)
- Arquitectura preparada para conexión con **PLC vía Modbus TCP**

---

## Variables Monitoreadas

| Variable | Unidad | Rango normal | Umbral de alarma |
|---|---|---|---|
| Voltaje de línea | V | 200 – 240 | > 228 V |
| Corriente de carga | A | 0 – 6 | > 5 A |
| Potencia activa | W | calculada | — |
| Factor de potencia | — | 0.75 – 0.95 | — |
| Temperatura | °C | 25 – 65 | > 65 °C |

---

## Stack Tecnológico

### Firmware ESP32
| Librería | Versión | Función |
|---|---|---|
| WiFiManager | tzapu | Portal cautivo para configuración WiFi |
| PubSubClient | Nick O'Leary | Cliente MQTT |
| ArduinoJson | Benoit Blanchon | Serialización JSON |
| WiFiClientSecure | ESP32 built-in | Conexión TLS/SSL |
| time.h | ESP32 built-in | Sincronización NTP |

### Backend Python
| Librería | Función |
|---|---|
| paho-mqtt | Suscripción al broker MQTT |
| flask | Servidor web y API REST |
| sqlite3 | Base de datos histórica |
| smtplib | Envío de alertas por email |

### Frontend
| Tecnología | Función |
|---|---|
| Chart.js | Gráficas de tendencia en tiempo real |
| HTML/CSS/JS | Dashboard SCADA responsivo |
| SVG | Gauges estilo velocímetro animados |

---

## Estructura del Proyecto

```
sistema-scada-iiot/
│
├── monitor.py                  # Suscriptor MQTT + SQLite + alertas email
├── app.py                      # Servidor Flask + API REST
├── .gitignore
│
├── templates/
│   └── index.html              # Dashboard SCADA (gauges, gráficas, alarmas)
│
├── static/
│   └── chart.js                # Librería Chart.js (local)
│
├── nodo_edge_iiot.ino/
│   └── nodo_edge_iiot.ino.ino  # Firmware ESP32 (MQTT/TLS + NTP + portal cautivo)
│
├── pcb/
│   ├── nodo edge IIot.kicad_sch   # Esquemático
│   ├── nodo edge IIot.kicad_pcb   # Layout PCB
│   └── nodo edge IIot.kicad_pro   # Proyecto KiCad
│
└── docs/
    ├── dashboard_motor_off.jpg    # Dashboard — motor apagado
    ├── dashboard_motor_on.jpg     # Dashboard — motor encendido
    └── dashboard_alarma.jpg       # Dashboard — alarma activa
```

---

## Cómo Correr el Proyecto

### Requisitos
```bash
pip install flask paho-mqtt
```

### 1. Iniciar el monitor MQTT
```bash
python monitor.py
```

### 2. Iniciar el servidor web
```bash
python app.py
```

### 3. Abrir el dashboard
```
http://127.0.0.1:5000
```

### 4. Publicar datos de prueba
Conectarse al broker HiveMQ y publicar al topic `industrial/motor/variables`:
```json
{
  "voltaje": 220.5,
  "corriente": 4.5,
  "potencia": 773.6,
  "factor_potencia": 0.78,
  "temperatura": 42.3,
  "estado_motor": 1,
  "timestamp": "2026-06-05 10:01:00"
}
```

---

## PCB — Nodo Edge IIoT

Diseñada en **KiCad 8** para integración con sensor **PZEM-004T** de medición de variables eléctricas reales.

**Componentes principales:**
- ESP32 DevKit 38 pines
- Conector PZEM-004T (UART — GPIO16/GPIO17)
- LEDs indicadores: WiFi (GPIO23), MQTT (GPIO22), Alarma (GPIO21)
- Condensadores de filtrado
- Conector DC Jack 5V

---

## Capturas del Dashboard

### Motor apagado — operación normal
![Motor OFF](docs/dashboard_motor_off.jpg)

### Motor encendido — carga nominal
![Motor ON](docs/dashboard_motor_on.jpg)

### Alarma activa — sobretemperatura
![Alarma](docs/dashboard_alarma.jpg)

---

## Autor

**Ricardo Felipe Bravo**
Ingeniero Electrónico — Universidad de Nariño
Orientado al sector eléctrico e industrial

📩 ricardofelipebravo12@gmail.com
🔗 [LinkedIn](https://www.linkedin.com/in/ricardo-bravo)
