# Sistema IoT para Monitoreo Ambiental

¡Bienvenidos! Somos estudiantes de Ingeniería en Telecomunicaciones de la Universidad Técnica del Norte. Nuestro equipo está conformado por Ariel David Gonzales Bueno, Jordan Raúl Manguay Chacua, Anthony Esteban Ibujes Tupiza, Pablo Alexander Nastacuaz Padilla y Cristopher Alexander Ruales Armijos. Este proyecto implementa un sistema para capturar datos ambientales con sensores DHT11 y HW080, procesados por ESP32 y retransmitidos mediante un router de borde basado en tecnología IEEE 802.11 hacia un servidor web.

## Introducción

Este sistema consta de nodos sensores basados en ESP32 que recolectan datos de temperatura, humedad y otros parámetros ambientales usando sensores DHT11 y HW080. La arquitectura modular permite escalar fácilmente la red, incorporando más nodos o sensores según las necesidades. La información recolectada se transmite a través del estándar IEEE 802.11 hacia un router de borde, que actúa como enlace hacia un servidor web central.

### Tecnologías empleadas

- **IEEE 802.11**: Protocolo estándar para conectividad inalámbrica.
- **HTTP**: Protocolo cliente-servidor para la transferencia de datos.
- **UDP**: Protocolo ligero y no orientado a conexión para la transmisión de datos.
- **UHCP**: Protocolo liviano para asignación de direcciones IP y configuración básica.
- **MQTT**: Protocolo ligero de mensajería para comunicación entre nodos y servidor. Permite un intercambio eficiente de datos en tiempo real.
- **MQTT-SN**: Variante de MQTT optimizada para dispositivos pequeños y redes con limitaciones de recursos, especialmente útil para aplicaciones IoT.

## Topología del Sistema

### Nodos Sensores (Clientes)
**Hardware**:
- ESP32-WROOM
- Sensor DHT11
- Sensor HW080 (sensor de humedad del suelo y temperatura)

**Software**:
- RIOT OS: Gestión de la red y transmisión de datos.
- Controladores para el sensor DHT11, HW080 y manejo de GPIO.
- MQTT para la transmisión de datos al servidor.

### Router de Borde
**Hardware**:
- ESP32 actuando como puente entre la red local y la global.

**Software**:
- RIOT OS: Configuración de interfaces IPv6 (local y global).
- MQTT-SN para la comunicación eficiente con el servidor web.

### Servidor Web
**Funcionalidad**:
- Procesamiento y almacenamiento de datos enviados por los nodos.
- Visualización de datos en tiempo real mediante una interfaz web.

**Acceso Remoto**:
- Consultar datos ambientales de manera centralizada.

## Conexión de los Sensores al ESP32

### Conexión del Sensor DHT11:

- **VCC**: Conectar a 3.3V del ESP32.
- **GND**: Conectar a GND del ESP32.
- **DATA**: Conectar al pin GPIO4 del ESP32.

### Conexión del Sensor HW080:

- **VCC**: Conectar a 3.3V del ESP32.
- **GND**: Conectar a GND del ESP32.
- **DATA (Humedad y Temperatura)**: Conectar al pin GPIO12 del ESP32.

El sensor HW080 es un sensor combinado de temperatura y humedad del suelo, lo cual permite mejorar el monitoreo en el ambiente de las plantaciones. El sensor transmite la humedad del suelo y la temperatura del aire al ESP32 para su posterior procesamiento y envío a la plataforma web.

## Implementación de MQTT y MQTT-SN

### MQTT

MQTT es un protocolo de mensajería ligero y basado en el modelo cliente-servidor, ideal para IoT debido a su eficiencia en el uso de recursos. En este sistema, los nodos sensores se conectan a un servidor MQTT para enviar datos de temperatura, humedad ambiental y humedad del suelo en tiempo real.

- **Cliente MQTT**: El ESP32 actúa como cliente MQTT y publica los datos de los sensores en un tópico específico.
- **Servidor MQTT**: Recibe y distribuye los mensajes a los clientes suscritos.

### MQTT-SN

MQTT-SN es una versión más liviana de MQTT, diseñada específicamente para redes de dispositivos con capacidades limitadas. En este sistema, utilizamos MQTT-SN para los nodos sensores que tienen menos recursos y para mejorar la eficiencia de la red.

- **Cliente MQTT-SN**: El ESP32 también puede actuar como cliente MQTT-SN, permitiendo la transmisión eficiente de datos con bajo consumo de energía y optimización de ancho de banda.
- **Gateway MQTT-SN**: Un gateway se encarga de convertir los mensajes de MQTT-SN a MQTT estándar, permitiendo la interoperabilidad con servidores y clientes MQTT tradicionales.

## Documentación Detallada

Para más detalles sobre el proceso de implementación, consulta la documentación completa [aquí](https://www.hackster.io/527576/sistema-de-monitoreo-ambiental-con-sensor-dht11-esp32-042fad?auth_token=95395441f291313b32ccad79eaf4570f).

---
