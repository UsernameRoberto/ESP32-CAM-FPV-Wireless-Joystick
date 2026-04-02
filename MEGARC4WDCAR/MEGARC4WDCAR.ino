#include <Servo.h>

#define HC12 Serial1   // Hardware serial for HC-12

// ================== Variables ==================
String buffer = "";
bool stringComplete = false;

int xValue = 0, yValue = 0, counterA = 0, counterB = 0, servoValue = 90;
int currentServo = 90; // current servo position for smoothing

// Motor pins
// Left side
int enA = 12, in1 = 44, in2 = 42;
int enB = 5,  in3 = 48, in4 = 46;
// Right side
int enC = 3,  in5 = 28, in6 = 30;
int enD = 11, in7 = 26, in8 = 24;

// Servo
Servo steeringServo;
const int SERVO_PIN = 9;  // attach your servo here

// Failsafe
unsigned long lastPacket = 0;
const unsigned long CONNECTION_TIMEOUT = 500; // ms

// Servo smoothing speed (degrees per loop)
const int SERVO_SPEED = 1;

// ================== Setup ==================
void setup() {
  Serial.begin(9600);
  HC12.begin(9600);

  Serial.println("HC-12 Mega RC Ready");

  // Motor pins setup
  int motorPins[] = {in1,in2,in3,in4,in5,in6,in7,in8,enA,enB,enC,enD};
  for(int i=0;i<12;i++) pinMode(motorPins[i], OUTPUT);
  stopMotors();

  // Attach servo and center
  steeringServo.attach(SERVO_PIN);
  steeringServo.write(currentServo);  // initial center
}

// ================== Loop ==================
void loop() {
  readHC12();

  // Smooth servo movement toward target
  if(currentServo < servoValue) {
    currentServo += SERVO_SPEED;
    if(currentServo > servoValue) currentServo = servoValue;
  } else if(currentServo > servoValue) {
    currentServo -= SERVO_SPEED;
    if(currentServo < servoValue) currentServo = servoValue;
  }
  steeringServo.write(currentServo);

  // Failsafe: stop motors if no packet for CONNECTION_TIMEOUT
  if(millis() - lastPacket > CONNECTION_TIMEOUT) {
    failsafe();
  }
}

// ================== HC12 Reading ==================
void readHC12() {
  while(HC12.available()) {
    char c = HC12.read();

    if(c == '<') {
      buffer = "<";  // start new packet
    } else if(buffer.length() > 0) {
      buffer += c;   // accumulate
    }

    if(c == '>' && buffer.startsWith("<")) {
      parseData(buffer);  // parse full packet
      buffer = "";        // reset buffer
    }
  }
}

// ================== Parse Data ==================
void parseData(String data) {
  if(data.startsWith("<")) data = data.substring(1);
  if(data.endsWith(">"))   data = data.substring(0, data.length()-1);

  int i1 = data.indexOf(',');
  int i2 = data.indexOf(',', i1+1);
  int i3 = data.indexOf(',', i2+1);
  int i4 = data.indexOf(',', i3+1);

  if(i1>0 && i2>i1 && i3>i2 && i4>i3) {
    xValue     = data.substring(0,i1).toInt();
    yValue     = data.substring(i1+1,i2).toInt();
    counterA   = data.substring(i2+1,i3).toInt();
    counterB   = data.substring(i3+1,i4).toInt();
    servoValue = data.substring(i4+1).toInt();

    Serial.print("X: "); Serial.print(xValue);
    Serial.print(" | Y: "); Serial.print(yValue);
    Serial.print(" | BtnA: "); Serial.print(counterA);
    Serial.print(" | BtnB: "); Serial.print(counterB);
    Serial.print(" | Servo: "); Serial.println(servoValue);

    driveMotors();  // motor logic
    lastPacket = millis();
  }
}

// ================== Drive Motors ==================
void driveMotors() {
  if(counterA != 1) { // Button A must be pressed to enable motors
    stopMotors();
    return;
  }

  int leftPower  = yValue;  // Base power from left joystick
  int rightPower = yValue;

  // Differential drift using xValue if Button B enabled
  if(counterB == 1) {
    if(xValue < 0) {
      rightPower = rightPower * (127 + xValue) / 127;
    } else if(xValue > 0) {
      leftPower  = leftPower * (127 - xValue) / 127;
    }
  }

  leftPower  = constrain(leftPower,  -127, 127);
  rightPower = constrain(rightPower, -127, 127);

  int pwmLeft  = map(abs(leftPower), 0, 127, 0, 255);
  int pwmRight = map(abs(rightPower),0, 127, 0, 255);

  setMotor(in1,in2,enA,leftPower >= 0,pwmLeft);
  setMotor(in3,in4,enB,leftPower >= 0,pwmLeft);
  setMotor(in5,in6,enC,rightPower >= 0,pwmRight);
  setMotor(in7,in8,enD,rightPower >= 0,pwmRight);
}

// ================== Set Single Motor ==================
void setMotor(int pinA,int pinB,int pwmPin,bool forward,int pwm) {
  if(forward) {
    digitalWrite(pinA,HIGH);
    digitalWrite(pinB,LOW);
  } else {
    digitalWrite(pinA,LOW);
    digitalWrite(pinB,HIGH);
  }
  analogWrite(pwmPin,pwm);
}

// ================== Stop Motors ==================
void stopMotors() {
  int motors[] = {enA,enB,enC,enD};
  for(int i=0;i<4;i++) analogWrite(motors[i],0);
  int dirs[] = {in1,in2,in3,in4,in5,in6,in7,in8};
  for(int i=0;i<8;i++) digitalWrite(dirs[i],LOW);
}

// ================== Failsafe ==================
void failsafe() {
  stopMotors();           
  steeringServo.write(90); // center steering
  Serial.println("Failsafe triggered: no connection");
}
