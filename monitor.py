import paho.mqtt.client as mqtt
import ssl
import json
import sqlite3
from datetime import datetime
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

# HiveMQ Cloud
MQTT_HOST  = "512b0b58cfc548398f97951eeb91530e.s1.eu.hivemq.cloud"
MQTT_PORT  = 8883
MQTT_USER  = "edgecloud"
MQTT_PASS  = "Esp32cloud"
MQTT_TOPIC = "industrial/motor/variables"

# Email
EMAIL_ORIGEN   = "tucorreo@gmail.com"
EMAIL_DESTINO  = "tucorreo@gmail.com"
EMAIL_PASSWORD = "oueh qvfp qqfb bmwr"

# Estado alarmas anteriores para flanco ascendente
alarma_t_anterior = 0
alarma_v_anterior = 0
alarma_i_anterior = 0

# Base de datos
conn = sqlite3.connect('monitor.db')
cursor = conn.cursor()
cursor.execute('''CREATE TABLE IF NOT EXISTS mediciones (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT,
    voltaje REAL,
    corriente REAL,
    potencia REAL,
    factor_potencia REAL,
    temperatura REAL,
    estado_motor INTEGER,
    alarma_voltaje INTEGER,
    alarma_corriente INTEGER,
    alarma_temperatura INTEGER
)''')
conn.commit()

def enviar_alerta(tipo, valor, umbral):
    try:
        msg = MIMEMultipart()
        msg['From']    = EMAIL_ORIGEN
        msg['To']      = EMAIL_DESTINO
        msg['Subject'] = f"[ALARMA SCADA] {tipo} fuera de rango"
        cuerpo = f"""
Sistema SCADA — Monitor de Variables Eléctricas
------------------------------------------------
ALARMA ACTIVA: {tipo}
Valor actual:  {valor:.1f}
Umbral:        {umbral}
Timestamp:     {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
------------------------------------------------
Mensaje automático del sistema de monitoreo.
        """
        msg.attach(MIMEText(cuerpo, 'plain'))
        server = smtplib.SMTP('smtp.gmail.com', 587)
        server.starttls()
        server.login(EMAIL_ORIGEN, EMAIL_PASSWORD)
        server.send_message(msg)
        server.quit()
        print(f">>> Email enviado: ALARMA {tipo} <<<")
    except Exception as e:
        print(f"Error enviando email: {e}")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Conectado a HiveMQ Cloud")
        client.subscribe(MQTT_TOPIC)
        print(f"Suscrito a: {MQTT_TOPIC}")
    else:
        print(f"Error conexion MQTT: {rc}")

def on_message(client, userdata, msg):
    global alarma_t_anterior, alarma_v_anterior, alarma_i_anterior

    try:
        data = json.loads(msg.payload.decode())

        voltaje         = data.get('voltaje', 0)
        corriente       = data.get('corriente', 0)
        potencia        = data.get('potencia', 0)
        factor_potencia = data.get('factor_potencia', 0)
        temperatura     = data.get('temperatura', 0)
        estado_motor    = data.get('estado_motor', 0)
        timestamp       = data.get('timestamp', datetime.now().strftime('%Y-%m-%d %H:%M:%S'))

        # Alarmas
        alarma_v = 1 if voltaje     > 228  else 0
        alarma_i = 1 if corriente   > 5.0  else 0
        alarma_t = 1 if temperatura > 65   else 0

        # Email en flanco ascendente
        if alarma_t == 1 and alarma_t_anterior == 0:
            enviar_alerta("Sobretemperatura", temperatura, "65°C")
        if alarma_v == 1 and alarma_v_anterior == 0:
            enviar_alerta("Sobrevoltaje", voltaje, "228V")
        if alarma_i == 1 and alarma_i_anterior == 0:
            enviar_alerta("Sobrecorriente", corriente, "5.0A")

        alarma_t_anterior = alarma_t
        alarma_v_anterior = alarma_v
        alarma_i_anterior = alarma_i

        # Guardar en SQLite
        cursor.execute('''INSERT INTO mediciones
            (timestamp, voltaje, corriente, potencia, factor_potencia,
             temperatura, estado_motor, alarma_voltaje, alarma_corriente, alarma_temperatura)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''',
            (timestamp, voltaje, corriente, potencia, factor_potencia,
             temperatura, estado_motor, alarma_v, alarma_i, alarma_t))
        conn.commit()

        print(f"V:{voltaje} I:{corriente} P:{potencia} FP:{factor_potencia} T:{temperatura} Motor:{'ON' if estado_motor else 'OFF'} | Alarmas V={alarma_v} I={alarma_i} T={alarma_t}")

    except Exception as e:
        print(f"Error procesando mensaje: {e}")

# Cliente MQTT
client = mqtt.Client()
client.username_pw_set(MQTT_USER, MQTT_PASS)
client.tls_set(cert_reqs=ssl.CERT_NONE)
client.on_connect = on_connect
client.on_message = on_message

print("Conectando a HiveMQ Cloud...")
client.connect(MQTT_HOST, MQTT_PORT, 60)
client.loop_forever()