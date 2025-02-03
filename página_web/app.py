from flask import Flask, render_template, jsonify, request
from flask_socketio import SocketIO
import mysql.connector
from datetime import datetime, timedelta
import paho.mqtt.client as mqtt
import json
import re
import socket  # Importar el módulo socket para enviar UDP

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

# Configuración de la base de datos
db = mysql.connector.connect(
    host="localhost",
    user="user",
    password="password"
    database="proyecto_iot"
)

# Configuración MQTT
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC = "sensores/#"

# Configuración UDP
UDP_IP = "2001:db8:a::2"  # Dirección IPv6 del ESP32
UDP_PORT = 12345  # Puerto UDP en el ESP32

# Diccionario para almacenar datos temporales de los nodos
node_data = {}

def extract_node_number(topic):
    match = re.search(r'nodo_(\d+)', topic)
    return int(match.group(1)) if match else None

def on_connect(client, userdata, flags, rc):
    print(f"Conectado al broker MQTT con código: {rc}")
    client.subscribe(MQTT_TOPIC)
    print(f"Suscrito a {MQTT_TOPIC}")

def on_message(client, userdata, msg):
    try:
        print("\n--- Mensaje recibido ---")
        print(f"Topic: {msg.topic}")
        print(f"Payload: {msg.payload}")
        
        topic_parts = msg.topic.split('/')
        if len(topic_parts) < 3:
            print(f"Formato de topic no válido: {msg.topic}")
            return

        node_number = extract_node_number(topic_parts[1])
        if node_number is None:
            print(f"No se pudo extraer número de nodo de: {topic_parts[1]}")
            return

        sensor_type = topic_parts[2]
        
        if node_number not in node_data:
            node_data[node_number] = {
                'node_number': node_number,
                'name': f'Nodo {node_number}',
                'temperature': None,
                'humidity': None,
                'moisture': None,
                'stress_state': None,
            }

        # Procesar el payload según el tipo de sensor
        if sensor_type == 'nombre':
            node_data[node_number]['name'] = json.loads(msg.payload.decode())
        elif sensor_type == 'temperatura':
            node_data[node_number]['temperature'] = float(msg.payload.decode())
        elif sensor_type == 'humedad':
            node_data[node_number]['humidity'] = float(msg.payload.decode())
        elif sensor_type == 'humedad_suelo':
            node_data[node_number]['moisture'] = float(msg.payload.decode())
        elif sensor_type == 'estado_estres':
            node_data[node_number]['stress_state'] = int(msg.payload.decode())

        current_timestamp = datetime.now()
        timestamp_str = current_timestamp.strftime('%Y-%m-%d %H:%M:%S')

        # Almacenar datos del DHT11
        if node_data[node_number]['temperature'] is not None and node_data[node_number]['humidity'] is not None:
            data_to_save = {
                'node_number': node_data[node_number]['node_number'],
                'name': node_data[node_number]['name'],
                'temperature': node_data[node_number]['temperature'],
                'humidity': node_data[node_number]['humidity'],
                'stress_state': node_data[node_number]['stress_state'],
                'timestamp': timestamp_str
            }

            cursor = db.cursor()
            query = """
                INSERT INTO dht11_data (node_number, name, temperature, humidity, estado_estres, timestamp)
                VALUES (%s, %s, %s, %s, %s, %s)
            """
            cursor.execute(query, (
                data_to_save['node_number'],
                data_to_save['name'],
                data_to_save['temperature'],
                data_to_save['humidity'],
                data_to_save['stress_state'],
                data_to_save['timestamp']
            ))
            db.commit()
            cursor.close()

            socketio.emit('new_data', data_to_save)
            print("--- Datos DHT11 almacenados ---")
            print(f"Datos: {data_to_save}")

            node_data[node_number]['temperature'] = None
            node_data[node_number]['humidity'] = None

        # Almacenar datos del HW080
        if node_data[node_number]['moisture'] is not None:
            data_to_save = {
                'node_number': node_data[node_number]['node_number'],
                'name': node_data[node_number]['name'],
                'moisture': node_data[node_number]['moisture'],
                'timestamp': timestamp_str
            }

            cursor = db.cursor()
            query = """
                INSERT INTO hw080_data (node_number, name, moisture, estado_estres, timestamp)
                VALUES (%s, %s, %s, %s, %s)
            """
            cursor.execute(query, (
                data_to_save['node_number'],
                data_to_save['name'],
                data_to_save['moisture'],
                node_data[node_number]['stress_state'] or 1,  # Usar stress_state si está disponible, de lo contrario, usar 1
                data_to_save['timestamp']
            ))
            db.commit()
            cursor.close()

            socketio.emit('new_data', data_to_save)
            print("--- Datos HW080 almacenados ---")
            print(f"Datos: {data_to_save}")

            node_data[node_number]['moisture'] = None

    except Exception as e:
        print("--- Error procesando mensaje MQTT ---")
        print(f"Error: {e}")
        print(f"Payload que causó el error: {msg.payload}")

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/data')
def get_data():
    cursor = db.cursor(dictionary=True)
    
    start_date = request.args.get('start_date', default=(datetime.now() - timedelta(days=1)).strftime('%Y-%m-%d %H:%M:%S'))
    end_date = request.args.get('end_date', default=datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
    
    query_dht11 = """
    SELECT DISTINCT 'dht11' as sensor_type, node_number, name, temperature, humidity, estado_estres, timestamp
    FROM dht11_data
    WHERE timestamp BETWEEN %s AND %s
    AND node_number IN (1, 2, 3, 4)
    ORDER BY node_number, timestamp
    """
    
    query_hw080 = """
    SELECT DISTINCT 'hw080' as sensor_type, node_number, name, moisture, estado_estres, timestamp
    FROM hw080_data
    WHERE timestamp BETWEEN %s AND %s
    AND node_number IN (1, 2, 3, 4)
    ORDER BY node_number, timestamp
    """
    
    # Ejecutar las consultas
    cursor.execute(query_dht11, (start_date, end_date))
    dht11_data = cursor.fetchall()
    
    cursor.execute(query_hw080, (start_date, end_date))
    hw080_data = cursor.fetchall()
    
    cursor.close()
    
    # Combinar los resultados
    combined_data = dht11_data + hw080_data
    
    return jsonify(combined_data)

@app.route('/api/last_n_values')
def get_last_n_values():
    cursor = db.cursor(dictionary=True)
    n = int(request.args.get('n', default=10))
    
    query_dht11 = """
    SELECT DISTINCT 'dht11' as sensor_type, node_number, name, temperature, humidity, estado_estres, timestamp 
    FROM dht11_data 
    WHERE node_number IN (1, 2, 3, 4)
    ORDER BY timestamp DESC
    LIMIT %s
    """
    
    query_hw080 = """
    SELECT DISTINCT 'hw080' as sensor_type, node_number, name, moisture, estado_estres, timestamp 
    FROM hw080_data 
    WHERE node_number IN (1, 2, 3, 4)
    ORDER BY timestamp DESC
    LIMIT %s
    """
    
    cursor.execute(query_dht11, (n,))
    dht11_data = cursor.fetchall()
    
    cursor.execute(query_hw080, (n,))
    hw080_data = cursor.fetchall()
    
    cursor.close()
    
    combined_data = dht11_data + hw080_data
    
    return jsonify(combined_data)

@socketio.on('connect')
def handle_connect():
    print("Cliente conectado")

@app.route('/actuator', methods=['POST'])
def control_actuator():
    data = request.json
    action = data.get('action')

    if action not in ['ON', 'OFF']:
        return jsonify({'error': 'Invalid action'}), 400

    # Enviar el comando UDP al ESP32
    with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as sock:
        sock.sendto(action.encode(), (UDP_IP, UDP_PORT))
        print(f"Sent {action} to ESP32")

    return jsonify({'message': f'Actuator turned {action}'}), 200

def start_mqtt_client():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.loop_start()
        print(f"Cliente MQTT iniciado y conectado a {MQTT_BROKER}:{MQTT_PORT}")
    except Exception as e:
        print(f"Error conectando al broker MQTT: {e}")

if __name__ == '__main__':
    start_mqtt_client()
    socketio.run(app, host='0.0.0.0', port=5000, debug=True)
