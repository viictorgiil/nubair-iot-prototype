/*
 * NUBAIR - Prototipo standalone (sin MQTT)
 * 
 * Firmware de validación local para ESP32.
 * Prueba toda la electrónica (servo, táctil, ultrasonidos, OLED, buzzer)
 * sin necesidad de conectividad WiFi ni broker MQTT.
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
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

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
const unsigned long TOUCH_HOLD_TIME = 3000;  // ms manteniendo para abrir
const unsigned long DOOR_OPEN_TIME  = 5000;  // ms antes de cierre automático

unsigned long touchStartTime  = 0;
unsigned long doorOpenedTime  = 0;

bool touchWasActive        = false;
bool doorIsOpen            = false;
bool actionAlreadyTriggered = false;

// -----------------------------
// Ultrasonidos
// -----------------------------
unsigned long lastDistanceRead = 0;
const unsigned long DISTANCE_INTERVAL = 500;

float lastDistanceCm = -1;

const float PRESENCE_DISTANCE_CM = 20.0;  // umbral de detección de presencia

// -----------------------------
// Sensor táctil
// -----------------------------
// TTP223 activo en HIGH por defecto. Cambiar a LOW si el módulo funciona al revés.
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
// Control de puerta
// -----------------------------
void openDoor() {
  doorServo.write(SERVO_OPEN_ANGLE);
  doorIsOpen     = true;
  doorOpenedTime = millis();

  happySound();

  Serial.println("Puerta abierta");
  showOLED("NUBAIR", "Puerta abierta", "Cerrando en 5 s");
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
}

// -----------------------------
// Manejadores de estado
// -----------------------------
void handleTouchSensor() {
  bool touchActive = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_STATE;

  if (touchActive && !touchWasActive) {
    touchStartTime = millis();
    touchWasActive = true;
    Serial.println("Toque detectado, empezando contador...");
    showOLED("NUBAIR", "Mantener 3 s", "para abrir");
  }

  if (!touchActive && touchWasActive) {
    touchWasActive = false;
    touchStartTime = 0;
    if (!doorIsOpen) {
      actionAlreadyTriggered = false;
      showOLED("NUBAIR", "Esperando toque");
    }
    Serial.println("Toque liberado");
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
      Serial.print("Distancia: ");
      Serial.print(lastDistanceCm);
      Serial.println(" cm");

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
}

// -----------------------------
// Setup / Loop
// -----------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nIniciando prototipo NUBAIR (standalone)...");

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

  showOLED("NUBAIR", "Sistema iniciado", "Esperando toque");
  beep(1000, 100); delay(150); beep(1400, 100);
  Serial.println("Sistema listo");
}

void loop() {
  handleTouchSensor();
  handleDoorAutoClose();
  handleUltrasonic();
}
