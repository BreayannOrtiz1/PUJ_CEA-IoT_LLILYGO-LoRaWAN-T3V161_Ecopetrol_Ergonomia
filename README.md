🌡️ CEA-IoT Telemetry Device
Dispositivo IoT para monitoreo ambiental que envía datos de temperatura y humedad a Azure IoT Hub, con configuración remota y display OLED integrado.

📋 Descripción General
Este firmware permite a un dispositivo ESP32 con sensor SHT31:

📊 Medir temperatura y humedad ambiental

☁️ Enviar telemetría a Azure IoT Hub

⚙️ Obtener configuración remota desde un endpoint

📱 Mostrar datos en pantalla OLED

🔄 Manejar fallos de conexión de forma elegante

🛠️ Hardware Requerido
Microcontrolador: ESP32

Sensor: Adafruit SHT31 (Temperatura/Humedad)

Display: OLED SSD1306 (128x64)

Conexiones:

SDA → GPIO 21

SCL → GPIO 22

🔄 Flujo de Operación
1. Inicialización






2. Configuración Remota
El dispositivo obtiene configuración desde:

text
http://4.150.10.133:8080/appandroid/configambiental/readconfig
Parámetros configurables:

✅ Envío de temperatura

✅ Envío de humedad

🔄 Estado activo/inactivo

⏱️ Frecuencia de envío (segundos)

🔑 Token SAS para Azure

3. Ciclo Principal
cpp
while(true) {
    leerSensores();
    construirPayloadJSON();
    enviarAzureIoT();
    esperar(frecuencia_configurada);
}
🛡️ Sistema de Fallbacks
🔄 Fallback de Configuración
Si el endpoint falla → usa configuración por defecto

Configuración por defecto:

Temperatura: ✅ Habilitada

Humedad: ✅ Habilitada

Estado: ✅ Activo

Frecuencia: ⏱️ 2 segundos

🔑 Fallback de Autenticación
Si el SAS Token del endpoint es inválido → genera token localmente

Usa IOT_CONFIG_DEVICE_KEY del archivo iot_configs.h

🌐 Fallback de Conexión
WiFi desconectado → reintenta conexión

Azure IoT Hub no disponible → continúa operación local

Display OLED siempre muestra estado actual

📦 Estructura del Payload
json
{
  "ID_Provision_Ambiental": "ID_UNICO",
  "TimeStamp_Sensor": "2024-01-01T12:00:00Z",
  "msgCount": 42,
  "Temperatura": 23.5,
  "Humedad": 65.2
}
⚙️ Configuración
Archivo iot_configs.h
cpp
#define IOT_CONFIG_WIFI_SSID "Tu_SSID"
#define IOT_CONFIG_WIFI_PASSWORD "Tu_Password"
#define IOT_CONFIG_IOTHUB_FQDN "tu-hub.azure-devices.net"
#define IOT_CONFIG_DEVICE_ID "dispositivo-01"
#define IOT_CONFIG_DEVICE_KEY "tu-device-key"
#define ID_Provision_Ambiental "ID_UNICO_DISPOSITIVO"
🎯 Estados del Display
El OLED muestra información en tiempo real:

🔌 Estado de conexión WiFi

⏰ Hora sincronizada

⚙️ Estado de configuración

🌡️ Datos de sensores

📤 Intentos de envío

🐛 Depuración
El código incluye logs extensivos:

✅ Estado de conexiones

🔍 Parsing de configuración JSON

📊 Métricas de envío Azure

❌ Errores detallados con códigos

🔧 Personalización
Modificar Sensores
cpp
// En readSensoresAndBuildPayload():
if (deviceConfig.sendTemperature) {
    // Tu código para nuevo sensor
}
Cambiar Endpoint
cpp
// En fetchDeviceConfiguration():
String endpoint = "tu-nuevo-endpoint";
Ajustar Frecuencia
La frecuencia se controla desde el endpoint o usa el valor por defecto en DEFAULT_TELEMETRY_FREQUENCY

📈 Monitoreo
Métricas clave:

📊 Tasa de envíos exitosos

⏱️ Latencia de conexión

🔄 Reintentos de conexión

❌ Fallos de sensor

🆘 Solución de Problemas
El dispositivo no envía datos
Verificar conexión WiFi en display

Confirmar configuración remota cargada

Revisar logs Azure IoT Hub

Verificar token SAS válido

El display no muestra información
Verificar conexiones I2C

Confirmar inicialización OLED

Revisar mensajes en monitor serial

Configuración no se actualiza
Verificar endpoint accesible

Revisar formato JSON respuesta

Confirmar ID_Provision_Ambiental correcto

📄 Licencia
Proyecto desarrollado para CEA-IoT. Consultar sobre términos de uso y modificación.
