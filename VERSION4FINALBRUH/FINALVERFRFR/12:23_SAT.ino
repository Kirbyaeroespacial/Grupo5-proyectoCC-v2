// Código SATELITE CORREGIDO - CHECKSUM FIJO + STEPPER OPTIMIZADO
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
const double ALTITUDE = 4000000;
const double TIME_COMPRESSION = 260.0;
const double EARTH_ROTATION_RATE = 7.2921159e-5;
double real_orbital_period;
double r;
unsigned long orbitStartTime = 0;

// ===== PANEL SOLAR CON STEPPER (NO-BLOQUEANTE) =====
const uint8_t PHOTORESISTOR_PIN = A1;
const int STEPS_PER_REV = 2048;
// IMPORTANTE: Orden correcto de pines para 28BYJ-48 (IN1, IN3, IN2, IN4)
Stepper stepperMotor(STEPS_PER_REV, 6, 8, 7, 9);

int currentPanelState = 0;
int targetPanelState = 0;
bool panelStateChanged = false;

// Total de revoluciones para despliegue completo (ajusta según tu mecanismo)
const int TOTAL_REVOLUTIONS = 120;  // 120 revoluciones
const long TOTAL_DEPLOYMENT_STEPS = TOTAL_REVOLUTIONS * STEPS_PER_REV;

unsigned long lastLightCheck = 0;
const unsigned long LIGHT_CHECK_INTERVAL = 3000;

// Umbrales de luz ajustados (400-900)
const int LIGHT_MIN = 400;   // Mínimo de luz detectada
const int LIGHT_MAX = 900;   // Máximo de luz detectada
const int LIGHT_RANGE = LIGHT_MAX - LIGHT_MIN;  // Rango total: 500

// Variables para movimiento no-bloqueante
bool stepperMoving = false;
long stepperRemaining = 0;
int stepperDirection = 1;
unsigned long lastStepperMove = 0;
const unsigned long STEPPER_INTERVAL = 2;  // ms entre micro-bloques
const int STEPS_PER_ITERATION = 8;  // Pasos por iteración (ajustable)

// ===== ESTRUCTURA TELEMETRÍA BINARIA =====
#pragma pack(push, 1)
struct TelemetryFrame {
  uint8_t header;        // 0xAA
  uint16_t humidity;     // ×100
  int16_t temperature;   // ×100
  uint16_t tempAvg;      // ×100
  uint16_t distance;     // cm
  uint8_t servoAngle;    // 0-180
  uint16_t time_s;       // segundos simulados
  // Coordenadas como bytes individuales
  uint8_t x_b0, x_b1, x_b2, x_b3;
  uint8_t y_b0, y_b1, y_b2, y_b3;
  uint8_t z_b0, z_b1, z_b2, z_b3;
  uint8_t panelState;    // 0/40/60/100
  uint8_t checksum;      // XOR de todos los bytes anteriores
};
#pragma pack(pop)
const size_t TELEMETRY_FRAME_SIZE = sizeof(TelemetryFrame);

// === ASCII checksum ===
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

// Enviar telemetría binaria con DEBUG
void sendTelemetryBinary(
  uint16_t hum100,
  int16_t temp100,
  uint16_t avg100,
  uint16_t dist,
  uint8_t servo,
  uint16_t time_s,
  int32_t x,
  int32_t y,
  int32_t z,
  uint8_t panel
) {
  TelemetryFrame frame;
  frame.header = 0xAA;
  frame.humidity = hum100;
  frame.temperature = temp100;
  frame.tempAvg = avg100;
  frame.distance = dist;
  frame.servoAngle = servo;
  frame.time_s = time_s;

  // Convertir coordenadas a bytes (little-endian)
  frame.x_b0 = (x) & 0xFF;
  frame.x_b1 = (x >> 8) & 0xFF;
  frame.x_b2 = (x >> 16) & 0xFF;
  frame.x_b3 = (x >> 24) & 0xFF;

  frame.y_b0 = (y) & 0xFF;
  frame.y_b1 = (y >> 8) & 0xFF;
  frame.y_b2 = (y >> 16) & 0xFF;
  frame.y_b3 = (y >> 24) & 0xFF;

  frame.z_b0 = (z) & 0xFF;
  frame.z_b1 = (z >> 8) & 0xFF;
  frame.z_b2 = (z >> 16) & 0xFF;
  frame.z_b3 = (z >> 24) & 0xFF;

  frame.panelState = panel;

  // Calcular checksum manualmente byte por byte
  uint8_t *ptr = (uint8_t*)&frame;
  uint8_t cs = 0;

  // XOR de todos los bytes EXCEPTO el último (checksum)
  for (size_t i = 0; i < TELEMETRY_FRAME_SIZE - 1; i++) {
    cs ^= ptr[i];
  }

  frame.checksum = cs;

  // DEBUG: Imprimir algunos bytes para verificar
  Serial.print("TX: H=");
  Serial.print(frame.header, HEX);
  Serial.print(" Hum=");
  Serial.print(frame.humidity);
  Serial.print(" T=");
  Serial.print(frame.temperature);
  Serial.print(" CS=");
  Serial.print(frame.checksum, HEX);
  Serial.print(" Size=");
  Serial.println(TELEMETRY_FRAME_SIZE);

  // Enviar frame binario
  satSerial.write((uint8_t*)&frame, TELEMETRY_FRAME_SIZE);
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
    unsigned long newPeriod = (unsigned long)cmd.substring(2).toInt();
    if (newPeriod < 200) newPeriod = 200;
    sendPeriod = newPeriod;
    Serial.print("DEBUG: nuevo sendPeriod: "); Serial.println(sendPeriod);
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

double eccentricity = 0.2;
double inclination = 51.6;
const double ecef = 1;

void compute_orbit(uint16_t &time_s_out, int32_t &x_out, int32_t &y_out, int32_t &z_out) {
    unsigned long currentMillis = millis();
    double time = (currentMillis / 1000.0) * TIME_COMPRESSION;
    double M_anomaly = 2.0 * PI * (time / real_orbital_period);
    double E = M_anomaly;
    for (int i = 0; i < 10; i++) {
        E = E - (E - eccentricity * sin(E) - M_anomaly) / (1.0 - eccentricity * cos(E));
    }
    double true_anomaly = 2.0 * atan2(
        sqrt(1.0 + eccentricity) * sin(E / 2.0),
        sqrt(1.0 - eccentricity) * cos(E / 2.0)
    );
    double r_orbit = r * (1.0 - eccentricity * eccentricity) / (1.0 + eccentricity * cos(true_anomaly));
    double x_orbital = r_orbit * cos(true_anomaly);
    double y_orbital = r_orbit * sin(true_anomaly);
    double z_orbital = 0.0;
    double inclination_rad = inclination * PI / 180.0;
    double y_inclined = y_orbital * cos(inclination_rad) - z_orbital * sin(inclination_rad);
    double z_inclined = y_orbital * sin(inclination_rad) + z_orbital * cos(inclination_rad);
    double x = x_orbital;
    double y = y_inclined;
    double z = z_inclined;
    if (ecef) {
        double theta = EARTH_ROTATION_RATE * time;
        double x_ecef = x * cos(theta) - y * sin(theta);
        double y_ecef = x * sin(theta) + y * cos(theta);
        x = x_ecef;
        y = y_ecef;
    }
    x_out = (int32_t)x;
    y_out = (int32_t)y;
    z_out = (int32_t)z;
    time_s_out = (uint16_t)((uint32_t)time & 0xFFFF);
}

void checkLightAndDeploy() {
  unsigned long now = millis();
  if (now - lastLightCheck < LIGHT_CHECK_INTERVAL) {
    return;
  }
  lastLightCheck = now;
  int lightLevel = analogRead(PHOTORESISTOR_PIN);
  Serial.print("Luz: ");
  Serial.print(lightLevel);
  if (lightLevel <= LIGHT_MIN) {
    targetPanelState = 100;
    Serial.println(" -> Panel 100% (luz baja)");
  } else if (lightLevel >= LIGHT_MAX) {
    targetPanelState = 0;
    Serial.println(" -> Panel 0% (luz alta)");
  } else {
    int mappedValue = map(lightLevel, LIGHT_MIN, LIGHT_MAX, 100, 0);
    targetPanelState = constrain(mappedValue, 0, 100);
    Serial.print(" -> Panel ");
    Serial.print(targetPanelState);
    Serial.println("%");
  }
  if (targetPanelState != currentPanelState && !stepperMoving) {
    movePanelToTarget();
  } else if (stepperMoving) {
    Serial.println("(Stepper ocupado, esperando...)");
  }
}

void movePanelToTarget() {
  if (stepperMoving) {
    Serial.println("Stepper ya en movimiento, ignorando comando");
    return;
  }
  Serial.print("Iniciando movimiento panel: ");
  Serial.print(currentPanelState);
  Serial.print("% -> ");
  Serial.print(targetPanelState);
  Serial.println("%");
  long currentSteps = ((long)currentPanelState * TOTAL_DEPLOYMENT_STEPS) / 100;
  long targetSteps = ((long)targetPanelState * TOTAL_DEPLOYMENT_STEPS) / 100;
  long stepsToMove = targetSteps - currentSteps;
  if (stepsToMove == 0) {
    Serial.println("Sin movimiento necesario");
    return;
  }
  stepperMotor.setSpeed(6);
  Serial.print("Pasos totales a mover: ");
  Serial.println(stepsToMove);
  stepperRemaining = abs(stepsToMove);
  stepperDirection = (stepsToMove > 0) ? 1 : -1;
  stepperMoving = true;
  lastStepperMove = millis();
}

void updateStepper() {
  if (!stepperMoving) {
    return;
  }
  unsigned long now = millis();
  if (now - lastStepperMove >= STEPPER_INTERVAL) {
    lastStepperMove = now;
    if (stepperRemaining > 0) {
      long stepsThisIteration = min((long)STEPS_PER_ITERATION, stepperRemaining);
      stepperMotor.step(stepperDirection * stepsThisIteration);
      stepperRemaining -= stepsThisIteration;
    }
    if (stepperRemaining == 0) {
      stepperMoving = false;
      currentPanelState = targetPanelState;
      Serial.println("Panel movido OK - No bloqueante");
      panelStateChanged = true;
    }
  }
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

  stepperMotor.setSpeed(6);  // RPM inicial - velocidad lenta para estabilidad
  pinMode(PHOTORESISTOR_PIN, INPUT);

  Serial.print("Tamaño frame: ");
  Serial.println(TELEMETRY_FRAME_SIZE);
  Serial.print("Total revoluciones panel: ");
  Serial.println(TOTAL_REVOLUTIONS);
  Serial.print("Total pasos panel: ");
  Serial.println(TOTAL_DEPLOYMENT_STEPS);
  Serial.println("SAT listo (binario + Stepper NO-BLOQUEANTE)");
}

void loop() {
  unsigned long now = millis();
  updateStepper();
  checkLightAndDeploy();

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

  if (satSerial.available()) {
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length())
      validateAndHandle(cmd);
  }

  if (!canTransmit && now - lastTokenTime > TOKEN_TIMEOUT) {
    canTransmit = true;
    Serial.println("Timeout: recuperando transmisión");
  }

  if (now - lastSend >= sendPeriod) {
    lastSend = now;

    if (sending && canTransmit) {
      float h = dht.readHumidity();
      float t = dht.readTemperature();
      bool temp_ok = !(isnan(h) || isnan(t));
      if (temp_ok) {
        updateTempMedia(t);
      }
      if (!temp_ok) {
        sendPacketWithChecksum(4, "e:1");
      }

      int dist = pingSensor();
      bool dist_ok = (dist != 0);

      uint16_t orb_time;
      int32_t orb_x, orb_y, orb_z;
      compute_orbit(orb_time, orb_x, orb_y, orb_z);

      uint16_t hum100 = temp_ok ? (uint16_t)((int)(h * 100.0f)) : 0;
      int16_t temp100 = temp_ok ? (int16_t)((int)(t * 100.0f)) : 0;
      uint16_t avg100 = (uint16_t)((int)(tempMedia * 100.0f));
      uint16_t dist_field = dist_ok ? (uint16_t)dist : 0;
      uint8_t servo_field = (motor.attached()) ? (uint8_t)servoAngle : 0xFF;

      sendTelemetryBinary(
        hum100,
        temp100,
        avg100,
        dist_field,
        servo_field,
        orb_time,
        orb_x,
        orb_y,
        orb_z,
        (uint8_t)currentPanelState
      );

      delay(100);
      sendPacketWithChecksum(67, "0");
      canTransmit = false;

      panelStateChanged = false;
    }

    digitalWrite(LEDPIN, HIGH);
    ledTimer = now;
    ledState = true;
  }

  if (panelStateChanged && canTransmit && sending && (now - lastSend > 1000)) {
    lastSend = now;

    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(t)) updateTempMedia(t);
    int dist = pingSensor();

    uint16_t orb_time;
    int32_t orb_x, orb_y, orb_z;
    compute_orbit(orb_time, orb_x, orb_y, orb_z);

    uint16_t hum100 = isnan(h) ? 0 : (uint16_t)((int)(h * 100.0f));
    int16_t temp100 = isnan(t) ? 0 : (int16_t)((int)(t * 100.0f));
    uint16_t avg100 = (uint16_t)((int)(tempMedia * 100.0f));
    uint16_t dist_field = dist == 0 ? 0 : (uint16_t)dist;
    uint8_t servo_field = (motor.attached()) ? (uint8_t)servoAngle : 0xFF;

    sendTelemetryBinary(
      hum100,
      temp100,
      avg100,
      dist_field,
      servo_field,
      orb_time,
      orb_x,
      orb_y,
      orb_z,
      (uint8_t)currentPanelState
    );

    delay(100);
    sendPacketWithChecksum(67, "0");
    canTransmit = false;
    panelStateChanged = false;
  }

  if (ledState && now - ledTimer > 80) {
    digitalWrite(LEDPIN, LOW);
    ledState = false;
  }
}
