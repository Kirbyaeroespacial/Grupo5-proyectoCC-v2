#include <DHT.h>
#include <SoftwareSerial.h>
#include <Servo.h>

// ----------------- Configuración sensores / pines -----------------
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// SoftwareSerial: RX, TX (no tocar si dependes del otro Arduino)
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

// Servo + pot
const uint8_t servoPin = 8;
const int potPin = A0; // potenciómetro en A0
Servo motor;

// Ultrasonidos (no usar 10/11)
const uint8_t trigPin = 3;
const uint8_t echoPin = 4;
const unsigned long PULSE_TIMEOUT_US = 30000UL; // 30 ms timeout para pulseIn
const int DIST_MAX_MM = 4000; // distancia máxima plausible (mm)

// LED sin bloquear
unsigned long ledTimer = 0;
bool ledState = false;

// ----------------- Control "radar" / modo distancia -----------------
// true = radar automático (servo barre 0..180), false = manual (potenciómetro)
bool autoDistance = true; // por defecto el sensor está en modo radar
int servoAngle = 90;      // ángulo actual (inicial)
int servoDir = 1;         // +1 o -1 para barrido
int manualTargetAngle = 90; // si recibimos RX:2 en auto -> almacenamos aquí

// Parámetros de movimiento del radar (ajustables)
const int SERVO_STEP = 2;                    // grados por paso
const unsigned long SERVO_MOVE_INTERVAL = 40; // ms entre pasos de movimiento
unsigned long lastServoMove = 0;

// ----------------- Funciones utilitarias -----------------
/**
 * sendPacket
 * Envía por satSerial y por Serial (debug) un paquete con el formato type:payload
 */
void sendPacket(uint8_t type, const String &payload) {
  String msg = String(type) + ":" + payload;
  satSerial.println(msg);
  Serial.println("-> Enviado (sat): " + msg);
}

/**
 * pingSensor
 * Realiza una medición con el sensor ultrasónico y devuelve la distancia en mm.
 * Devuelve:
 *  - distancia en mm (>0) si se midió correctamente
 *  - 0 si timeout / sin eco
 */
int pingSensor(uint8_t trig, uint8_t echo, unsigned long timeoutMicros) {
  // Generar pulso
  digitalWrite(trig, LOW);
  delayMicroseconds(4);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  // Espera por el pulso en echo (con timeout)
  unsigned long duration = pulseIn(echo, HIGH, timeoutMicros); // microsegundos
  if (duration == 0) {
    // timeout -> sin eco
    return 0;
  }

  // Velocidad del sonido ≈ 343 m/s => 0.343 mm/µs
  // Distancia (mm) = duration_us * 0.343 / 2
  float dist_mm_f = (float)duration * 0.343f / 2.0f;
  int dist_mm = (int)round(dist_mm_f);
  return dist_mm;
}

/**
 * handleReceivedPacket
 * Parsea y aplica comandos en formato RX:<tipo>[:<valor>]
 * Tipos:
 *   RX:1:<ms>     -> cambiar periodo de envío (sendPeriod)
 *   RX:2:<angulo> -> nuevo ángulo objetivo para el servo (si manual -> mueve)
 *   RX:3          -> detener envío de datos (pausa)
 *   RX:4:<modo>   -> cambiar modo distancia: "auto" o "manual"
 *
 * Responde siempre con ACK_RX:<tipo> o ACK_RX:ERR
 */
void handleReceivedPacket(const String &rx) {
  // copia para manipular
  String msg = rx;
  msg.trim();

  // formato esperado: RX:<tipo>[:<valor>]
  // separar por ':'
  int firstColon = msg.indexOf(':'); // debería ser 2 (R=0,X=1, : =2)
  int secondColon = -1;
  if (firstColon >= 0) secondColon = msg.indexOf(':', firstColon + 1);

  // tipo como string
  String typeStr;
  String valueStr;
  if (secondColon == -1) {
    // sólo hay un colon => puede ser RX:3
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

  // Procesar tipos admitidos
  if (typeStr == "1") {
    // Cambiar periodo de envío (ms)
    long ms = valueStr.toInt();
    if (ms < 200) {
      // demasiado pequeño -> rechazamos (mínimo 200 ms)
      satSerial.println("ACK_RX:ERR"); // error
      Serial.println("RX:1 -> valor inválido (min 200ms). Ignorado.");
    } else {
      sendPeriod = (unsigned long)ms;
      satSerial.println("ACK_RX:1");
      Serial.print("RX:1 -> sendPeriod cambiado a ");
      Serial.print(sendPeriod); Serial.println(" ms");
    }

  } else if (typeStr == "2") {
    // Nueva orientación del sensor (ángulo)
    int ang = valueStr.toInt();
    if (ang < 0 || ang > 180) {
      satSerial.println("ACK_RX:ERR");
      Serial.println("RX:2 -> ángulo fuera de rango 0..180. Ignorado.");
    } else {
      manualTargetAngle = ang; // guardamos como objetivo manual
      if (!autoDistance) {
        // Si estamos en modo manual, movemos el servo inmediatamente
        servoAngle = ang;
        motor.write(servoAngle);
        Serial.print("RX:2 -> servo movido a "); Serial.println(servoAngle);
      } else {
        // En modo automático no movemos, pero guardamos el valor
        Serial.print("RX:2 -> modo auto: objetivo manual guardado: ");
        Serial.println(manualTargetAngle);
      }
      satSerial.println("ACK_RX:2");
    }

  } else if (typeStr == "3") {
    // Detener envío de datos (pausa)
    sending = false;
    satSerial.println("ACK_RX:3");
    Serial.println("RX:3 -> Envío pausado (sending=false)");

  } else if (typeStr == "4") {
    // Cambiar entre manual/auto para el sensor de distancia
    String m = valueStr;
    m.toLowerCase();
    if (m == "auto" || m == "1") {
      autoDistance = true;
      // no cambiamos servoAngle ahora; radar retomará desde servoAngle actual
      satSerial.println("ACK_RX:4");
      Serial.println("RX:4 -> modo DISTANCIA: AUTO (radar)");
    } else if (m == "manual" || m == "0") {
      autoDistance = false;
      // Si hay un manualTargetAngle guardado, aplicarlo
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
    // comando desconocido
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

  // Ultrasonidos
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Servo
  motor.attach(servoPin);
  motor.write(servoAngle); // posición inicial

  delay(1000);
  Serial.println("Satelite listo, esperando comandos...");
  satSerial.println("LISTO"); // mensaje inicial a Tierra/PC
}

// ----------------- Loop principal -----------------
void loop() {
  unsigned long now = millis();

  // 0) Mover servo según modo (radar automático o manual/pot)
  if (autoDistance) {
    // radar automático: barrido 0..180 sin blocking
    if (now - lastServoMove >= SERVO_MOVE_INTERVAL) {
      lastServoMove = now;
      servoAngle += servoDir * SERVO_STEP;
      if (servoAngle >= 180) {
        servoAngle = 180;
        servoDir = -1;
      } else if (servoAngle <= 0) {
        servoAngle = 0;
        servoDir = +1;
      }
      motor.write(servoAngle);
    }
  } else {
    // modo manual: controlar por potenciómetro (si no hemos movido por RX:2)
    // Si se quiere que RX:2 tenga prioridad absoluta, podríamos priorizar manualTargetAngle.
    // Aquí hacemos lectura por pot siempre que estemos en manual y no haya un ajuste por RX:2 reciente.
    int potVal = analogRead(potPin);
    int angleFromPot = map(potVal, 0, 1023, 180, 0);
    // Solo escribir si hay diferencia notable (evita micro escrituras)
    if (abs(angleFromPot - servoAngle) > 1) {
      servoAngle = angleFromPot;
      motor.write(servoAngle);
    }
  }

  // 1) Actualizar servo con potenciómetro continuamente (la lectura del pot la hacemos solo en manual arriba)
  // (EN LA VERSIÓN ORIGINAL había un motor.write(angle) incondicional; lo mantenemos conceptualmente
  // pero evitando reescribir el servo si no hay cambio.)

  // 2) Leer comandos entrantes por satSerial (p, r, i) y nuevos RX:...
  if (satSerial.available()) {
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.length() > 0) {
      Serial.print("Cmd recibido: "); Serial.println(cmd);

      // Si empieza con "rx:" -> manejador de protocolo recibido
      if (cmd.startsWith("rx:")) {
        handleReceivedPacket(cmd);
      } else {
        // Bloque de comando original (mantenido)
        if (cmd == "p") {
          sending = false;
          Serial.println("-> PAUSADO");
          satSerial.println("ACK:P");
        } else if (cmd == "r") {
          sending = true;
          Serial.println("-> REANUDADO");
          satSerial.println("ACK:R");
        } else if (cmd == "i") {
          sending = true;
          Serial.println("-> INICIADO");
          satSerial.println("ACK:I");
        } else {
          satSerial.print("ACK:?"); satSerial.println(cmd);
        }
      }
    }
  }

  // 3) Enviar datos o heartbeat cada sendPeriod ms
  if (now - lastSend >= sendPeriod) {
    if (sending) {
      // --- Leer DHT ---
      float h = dht.readHumidity();
      float t = dht.readTemperature();

      if (isnan(h) || isnan(t)) {
        // Error lectura DHT -> enviar 4:e:1
        Serial.println("ERROR lectura DHT! -> enviando 4:e:1");
        sendPacket(4, "e:1"); // 4:e:1 = error lectura (NaN)
      } else {
        // Detectar valores fuera de rango
        if (h < HUM_MIN || h > HUM_MAX || t < TEMP_MIN || t > TEMP_MAX) {
          // Error fuera de rango -> enviar 4:e:2
          Serial.println("Lectura fuera de rango DHT -> enviando 4:e:2");
          sendPacket(4, "e:2"); // 4:e:2 = datos fuera de rango (temp/hum)
        } else {
          // Lectura válida: enviar tipo 1 (hum y temp convertidos *100)
          int hi = (int)round(h * 100.0f);
          int ti = (int)round(t * 100.0f);
          String payload1 = String(hi) + ":" + String(ti);
          sendPacket(1, payload1); // 1:hi:ti
          // Indicador LED corto
          ledState = true;
          ledTimer = now;
          digitalWrite(LEDPIN, HIGH);
        }
      }

      // --- Medir distancia y enviar tipo 2 o error ---
      int dist_mm = pingSensor(trigPin, echoPin, PULSE_TIMEOUT_US);

      if (dist_mm == 0) {
        // Timeout / sin eco -> enviar 5:e:1
        Serial.println("ERROR Ultrasonidos: Timeout/sin eco -> enviando 5:e:1");
        sendPacket(5, "e:1");
      } else {
        if (dist_mm > DIST_MAX_MM) {
          // Fuera de rango -> enviar 5:e:2
          Serial.print("Ultrasonidos: distancia fuera de rango (");
          Serial.print(dist_mm);
          Serial.println(" mm) -> enviando 5:e:2");
          sendPacket(5, "e:2");
        } else {
          // Valor válido -> enviar 2:dist_mm
          sendPacket(2, String(dist_mm));
        }
      }

    } else {
      // sending == false -> heartbeat (mantenemos 'g' como antes)
      satSerial.println("g");
      Serial.println("Heartbeat: g");
    }
    lastSend = now;
  }

  // Apagar LED tras 80 ms (no bloqueante)
  if (ledState && (now - ledTimer > 80)) {
    digitalWrite(LEDPIN, LOW);
    ledState = false;
  }

  // NO delays largos aquí (servo y comprobaciones continuas)
}