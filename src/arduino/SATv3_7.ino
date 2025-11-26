/* SATELITE - Arduino
   - Acepta mensajes con o sin checksum "*XX"
   - Envía paquetes con checksum (tipo:payload*CK)
   - Token: 67:1 (ground concede) / 67:0 (sat libera)
   - Pequeños delays tras println() para módulos LoRa
*/

#include <DHT.h>
#include <SoftwareSerial.h>
#include <Servo.h>

#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// SoftwareSerial para módulo LoRa (RX, TX)
SoftwareSerial satSerial(10, 11);

const uint8_t LEDPIN = 12;
const uint8_t servoPin = 5;
Servo motor;

// Ultrasonidos
const uint8_t trigPin = 3;
const uint8_t echoPin = 4;
const unsigned long PULSE_TIMEOUT_US = 30000UL;

// Timing / control
bool sending = false;            // si true, sat debe enviar datos cuando tenga token
bool canTransmit = false;        // token recibido (67:1)
unsigned long lastSend = 0;
unsigned long sendPeriod = 2000UL;  // intervalo entre bloques (ms)
unsigned long lastHeartbeat = 0;
unsigned long lastMessageReceived = 0;

const unsigned long HEARTBEAT_INTERVAL = 10000UL; // 10s (reduce tráfico)
const unsigned long COMM_TIMEOUT = 15000UL;
const unsigned long AFTER_TX_DELAY = 60; // ms delay tras println hacia LoRa (ajustable)

// Servo auto
bool autoDistance = true;
int servoAngle = 90;
int servoDir = 1;
int manualTargetAngle = 90;
const int SERVO_STEP = 2;
const unsigned long SERVO_MOVE_INTERVAL = 40;
unsigned long lastServoMove = 0;

// Temp rolling average
#define TEMP_HISTORY 10
float tempHistory[TEMP_HISTORY];
int tempIndex = 0;
bool tempFilled = false;
float tempMedia = 0.0;
float medias[3] = {0,0,0};
int mediaIndex = 0;

// Stats
int corruptedCommands = 0;
int noChecksumReceived = 0;

// Orbit simulation (kept)
const double G = 6.67430e-11;
const double M = 5.97219e24;
const double R_EARTH = 6371000;
const double ALTITUDE = 400000;
const double TIME_COMPRESSION = 90.0;
double real_orbital_period;
double r;
unsigned long orbitStartTime = 0;

// ---------------- utils: checksum XOR hex 2 chars ----------------
String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i = 0; i < msg.length(); ++i) xorSum ^= (uint8_t)msg[i];
  char hexbuf[3];
  sprintf(hexbuf, "%02X", xorSum);
  return String(hexbuf);
}

// envia tipo:payload*CK (siempre con CK)
void sendPacketWithChecksum(uint8_t type, const String &payload) {
  String msg = String(type) + ":" + payload;
  String chk = calcChecksum(msg);
  String full = msg + "*" + chk;
  satSerial.println(full);
  delay(AFTER_TX_DELAY);
  Serial.println("TX> " + full);
}

// sensor ultrasónico -> mm (0 si timeout)
int pingSensor() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(4);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  unsigned long dur = pulseIn(echoPin, HIGH, PULSE_TIMEOUT_US);
  if (dur == 0) return 0;
  return (int)(dur * 0.343 / 2.0);
}

// Maneja comandos "limpios" (sin CK) como antes
void handleCommand(const String &cmd) {
  // actualizar timestamp de recepción
  lastMessageReceived = millis();

  if (cmd == "67:1") { canTransmit = true; Serial.println("CMD: TOKEN ON"); return; }
  if (cmd == "67:0") { canTransmit = false; Serial.println("CMD: TOKEN OFF"); return; }

  int sep = cmd.indexOf(':');
  if (sep < 0) return;
  int id = cmd.substring(0, sep).toInt();
  String val = cmd.substring(sep + 1);

  switch (id) {
    case 1:
      sendPeriod = max(200UL, (unsigned long)val.toInt());
      Serial.println("SET sendPeriod=" + String(sendPeriod));
      break;
    case 2:
      manualTargetAngle = constrain(val.toInt(), 0, 180);
      if (!autoDistance) { motor.write(manualTargetAngle); servoAngle = manualTargetAngle; }
      break;
    case 3:
      if (val == "i" || val == "r") { sending = true; Serial.println("CMD TX START"); }
      else if (val == "p") { sending = false; Serial.println("CMD TX STOP"); }
      break;
    case 4:
      if (val == "a") { autoDistance = true; Serial.println("MODE AUTO"); }
      else if (val == "m") { autoDistance = false; motor.write(manualTargetAngle); servoAngle = manualTargetAngle; Serial.println("MODE MANUAL"); }
      break;
    case 5:
      manualTargetAngle = constrain(val.toInt(), 0, 180);
      if (!autoDistance) servoAngle = manualTargetAngle;
      Serial.println("SET manualTargetAngle=" + String(manualTargetAngle));
      break;
  }
}

// Valida entrada: acepta con '*' y sin '*' (legacy).
bool validateAndHandle(const String &data) {
  String s = data;
  s.trim();
  if (s.length() == 0) return false;
  int ast = s.indexOf('*');
  if (ast == -1) {
    // Legacy: aceptar pero contar
    noChecksumReceived++;
    Serial.println("WARN: recibido SIN checksum -> procesando por compatibilidad");
    handleCommand(s);
    return true;
  }
  String msg = s.substring(0, ast);
  String chkRecv = s.substring(ast + 1);
  chkRecv.trim();
  chkRecv.toUpperCase();
  String chkCalc = calcChecksum(msg);
  if (chkRecv.equalsIgnoreCase(chkCalc)) {
    handleCommand(msg);
    return true;
  } else {
    Serial.println("ERR: checksum mismatch recv=" + chkRecv + " calc=" + chkCalc);
    corruptedCommands++;
    return false;
  }
}

void updateTempMedia(float nuevaTemp) {
  tempHistory[tempIndex] = nuevaTemp;
  tempIndex = (tempIndex + 1) % TEMP_HISTORY;
  if (tempIndex == 0) tempFilled = true;
  int n = tempFilled ? TEMP_HISTORY : tempIndex;
  float suma = 0;
  for (int i = 0; i < n; ++i) suma += tempHistory[i];
  tempMedia = n ? suma / n : 0.0;
  medias[mediaIndex] = tempMedia;
  mediaIndex = (mediaIndex + 1) % 3;
  bool alerta = true;
  for (int i = 0; i < 3; ++i) if (medias[i] <= 100.0) alerta = false;
  if (alerta) sendPacketWithChecksum(8, "e");
}

void simulate_orbit() {
  unsigned long now = millis();
  double time = ((now - orbitStartTime) / 1000.0) * TIME_COMPRESSION;
  double angle = 2.0 * PI * (time / real_orbital_period);
  long x = (long)(r * cos(angle));
  long y = (long)(r * sin(angle));
  long z = 0;
  String payload = String((long)time) + ":" + String(x) + ":" + String(y) + ":" + String(z);
  sendPacketWithChecksum(9, payload);
}

void checkTimeouts() {
  unsigned long now = millis();
  if (now - lastMessageReceived > COMM_TIMEOUT) {
    Serial.println("WARN: COMM TIMEOUT -> reset states");
    canTransmit = false;
    sending = false;
    lastMessageReceived = now;
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

  r = R_EARTH + ALTITUDE;
  real_orbital_period = 2.0 * PI * sqrt(pow(r, 3) / (G * M));
  orbitStartTime = millis();

  lastMessageReceived = millis();
  lastHeartbeat = millis();

  Serial.println("=== SAT READY (checksum compat) ===");
  delay(200);
}

void loop() {
  unsigned long now = millis();

  // 1) Leer comandos siempre (no bloqueante)
  while (satSerial.available()) {
    String line = satSerial.readStringUntil('\n');
    line.trim();
    if (line.length()) validateAndHandle(line);
  }

  // 2) Servo automático
  if (autoDistance && now - lastServoMove >= SERVO_MOVE_INTERVAL) {
    lastServoMove = now;
    servoAngle += servoDir * SERVO_STEP;
    if (servoAngle >= 180) { servoAngle = 180; servoDir = -1; }
    else if (servoAngle <= 0) { servoAngle = 0; servoDir = 1; }
    motor.write(servoAngle);
  }

  // 3) Timeouts
  checkTimeouts();

  // 4) Heartbeat cuando no transmites
  if (!sending && now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    String hb = "g";
    String chk = calcChecksum(hb);
    satSerial.println(hb + "*" + chk);
    delay(AFTER_TX_DELAY);
    Serial.println("TX> HEARTBEAT");
    lastHeartbeat = now;
  }

  // 5) Envío de bloque cuando tienes token y sending=true
  if (canTransmit && sending && now - lastSend >= sendPeriod) {
    Serial.println("=== SENDING BLOCK ===");
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) {
      sendPacketWithChecksum(4, "e:1");
    } else {
      sendPacketWithChecksum(1, String((int)(h * 100)) + ":" + String((int)(t * 100)));
      updateTempMedia(t);
      sendPacketWithChecksum(7, String((int)(tempMedia * 100)));
    }
    int dist = pingSensor();
    if (dist == 0) sendPacketWithChecksum(5, "e:1");
    else sendPacketWithChecksum(2, String(dist));

    if (!motor.attached()) sendPacketWithChecksum(6, "e:1");
    else sendPacketWithChecksum(6, String(servoAngle));

    simulate_orbit();

    // Liberar token (enviamos 67:0 con checksum)
    sendPacketWithChecksum(67, "0");
    canTransmit = false;
    Serial.println("=== TOKEN RELEASED ===");

    // LED blink
    digitalWrite(LEDPIN, HIGH);
    lastSend = now;
    delay(10);
    digitalWrite(LEDPIN, LOW);
  }

  // 6) Apagar led si estaba encendido (ya se apagó arriba)
}
