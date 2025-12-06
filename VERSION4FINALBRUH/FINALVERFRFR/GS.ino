//Código GROUND STATION CORREGIDO - CHECKSUM FIJO
#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

int errpin = 2;
char potent = A0;
unsigned long lastReceived = 0;
unsigned long last = 0;
const unsigned long timeout = 20000;
const unsigned long delay_ang = 20000;

// Gestión de turnos
bool satHasToken = false;
unsigned long lastTokenSent = 0;
const unsigned long TOKEN_CYCLE = 2500;

// Checksum: contadores
int corruptedFromSat = 0;
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000;

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

// Protocolo de aplicación
void prot1(String valor) { Serial.println("1:" + valor); }
void prot2(String valor) { Serial.println("2:" + valor); }
void prot3(String valor) { Serial.println("3:" + valor); }
void prot4(String valor) { Serial.println("4:" + valor); }
void prot5(String valor) { Serial.println("5:" + valor); }
void prot6(String valor) { Serial.println("6:" + valor); }
void prot7(String valor) { Serial.println("7:" + valor); }
void prot8(String valor) { Serial.println("8:e"); }

void prot9(String valor) {
  // Formato: tiempo:X:Y:Z
  int sep1 = valor.indexOf(':');
  int sep2 = valor.indexOf(':', sep1 + 1);
  int sep3 = valor.indexOf(':', sep2 + 1);
  
  if (sep1 > 0 && sep2 > 0 && sep3 > 0) {
    String x = valor.substring(sep1 + 1, sep2);
    String y = valor.substring(sep2 + 1, sep3);
    String z = valor.substring(sep3 + 1);
    
    Serial.print("Position: (X: ");
    Serial.print(x);
    Serial.print(" m, Y: ");
    Serial.print(y);
    Serial.print(" m, Z: ");
    Serial.print(z);
    Serial.println(" m)");
  }
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
  // Coordenadas como bytes individuales
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
  
  // Leer byte por byte con timeout
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
    return true;
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

  // DEBUG: Imprimir info del frame recibido
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

  // Validar checksum
  if (cs != frame.checksum) {
    Serial.println(" -> CORRUPTO!");
    corruptedFromSat++;
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
    return true;
  }

  Serial.println(" -> OK!");

  // Frame válido - extraer coordenadas
  int32_t x = bytesToInt32(frame.x_b0, frame.x_b1, frame.x_b2, frame.x_b3);
  int32_t y = bytesToInt32(frame.y_b0, frame.y_b1, frame.y_b2, frame.y_b3);
  int32_t z = bytesToInt32(frame.z_b0, frame.z_b1, frame.z_b2, frame.z_b3);

  // Generar salidas en formato ASCII para Python
  // prot1: humedad:temperatura
  String humTemp = String(frame.humidity) + ":" + String(frame.temperature);
  prot1(humTemp);

  // prot7: temperatura media
  prot7(String(frame.tempAvg));

  // prot2: distancia
  prot2(String(frame.distance));

  // prot6: ángulo servo
  prot6(String(frame.servoAngle));

  // prot9: órbita
  String orbStr = String(frame.time_s) + ":" + String(x) + ":" + String(y) + ":" + String(z);
  prot9(orbStr);

  // prot10: estado panel
  prot10(String(frame.panelState));

  // Actualizar timestamp
  lastReceived = millis();
  
  return true;
}

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  
  pinMode(errpin, OUTPUT);
  
  Serial.print("Tamaño frame esperado: ");
  Serial.println(TELEMETRY_FRAME_SIZE);
  Serial.println("GS LISTO (protocolo binario con DEBUG)");
  
  lastTokenSent = millis();
  lastStatsReport = millis();
  lastReceived = millis();
}

void loop() {
  unsigned long now = millis();

  // Gestión de turnos
  if (!satHasToken && now - lastTokenSent > TOKEN_CYCLE) {
    sendWithChecksum("67:1");
    satHasToken = true;
    lastTokenSent = now;
  }

  // Estadísticas cada 10s
  if (now - lastStatsReport > STATS_INTERVAL) {
    if (corruptedFromSat > 0) {
      Serial.println("99:" + String(corruptedFromSat));
      corruptedFromSat = 0;
    }
    lastStatsReport = now;
  }

  // Comandos de usuario
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      if (command.indexOf('*') != -1) {
        mySerial.println(command);
        Serial.println("GS-> " + command);
      } else {
        sendWithChecksum(command);
      }
    }
  }

  // Ángulo del potenciómetro
  if (now - last > delay_ang) {
    int potval = analogRead(potent);
    int angle = map(potval, 0, 1023, 180, 0);
    sendWithChecksum("5:" + String(angle));
    last = now;
  }
  
  // ===== RECEPCIÓN DE DATOS =====
  if (mySerial.available()) {
    // Intentar leer telemetría binaria primero
    int p = mySerial.peek();
    
    if (p == 0xAA) {
      // Posible frame binario - esperar a que llegue completo
      delay(50); // Dar tiempo a que lleguen todos los bytes
      
      bool processed = readBinaryTelemetry();
      if (processed) {
        return; // Ya procesado
      }
      // Si no se procesó, continuar al flujo ASCII
    }
    
    // Flujo ASCII (comandos y tokens)
    String data = mySerial.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      String cleanMsg;
      
      if (!validateMessage(data, cleanMsg)) {
        Serial.println("SAT-> CORRUPTO: " + data);
        corruptedFromSat++;
        digitalWrite(errpin, HIGH);
        delay(100);
        digitalWrite(errpin, LOW);
        return;
      }
      
      Serial.println("SAT-> OK: " + cleanMsg);
      
      int sepr = cleanMsg.indexOf(':');
      if (sepr > 0) {
        int id = cleanMsg.substring(0, sepr).toInt();
        String valor = cleanMsg.substring(sepr + 1);

        // Token
        if (id == 67 && valor == "0") {
          satHasToken = false;
          lastTokenSent = now;
          return;
        }

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

        // Manejo de errores
        if (valor.startsWith("e")) {
          digitalWrite(errpin, HIGH);
          delay(500);
          digitalWrite(errpin, LOW);
        }
      }
      lastReceived = now;
    }
  }

  // Timeout
  if (now - lastReceived > timeout) {
    Serial.println("TIMEOUT: sin datos del satélite");
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
    delay(50);
    lastReceived = now;
  }
}
