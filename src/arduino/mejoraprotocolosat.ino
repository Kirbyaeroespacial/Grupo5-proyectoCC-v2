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

// Umbrales para lecturas absurdas
const float HUM_MIN = 0.0;
const float HUM_MAX = 100.0;
const float TEMP_MIN = -40.0;
const float TEMP_MAX = 80.0;

// Servo + pot
const uint8_t servoPin = 8;
const int potPin = A0;
Servo motor;

// Ultrasonidos
const uint8_t trigPin = 3;
const uint8_t echoPin = 4;
const unsigned long PULSE_TIMEOUT_US = 30000UL;
const int DIST_MAX_MM = 4000;

// LED sin bloquear
unsigned long ledTimer = 0;
bool ledState = false;

// ----------------- Funciones -----------------
void sendPacket(uint8_t type, const char* payload) {
  satSerial.print(type);
  satSerial.print(":");
  satSerial.println(payload);
  Serial.print("-> Enviado (sat): ");
  Serial.print(type);
  Serial.print(":");
  Serial.println(payload);
}

int pingSensor(uint8_t trig, uint8_t echo, unsigned long timeoutMicros) {
  digitalWrite(trig, LOW);
  delayMicroseconds(4);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  unsigned long duration = pulseIn(echo, HIGH, timeoutMicros);
  if (duration == 0) return 0;

  float dist_mm_f = duration * 0.343f / 2.0f;
  return (int)(dist_mm_f + 0.5f);
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

  delay(1000);
  Serial.println("Satelite listo, esperando comandos...");
  satSerial.println("LISTO");
}

// ----------------- Loop principal -----------------
void loop() {
  // Actualizar servo continuamente
  int potVal = analogRead(potPin);
  int angle = map(potVal, 0, 1023, 180, 0);
  motor.write(angle);

  // Leer comandos entrantes
  if (satSerial.available()) {
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    
    if (cmd.length() > 0) {
      Serial.print("Cmd recibido: "); Serial.println(cmd);

      if (cmd == "p") { sending = false; satSerial.println("ACK:P"); }
      else if (cmd == "r") { sending = true; satSerial.println("ACK:R"); }
      else if (cmd == "i") { sending = true; satSerial.println("ACK:I"); }
      else { satSerial.print("ACK:?"); satSerial.println(cmd); }
    }
  }

  unsigned long now = millis();
  if (now - lastSend >= 2000UL) {
    if (sending) {
      // --- DHT11 ---
      float h = dht.readHumidity();
      float t = dht.readTemperature();
      char buf[16];

      if (isnan(h) || isnan(t)) {
        sendPacket(4, "e:1");  // error lectura
      } else if (h < HUM_MIN || h > HUM_MAX || t < TEMP_MIN || t > TEMP_MAX) {
        sendPacket(4, "e:2");  // fuera de rango
      } else {
        int hi = (int)(h * 100.0f + 0.5f);
        int ti = (int)(t * 100.0f + 0.5f);
        sprintf(buf, "%d:%d", hi, ti);
        sendPacket(1, buf);

        // LED indicador sin bloquear
        ledState = true;
        ledTimer = now;
        digitalWrite(LEDPIN, HIGH);
      }

      // --- Ultrasonidos ---
      int dist_mm = pingSensor(trigPin, echoPin, PULSE_TIMEOUT_US);
      if (dist_mm == 0) sendPacket(5, "e:1");       // timeout
      else if (dist_mm > DIST_MAX_MM) sendPacket(5, "e:2"); // fuera de rango
      else {
        sprintf(buf, "%d", dist_mm);
        sendPacket(2, buf);
      }
    } else {
      satSerial.println("g"); // heartbeat
      Serial.println("Heartbeat: g");
    }
    lastSend = now;
  }

  // Apagar LED tras 80 ms
  if (ledState && now - ledTimer > 80) {
    digitalWrite(LEDPIN, LOW);
    ledState = false;
  }
}
