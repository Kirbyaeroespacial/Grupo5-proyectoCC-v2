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
const int potPin = A0;
Servo motor;

const uint8_t trigPin = 3;
const uint8_t echoPin = 4;
const unsigned long PULSE_TIMEOUT_US = 30000UL;
const int DIST_MAX_MM = 4000;

bool autoDistance = true;
int servoAngle = 90;
int servoDir = 1;
int manualTargetAngle = 90;

const int SERVO_STEP = 2;
const unsigned long SERVO_MOVE_INTERVAL = 40;
unsigned long lastServoMove = 0;

bool ledState = false;
unsigned long ledTimer = 0;

// --- Media móvil temperatura ---
#define TEMP_HISTORY 10
float tempHistory[TEMP_HISTORY];
int tempIndex = 0;
bool tempFilled = false;
float tempMedia = 0.0;
float medias[3] = {0, 0, 0};
int mediaIndex = 0;


// ============================================================
// a! PROTOCOLO DE APLICACIÓN: sendPacket()
// Envía mensajes con un formato estandarizado "ID:datos:CS".
// CS = CRC-8 (polynomial 0x07) en dos dígitos hex ASCII.
// ============================================================
static uint8_t crc8_buf(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i) {
      if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x07);
      else crc <<= 1;
    }
  }
  return crc;
}
static String byteToHex(uint8_t b) {
  const char *h = "0123456789ABCDEF";
  char s[3] = { h[(b>>4)&0x0F], h[b&0x0F], '\0' };
  return String(s);
}
void sendPacket(uint8_t type, const String &payload) {
  String msg = String(type) + ":" + payload;        // "type:payload"
  // calcular CRC sobre bytes ASCII del mensaje
  size_t L = msg.length();
  uint8_t buf[L];
  for (size_t i = 0; i < L; ++i) buf[i] = (uint8_t)msg[i];
  uint8_t cs = crc8_buf(buf, L);
  String finalMsg = msg + ":" + byteToHex(cs);     // "type:payload:CS"
  satSerial.println(finalMsg);
  Serial.println("-> " + finalMsg);
}


// ============================================================
// a! SENSOR ULTRASÓNICO: pingSensor()
// ============================================================
int pingSensor() {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(4);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    unsigned long dur = pulseIn(echoPin, HIGH, PULSE_TIMEOUT_US);
    if (dur == 0) return 0; // 0 = error o sin eco
    return (int)(dur * 0.343 / 2.0); // conversión a milímetros
}


// ============================================================
// a! INTERPRETACIÓN DE COMANDOS: handleCommand()
// ============================================================
void handleCommand(const String &cmd) {
    Serial.println(cmd);
    if (cmd.startsWith("1:")) sendPeriod = max(200UL, cmd.substring(2).toInt());
    else if (cmd.startsWith("2:")) {
        manualTargetAngle = constrain(cmd.substring(2).toInt(), 0, 180);
        if (!autoDistance) {
            motor.write(manualTargetAngle);
            servoAngle = manualTargetAngle;
        }
    }
    else if (cmd == "3:i" || cmd == "3:r") sending = true;
    else if (cmd == "3:p") sending = false;
    else if (cmd == "4:a") autoDistance = true;
    else if (cmd == "4:m") { 
        autoDistance = false;
        motor.write(manualTargetAngle);
        servoAngle = manualTargetAngle;
    }
    else if (cmd.startsWith("5:")) {
        int ang = constrain(cmd.substring(2).toInt(), 0, 180);
        manualTargetAngle = ang;
        if (!autoDistance) {
          servoAngle = manualTargetAngle;
        }
    }
}


// ============================================================
// a! MEDIA MÓVIL DE TEMPERATURA: updateTempMedia()
// ============================================================
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
  if (alerta) sendPacket(8, "e");  // ID 8 -> alerta de alta temperatura
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
    Serial.println("SAT listo");
}


void loop() {
    unsigned long now = millis();
    motor.write(servoAngle);

    // ============================================================
    // a! SERVO AUTOMÁTICO / MANUAL
    // ============================================================
    if (autoDistance && now - lastServoMove >= SERVO_MOVE_INTERVAL) {
        lastServoMove = now;
        servoAngle += servoDir * SERVO_STEP;
        if (servoAngle >= 180) { servoAngle = 180; servoDir = -1; }
        else if (servoAngle <= 0) { servoAngle = 0; servoDir = 1; }
        motor.write(servoAngle);
    }

    if (satSerial.available()) {
        String cmd = satSerial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length()) handleCommand(cmd);
    }

    if (now - lastSend >= sendPeriod) {
        if (sending) {
            float h = dht.readHumidity();
            float t = dht.readTemperature();
            if (isnan(h) || isnan(t)) {
                sendPacket(4, "e:1");
            } else {
                sendPacket(1, String((int)(h * 100)) + ":" + String((int)(t * 100)));
                updateTempMedia(t);
                sendPacket(7, String((int)(tempMedia * 100)));
            }

            int dist = pingSensor();
            if (dist == 0) sendPacket(5, "e:1");
            else sendPacket(2, String(dist));

            if (!motor.attached()) sendPacket(6, "e:1");
            else sendPacket(6, String(servoAngle));
        } else {
            satSerial.println("g");
        }

        digitalWrite(LEDPIN, HIGH);
        ledTimer = now;
        ledState = true;
        lastSend = now;
    }

    if (ledState && now - ledTimer > 80) {
        digitalWrite(LEDPIN, LOW);
        ledState = false;
    }
}
