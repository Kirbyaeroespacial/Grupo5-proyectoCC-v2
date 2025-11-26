#include <DHT.h> 
#include <SoftwareSerial.h>
#include <Servo.h>

#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

SoftwareSerial satSerial(10, 11); // RX=10, TX=11
const uint8_t LEDPIN = 12;
bool sending = false;
unsigned long lastSend = 0;
unsigned long sendPeriod = 2000UL;

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

// Sistema de turnos SIMPLIFICADO
bool canTransmit = false;
unsigned long lastHeartbeat = 0;
unsigned long lastMessageReceived = 0;
const unsigned long HEARTBEAT_INTERVAL = 4000;
const unsigned long COMM_TIMEOUT = 15000;

// Checksum
int corruptedCommands = 0;

// Media móvil temperatura
#define TEMP_HISTORY 10
float tempHistory[TEMP_HISTORY];
int tempIndex = 0;
bool tempFilled = false;
float tempMedia = 0.0;
float medias[3] = {0, 0, 0};
int mediaIndex = 0;

// === SIMULACIÓN ORBITAL ===
const double G = 6.67430e-11;
const double M = 5.97219e24;
const double R_EARTH = 6371000;
const double ALTITUDE = 400000;
const double TIME_COMPRESSION = 90.0;
double real_orbital_period;
double r;
unsigned long orbitStartTime = 0;

// === CHECKSUM CORREGIDO ===
String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i = 0; i < msg.length(); i++) {
    xorSum ^= (uint8_t)msg[i];
  }
  char hex[3];
  sprintf(hex, "%02X", xorSum);
  return String(hex);
}

void sendPacketWithChecksum(uint8_t type, const String &payload) {
  String msg = String(type) + ":" + payload;
  String chk = calcChecksum(msg);
  String fullMsg = msg + "*" + chk;
  satSerial.println(fullMsg);
  Serial.println("TX> " + fullMsg);
  delay(50); // Delay crítico para LoRa
}

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

void handleCommand(const String &cmd) {
    Serial.println("RX< " + cmd);
    lastMessageReceived = millis();
    
    // TOKEN RECIBIDO
    if (cmd == "67:1") {
        canTransmit = true;
        Serial.println("✓ TOKEN ON");
        return;
    } else if (cmd == "67:0") {
        canTransmit = false;
        Serial.println("✓ TOKEN OFF");
        return;
    }
    
    // Comandos normales
    int sep = cmd.indexOf(':');
    if (sep < 0) return;
    
    int cmdId = cmd.substring(0, sep).toInt();
    String value = cmd.substring(sep + 1);
    
    switch(cmdId) {
        case 1: // Cambiar período
            sendPeriod = max(200UL, (unsigned long)value.toInt());
            Serial.println("Period=" + String(sendPeriod));
            break;
            
        case 2: // Ángulo manual directo
            manualTargetAngle = constrain(value.toInt(), 0, 180);
            if (!autoDistance) {
                motor.write(manualTargetAngle);
                servoAngle = manualTargetAngle;
            }
            break;
            
        case 3: // Control transmisión
            if (value == "i" || value == "r") {
                sending = true;
                Serial.println("✓ TX START");
            } else if (value == "p") {
                sending = false;
                Serial.println("✓ TX STOP");
            }
            break;
            
        case 4: // Modo servo
            if (value == "a") {
                autoDistance = true;
                Serial.println("Mode=AUTO");
            } else if (value == "m") { 
                autoDistance = false;
                motor.write(manualTargetAngle);
                servoAngle = manualTargetAngle;
                Serial.println("Mode=MANUAL");
            }
            break;
            
        case 5: // Ángulo manual objetivo
            manualTargetAngle = constrain(value.toInt(), 0, 180);
            if (!autoDistance) {
                servoAngle = manualTargetAngle;
            }
            break;
    }
}

bool validateAndHandle(const String &data) {
    int asterisco = data.indexOf('*');
    if (asterisco == -1) {
        Serial.println("⚠ Sin checksum");
        corruptedCommands++;
        return false;
    }
    
    String msg = data.substring(0, asterisco);
    String chkRecv = data.substring(asterisco + 1);
    chkRecv.trim();
    String chkCalc = calcChecksum(msg);
    
    if (chkRecv.equalsIgnoreCase(chkCalc)) {
        handleCommand(msg);
        return true;
    } else {
        Serial.println("⚠ BAD CHK: recv=" + chkRecv + " calc=" + chkCalc);
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
  for (int i = 0; i < n; i++) suma += tempHistory[i];
  tempMedia = suma / n;

  medias[mediaIndex] = tempMedia;
  mediaIndex = (mediaIndex + 1) % 3;

  bool alerta = true;
  for (int i = 0; i < 3; i++) {
    if (medias[i] <= 100.0) alerta = false;
  }
  if (alerta) sendPacketWithChecksum(8, "e");
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

void checkTimeout() {
    unsigned long now = millis();
    if (now - lastMessageReceived > COMM_TIMEOUT) {
        Serial.println("⚠ TIMEOUT COMM");
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
    
    Serial.println("=============================");
    Serial.println("SATELLITE READY");
    Serial.println("Waiting for token (67:1)...");
    Serial.println("=============================");
    
    delay(1000); // Estabilización inicial
}

void loop() {
    unsigned long now = millis();
    
    // PRIORIDAD 1: Leer comandos SIEMPRE
    while (satSerial.available()) {
        String cmd = satSerial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0) {
            validateAndHandle(cmd);
        }
    }

    // PRIORIDAD 2: Servo automático
    if (autoDistance && now - lastServoMove >= SERVO_MOVE_INTERVAL) {
        lastServoMove = now;
        servoAngle += servoDir * SERVO_STEP;
        if (servoAngle >= 180) { servoAngle = 180; servoDir = -1; }
        else if (servoAngle <= 0) { servoAngle = 0; servoDir = 1; }
        motor.write(servoAngle);
    }

    // PRIORIDAD 3: Verificar timeout
    checkTimeout();

    // PRIORIDAD 4: Heartbeat (solo si NO está transmitiendo datos)
    if (!sending && now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
        String hb = "g";
        String chk = calcChecksum(hb);
        satSerial.println(hb + "*" + chk);
        Serial.println("TX> HEARTBEAT");
        lastHeartbeat = now;
        delay(50);
    }

    // PRIORIDAD 5: Envío de datos (solo con token Y en modo sending)
    if (canTransmit && sending && now - lastSend >= sendPeriod) {
        Serial.println("=== SENDING DATA PACKET ===");
        
        // Sensores
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

        // Liberar token
        sendPacketWithChecksum(67, "0");
        canTransmit = false;
        Serial.println("=== TOKEN RELEASED ===");
        
        digitalWrite(LEDPIN, HIGH);
        ledTimer = now;
        ledState = true;
        lastSend = now;
    }

    // LED
    if (ledState && now - ledTimer > 80) {
        digitalWrite(LEDPIN, LOW);
        ledState = false;
    }
}
