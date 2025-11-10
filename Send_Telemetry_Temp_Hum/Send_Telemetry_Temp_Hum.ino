#include <time.h>
#include <cstdlib>
#include <string.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Libraries for OLED Display
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "AzIoTSasToken.h"
#include "iot_configs.h"

// Temperature and Humidity sensor
#include "Adafruit_SHT31.h"

// Configuration structure to store device settings
struct DeviceConfig {
  bool sendTemperature;
  bool sendHumidity;
  bool isActive;
  String azurePrimaryKey;
  unsigned long telemetryFrequency;
};

DeviceConfig deviceConfig;

bool enableHeater = false;
Adafruit_SHT31 sht31 = Adafruit_SHT31();

#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))

// Translate iot_configs.h defines into variables used by the sample
static const char* ssid = IOT_CONFIG_WIFI_SSID;
static const char* password = IOT_CONFIG_WIFI_PASSWORD;
static const char* host = IOT_CONFIG_IOTHUB_FQDN;
static const char* mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char* device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[256];
static char mqtt_username[512];
static char mqtt_password[1024];

static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;

static uint32_t telemetry_send_count = 0;
static String telemetry_payload = "{}";
#define INCOMING_DATA_BUFFER_SIZE 128
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

// Global device key - will be set from config or from iot_configs.h
// static char device_key_buffer[128];
// static AzIoTSasToken sasToken(
//     &client,
//     AZ_SPAN_FROM_BUFFER(device_key_buffer),
//     AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
//     AZ_SPAN_FROM_BUFFER(mqtt_password));

#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define PST_TIME_ZONE -5
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 0
#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)
#define UNIX_TIME_NOV_13_2017 1510592825
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define DEFAULT_TELEMETRY_FREQUENCY 2000  // Default 2 seconds

#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

char* scrtxt[] = { ".", ".", ".", ".", ".", ".", ".", "CEA-IoT" };
char buff[21];
int last_text = 7;

// === WATCHDOG Y CONTROL DE CONEXIÓN ===
// unsigned long lastSuccessfulSend = 0;
// const unsigned long MAX_TIME_WITHOUT_SEND = 30 * 60 * 1000; // 30 minutos
// const unsigned long WIFI_CHECK_INTERVAL = 10000; // 10 segundos
// const unsigned long MQTT_RECONNECT_INTERVAL = 30000; // 30 segundos
// unsigned long lastWifiCheck = 0;
// unsigned long lastMqttReconnectAttempt = 0;
// bool wifiConnected = false;
// bool mqttConnected = false;
// int consecutiveFailures = 0;
// const int MAX_CONSECUTIVE_FAILURES = 5;

// === WATCHDOG Y CONTROL DE CONEXIÓN ===
unsigned long lastSuccessfulSend = 0;
const unsigned long MAX_TIME_WITHOUT_SEND = 30 * 60 * 1000; // 30 minutos
const unsigned long WIFI_CHECK_INTERVAL = 10000; // 10 segundos
unsigned long lastWifiCheck = 0;
int consecutiveFailures = 0;
const int MAX_CONSECUTIVE_FAILURES = 5;


unsigned long telemetryFrequencyGlobal = DEFAULT_TELEMETRY_FREQUENCY;

// Function to update OLED display
static void printfifo() {
  int i;
  display.clearDisplay();
  display.setCursor(0, 0);
  for (i = 1; i <= 8; i++) {
    if (last_text + i <= 7)
      display.println(scrtxt[last_text + i]);
    else
      display.println(scrtxt[last_text + i - 8]);
  }
  display.display();
}

static void insertfifo(char* new_text) {
  if (last_text < 7) {
    scrtxt[last_text + 1] = new_text;
    last_text = last_text + 1;
  } else {
    scrtxt[0] = new_text;
    last_text = 0;
  }
}

// Function to get current timestamp in ISO 8601 format
String getCurrentTimestamp_Local() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time for timestamp");
    return "2024-01-01T00:00:00Z";  // Fallback timestamp
  }

  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timestamp);
}

// Function to get current timestamp in ISO 8601 format in UTC
String getCurrentTimestamp() {
  time_t now;
  struct tm timeinfo;
  
  // Get current time in UTC
  time(&now);
  gmtime_r(&now, &timeinfo);  // Use gmtime_r for thread safety

  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timestamp);
}

// Function to fetch device configuration from endpoint - DEBUG VERSION
bool fetchDeviceConfiguration() {
  Serial.println("=== DEBUG: Starting fetchDeviceConfiguration ===");
  insertfifo("Fetching config...");
  printfifo();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: WiFi not connected");
    insertfifo("WiFi not connected");
    printfifo();
    return false;
  }

  HTTPClient http;
  bool success = false;

  // Configure endpoint
  Serial.println("DEBUG: Connecting to endpoint...");
  String endpoint = "http://4.150.10.133:8080/appandroid/configambiental/readconfig";
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);  // 10 second timeout

  // Prepare JSON payload
  String jsonPayload = "{\"ID_Provision_Ambiental\":" + String(ID_Provision_Ambiental) + "}";
  Serial.println("DEBUG: Sending POST request...");
  Serial.println("Payload: " + jsonPayload);

  // Send POST request
  int httpResponseCode = http.POST(jsonPayload);
  Serial.println("DEBUG: HTTP Response Code: " + String(httpResponseCode));

  if (httpResponseCode >= 200 && httpResponseCode < 300) {
    String response = http.getString();
    Serial.println("DEBUG: Response received: " + response);

    // Parse JSON response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);

    if (!error && doc["ok"] == true) {
      // Extract configuration values
      deviceConfig.sendTemperature = doc["data"]["Temperatura"].as<bool>();
      deviceConfig.sendHumidity = doc["data"]["Humedad"].as<bool>();
      deviceConfig.isActive = doc["data"]["Activo"].as<bool>();

      // CAMBIO AQUÍ: Usar SASTOKEN en lugar de Azure_Primary_Key
      deviceConfig.azurePrimaryKey = doc["data"]["SASTOKEN"].as<String>();
      deviceConfig.telemetryFrequency = doc["data"]["Frecuencia_Envio_Segundos"].as<unsigned long>() * 1000;
      // DEBUG: Verificar los valores extraídos
      Serial.println("DEBUG: Extracted values:");
      Serial.println("- Temperatura: " + String(deviceConfig.sendTemperature));
      Serial.println("- Humedad: " + String(deviceConfig.sendHumidity));
      Serial.println("- Activo: " + String(deviceConfig.isActive));
      Serial.println("- SASTOKEN length: " + String(deviceConfig.azurePrimaryKey.length()));
      Serial.println("- Frecuencia: " + String(deviceConfig.telemetryFrequency));

      Serial.println("DEBUG: Configuration parsed successfully");
      success = true;

      Serial.println("DEBUG: Configuration parsed successfully");
      success = true;
    } else {
      Serial.println("ERROR: Failed to parse JSON or response not OK");
      if (error) {
        Serial.println("JSON Error: " + String(error.c_str()));
      }
    }
  } else {
    Serial.println("ERROR: HTTP request failed with code: " + String(httpResponseCode));
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Response: " + response);
    }
  }

  http.end();

  if (!success) {
    Serial.println("DEBUG: Using default configuration as fallback");
    // Fallback to default configuration
    deviceConfig.sendTemperature = true;
    deviceConfig.sendHumidity = true;
    deviceConfig.isActive = true;
    deviceConfig.telemetryFrequency = DEFAULT_TELEMETRY_FREQUENCY;
    deviceConfig.azurePrimaryKey = IOT_CONFIG_SASTOKEN;
    
  } else {
    Serial.println("DEBUG: Using configuration from endpoint");
    // Ya se configuró con los valores del endpoint
  }

  // Use Azure Primary Key from config
  //deviceConfig.azurePrimaryKey.toCharArray(device_key_buffer, sizeof(device_key_buffer));

  Serial.println("=== Final Configuration ===");
  Serial.println("- Send Temperature: " + String(deviceConfig.sendTemperature));
  Serial.println("- Send Humidity: " + String(deviceConfig.sendHumidity));
  Serial.println("- Is Active: " + String(deviceConfig.isActive));
  Serial.println("- Telemetry Frequency: " + String(deviceConfig.telemetryFrequency) + " ms");
  Serial.println("- Azure Key Length: " + String(deviceConfig.azurePrimaryKey.length()));
  Serial.println("===========================");

  if (success) {
    insertfifo("Config loaded OK");
  } else {
    insertfifo("Using default config");
  }
  printfifo();

  return success;
}

static void connectToWiFi() {
  Serial.println("Connecting to WIFI SSID " + String(ssid));

  insertfifo("Cntng to WIFI SSID");
  printfifo();
  strcpy(buff, ssid);
  insertfifo(buff);
  printfifo();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
  }

  Serial.println("");
  delay(2000);
  Serial.println("WiFi connected, IP address: " + WiFi.localIP().toString());
  insertfifo("WiFi connected IP:");
  printfifo();
  String localIp = WiFi.localIP().toString();
  strcpy(buff, localIp.c_str());
  insertfifo(buff);
  printfifo();
}

static void initializeTime() {
  Serial.println("Setting time using SNTP");
  insertfifo("Stng time using SNTP");
  printfifo();

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  Serial.println("Time initialized!");
  Serial.println(ctime(&now));

  insertfifo("Time initialized!");
  printfifo();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
  char locTime[20];
  sprintf(locTime, "%02d-%02d-%04d %02d:%02d:%02d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  insertfifo(locTime);
  printfifo();
}

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
  (void)handler_args;
  (void)base;
  (void)event_id;

  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

  switch (event->event_id) {
    int i, r;
    case MQTT_EVENT_ERROR:
      Serial.println("MQTT event MQTT_EVENT_ERROR");
      // Mostrar detalles del error
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        Serial.printf("Transport error: errno %d\n", event->error_handle->esp_transport_sock_errno);
        Serial.printf("Transport error: esp_tls_last_esp_err: 0x%x\n", event->error_handle->esp_tls_last_esp_err);
        Serial.printf("Transport error: esp_tls_stack_err: 0x%x\n", event->error_handle->esp_tls_stack_err);
        Serial.printf("Transport error: esp_tls_cert_verify_flags: 0x%x\n", event->error_handle->esp_tls_cert_verify_flags);
      }
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        Serial.printf("Connection refused error: 0x%x\n", event->error_handle->connect_return_code);
      }
      break;
      // ... el resto del código del event handler se mantiene igual
  }
}

bool checkWiFiConnection() {
   if (WiFi.status() == WL_CONNECTED) {
    // Verificar que realmente tenemos internet
    if (WiFi.localIP().toString() == "0.0.0.0") {
      Serial.println("WiFi connected but no IP address");
      return false;
    }
    return true;
  } else {
    Serial.println("WiFi disconnected. Attempting to reconnect...");
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Lost");
    display.println("Reconnecting...");
    display.display();
    
    // Intentar reconectar
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { // 10 segundos máximo
      delay(500);
      attempts++;
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi reconnected!");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi Reconnected");
      display.display();
      delay(2000);
      return true;
    } else {
      Serial.println("\nWiFi reconnection failed");
      return false;
    }
  }
}

bool checkMqttConnection() {
  if (mqtt_client == NULL) {
    Serial.println("MQTT client not initialized");
    return false;
  }
  
  // Método NO INTRUSIVO para verificar conexión MQTT
  // Simplemente intentamos enviar el telemetry normal sin forzar reconexión
  // La reconexión automática del MQTT client se encargará de reconectar si es necesario
  
  return true; // Dejamos que MQTT client maneje la reconexión automáticamente

}

void systemWatchdog() {
  unsigned long currentTime = millis();
  
  // Verificar WiFi periódicamente (cada 30 segundos en lugar de 10)
  if (currentTime - lastWifiCheck > 30000) {
    lastWifiCheck = currentTime;
    
    if (!checkWiFiConnection()) {
      consecutiveFailures++;
      Serial.println("WiFi check failed. Consecutive failures: " + String(consecutiveFailures));
    } else {
      // Resetear fallos solo si WiFi está bien
      consecutiveFailures = 0;
    }
  }
  
  // Verificar si no se han enviado datos en 30 minutos
  if (lastSuccessfulSend > 0 && (currentTime - lastSuccessfulSend) > MAX_TIME_WITHOUT_SEND) {
    Serial.println("CRITICAL: No data sent in 30 minutes. Restarting...");
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WATCHDOG: 30min");
    display.println("No data sent");
    display.println("Restarting...");
    display.display();
    delay(5000);
    
    ESP.restart();
  }
  
  // Si hay demasiados fallos consecutivos (15 = 7.5 minutos), reiniciar
  if (consecutiveFailures >= 15) {
    Serial.println("CRITICAL: Too many consecutive failures. Restarting...");
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("TOO MANY ERRORS");
    display.println("Restarting...");
    display.display();
    delay(5000);
    
    ESP.restart();
  }
}

static void initializeIoTHubClient() {
  Serial.println("=== Initializing IoT Hub Client ===");
  Serial.println("Host: " + String(host));
  Serial.println("Device ID: " + String(device_id));

  if (strlen(host) == 0 || strlen(device_id) == 0) {
    Serial.println("ERROR: Host or Device ID is empty!");
    Serial.println("Check your iot_configs.h file");
    return;
  }

  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  Serial.println("Creating az_span for host and device_id...");
  az_span host_span = az_span_create((uint8_t*)host, strlen(host));
  az_span device_span = az_span_create((uint8_t*)device_id, strlen(device_id));

  Serial.printf("Host span length: %d\n", az_span_size(host_span));
  Serial.printf("Device span length: %d\n", az_span_size(device_span));

  az_result result = az_iot_hub_client_init(&client, host_span, device_span, &options);
  if (az_result_failed(result)) {
    Serial.printf("Failed initializing Azure IoT Hub client. Error code: 0x%08x\n", result);
    return;
  }
  Serial.println("Azure IoT Hub client initialized successfully");

  size_t client_id_length;
  result = az_iot_hub_client_get_client_id(&client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length);
  if (az_result_failed(result)) {
    Serial.printf("Failed getting client id. Error code: 0x%08x\n", result);
    return;
  }
  mqtt_client_id[client_id_length] = '\0';
  Serial.println("Client ID generated successfully: " + String(mqtt_client_id));

  size_t username_length;
  result = az_iot_hub_client_get_user_name(&client, mqtt_username, sizeof(mqtt_username) - 1, &username_length);
  if (az_result_failed(result)) {
    Serial.printf("Failed to get MQTT username. Error code: 0x%08x\n", result);
    Serial.printf("Username buffer size: %d\n", sizeof(mqtt_username));
    return;
  }
  mqtt_username[username_length] = '\0';
  Serial.println("Username generated successfully: " + String(mqtt_username));

  Serial.println("=== IoT Hub Client Initialization Complete ===");
}

// Función de emergencia para generar SAS Token localmente si el del endpoint falla
String generateEmergencySasToken() {
  Serial.println("WARNING: Using emergency local SAS Token generation");

  // Usar la clave del dispositivo para generar un token temporal
  // Esto es solo para emergencias mientras se corrige el backend
  AzIoTSasToken emergencyToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));

  if (emergencyToken.Generate(60) == 0) {  // 60 minutos
    az_span token_span = emergencyToken.Get();
    String token = String((const char*)az_span_ptr(token_span), az_span_size(token_span));
    Serial.println("DEBUG: Emergency SAS Token generated: " + token);
    return token;
  } else {
    Serial.println("ERROR: Failed to generate emergency SAS Token");
    return "";
  }
}

static int initializeMqttClient() {
  Serial.println("=== Initializing MQTT Client ===");

  if (strlen(mqtt_client_id) == 0 || strlen(mqtt_username) == 0) {
    Serial.println("ERROR: Missing MQTT client ID or username");
    return 1;
  }

  // DEBUG: Verificar que tenemos el SASTOKEN
  Serial.println("DEBUG: SASTOKEN length: " + String(deviceConfig.azurePrimaryKey.length()));
  Serial.println("DEBUG: SASTOKEN content: " + deviceConfig.azurePrimaryKey);

  // CORRECCIÓN: Extraer y limpiar el SASTOKEN
  String sasToken;
  String fullSasToken = deviceConfig.azurePrimaryKey;

  // Buscar la parte que contiene "SharedAccessSignature sr="
  int startIndex = fullSasToken.indexOf("SharedAccessSignature sr=");

  if (startIndex != -1) {
    // Encontramos el inicio correcto del token
    sasToken = fullSasToken.substring(startIndex);
    Serial.println("DEBUG: Clean SAS Token found: " + sasToken);
  } else {
    // Intentar otro formato: buscar "sr=" directamente
    startIndex = fullSasToken.indexOf("sr=");
    if (startIndex != -1) {
      // Retroceder para incluir "SharedAccessSignature "
      int sharedAccessIndex = fullSasToken.lastIndexOf("SharedAccessSignature", startIndex);
      if (sharedAccessIndex != -1) {
        sasToken = fullSasToken.substring(sharedAccessIndex);
        // Limpiar posibles duplicados
        sasToken.replace("SharedAccessSignature=", "SharedAccessSignature ");
        Serial.println("DEBUG: Reformed SAS Token: " + sasToken);
      } else {
        sasToken = "SharedAccessSignature " + fullSasToken.substring(startIndex);
        Serial.println("DEBUG: Reconstructed SAS Token: " + sasToken);
      }
    } else {
      Serial.println("ERROR: Could not extract valid SAS Token from SASTOKEN");
      return 1;
    }
  }

  // Verificar que el token no esté vacío
  if (sasToken.length() == 0) {
    Serial.println("ERROR: Extracted SAS Token is empty");
    sasToken = generateEmergencySasToken();
    if (sasToken.length() == 0) {
      return 1;
    }
  }

  // DEBUG: Mostrar el token final que se usará
  Serial.println("DEBUG: Final SAS Token length: " + String(sasToken.length()));
  Serial.println("DEBUG: Final SAS Token: " + sasToken);

  esp_mqtt_client_config_t mqtt_config = {};
  mqtt_config.broker.address.uri = mqtt_broker_uri;
  mqtt_config.broker.address.port = mqtt_port;
  mqtt_config.credentials.client_id = mqtt_client_id;
  mqtt_config.credentials.username = mqtt_username;
  mqtt_config.credentials.authentication.password = sasToken.c_str();
  mqtt_config.session.keepalive = 30;
  mqtt_config.session.disable_clean_session = 0;
  mqtt_config.network.disable_auto_reconnect = false;
  mqtt_config.broker.verification.certificate = (const char*)ca_pem;
  mqtt_config.broker.verification.certificate_len = (size_t)ca_pem_len;
  mqtt_config.network.reconnect_timeout_ms = 10000; // 10 segundos para reconexión

  Serial.println("MQTT Config:");
  Serial.println("- URI: " + String(mqtt_config.broker.address.uri));
  Serial.println("- Port: " + String(mqtt_config.broker.address.port));
  Serial.println("- Client ID: " + String(mqtt_config.credentials.client_id));
  Serial.println("- Username: " + String(mqtt_config.credentials.username));
  Serial.println("- Password length: " + String(sasToken.length()));

  mqtt_client = esp_mqtt_client_init(&mqtt_config);
  if (mqtt_client == NULL) {
    Serial.println("Failed creating mqtt client");
    return 1;
  }

  esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);

  Serial.println("Starting MQTT client...");
  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK) {
    Serial.printf("Could not start mqtt client; error code: 0x%08x (%s)\n",
                  start_result, esp_err_to_name(start_result));
    return 1;
  } else {
    Serial.println("MQTT client started successfully");
    return 0;
  }
}

static void establishConnection() {
  connectToWiFi();
  initializeTime();

  // Fetch device configuration - si falla, usa defaults
  bool configLoaded = fetchDeviceConfiguration();

  if (!configLoaded) {
    Serial.println("WARNING: Using default configuration");
  }

  initializeIoTHubClient();

  int mqttResult = initializeMqttClient();
  if (mqttResult != 0) {
    Serial.println("WARNING: MQTT initialization failed, but continuing...");
  }

   // Inicializar watchdog
  lastSuccessfulSend = millis();
  lastWifiCheck = millis();
  consecutiveFailures = 0;

  // Calcular frecuencia para mostrar
  telemetryFrequencyGlobal = deviceConfig.telemetryFrequency > 0 ? 
                           deviceConfig.telemetryFrequency : DEFAULT_TELEMETRY_FREQUENCY;

  insertfifo("SETUP DONE");
  printfifo();

  // Convertir frecuencia a string y mostrar
  char freqText[20];
  if (telemetryFrequencyGlobal >= 1000) {
    // Mostrar en segundos si es mayor a 1000 ms
    sprintf(freqText, "Freq: %lu s", telemetryFrequencyGlobal / 1000);
  } else {
    // Mostrar en milisegundos
    sprintf(freqText, "Freq: %lu ms", telemetryFrequencyGlobal);
  }
  insertfifo(freqText);
  printfifo();

  Serial.println("Frequency set to: " + String(telemetryFrequencyGlobal) + " ms");
}

// Initialize OLED display
static void initializeOLED() {
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  printfifo();
}

static void sendTelemetry() {
  readSensorsAndBuildPayload();
 /// Verificar estado del dispositivo
  if (!deviceConfig.isActive) {
    Serial.println("Device is inactive, skipping telemetry send");
    return;
  }

  // Verificar conexiones
  if (!checkWiFiConnection()) {
    Serial.println("Cannot send telemetry: WiFi unavailable");
    return;
  }
  
  if (!checkMqttConnection()) {
    Serial.println("Cannot send telemetry: MQTT unavailable");
    return;
  }

  Serial.println("Sending telemetry ...");

  // Crear telemetry topic
  String telemetry_topic = "devices/" + String(device_id) + "/messages/events/";

  Serial.println("Topic: " + telemetry_topic);
  Serial.println("Payload: " + telemetry_payload);

  int message_id = esp_mqtt_client_publish(
    mqtt_client,
    telemetry_topic.c_str(),
    telemetry_payload.c_str(),
    telemetry_payload.length(),
    MQTT_QOS1,
    DO_NOT_RETAIN_MSG);

  if (message_id > 0) {
    Serial.println("Message published successfully, ID: " + String(message_id));
    lastSuccessfulSend = millis(); // Actualizar timestamp de último envío exitoso
    consecutiveFailures = 0; // Resetear contador de fallos
    
    
   
  } else {
    Serial.println("Failed publishing telemetry. Error: " + String(message_id));
    consecutiveFailures++;
    
    // Actualizar display con error
    insertfifo("Send FAILED");
    printfifo();
  }
}

static void readSensorsAndBuildPayload() {
  // Only read sensors if device is active
  if (!deviceConfig.isActive) {
    Serial.println("Device is inactive, skipping sensor reading");
    telemetry_payload = "{\"status\":\"inactive\"}";
    return;
  }

  float temperature = NAN;
  float humidity = NAN;

  // Read temperature if enabled
  if (deviceConfig.sendTemperature) {
    temperature = sht31.readTemperature();
    if (!isnan(temperature)) {
      Serial.print("Temp *C = ");
      Serial.print(temperature);
      Serial.print("\t\t");
    } else {
      Serial.println("Failed to read temperature");
    }
  }

  // Read humidity if enabled
  if (deviceConfig.sendHumidity) {
    humidity = sht31.readHumidity();
    if (!isnan(humidity)) {
      Serial.print("Hum. % = ");
      Serial.println(humidity);
    } else {
      Serial.println("Failed to read humidity");
    }
  }

  // Get current timestamp
  String timestamp = getCurrentTimestamp();

  // Build JSON payload dynamically based on enabled sensors
  // INCLUYENDO TimeStamp_Sensor como requiere tu backend
  String payload = "{";
  payload += "\"ID_Provision_Ambiental\": " + String(ID_Provision_Ambiental) + ",";
  payload += "\"TimeStamp_Sensor\": \"" + timestamp + "\",";
  payload += "\"msgCount\": " + String(telemetry_send_count++);

  if (deviceConfig.sendTemperature && !isnan(temperature)) {
    payload += ", \"Temperatura\":" + String(temperature);
  }

  if (deviceConfig.sendHumidity && !isnan(humidity)) {
    payload += ", \"Humedad\":" + String(humidity);
  }

  payload += "}";

  telemetry_payload = payload;

  // Update OLED display
  char new_text[50];
  if (!isnan(temperature) && !isnan(humidity)) {
    sprintf(new_text, "Temp: %.1f°C Hum: %.1f%%", temperature, humidity);
  } else if (!isnan(temperature)) {
    sprintf(new_text, "Temp: %.1f°C", temperature);
  } else if (!isnan(humidity)) {
    sprintf(new_text, "Hum: %.1f%%", humidity);
  } else {
    sprintf(new_text, "No sensor data");
  }
  insertfifo(new_text);
  printfifo();
}

void printLibraryVersions() {
  Serial.println("=== Library Versions ===");
  Serial.println("ESP32 Arduino Core: " + String(ESP_ARDUINO_VERSION_STR));
  Serial.println("Azure SDK Version: " + String(AZ_SDK_VERSION_STRING));
  Serial.println("========================");
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // Give time for Serial Monitor

  Serial.println("\n=== CEA-IoT Device Starting ===");
  printLibraryVersions();

  // Configuration check
  Serial.println("\n=== Configuration Check ===");
  Serial.println("SSID: " + String(ssid));
  Serial.println("IoT Hub: " + String(host));
  Serial.println("Device ID: " + String(device_id));
  Serial.printf("Device Key length: %d\n", strlen(IOT_CONFIG_DEVICE_KEY));
  Serial.println("ID Provision Ambiental: " + String(ID_Provision_Ambiental));

  Serial.println("CEA-IoT");
  Serial.printf(" ESP32 Arduino core version: %s\n", ESP_ARDUINO_VERSION_STR);

  // Initialize default device key
  //strcpy(device_key_buffer, IOT_CONFIG_DEVICE_KEY);

  initializeOLED();
  establishConnection();

  // Initialize SHT31 sensor
  if (!sht31.begin(0x44)) {
    Serial.println("Couldn't find SHT31");
    while (1) delay(1);
  }

  Serial.print("Heater Enabled State: ");
  if (sht31.isHeaterEnabled())
    Serial.println("ENABLED");
  else
    Serial.println("DISABLED");
}

void loop() {
   // Ejecutar watchdog en cada iteración
  systemWatchdog();
  
  // Enviar telemetría
  sendTelemetry();
  
  // Usar frecuencia configurada o default si no está configurada
  unsigned long frequency = deviceConfig.telemetryFrequency > 0 ? 
                           deviceConfig.telemetryFrequency : DEFAULT_TELEMETRY_FREQUENCY;
  
  delay(frequency);
  
  // Actualizar configuración periódicamente (cada 5 ciclos para no saturar)
  static int configCounter = 0;
  if (configCounter++ >= 5) {
    fetchDeviceConfiguration();
    configCounter = 0;
  }
}