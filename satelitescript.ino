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

//!G 1. SISTEMA DE TURNOS - Implementa protocolo token-passing para evitar colisiones
// Solo transmite cuando canTransmit=true. Comandos 67:1 (dar turno) y 67:0 (liberar turno)
// Timeout de 6 segundos recupera automáticamente si se pierde el token
bool canTransmit = true;
unsigned long lastTokenTime = 0;
const unsigned long TOKEN_TIMEOUT = 6000;

//!G 2. CHECKSUM XOR - Valida integridad de TODOS los mensajes (envío y recepción)
// Formato: "tipo:payload*HEX". Descarta comandos corruptos y cuenta errores
int corruptedCommands = 0;

// Media móvil temperatura
#define TEMP_HISTORY 10
float tempHistory[TEMP_HISTORY];
int tempIndex = 0;
bool tempFilled = false;
float tempMedia = 0.0;
float medias[3] = {0, 0, 0};
int mediaIndex = 0;

//!G 3. SIMULACIÓN ORBITAL - Órbita circular a 400km usando tercera ley de Kepler
// Envía paquete tipo 9 con coordenadas XYZ cada ciclo. TIME_COMPRESSION=90x para visualización rápida
const double G = 6.67430e-11;
const double M = 5.97219e24;
const double R_EARTH = 6371000;
const double ALTITUDE = 400000;
const double TIME_COMPRESSION = 90.0;
double real_orbital_period;
double r;
unsigned long orbitStartTime = 0;

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

//!G 4. VALIDACIÓN DE COMANDOS - Verifica checksum antes de ejecutar
// Evita comandos corruptos que podrían causar comportamiento errático
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

  Serial.println("SAT listo con orbital");
}

void loop() {
  unsigned long now = millis();

  // Servo automático
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

  //!G 5. Comandos recibidos ahora pasan por validación de checksum
  if (satSerial.available()) {
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length())
      validateAndHandle(cmd);
  }

  //!G 6. Recuperación automática si no recibe token en 6 segundos
  if (!canTransmit && now - lastTokenTime > TOKEN_TIMEOUT) {
    canTransmit = true;
    Serial.println("Timeout: recuperando transmisión");
  }

  // Envío de datos
  if (now - lastSend >= sendPeriod) {
    //!G 7. Verificación de turno antes de transmitir (previene colisiones)
    if (sending && canTransmit) {
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
      if (dist == 0)
        sendPacketWithChecksum(5, "e:1");
      else
        sendPacketWithChecksum(2, String(dist));

      if (!motor.attached())
        sendPacketWithChecksum(6, "e:1");
      else
        sendPacketWithChecksum(6, String(servoAngle));

      //!G 8. Envío de posición orbital (paquete tipo 9: "tiempo:X:Y:Z")
      simulate_orbit();

      //!G 9. Liberación de turno al terminar transmisión completa
      sendPacketWithChecksum(67, "0");
      canTransmit = false;
    }

    //!G 10. Eliminado heartbeat "g" que interfería con sistema de turnos
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
