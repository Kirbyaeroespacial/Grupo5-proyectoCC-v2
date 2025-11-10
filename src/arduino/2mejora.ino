#include <DHT.h>
#include <SoftwareSerial.h>
#include <Servo.h>

// ----------------- Configuración sensores / pines -----------------
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// SoftwareSerial: RX, TX
SoftwareSerial satSerial(10, 11); // RX=10, TX=11

const uint8_t LEDPIN = 12;    
bool sending = false;         
unsigned long lastSend = 0;   

unsigned long sendPeriod = 2000UL; // ms (por defecto)

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

// ----------------- Funciones utilitarias -----------------
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

void handleReceivedPacket(const String &msg) {
    int firstColon = msg.indexOf(':');
    if (firstColon < 0) return;

    String idStr = msg.substring(0, firstColon);
    String valor = msg.substring(firstColon + 1);
    idStr.trim();
    valor.trim();

    int id = idStr.toInt();

    Serial.print("RX -> ID: "); Serial.print(id);
    Serial.print(" valor: "); Serial.println(valor);

    if (id == 1) { // Cambiar periodo
        long ms = valor.toInt();
        if (ms >= 200 && ms <= 10000) {
            sendPeriod = (unsigned long)ms;
            Serial.print("Periodo cambiado a "); Serial.println(sendPeriod);
        }
    } else if (id == 2) { // Ángulo manual (no implementado aún)
        int ang = valor.toInt();
        if (ang >= 0 && ang <= 180) {
            manualTargetAngle = ang;
            if (!autoDistance) {
                servoAngle = ang;
                motor.write(servoAngle);
            }
        }
    } else if (id == 3) { // Control envío
        if (valor == "i") {
            sending = true;
            Serial.println("INICIO ENVÍO");
        } else if (valor == "p") {
            sending = false;
            Serial.println("PAUSA ENVÍO");
        } else if (valor == "r") {
            sending = true;
            Serial.println("REANUDAR ENVÍO");
        }
    } else if (id == 4) { // Modo radar
        valor.toLowerCase();
        if (valor == "a") {
            autoDistance = true;
            Serial.println("Modo radar automático");
        } else if (valor == "m") {
            autoDistance = false;
            servoAngle = manualTargetAngle;
            motor.write(servoAngle);
            Serial.println("Modo radar manual");
        }
    }
}

// ----------------- Setup -----------------
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
    Serial.println("Satelite listo.");
}

// ----------------- Loop principal -----------------
void loop() {
    unsigned long now = millis();

    // Movimiento del servo
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
        if (abs(angleFromPot - servoAngle) > 1) {
            servoAngle = angleFromPot;
            motor.write(servoAngle);
        }
    }

    // Lectura de comandos
    if (satSerial.available()) {
        String cmd = satSerial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0) {
            handleReceivedPacket(cmd);
        }
    }

    // Envío de datos
    if (now - lastSend >= sendPeriod) {
        if (sending) {
            float h = dht.readHumidity();
            float t = dht.readTemperature();

            if (isnan(h) || isnan(t)) {
                sendPacket(4, "e");
            } else {
                int hi = (int)round(h * 100.0f);
                int ti = (int)round(t * 100.0f);
                sendPacket(1, String(hi) + ":" + String(ti));
            }

            int dist_mm = pingSensor(trigPin, echoPin, PULSE_TIMEOUT_US);
            if (dist_mm == 0) sendPacket(5, "e");
            else sendPacket(2, String(dist_mm));
        }
        lastSend = now;
    }

    if (ledState && (now - ledTimer > 80)) {
        digitalWrite(LEDPIN, LOW);
        ledState = false;
    }
}
