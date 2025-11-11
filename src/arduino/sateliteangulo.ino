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

const uint8_t servoPin = 8;
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

void sendPacket(uint8_t type, const String &payload) {
    String msg = String(type) + ":" + payload;
    satSerial.println(msg);
    Serial.println("-> " + msg);
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
    if (cmd.startsWith("1:")) sendPeriod = max(200UL, cmd.substring(2).toInt());
    else if (cmd.startsWith("2:")) {
        manualTargetAngle = constrain(cmd.substring(2).toInt(), 0, 180);
        if (!autoDistance) motor.write(manualTargetAngle);
    }
    else if (cmd == "3:i" || cmd == "3:r") sending = true;
    else if (cmd == "3:p") sending = false;
    else if (cmd == "4:a") autoDistance = true;
    else if (cmd == "4:m") { autoDistance = false; motor.write(manualTargetAngle); }
}

void setup() {
    Serial.begin(9600);
    satSerial.begin(9600);
    pinMode(LEDPIN, OUTPUT);
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    dht.begin();
    motor.attach(servoPin);
    motor.write(servoAngle);
    Serial.println("SAT listo");
}

void loop() {
    unsigned long now = millis();

    // Servo automático
    if (autoDistance && now - lastServoMove >= SERVO_MOVE_INTERVAL) {
        lastServoMove = now;
        servoAngle += servoDir * SERVO_STEP;
        if (servoAngle >= 180) { servoAngle = 180; servoDir = -1; }
        else if (servoAngle <= 0) { servoAngle = 0; servoDir = 1; }
        motor.write(servoAngle);
    }

    // Leer comandos de Tierra
    if (satSerial.available()) {
        String cmd = satSerial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length()) handleCommand(cmd);
    }

    // Enviar datos
    if (now - lastSend >= sendPeriod) {
        if (sending) {
            float h = dht.readHumidity();
            float t = dht.readTemperature();
            if (isnan(h) || isnan(t)) sendPacket(4, "e:1");
            else sendPacket(1, String((int)(h * 100)) + ":" + String((int)(t * 100)));

            int dist = pingSensor();
            if (dist == 0) sendPacket(5, "e:1");
            else sendPacket(2, String(dist));

            // --- NUEVO: enviar ANGULO (ID 6) o error si hay problema con servo ---
            if (!motor.attached()) {
                sendPacket(6, "e:1"); // error: servo no attached
            } else {
                // comprobar rango por si acaso
                if (servoAngle < 0 || servoAngle > 180) {
                    sendPacket(6, "e:1"); // error: angulo fuera de rango
                } else {
                    sendPacket(6, String(servoAngle));
                }
            }
            // --------------------------------------------------------------------

        } else {
            satSerial.println("g"); // heartbeat
        }
        digitalWrite(LEDPIN, HIGH);
        ledTimer = now;
        lastSend = now;
    }

    if (ledState && now - ledTimer > 80) {
        digitalWrite(LEDPIN, LOW);
        ledState = false;
    }
}
