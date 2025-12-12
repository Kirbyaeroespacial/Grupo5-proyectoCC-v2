// GS_CORREGIDO_TIMEOUT_POT_AND_BINARY_PARSE.ino
// Ground Station corregido - checksum ASCII, parseo binario, pot rápido, timeout led blink
#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

const uint8_t errpin = 2;
const uint8_t potent = A0;            // pin del potenciómetro
unsigned long lastReceived = 0;
unsigned long last = 0;
const unsigned long timeout = 20000;        // 20 s -> tiempo sin recepción para activar blink
const unsigned long delay_ang = 200;        // --- MODIFICADO --- antes 20000 (20s), ahora 200ms para control manual fluido

// Gestión de turnos
bool satHasToken = false;
unsigned long lastTokenSent = 0;
const unsigned long TOKEN_CYCLE = 2500;

// Checksum: contadores
int corruptedFromSat = 0;
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000;

// Variables para led de timeout (no bloqueante)
bool errBlinkState = false;
unsigned long errLastToggle = 0;
const unsigned long ERR_BLINK_INTERVAL = 500; // 500 ms parpadeo cuando hay timeout

// === CHECKSUM (ASCII) ===
String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i = 0; i < msg.length(); i++) {
    xorSum ^= msg[i];
  }
  String hex = String(xorSum, HEX);
  hex.toUpperCase();
  if (hex.length() == 1) hex = "0" + hex;
  return hex;
}

void sendWithChecksum(const String &msg) {
  String chk = calcChecksum(msg);
  String fullMsg = msg + "*" + chk;
  mySerial.println(fullMsg);
  Serial.println("GS-> " + fullMsg);
}

bool validateMessage(const String &data, String &cleanMsg) {
  int asterisco = data.indexOf('*');
  if (asterisco == -1) return false;

  cleanMsg = data.substring(0, asterisco);
  String chkRecv = data.substring(asterisco + 1);
  String chkCalc = calcChecksum(cleanMsg);

  return (chkRecv == chkCalc);
}

// Protocolo de aplicación (stubs - ajustar según lo que quieras mostrar)
void prot1(String valor) { Serial.println("1:" + valor); }
void prot2(String valor) { Serial.println("2:" + valor); }
void prot3(String valor) { Serial.println("3:" + valor); }
void prot4(String valor) { Serial.println("4:" + valor); }
void prot5(String valor) { Serial.println("5:" + valor); }
void prot6(String valor) { Serial.println("6:" + valor); }
void prot7(String valor) { Serial.println("7:" + valor); }
void prot8(String valor) { Serial.println("8:e"); }

void prot9(String valor) {
  // Formato esperado: time:X:Y:Z (o similar según el sat)
  // Si el SAT envía "time:123:456:789", lo mostramos en formato Position...
  // Esta función puede adaptarse si el formato real es distinto.
  int sepr = valor.indexOf(':');
  if (sepr <= 0) {
    Serial.println("prot9: formato inesperado");
    return;
  }
  // Buscar siguientes separadores
  int s1 = valor.indexOf(':', sepr + 1);
  int s2 = valor.indexOf(':', s1 + 1);
  if (s1 <= 0 || s2 <= 0) {
    Serial.println("prot9: separadores insuficientes");
    return;
  }
  String sx = valor.substring(s1 + 1, s2);
  String sy = valor.substring(s2 + 1);
  Serial.print("Position: (X: ");
  Serial.print(sx);
  Serial.print(" m, Y: ");
  Serial.print(sy);
  Serial.print(" m, Z: ");
  Serial.print("0");
  Serial.println(" m)");
}

void prot10(String valor) {
  Serial.print("Panel:");
  Serial.println(valor);
}

// ===== ESTRUCTURA TELEMETRÍA BINARIA (IDÉNTICA AL SAT) =====
#pragma pack(push, 1)
struct TelemetryFrame {
  uint8_t header;        // 0xAA
  uint16_t humidity;     // ×100
  int16_t temperature;   // ×100
  uint16_t tempAvg;      // ×100
  uint16_t distance;     // cm
  uint8_t servoAngle;    // 0-180
  uint16_t time_s;       // segundos simulados
  uint8_t x_b0, x_b1, x_b2, x_b3;
  uint8_t y_b0, y_b1, y_b2, y_b3;
  uint8_t z_b0, z_b1, z_b2, z_b3;
  uint8_t panelState;    // 0/40/60/100
  uint8_t checksum;      // XOR
};
#pragma pack(pop)
const size_t TELEMETRY_FRAME_SIZE = sizeof(TelemetryFrame);

// Convertir bytes a int32 (little-endian)
int32_t bytesToInt32(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  int32_t val = 0;
  val |= ((int32_t)b0);
  val |= ((int32_t)b1 << 8);
  val |= ((int32_t)b2 << 16);
  val |= ((int32_t)b3 << 24);
  return val;
}

// Leer y procesar telemetría binaria con DEBUG
// Retorna true si procesó (válido o corrupto) y por tanto el caller puede evitar
// interpretar más bytes como ASCII para el mismo paquete.
bool readBinaryTelemetry() {
  // Verificar si hay suficientes bytes
  if (mySerial.available() < (int)TELEMETRY_FRAME_SIZE) {
    return false;
  }

  // Peek para verificar header
  int p = mySerial.peek();
  if (p != 0xAA) {
    return false;
  }

  // Leer frame completo
  TelemetryFrame frame;
  uint8_t *raw = (uint8_t*)&frame;

  size_t bytesRead = 0;
  unsigned long startTime = millis();

  // Leer byte por byte con timeout corto
  while (bytesRead < TELEMETRY_FRAME_SIZE && (millis() - startTime) < 200) {
    if (mySerial.available()) {
      raw[bytesRead] = mySerial.read();
      bytesRead++;
    }
  }

  if (bytesRead != TELEMETRY_FRAME_SIZE) {
    Serial.print("ERROR: frame incompleto, leidos ");
    Serial.print(bytesRead);
    Serial.print("/");
    Serial.println(TELEMETRY_FRAME_SIZE);
    corruptedFromSat++;
    return true; // consideramos que intentamos procesar (evita doble lectura como ASCII)
  }

  // Validar header
  if (frame.header != 0xAA) {
    Serial.println("ERROR: header inválido");
    corruptedFromSat++;
    return true;
  }

  // Calcular checksum manualmente
  uint8_t cs = 0;
  for (size_t i = 0; i < TELEMETRY_FRAME_SIZE - 1; i++) {
    cs ^= raw[i];
  }

  Serial.print("RX: H=");
  Serial.print(frame.header, HEX);
  Serial.print(" Hum=");
  Serial.print(frame.humidity);
  Serial.print(" T=");
  Serial.print(frame.temperature);
  Serial.print(" CS_calc=");
  Serial.print(cs, HEX);
  Serial.print(" CS_recv=");
  Serial.print(frame.checksum, HEX);

  if (cs != frame.checksum) {
    Serial.println(" -> CORRUPTO!");
    corruptedFromSat++;
    // También podríamos encender errpin brevemente para aviso de corrupción
    digitalWrite(errpin, HIGH);
    delay(50);
    digitalWrite(errpin, LOW);
    return true;
  }

  Serial.println(" -> OK!");

  // Extraer coordenadas
  int32_t x = bytesToInt32(frame.x_b0, frame.x_b1, frame.x_b2, frame.x_b3);
  int32_t y = bytesToInt32(frame.y_b0, frame.y_b1, frame.y_b2, frame.y_b3);
  int32_t z = bytesToInt32(frame.z_b0, frame.z_b1, frame.z_b2, frame.z_b3);

  // Mostrar de forma legible para que el Python lo parsee (igual que en el PDF)
  Serial.print("Position: (X: ");
  Serial.print(x);
  Serial.print(" m, Y: ");
  Serial.print(y);
  Serial.print(" m, Z: ");
  Serial.print(z);
  Serial.println(" m)");

  // Mostrar panel y otros campos como ASCII (para compatibilidad con el parser Python)
  Serial.print("Panel:");
  Serial.println(frame.panelState);

  // --- MODIFICADO --- actualizar timestamp de última recepción válida
  lastReceived = millis();

  return true;
}

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);

  pinMode(errpin, OUTPUT); // --- MODIFICADO --- asegurar pinMode
  digitalWrite(errpin, LOW);

  Serial.println("GS listo");
}

void loop() {
  unsigned long now = millis();

  // Estadísticas cada STATS_INTERVAL
  if (now - lastStatsReport > STATS_INTERVAL) {
    if (corruptedFromSat > 0) {
      // Informar al sat (o al logger) cuantos corruptos hemos contado
      Serial.println("99:" + String(corruptedFromSat));
      corruptedFromSat = 0;
    }
    lastStatsReport = now;
  }

  // Comandos desde USB->Serial (usuario via Monitor serie)
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      if (command.indexOf('*') != -1) {
        // Si ya trae checksum, lo reenvío tal cual
        mySerial.println(command);
        Serial.println("GS-> " + command);
      } else {
        sendWithChecksum(command);
      }
    }
  }

  // Envío del potenciómetro periódicamente (ahora rápido)
  if (now - last > delay_ang) {
    int potval = analogRead(potent);
    int angle = map(potval, 0, 1023, 180, 0);
    sendWithChecksum("5:" + String(angle)); // envía 5:angle*CHK al sat
    last = now;
  }

  // ===== RECEPCIÓN DE DATOS =====
  bool processedBinary = false;

  if (mySerial.available()) {
    int p = mySerial.peek();

    if (p == 0xAA) {
      // Posible frame binario - dar un pequeño margen y procesar
      delay(50); // dar tiempo a que lleguen todos los bytes
      processedBinary = readBinaryTelemetry(); // true si procesó intento binario (válido o corrupto)
    }

    // Si no procesamos binario, intentamos leer ASCII (comandos con checksum, tokens, etc.)
    if (!processedBinary) {
      // Comprobamos de nuevo disponibilidad (puede que haya bytes ASCII)
      if (mySerial.available()) {
        String data = mySerial.readStringUntil('\n');
        data.trim();
        if (data.length() > 0) {
          String cleanMsg;
          if (!validateMessage(data, cleanMsg)) {
            Serial.println("SAT-> CORRUPTO: " + data);
            corruptedFromSat++;
            // breve indicación de error
            digitalWrite(errpin, HIGH);
            delay(50);
            digitalWrite(errpin, LOW);
            // NOTA: no actualizamos lastReceived aquí porque el mensaje fue corrupto
          } else {
            Serial.println("SAT-> OK: " + cleanMsg);
            // --- MODIFICADO --- Actualizamos última recepción válida
            lastReceived = millis();

            int sepr = cleanMsg.indexOf(':');
            if (sepr > 0) {
              int id = cleanMsg.substring(0, sepr).toInt();
              String valor = cleanMsg.substring(sepr + 1);

              // Manejar token
              if (id == 67 && valor == "0") {
                satHasToken = false;
                lastTokenSent = now;
                // no hacemos más
              } else {
                // Protocolos
                if (id == 1) prot1(valor);
                else if (id == 2) prot2(valor);
                else if (id == 3) prot3(valor);
                else if (id == 4) prot4(valor);
                else if (id == 5) prot5(valor);
                else if (id == 6) prot6(valor);
                else if (id == 7) prot7(valor);
                else if (id == 8) prot8(valor);
                else if (id == 9) prot9(valor);
                else if (id == 10) prot10(valor);
              }
            }
          }
        }
      }
    }
  }

  // --- MODIFICADO --- Timeout: si no se recibe nada desde hace 'timeout' ms -> parpadear errpin (no bloqueante)
  if (millis() - lastReceived > timeout) {
    if (millis() - errLastToggle >= ERR_BLINK_INTERVAL) {
      errLastToggle = millis();
      errBlinkState = !errBlinkState;
      digitalWrite(errpin, errBlinkState ? HIGH : LOW);
    }
  } else {
    // Si hay recepción reciente, asegurar que errpin esté apagado
    if (errBlinkState) {
      errBlinkState = false;
      digitalWrite(errpin, LOW);
    }
  }

  // Pequeña pausa no bloqueante para evitar hogging de CPU
  delay(1);
}
