#include <SoftwareSerial.h>
#include <ezButton.h>

// ================== PINS ==================
#define RX_PIN 13
#define TX_PIN 12

#define VRX_PIN  A1
#define VRY_PIN  A0
#define SERVO_PIN A3

#define SW_PIN_A 2
#define SW_PIN_B 4

// ================== SETTINGS ==================
#define BAUD 9600
#define DEADZONE 15
#define SEND_INTERVAL 50   // 20Hz (smooth RC feel)

// ================== OBJECTS ==================
SoftwareSerial HC12(RX_PIN, TX_PIN);
ezButton button_A(SW_PIN_A);
ezButton button_B(SW_PIN_B);

// ================== VARIABLES ==================
int xValue = 0;
int yValue = 0;

int counter_A = 0;
int counter_B = 0;

// ---- Servo smoothing ----
const int numReadings = 10;
int readings[numReadings];
int readIndex = 0;
int total = 0;
int servoSmoothed = 0;

// ---- Timing ----
unsigned long lastSend = 0;

// ================== SETUP ==================
void setup() {
  Serial.begin(BAUD);

  pinMode(RX_PIN, INPUT);
  pinMode(TX_PIN, OUTPUT);

  pinMode(SW_PIN_A, INPUT_PULLUP);
  pinMode(SW_PIN_B, INPUT_PULLUP);

  HC12.begin(BAUD);

  Serial.println("HC-12 Sender Ready");

  // init smoothing buffer
  for (int i = 0; i < numReadings; i++) {
    readings[i] = 0;
  }
}

// ================== LOOP ==================
void loop() {
  button_A.loop();
  button_B.loop();
//  Serial.print("RAW POT: ");
//Serial.println(analogRead(A3));

  // ================== BUTTONS ==================
  if (button_A.isPressed()) {
    counter_A++;
    if (counter_A > 1) counter_A = 0;
  }

  if (button_B.isPressed()) {
    counter_B++;
    if (counter_B > 1) counter_B = 0;
  }

  // ================== SERVO SMOOTHING ==================
  total -= readings[readIndex];
  readings[readIndex] = analogRead(SERVO_PIN);
  total += readings[readIndex];

  readIndex++;
  if (readIndex >= numReadings) readIndex = 0;

  servoSmoothed = total / numReadings;
  servoSmoothed = map(servoSmoothed, 0, 1023, 15, 165);

  // ================== JOYSTICK ==================
  xValue = analogRead(VRX_PIN);
  yValue = analogRead(VRY_PIN);


  xValue = map(xValue, 0, 1023, 127, -127);
  yValue = map(yValue, 0, 1023, 127, -127);

  // deadzone
  if (abs(xValue) < DEADZONE) xValue = 0;
  if (abs(yValue) < DEADZONE) yValue = 0;

  // ================== SEND DATA ==================
  if (millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();

    HC12.print("<");
    HC12.print(xValue);
    HC12.print(",");
    HC12.print(yValue);
    HC12.print(",");
    HC12.print(counter_A);
    HC12.print(",");
    HC12.print(counter_B);
    HC12.print(",");
    HC12.print(servoSmoothed);
    HC12.print(">");

    // Debug output
    Serial.print("X: "); Serial.print(xValue);
    Serial.print(" | Y: "); Serial.print(yValue);
    Serial.print(" | A: "); Serial.print(counter_A);
    Serial.print(" | B: "); Serial.print(counter_B);
    Serial.print(" | S: "); Serial.println(servoSmoothed);
  }
}
