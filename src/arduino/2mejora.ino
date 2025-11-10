#include <DHT.h>
#include <SoftwareSerial.h>
#include <Servo.h>

// ----------------- Configuración sensores / pines -----------------
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// SoftwareSerial: RX, TX
SoftwareSerial satSerial(10, 11); // RX=10, TX=11

const uint8_t LEDPIN = 12;    // LED indicador
bool sending = false;         // empieza sin enviar
unsigned long lastSend = 0;   // temporizador para envío (usa sendPeriod)

// Periodo de envío (puede cambiar mediante RX:1:<ms>)
unsigned long sendPeriod = 2000UL; // ms (valor por defecto)

// Umbrales para detectar lecturas absurdas
const float HUM_MIN = 0.0;
const float HUM_MAX = 100.0;
const float TEMP_MIN = -40.0;
const float TEMP_MAX = 80.0;

// Servo + potenciómetro
const uint8_t servoPin = 8;
const int potPin = A0;
Servo motor;

// Ultrasonidos (no usar pines 10/11)
const uint8_t trigPin = 3;
const uint8_t echoPin = 4;
const unsigned long PULSE_TIMEOUT_US = 30000UL; // 30 ms timeout
const int DIST_MAX_MM = 4000;                    // distancia máxima plausible

// LED sin bloquear
unsigned long ledTimer = 0;
bool ledState = false;

// ----------------- Control "radar" / modo distancia -----------------
bool autoDistance = true;      // radar automático por defecto
int servoAngle = 90;           // ángulo actual (inicial)
int servoDir = 1;              // +1 o -1 para barrido
int manualTargetAngle = 90;    // objetivo manual si se recibe RX:2 en modo auto

// Parámetros de movimiento del radar
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
    if (duration == 0) return 0; // timeout

    float dist_mm_f = (float)duration * 0.343f / 2.0f;
    return (int)round(dist_mm_f);
}

void handleReceivedPacket(const String &rx) {
    String msg = rx;
    msg.trim();

    int firstColon = msg.indexOf(':');
    int secondColon = -1;
    if (firstColon >= 0) secondColon = msg.indexOf(':', firstColon + 1);

    String typeStr, valueStr;
    if (secondColon == -1) {
        typeStr = msg.substring(firstColon + 1);
        valueStr = "";
    } else {
        typeStr = msg.substring(firstColon + 1, secondColon);
        valueStr = msg.substring(secondColon + 1);
    }
    typeStr.trim();
    valueStr.trim();

    Serial.print("RX packet parse -> type: '"); Serial.print(typeStr);
    Serial.print("' value: '"); Serial.print(valueStr); Serial.println("'");

    if (typeStr == "1") { // Cambiar periodo de envío
        long ms = valueStr.toInt();
        if (ms < 200) {
            satSerial.println("ACK_RX:ERR");
            Serial.println("RX:1 -> valor inválido (min 200ms). Ignorado.");
        } else {
            sendPeriod = (unsigned long)ms;
            satSerial.println("ACK_RX:1");
            Serial.print("RX:1 -> sendPeriod cambiado a "); Serial.print(sendPeriod); Serial.println(" ms");
        }
    } else if (typeStr == "2") { // Nueva orientación del sensor
        int ang = valueStr.toInt();
        if (ang < 0 || ang > 180) {
            satSerial.println("ACK_RX:ERR");
            Serial.println("RX:2 -> ángulo fuera de rango 0..180. Ignorado.");
        } else {
            manualTargetAngle = ang;
            if (!autoDistance) {
                servoAngle = ang;
                motor.write(servoAngle);
                Serial.print("RX:2 -> servo movido a "); Serial.println(servoAngle);
            } else {
                Serial.print("RX:2 -> modo auto: objetivo manual guardado: "); 
                Serial.println(manualTargetAngle);
            }
            satSerial.println("ACK_RX:2");
        }
    } else if (typeStr == "3") { // Detener envío de datos
        sending = false;
        satSerial.println("ACK_RX:3");
        Serial.println("RX:3 -> Envío pausado (sending=false)");
    } else if (typeStr == "4") { // Cambiar modo distancia
        String m = valueStr;
        m.toLowerCase();
        if (m == "auto" || m == "1") {
            autoDistance = true;
            satSerial.println("ACK_RX:4");
            Serial.println("RX:4 -> modo DISTANCIA: AUTO (radar)");
        } else if (m == "manual" || m == "0") {
            autoDistance = false;
            servoAngle = manualTargetAngle;
            motor.write(servoAngle);
            satSerial.println("ACK_RX:4");
            Serial.print("RX:4 -> modo DISTANCIA: MANUAL (servo -> ");
            Serial.print(servoAngle); Serial.println(" )");
        } else {
            satSerial.println("ACK_RX:ERR");
            Serial.println("RX:4 -> valor inválido (usar 'auto' o 'manual').");
        }
    } else {
        satSerial.println("ACK_RX:ERR");
        Serial.print("RX -> tipo desconocido: "); Serial.println(typeStr);
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
    motor.write(servoAngle); // posición inicial

    delay(1000);
    Serial.println("Satelite listo, esperando comandos...");
    satSerial.println("LISTO");
}

// ----------------- Loop principal -----------------
void loop() {
    unsigned long now = millis();

    // 0) Mover servo según modo
    if (autoDistance) {
        if (now - lastServoMove >= SERVO_MOVE_INTERVAL) {
            lastServoMove = now;
            servoAngle += servoDir * SERVO_STEP;
            if (servoAngle >= 180) { servoAngle = 180; servoDir = -1; }
            else if (servoAngle <= 0) { servoAngle = 0; servoDir = +1; }
            motor.write(servoAngle);
        }
    } else { // Modo manual
        int potVal = analogRead(potPin);
        int angleFromPot = map(potVal, 0, 1023, 180, 0);
        if (abs(angleFromPot - servoAngle) > 1) {
            servoAngle = angleFromPot;
            motor.write(servoAngle);
        }
    }

    // 1) Leer comandos entrantes por satSerial
    if (satSerial.available()) {
        String cmd = satSerial.readStringUntil('\n');
        cmd.trim();
        cmd.toLowerCase();

        if (cmd.length() > 0) {
            Serial.print("Cmd recibido: "); Serial.println(cmd);

            if (cmd.startsWith("rx:")) {
                handleReceivedPacket(cmd);
            } else {
                // Bloque comando original
                if (cmd == "3:p") {
                    sending = false;
                    Serial.println("-> PAUSADO");
                    satSerial.println("ACK:P");
                } else if (cmd == "3:r") {
                    sending = true;
                    Serial.println("-> REANUDADO");
                    satSerial.println("ACK:R");
                } else if (cmd == "3:i") {
                    sending = true;
                    Serial.println("-> INICIADO");
                    satSerial.println("ACK:I");
                } else {
                    satSerial.print("ACK:?"); satSerial.println(cmd);
                }
            }
        }
    }

    // 2) Enviar datos o heartbeat
    if (now - lastSend >= sendPeriod) {
        if (sending) {
            // --- Leer DHT ---
            float h = dht.readHumidity();
            float t = dht.readTemperature();

            if (isnan(h) || isnan(t)) {
                Serial.println("ERROR lectura DHT! -> enviando 4:e:1");
                sendPacket(4, "e:1");
            } else if (h < HUM_MIN || h > HUM_MAX || t < TEMP_MIN || t > TEMP_MAX) {
                Serial.println("Lectura fuera de rango DHT -> enviando 4:e:2");
                sendPacket(4, "e:2");
            } else {
                int hi = (int)round(h * 100.0f);
                int ti = (int)round(t * 100.0f);
                String payload1 = String(hi) + ":" + String(ti);
                sendPacket(1, payload1);
                ledState = true;
                ledTimer = now;
                digitalWrite(LEDPIN, HIGH);
            }

            // --- Medir distancia ---
            int dist_mm = pingSensor(trigPin, echoPin, PULSE_TIMEOUT_US);
            if (dist_mm == 0) {
                Serial.println("ERROR Ultrasonidos: Timeout/sin eco -> enviando 5:e:1");
                sendPacket(5, "e:1");
            } else if (dist_mm > DIST_MAX_MM) {
                Serial.print("Ultrasonidos: distancia fuera de rango (");
                Serial.print(dist_mm); Serial.println(" mm) -> enviando 5:e:2");
                sendPacket(5, "e:2");
            } else {
                sendPacket(2, String(dist_mm));
            }

        } else { 
            satSerial.println("g"); // heartbeat
            Serial.println("Heartbeat: g");
        }
        lastSend = now;
    }

    // 3) Apagar LED tras 80 ms
    if (ledState && (now - ledTimer > 80)) {
        digitalWrite(LEDPIN, LOW);
        ledState = false;
    }
}