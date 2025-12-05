#include <DHT.h>
#include <SoftwareSerial.h>
#include <Servo.h>
#include <Stepper.h>

#define DHTPIN 2
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);
SoftwareSerial satSerial(10, 11); // RX=10, TX=11

const uint8_t LEDPIN = 12;
bool sending = false;
unsigned long lastSend = 0;
unsigned long sendPeriod = 20000UL;

const uint8_t servoPin = 5;
Servo motor;

const uint8_t trigPin = 3;
const uint8_t echoPin = 4;
const unsigned long PULSE_TIMEOUT_US = 30000UL;

bool autoDistance = true;
int servoAngle = 90;
int servoDir = 1;
int manualTargetAngle = 90;

const int SERVO_STEP = 2;
const unsigned long SERVO_MOVE_INTERVAL = 40;
unsigned long lastServoMove = 0;

bool ledState = false;
unsigned long ledTimer = 0;

bool canTransmit = true;
unsigned long lastTokenTime = 0;
const unsigned long TOKEN_TIMEOUT = 6000;

int corruptedCommands = 0;

#define TEMP_HISTORY 10
float tempHistory[TEMP_HISTORY];
int tempIndex = 0;
bool tempFilled = false;
float tempMedia = 0.0;
float medias[3] = {0, 0, 0};
int mediaIndex = 0;

const double G = 6.67430e-11;
const double M = 5.97219e24;
const double R_EARTH = 6371000;
const double ALTITUDE = 400000;
const double TIME_COMPRESSION = 90.0;
double real_orbital_period;
double r;
unsigned long orbitStartTime = 0;

// ===== PANEL SOLAR CON STEPPER =====
const uint8_t PHOTORESISTOR_PIN = A1;
const int STEPS_PER_REV = 2048;
Stepper stepperMotor(STEPS_PER_REV, 6, 7, 8, 9);

int currentPanelState = 0;     // 0, 40, 60, 100
int targetPanelState = 0;
bool panelStateChanged = false;  // Flag para enviar cambio inmediato

const int TOTAL_DEPLOYMENT_STEPS = 1024;

unsigned long lastLightCheck = 0;
const unsigned long LIGHT_CHECK_INTERVAL = 3000;  // Revisar cada 3 segundos

// Umbrales de luz (AJUSTAR según test unitario)
const int LIGHT_NONE = 200;
const int LIGHT_LOW = 500;
const int LIGHT_MEDIUM = 700;

String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i = 0; i < msg.length(); i++) {
    xorSum ^= msg[i];
  }
  String hex = String(xorSum, HEX);
  hex.toUpperCase();
  if (hex.length() == 1)
    hex = "0" + hex;
  return hex;
}

void sendPacketWithChecksum(uint8_t type, const String &payload) {
  String msg = String(type) + ":" + payload;
  String chk = calcChecksum(msg);
  String fullMsg = msg + "*" + chk;
  satSerial.println(fullMsg);
  Serial.println("-> " + fullMsg);
}

int pingSensor() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(4);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long dur = pulseIn(echoPin, HIGH, PULSE_TIMEOUT_US);
  if (dur == 0)
    return 0;
  return (int)(dur * 0.343 / 2.0);
}

void handleCommand(const String &cmd) {
  Serial.println(cmd);

  if (cmd == "67:1") {
    canTransmit = true;
    lastTokenTime = millis();
    return;
  } else if (cmd == "67:0") {
    canTransmit = false;
    return;
  }

  if (cmd.startsWith("1:")) {
    sendPeriod = max(200UL, cmd.substring(2).toInt());
  } else if (cmd.startsWith("2:")) {
    manualTargetAngle = constrain(cmd.substring(2).toInt(), 0, 180);
    if (!autoDistance) {
      motor.write(manualTargetAngle);
      servoAngle = manualTargetAngle;
    }
  } else if (cmd == "3:i" || cmd == "3:r") {
    sending = true;
  } else if (cmd == "3:p") {
    sending = false;
  } else if (cmd == "4:a") {
    autoDistance = true;
  } else if (cmd == "4:m") {
    autoDistance = false;
    motor.write(manualTargetAngle);
    servoAngle = manualTargetAngle;
  } else if (cmd.startsWith("5:")) {
    int ang = constrain(cmd.substring(2).toInt(), 0, 180);
    manualTargetAngle = ang;
    if (!autoDistance)
      servoAngle = manualTargetAngle;
  }
}

void validateAndHandle(const String &data) {
  int asterisco = data.indexOf('*');
  if (asterisco == -1) {
    Serial.println("CMD sin checksum, descartado");
    corruptedCommands++;
    return;
  }

  String msg = data.substring(0, asterisco);
  String chkRecv = data.substring(asterisco + 1);
  String chkCalc = calcChecksum(msg);

  if (chkRecv == chkCalc) {
    handleCommand(msg);
  } else {
    Serial.println("CMD corrupto, descartado");
    corruptedCommands++;
  }
}

void updateTempMedia(float nuevaTemp) {
  tempHistory[tempIndex] = nuevaTemp;
  tempIndex = (tempIndex + 1) % TEMP_HISTORY;
  if (tempIndex == 0)
    tempFilled = true;

  int n = tempFilled ? TEMP_HISTORY : tempIndex;
  float suma = 0;
  for (int i = 0; i < n; i++)
    suma += tempHistory[i];

  tempMedia = suma / n;
  medias[mediaIndex] = tempMedia;
  mediaIndex = (mediaIndex + 1) % 3;

  bool alerta = true;
  for (int i = 0; i < 3; i++) {
    if (medias[i] <= 100.0)
      alerta = false;
  }

  if (alerta)
    sendPacketWithChecksum(8, "e");
}

void simulate_orbit() {
  unsigned long currentMillis = millis();
  double time = ((currentMillis - orbitStartTime) / 1000.0) * TIME_COMPRESSION;
  double angle = 2.0 * PI * (time / real_orbital_period);

  long x = (long)(r * cos(angle));
  long y = (long)(r * sin(angle));
  long z = 0;

  String payload = String((long)time) + ":" + String(x) + ":" + String(y) + ":" + String(z);
  sendPacketWithChecksum(9, payload);
}

// ===== GESTIÓN DEL PANEL SOLAR =====
void checkLightAndDeploy() {
  unsigned long now = millis();
  
  if (now - lastLightCheck < LIGHT_CHECK_INTERVAL) {
    return;
  }
  
  lastLightCheck = now;
  
  // Leer nivel de luz
  int lightLevel = analogRead(PHOTORESISTOR_PIN);
  Serial.print("Luz: ");
  Serial.println(lightLevel);
  
  // Determinar estado objetivo
  int oldTarget = targetPanelState;
  
  if (lightLevel < LIGHT_NONE) {
    targetPanelState = 0;
  } else if (lightLevel < LIGHT_LOW) {
    targetPanelState = 100;
  } else if (lightLevel < LIGHT_MEDIUM) {
    targetPanelState = 60;
  } else {
    targetPanelState = 40;
  }
  
  // Si cambió, mover y marcar flag
  if (targetPanelState != currentPanelState) {
    movePanelToTarget();
    panelStateChanged = true;  // Marcar para envío inmediato
  }
}

void movePanelToTarget() {
  Serial.print("Moviendo panel: ");
  Serial.print(currentPanelState);
  Serial.print("% -> ");
  Serial.print(targetPanelState);
  Serial.println("%");
  
  int currentSteps = (TOTAL_DEPLOYMENT_STEPS * currentPanelState) / 100;
  int targetSteps = (TOTAL_DEPLOYMENT_STEPS * targetPanelState) / 100;
  int stepsToMove = targetSteps - currentSteps;
  
  stepperMotor.setSpeed(10);
  
  if (stepsToMove != 0) {
    stepperMotor.step(stepsToMove);
    delay(100);
  }
  
  currentPanelState = targetPanelState;
  Serial.println("Panel movido OK");
}

void setup() {
  Serial.begin(9600);
  satSerial.begin(9600);

  pinMode(LEDPIN, OUTPUT);
  digitalWrite(LEDPIN, LOW);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  dht.begin();

  motor.attach(servoPin);
  motor.write(servoAngle);

  lastTokenTime = millis();

  r = R_EARTH + ALTITUDE;
  real_orbital_period = 2.0 * PI * sqrt(pow(r, 3) / (G * M));
  orbitStartTime = millis();
  
  stepperMotor.setSpeed(10);
  pinMode(PHOTORESISTOR_PIN, INPUT);

  Serial.println("SAT listo con panel solar");
}

void loop() {
  unsigned long now = millis();

  // Revisar luz y ajustar panel
  checkLightAndDeploy();

  // Servo en modo auto
  if (autoDistance && now - lastServoMove >= SERVO_MOVE_INTERVAL) {
    lastServoMove = now;
    servoAngle += servoDir * SERVO_STEP;
    if (servoAngle >= 180) {
      servoAngle = 180;
      servoDir = -1;
    } else if (servoAngle <= 0) {
      servoAngle = 0;
      servoDir = 1;
    }
    motor.write(servoAngle);
  }
  
  // Recepción de comandos
  if (satSerial.available()) {
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length())
      validateAndHandle(cmd);
  }

  // Timeout de token
  if (!canTransmit && now - lastTokenTime > TOKEN_TIMEOUT) {
    canTransmit = true;
    Serial.println("Timeout: recuperando transmisión");
  }

  // Ciclo de transmisión
  if (now - lastSend >= sendPeriod) {
    if (sending && canTransmit) {
      // Temp y humedad
      float h = dht.readHumidity();
      float t = dht.readTemperature();
      if (isnan(h) || isnan(t)) {
        sendPacketWithChecksum(4, "e:1");
      } else {
        sendPacketWithChecksum(1, String((int)(h * 100)) + ":" + String((int)(t * 100)));
        updateTempMedia(t);
        sendPacketWithChecksum(7, String((int)(tempMedia * 100)));
      }

      // Distancia
      int dist = pingSensor();
      if (dist == 0)
        sendPacketWithChecksum(5, "e:1");
      else
        sendPacketWithChecksum(2, String(dist));

      // Servo
      if (!motor.attached())
        sendPacketWithChecksum(6, "e:1");
      else
        sendPacketWithChecksum(6, String(servoAngle));

      // Órbita
      simulate_orbit();
      
      // Panel solar (SIEMPRE enviar estado actual)
      sendPacketWithChecksum(10, String(currentPanelState));
      panelStateChanged = false;  // Reset flag

      // Liberar turno
      sendPacketWithChecksum(67, "0");
      canTransmit = false;
    }

    digitalWrite(LEDPIN, HIGH);
    ledTimer = now;
    ledState = true;
    lastSend = now;
  }

  // Si el panel cambió FUERA del ciclo normal, enviar inmediatamente
  if (panelStateChanged && canTransmit && sending) {
    sendPacketWithChecksum(10, String(currentPanelState));
    panelStateChanged = false;
  }

  // LED
  if (ledState && now - ledTimer > 80) {
    digitalWrite(LEDPIN, LOW);
    ledState = false;
  }
}
