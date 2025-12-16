#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX=10, TX=11 para comunicación con satélite

// ==================== PINES Y CONFIGURACIÓN ====================
int errpin = 2;        // LED de error (checksum, sensores, timeout)
char potent = A0;      // Potenciómetro para control manual del servo

// Timeouts y delays
unsigned long lastReceived = 0;
unsigned long last = 0;
const unsigned long timeout = 20000;    // 20 segundos sin datos = alarma
const unsigned long delay_ang = 5000;    // Leer potenciómetro cada 5000ms

// Variables de modo manual
bool manualMode = false;  // true = potenciómetro controla servo
int lastPotAngle = -1;    // Último ángulo enviado (evitar duplicados)

// Gestión de turnos (token passing)
bool satHasToken = false;
unsigned long lastTokenSent = 0;
const unsigned long TOKEN_CYCLE = 2500;  // Ciclo de turno: 2.5 segundos

// Contador de mensajes corruptos
int corruptedFromSat = 0;
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000;  // Reportar cada 10 segundos

// Variables para alarma de timeout
bool timeoutAlarm = false;
bool alarmState = false;
unsigned long lastAlarmToggle = 0;
const unsigned long ALARM_BLINK_INTERVAL = 300;  // Parpadeo cada 300ms

// ==================== FUNCIONES DE CHECKSUM ====================
String calcChecksum(const String &msg) {
  /**
   * Calcula checksum XOR de un mensaje
   * Retorna: String hexadecimal de 2 dígitos (ej: "A5", "3F")
   */
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
  /**
   * Envía mensaje con checksum al satélite
   * Formato: mensaje*checksum\n
   * Ejemplo: "5:90*4A\n"
   */
  String chk = calcChecksum(msg);
  String fullMsg = msg + "*" + chk;
  mySerial.println(fullMsg);
  Serial.println("GS-> " + fullMsg);
}

bool validateMessage(const String &data, String &cleanMsg) {
  /**
   * Valida el checksum de un mensaje recibido
   * Retorna: true si checksum válido, false si corrupto
   * cleanMsg: mensaje sin el checksum (salida por referencia)
   */
  int asterisco = data.indexOf('*');
  if (asterisco == -1) return false;
  
  cleanMsg = data.substring(0, asterisco);
  String chkRecv = data.substring(asterisco + 1);
  String chkCalc = calcChecksum(cleanMsg);
  
  return (chkRecv == chkCalc);
}

// ==================== PROTOCOLOS DE APLICACIÓN ====================
// Funciones que reenvían datos del satélite al PC mediante Serial

void prot1(String valor) { Serial.println("1:" + valor); }  // Temp y humedad
void prot2(String valor) { Serial.println("2:" + valor); }  // Distancia
void prot3(String valor) { Serial.println("3:" + valor); }  // Error transmisión
void prot4() {Serial.println("4:e1");}
void prot5(String valor) { Serial.println("5:"); }  // Error angulo
void prot6(String valor) { Serial.println("6:" + valor); }  // Confirmación ángulo
void prot7(String valor) { Serial.println("7:" + valor); }  // Temperatura media
void prot8(String valor) { Serial.println("8:e"); }         // Alta temperatura

void prot9(String valor) {
  /**
   * Procesa datos orbitales (formato: time:x:y:z)
   * Reenvía al PC en formato legible
   */
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
  /**
   * Reenvía estado del panel solar (0, 40, 60, 100%)
   */
  Serial.print("Panel:");
  Serial.println(valor);
}

// ==================== TELEMETRÍA BINARIA ====================
// Estructura para recibir telemetría en formato compacto

#pragma pack(push, 1)  // Empaquetar sin padding
struct TelemetryFrame {
  uint8_t header;        // 0xAA (marcador de inicio)
  uint16_t humidity;     // Humedad (dividir por 100 para %)
  int16_t temperature;   // Temperatura (dividir por 100 para °C)
  uint16_t tempAvg;      // Temperatura media
  uint16_t distance;     // Distancia en mm
  uint8_t servoAngle;    // Ángulo del servo (0-180)
  uint16_t time_s;       // Tiempo de órbita en segundos
  // Coordenadas orbitales (4 bytes cada una, little-endian)
  uint8_t x_b0, x_b1, x_b2, x_b3;
  uint8_t y_b0, y_b1, y_b2, y_b3;
  uint8_t z_b0, z_b1, z_b2, z_b3;
  uint8_t panelState;    // Estado panel solar (0, 40, 60, 100)
  uint8_t checksum;      // XOR de todos los bytes anteriores
};
#pragma pack(pop)
const size_t TELEMETRY_FRAME_SIZE = sizeof(TelemetryFrame);

int32_t bytesToInt32(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  /**
   * Reconstruye un int32 desde 4 bytes (little-endian)
   */
  int32_t val = 0;
  val |= ((int32_t)b0);
  val |= ((int32_t)b1 << 8);
  val |= ((int32_t)b2 << 16);
  val |= ((int32_t)b3 << 24);
  return val;
}

bool readBinaryTelemetry() {
  /**
   * Lee un frame de telemetría binario completo
   * Retorna: true si se procesó un frame (válido o corrupto)
   *          false si no hay suficientes datos
   */
  if (mySerial.available() < (int)TELEMETRY_FRAME_SIZE) {
    return false;
  }

  // Verificar header sin consumir
  int p = mySerial.peek();
  if (p != 0xAA) {
    return false;
  }

  // Leer frame completo
  TelemetryFrame frame;
  uint8_t *raw = (uint8_t*)&frame;
  
  size_t bytesRead = 0;
  unsigned long startTime = millis();
  
  // Leer con timeout de 200ms
  while (bytesRead < TELEMETRY_FRAME_SIZE && (millis() - startTime) < 200) {
    if (mySerial.available()) {
      raw[bytesRead] = mySerial.read();
      bytesRead++;
    }
  }
  
  // Verificar que se leyó el frame completo
  if (bytesRead != TELEMETRY_FRAME_SIZE) {
    Serial.print("ERROR: frame incompleto, leidos ");
    Serial.print(bytesRead);
    Serial.print("/");
    Serial.println(TELEMETRY_FRAME_SIZE);
    corruptedFromSat++;
    return true;
  }

  if (frame.header != 0xAA) {
    Serial.println("ERROR: header inválido");
    corruptedFromSat++;
    return true;
  }

  // Calcular checksum (XOR de todos los bytes excepto el último)
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
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
    return true;
  }

  Serial.println(" -> OK!");

  // Reconstruir coordenadas orbitales
  int32_t x = bytesToInt32(frame.x_b0, frame.x_b1, frame.x_b2, frame.x_b3);
  int32_t y = bytesToInt32(frame.y_b0, frame.y_b1, frame.y_b2, frame.y_b3);
  int32_t z = bytesToInt32(frame.z_b0, frame.z_b1, frame.z_b2, frame.z_b3);

  // Reenviar datos al PC
  String humTemp = String(frame.humidity) + ":" + String(frame.temperature);
  prot1(humTemp);
  prot7(String(frame.tempAvg));
  prot2(String(frame.distance));
  prot6(String(frame.servoAngle));

  String orbStr = String(frame.time_s) + ":" + String(x) + ":" + String(y) + ":" + String(z);
  prot9(orbStr);
  prot10(String(frame.panelState));

  lastReceived = millis();
  
  return true;
}

// ==================== GESTIÓN DE ALARMA DE TIMEOUT ====================
void updateTimeoutAlarm() {
  /**
   * Verifica si han pasado >20s sin recibir datos
   * Si es así, hace parpadear el LED de alarma
   */
  unsigned long now = millis();
  
  if (now - lastReceived > timeout) {
    if (!timeoutAlarm) {
      timeoutAlarm = true;
      Serial.println("3:TIMEOUT: sin datos del satélite");
    }
    
    // Parpadeo del LED
    if (now - lastAlarmToggle > ALARM_BLINK_INTERVAL) {
      alarmState = !alarmState;
      digitalWrite(errpin, alarmState ? HIGH : LOW);
      lastAlarmToggle = now;
    }
  } else {
    // Comunicación OK
    if (timeoutAlarm) {
      timeoutAlarm = false;
      Serial.println("Comunicación restaurada");
    }
    digitalWrite(errpin, LOW);
    alarmState = false;
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(9600);      // Comunicación con PC
  mySerial.begin(9600);    // Comunicación con satélite
  
  pinMode(errpin, OUTPUT);
  pinMode(errpin, OUTPUT);
  digitalWrite(errpin, LOW);
  
  Serial.print("Tamaño frame esperado: ");
  Serial.println(TELEMETRY_FRAME_SIZE);
  Serial.println("GS LISTO (protocolo binario + modo manual potenciómetro + timeout)");
  
  lastTokenSent = millis();
  lastStatsReport = millis();
  lastReceived = millis();
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  unsigned long now = millis();

  // Verificar timeout
  updateTimeoutAlarm();

  // Gestión de turnos: enviar token al satélite cada 2.5s
  if (!satHasToken && now - lastTokenSent > TOKEN_CYCLE) {
    sendWithChecksum("67:1");  // Token = 1 (satélite puede transmitir)
    satHasToken = true;
    lastTokenSent = now;
  }

  // Reportar estadísticas de errores cada 10s
  if (now - lastStatsReport > STATS_INTERVAL) {
    if (corruptedFromSat > 0) {
      Serial.println("99:" + String(corruptedFromSat));
      corruptedFromSat = 0;
    }
    lastStatsReport = now;
  }

  // ==================== COMANDOS DESDE PC ====================
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.length() > 0) {
      // Strip checksum for mode detection
      String baseCmd = command;
      int asteriskIndex = command.indexOf('*');
      if (asteriskIndex != -1) {
        baseCmd = command.substring(0, asteriskIndex);
      }

      // Detect mode change commands
      if (baseCmd == "4:a") {
        manualMode = false;
        Serial.println("Modo AUTO desde comando");
      } else if (baseCmd == "4:m") {
        manualMode = true;
        Serial.println("Modo MANUAL desde comando");
      }

      // Forward exactly what Python sent (already has checksum)
      mySerial.println(command);
      Serial.println("GS-> " + command);
    }
  }

  // Control del potenciómetro (cada 500ms)
  if (now - last > delay_ang) {
    int potval = analogRead(potent);
    int angle = map(potval, 0, 1023, 180, 0);  // Invertir rango
    
    // Solo enviar en modo manual y si cambió significativamente (±2°)
    if (manualMode) {
      if (abs(angle - lastPotAngle) > 2) {
        sendWithChecksum("5:" + String(angle));
        lastPotAngle = angle;
        Serial.print("Potenciómetro manual: ");
        Serial.println(angle);
      }
    } else {
      lastPotAngle = angle;  // Actualizar para cuando cambie a manual
    }
    
    last = now;
  }
  
  // ==================== RECEPCIÓN DE DATOS ====================
  if (mySerial.available()) {
    int p = mySerial.peek();
    
    // Si es telemetría binaria (empieza con 0xAA)
    if (p == 0xAA) {
      delay(50);  // Esperar a que llegue el frame completo
      bool processed = readBinaryTelemetry();
      if (processed) {
        return;  // Frame procesado, volver al loop
      }
    }
    
    // Mensajes ASCII (protocolo texto con checksum)
    String data = mySerial.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      String cleanMsg;
      
      // Validar checksum
      if (!validateMessage(data, cleanMsg)) {
        Serial.println("SAT-> CORRUPTO: " + data);
        corruptedFromSat++;
        digitalWrite(errpin, HIGH);
        delay(100);
        digitalWrite(errpin, LOW);
        return;
      }
      
      Serial.println("SAT-> OK: " + cleanMsg);
      
      // Parsear mensaje (formato: ID:valor)
      int sepr = cleanMsg.indexOf(':');
      if (sepr > 0) {
        int id = cleanMsg.substring(0, sepr).toInt();
        String valor = cleanMsg.substring(sepr + 1);

        // Protocolo 67: gestión de turnos
        if (id == 67 && valor == "0") {
          satHasToken = false;  // Satélite devuelve el token
          lastTokenSent = now;
          return;
        }

        // Procesar según protocolo
        if (id == 1) prot1(valor);
        else if (id == 2) prot2(valor);
        else if (id == 3) prot3(valor);
        else if (id == 4) prot4();
        else if (id == 5) prot5(valor);
        else if (id == 6) prot6(valor);
        else if (id == 7) prot7(valor);
        else if (id == 8) prot8(valor);
        else if (id == 9) prot9(valor);
        else if (id == 10) prot10(valor);

        // Encender LED de error si el mensaje contiene 'e'
        if (valor.startsWith("e")) {
          digitalWrite(errpin, HIGH);
          delay(500);
          digitalWrite(errpin, LOW);
        }
      }
      lastReceived = now;
    }
  }
}
