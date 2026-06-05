from flask import Flask, jsonify, render_template
import sqlite3
import os

app = Flask(__name__, 
    template_folder=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'templates'),
    static_folder=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'static'))

def get_db():
    conn = sqlite3.connect(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'monitor.db'))
    conn.row_factory = sqlite3.Row
    return conn

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/datos')
def datos():
    conn = get_db()
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM mediciones ORDER BY id DESC LIMIT 50')
    rows = cursor.fetchall()
    conn.close()
    data = []
    for row in rows:
        data.append({
            'timestamp': row['timestamp'],
            'voltaje': row['voltaje'],
            'corriente': row['corriente'],
            'potencia': row['potencia'],
            'temperatura': row['temperatura'],
            'estado_motor': row['estado_motor'],
            'alarma_voltaje': row['alarma_voltaje'],
            'alarma_corriente': row['alarma_corriente'],
            'alarma_temperatura': row['alarma_temperatura']
        })
    return jsonify(data)

@app.route('/api/ultimo')
def ultimo():
    conn = get_db()
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM mediciones ORDER BY id DESC LIMIT 1')
    row = cursor.fetchone()
    conn.close()
    if row:
        return jsonify(dict(row))
    return jsonify({})

@app.route('/api/alarmas')
def alarmas():
    conn = get_db()
    cursor = conn.cursor()
    cursor.execute('''SELECT timestamp, voltaje, corriente, temperatura,
                     alarma_voltaje, alarma_corriente, alarma_temperatura
                     FROM mediciones 
                     WHERE alarma_voltaje=1 OR alarma_corriente=1 OR alarma_temperatura=1
                     ORDER BY id DESC LIMIT 20''')
    rows = cursor.fetchall()
    conn.close()
    data = []
    for row in rows:
        data.append({
            'timestamp': row['timestamp'],
            'voltaje': row['voltaje'],
            'corriente': row['corriente'],
            'temperatura': row['temperatura'],
            'alarma_voltaje': row['alarma_voltaje'],
            'alarma_corriente': row['alarma_corriente'],
            'alarma_temperatura': row['alarma_temperatura']
        })
    return jsonify(data)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)