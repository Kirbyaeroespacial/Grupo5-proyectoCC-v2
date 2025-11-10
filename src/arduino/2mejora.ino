#include <DHT.h>
#include <SoftwareSerial.h>
#include <Servo.h>

// ----------------- Configuración sensores / pines -----------------
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

SoftwareSerial satSerial(10, 11); // RX=10, TX=11
const uint8_t LEDPIN = 12;    
bool sending = false;         
unsigned long lastSend = 0;   

unsigned long sendPeriod = 2000UL;

const float HUM_MIN = 0.0;
const float HUM_MAX = 100.0;
const float TEMP_MIN = -40.0;
const float TEMP_MAX = 80.0;

const uint8_t servoPin = 8;
const int potPin = A0;
Servo motor;

const uint8_t trigPin = 3;
const uint8_t echoPin = 4;
const unsigned long PULSE_TIMEOUT_US = 30000UL;
const int DIST_MAX_MM = 4000;

unsigned long ledTimer = 0;
bool ledState = false;

bool autoDistance = true;
int servoAngle = 90;
int servoDir = 1;
int manualTargetAngle = 90;

const int SERVO_STEP = 2;                    
const unsigned long SERVO_MOVE_INTERVAL = 40; 
unsigned long lastServoMove = 0;

void sendPacket(uint8_t type, const String &payload) {
    String msg = String(type) + ":" + payload;
    satSerial.println(msg);
    Serial.println("-> Enviado (sat): " + msg);
}

int pingSensor(uint8_t trig, uint8_t echo, unsigned long timeoutMicros) {
    digitalWrite(trig, LOW);
    delayMicroseconds(4);
    digitalWrite(trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig, LOW);
    unsigned long duration = pulseIn(echo, HIGH, timeoutMicros);
    if (duration == 0) return 0;
    float dist_mm_f = (float)duration * 0.343f / 2.0f;
    return (int)round(dist_mm_f);
}

void handleReceivedPacket(const String &cmd) {
    if (cmd.startsWith("1:")) sendPeriod = max(200UL, cmd.substring(2).toInt());
    else if (cmd.startsWith("2:")) {
        manualTargetAngle = constrain(cmd.substring(2).toInt(), 0, 180);
        if (!autoDistance) { servoAngle = manualTargetAngle; motor.write(servoAngle); }
    }
    else if (cmd == "3:i" || cmd == "3:r") sending = true;
    else if (cmd == "3:p") sending = false;
    else if (cmd == "4:a") autoDistance = true;
    else if (cmd == "4:m") { autoDistance = false; servoAngle = manualTargetAngle; motor.write(servoAngle); }
}

void setup() {
    Serial.begin(9600);
    satSerial.begin(9600);
    dht.begin();
    pinMode(LEDPIN, OUTPUT);
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    motor.attach(servoPin);
    motor.write(servoAngle);
    delay(1000);
    Serial.println("Satelite listo, esperando comandos...");
    satSerial.println("LISTO");
}

void loop() {
    unsigned long now = millis();

    // Mover servo
    if (autoDistance) {
        if (now - lastServoMove >= SERVO_MOVE_INTERVAL) {
            lastServoMove = now;
            servoAngle += servoDir * SERVO_STEP;
            if (servoAngle >= 180) { servoAngle = 180; servoDir = -1; }
            else if (servoAngle <= 0) { servoAngle = 0; servoDir = +1; }
            motor.write(servoAngle);
        }
    } else {
        int potVal = analogRead(potPin);
        int angleFromPot = map(potVal, 0, 1023, 180, 0);
        if (abs(angleFromPot - servoAngle) > 1) { servoAngle = angleFromPot; motor.write(servoAngle); }
    }

    // Leer comandos
    if (satSerial.available()) {
        String cmd = satSerial.readStringUntil('\n');
        cmd.trim(); cmd.toLowerCase();
        if (cmd.length() > 0) handleReceivedPacket(cmd);
    }

    // Enviar datos o heartbeat
    if (now - lastSend >= sendPeriod) {
        if (sending) {
            float h = dht.readHumidity();
            float t = dht.readTemperature();
            if (isnan(h) || isnan(t)) sendPacket(4, "e:1");
            else if (h < HUM_MIN || h > HUM_MAX || t < TEMP_MIN || t > TEMP_MAX) sendPacket(4, "e:2");
            else sendPacket(1, String((int)round(h*100)) + ":" + String((int)round(t*100)));

            int dist_mm = pingSensor(trigPin, echoPin, PULSE_TIMEOUT_US);
            if (dist_mm == 0) sendPacket(5, "e:1");
            else if (dist_mm > DIST_MAX_MM) sendPacket(5, "e:2");
            else sendPacket(2, String(dist_mm));

            ledState = true; ledTimer = now; digitalWrite(LEDPIN, HIGH);
        } else {
            satSerial.println("g"); // heartbeat
            Serial.println("Heartbeat: g");
        }
        lastSend = now;
    }

    if (ledState && (now - ledTimer > 80)) { digitalWrite(LEDPIN, LOW); ledState = false; }
}
