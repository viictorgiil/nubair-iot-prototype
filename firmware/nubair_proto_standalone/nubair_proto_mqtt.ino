/*
 * NUBAIR - Firmware con MQTT
 * 
 * Firmware principal del prototipo ESP32 con conectividad WiFi y MQTT.
 * Integra el control de puerta con el broker Mosquitto corriendo en
 * Docker en la Raspberry Pi, y expone las entidades en Home Assistant.
 * 
 * Hardware:
 *   - ESP32
 *   - Servo (pin 13)
 *   - Sensor táctil TTP223 (pin 27)
 *   - Sensor ultrasónico HC-SR04 (TRIG: 5, ECHO: 18)
 *   - Buzzer pasivo (pin 25)
 *   - Pantalla OLED SSD1306 128x64 por I2C (SDA: 21, SCL: 22)
 * 
 * Librerías necesarias:
 *   - Adafruit GFX Library
 *   - Adafruit SSD1306
 *   - ESP32Servo
 *   - PubSubClient
 * 
 * Configuración:
 *   - Renombrar secrets.h.example → secrets.h y rellenar con tus valores
 *   - El ESP32 debe conectarse a la red WiFi de 2.4 GHz
 *   - El broker MQTT (Mosquitto) debe estar accesible en la IP configurada
 * 
 * Topics MQTT:
 *   esp32/status          → publica "online" / "offline" (LWT)
 *   esp32/door/state      → publica "OPEN" / "CLOSED"
 *   esp32/door/set        → escucha "OPEN" / "CLOSE"
 *   esp32/distance/state  → publica distancia en cm
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "secrets.h"  // WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER

// -----------------------------
// MQTT
// -----------------------------
const int mqtt_port = 1883;

WiFiClient   espClient;
PubSubClient mqttClient(espClient);

const char* TOPIC_STATUS         = "esp32/status";
const char* TOPIC_DOOR_SET       = "esp32/door/set";
const char* TOPIC_DOOR_STATE     = "esp32/door/state";
const char* TOPIC_DISTANCE_STATE = "esp32/distance/state";

// -----------------------------
// OLED
// -----------------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const int OLED_SDA = 21;
const int OLED_SCL = 22;

// -----------------------------
// Pines
// -----------------------------
const int SERVO_PIN  = 13;
const int TOUCH_PIN  = 27;
const int TRIG_PIN   = 5;
const int ECHO_PIN   = 18;
const int BUZZER_PIN = 25;

// -----------------------------
// Servo
// -----------------------------
Servo doorServo;

const int SERVO_CLOSED_ANGLE = 0;
const int SERVO_OPEN_ANGLE   = 90;

// -----------------------------
// Tiempos
// -----------------------------
const unsigned long TOUCH_HOLD_TIME = 3000;
const unsigned long DOOR_OPEN_TIME  = 5000;

unsigned long touchStartTime  = 0;
unsigned long doorOpenedTime  = 0;

bool touchWasActive         = false;
bool doorIsOpen             = false;
bool actionAlreadyTriggered = false;

// -----------------------------
// Ultrasonidos
// -----------------------------
unsigned long lastDistanceRead       = 0;
const unsigned long DISTANCE_INTERVAL = 500;

float lastDistanceCm = -1;

const float PRESENCE_DISTANCE_CM = 20.0;

unsigned long lastMqttDistancePublish       = 0;
const unsigned long MQTT_DISTANCE_INTERVAL  = 2000;

// -----------------------------
// Sensor táctil
// -----------------------------
const int TOUCH_ACTIVE_STATE = HIGH;

// -----------------------------
// Funciones auxiliares
// -----------------------------
void beep(int frequency, int durationMs) {
  tone(BUZZER_PIN, frequency, durationMs);
}

void happySound() {
  tone(BUZZER_PIN, 988,  100); delay(120);
  tone(BUZZER_PIN, 1319, 100); delay(120);
  tone(BUZZER_PIN, 1568, 130); delay(150);
  tone(BUZZER_PIN, 2093, 180); delay(200);
  noTone(BUZZER_PIN);
}

void showOLED(const String& line1, const String& line2 = "", const String& line3 = "") {
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(line1);

  display.setTextSize(1);
  display.setCursor(0, 28);
  display.println(line2);

  display.setCursor(0, 42);
  display.println(line3);

  display.display();
}

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;

  return duration * 0.0343 / 2.0;
}

// -----------------------------
// MQTT publish
// -----------------------------
void publishDoorState() {
  if (!mqttClient.connected()) return;
  mqttClient.publish(TOPIC_DOOR_STATE, doorIsOpen ? "OPEN" : "CLOSED", true);
}

void publishDistance() {
  if (!mqttClient.connected()) return;
  if (lastDistanceCm > 0) {
    String payload = String(lastDistanceCm, 1);
    mqttClient.publish(TOPIC_DISTANCE_STATE, payload.c_str(), true);
  }
}

// -----------------------------
// MQTT callback
// -----------------------------
void mqttCallback(char* topic, byte* message, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)message[i];

  Serial.print("MQTT recibido [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);

  if (String(topic) == TOPIC_DOOR_SET) {
    msg.toUpperCase();
    if (msg == "OPEN")  openDoor();
    if (msg == "CLOSE") closeDoor();
  }
}

// -----------------------------
// Conexión WiFi y MQTT
// -----------------------------
void conectarWiFi() {
  Serial.print("Conectando a WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi conectado - IP: ");
  Serial.println(WiFi.localIP());
}

void conectarMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando a MQTT... ");

    if (mqttClient.connect("esp32_nubair_01", TOPIC_STATUS, 0, true, "offline")) {
      Serial.println("conectado");

      mqttClient.publish(TOPIC_STATUS, "online", true);
      mqttClient.subscribe(TOPIC_DOOR_SET);

      publishDoorState();
      publishDistance();
    } else {
      Serial.print("falló rc=");
      Serial.print(mqttClient.state());
      Serial.println(", reintentando en 5 s...");
      delay(5000);
    }
  }
}

// -----------------------------
// Control de puerta
// -----------------------------
void openDoor() {
  doorServo.write(SERVO_OPEN_ANGLE);
  doorIsOpen     = true;
  doorOpenedTime = millis();

  happySound();

  Serial.println("Puerta abierta");
  showOLED("NUBAIR", "Puerta abierta", "Cerrando en 5 s");

  publishDoorState();
}

void closeDoor() {
  doorServo.write(SERVO_CLOSED_ANGLE);
  doorIsOpen             = false;
  actionAlreadyTriggered = false;
  touchWasActive         = false;
  touchStartTime         = 0;

  beep(800, 120);

  Serial.println("Puerta cerrada");
  showOLED("NUBAIR", "Puerta cerrada");

  publishDoorState();
}

// -----------------------------
// Manejadores de estado
// -----------------------------
void handleTouchSensor() {
  bool touchActive = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_STATE;

  if (touchActive && !touchWasActive) {
    touchStartTime = millis();
    touchWasActive = true;
    Serial.println("Toque detectado...");
    showOLED("NUBAIR", "Mantener 3 s", "para abrir");
  }

  if (!touchActive && touchWasActive) {
    touchWasActive = false;
    touchStartTime = 0;
    if (!doorIsOpen) {
      actionAlreadyTriggered = false;
      showOLED("NUBAIR", "Esperando toque");
    }
  }

  if (touchActive && touchWasActive && !doorIsOpen && !actionAlreadyTriggered) {
    if (millis() - touchStartTime >= TOUCH_HOLD_TIME) {
      actionAlreadyTriggered = true;
      openDoor();
    }
  }
}

void handleDoorAutoClose() {
  if (doorIsOpen && millis() - doorOpenedTime >= DOOR_OPEN_TIME) {
    closeDoor();
  }
}

void handleUltrasonic() {
  if (millis() - lastDistanceRead >= DISTANCE_INTERVAL) {
    lastDistanceRead = millis();
    lastDistanceCm   = readDistanceCm();

    if (lastDistanceCm > 0) {
      Serial.print("Distancia: "); Serial.print(lastDistanceCm); Serial.println(" cm");

      if (!doorIsOpen && !touchWasActive) {
        if (lastDistanceCm < PRESENCE_DISTANCE_CM)
          showOLED("NUBAIR", "Presencia cerca", String(lastDistanceCm, 1) + " cm");
        else
          showOLED("NUBAIR", "Esperando toque", String(lastDistanceCm, 1) + " cm");
      }
    } else {
      if (!doorIsOpen && !touchWasActive)
        showOLED("NUBAIR", "Sin lectura", "ultrasonidos");
    }
  }

  if (millis() - lastMqttDistancePublish >= MQTT_DISTANCE_INTERVAL) {
    lastMqttDistancePublish = millis();
    publishDistance();
  }
}

// -----------------------------
// Setup / Loop
// -----------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nIniciando prototipo NUBAIR (MQTT)...");

  pinMode(TOUCH_PIN,  INPUT);
  pinMode(TRIG_PIN,   OUTPUT);
  pinMode(ECHO_PIN,   INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);

  doorServo.setPeriodHertz(50);
  doorServo.attach(SERVO_PIN, 500, 2400);
  doorServo.write(SERVO_CLOSED_ANGLE);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("ERROR: pantalla OLED no detectada");
    while (true) { beep(400, 200); delay(1000); }
  }

  showOLED("NUBAIR", "Conectando WiFi");
  conectarWiFi();

  mqttClient.setServer(MQTT_SERVER, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  conectarMQTT();

  showOLED("NUBAIR", "Sistema listo", "MQTT conectado");
  beep(1000, 100); delay(150); beep(1400, 100);
  Serial.println("Sistema listo");
}

void loop() {
  if (!mqttClient.connected()) conectarMQTT();
  mqttClient.loop();

  handleTouchSensor();
  handleDoorAutoClose();
  handleUltrasonic();
}
