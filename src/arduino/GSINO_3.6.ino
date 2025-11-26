#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX=10, TX=11 (cruzado físicamente con satélite)

int errpin = 2;
char potent = A0;
unsigned long lastReceived = 0;

// Gestión de turnos MEJORADA
bool satHasToken = false;
unsigned long lastTokenSent = 0;
unsigned long lastMessageSent = 0;
const unsigned long TOKEN_CYCLE = 4000; // AUMENTADO: De 3500 a 4000ms
const unsigned long TOKEN_TIMEOUT = 8000; // Timeout para recuperar token
const unsigned long COMM_TIMEOUT = 12000; // Timeout total sin comunicación
bool waitingForRelease = false;

// Checksum: contadores
int corruptedFromSat = 0;
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000;

// === CHECKSUM ===
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
  mySerial.flush(); // NUEVO: Asegurar envío completo
  Serial.println("-> " + fullMsg);
  lastMessageSent = millis();
}

bool validateMessage(const String &data, String &cleanMsg) {
  int asterisco = data.indexOf('*');
  if (asterisco == -1) return false;
  
  cleanMsg = data.substring(0, asterisco);
  String chkRecv = data.substring(asterisco + 1);
  chkRecv.toUpperCase(); // NUEVO: Normalizar checksum recibido
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
  // Recibe: tiempo:X:Y:Z
  // Envía: Position: (X: ... m, Y: ... m, Z: ... m)
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

void checkAndRecoverFromTimeout() {
  unsigned long now = millis();
  
  // Si el satélite tiene el token pero lleva mucho sin responder, recuperarlo
  if (satHasToken && waitingForRelease && now - lastTokenSent > TOKEN_TIMEOUT) {
    Serial.println("⚠ TIMEOUT: Satélite no liberó token, RECUPERANDO");
    satHasToken = false;
    waitingForRelease = false;
    lastTokenSent = now;
  }
  
  // Si llevamos mucho sin recibir NADA del satélite, resetear sistema
  if (now - lastReceived > COMM_TIMEOUT) {
    Serial.println("⚠⚠ TIMEOUT CRÍTICO: Sin comunicación, RESETEANDO");
    satHasToken = false;
    waitingForRelease = false;
    lastTokenSent = now - TOKEN_CYCLE; // Forzar envío inmediato
    lastReceived = now; // Evitar spam
    
    // Parpadear LED de error
    digitalWrite(errpin, HIGH);
    delay(50);
    digitalWrite(errpin, LOW);
    delay(50);
    digitalWrite(errpin, HIGH);
    delay(50);
    digitalWrite(errpin, LOW);
  }
}

void setup() {
  Serial.begin(9600);
  mySerial.begin(4800); // CAMBIADO: De 9600 a 4800 para mayor fiabilidad
  mySerial.setTimeout(50); // NUEVO: Timeout corto para no bloquear
  
  Serial.println("====================================");
  Serial.println("GROUND STATION LISTO");
  Serial.println("Baudrate: 4800 bps");
  Serial.println("Sistema de recuperación ACTIVO");
  Serial.println("====================================");
  pinMode(errpin, OUTPUT);
  
  // Inicializar timers
  lastTokenSent = millis();
  lastStatsReport = millis();
  lastReceived = millis();
  lastMessageSent = millis();
}

void loop() {
  unsigned long now = millis();

  // PRIORIDAD 1: Verificar timeouts y recuperar si es necesario
  checkAndRecoverFromTimeout();

  // PRIORIDAD 2: Gestión de turnos
  // Solo enviar token si el satélite NO lo tiene y ha pasado el tiempo
  if (!satHasToken && !waitingForRelease && now - lastTokenSent > TOKEN_CYCLE) {
    Serial.println("=== ENVIANDO TOKEN AL SATÉLITE ===");
    sendWithChecksum("67:1");
    satHasToken = true;
    waitingForRelease = true;
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

  // PRIORIDAD 3: Comandos de usuario con checksum
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      sendWithChecksum(command);
      Serial.println("✓ Comando enviado: " + command);
    }
  }

  // ELIMINADO COMPLETAMENTE: El envío automático del potenciómetro
  // Ya no enviamos ángulos automáticamente, solo bajo comando manual
  
  // PRIORIDAD 4: Recepción con validación
  while (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      String cleanMsg;
      
      if (!validateMessage(data, cleanMsg)) {
        corruptedFromSat++;
        Serial.println("⚠ MENSAJE CORRUPTO: " + data);
        digitalWrite(errpin, HIGH);
        delay(50);
        digitalWrite(errpin, LOW);
        continue; // NUEVO: Usar continue en vez de return
      }
      
      // Actualizar tiempo de última recepción
      lastReceived = now;
      
      // Heartbeat especial (ignorado en esta versión)
      if (cleanMsg == "g") {
        Serial.println("<- HEARTBEAT del satélite ✓");
        continue;
      }
      
      int sepr = cleanMsg.indexOf(':');
      if (sepr > 0) {
        int id = cleanMsg.substring(0, sepr).toInt();
        String valor = cleanMsg.substring(sepr + 1);

        // Manejo del token de liberación
        if (id == 67 && valor == "0") {
          satHasToken = false;
          waitingForRelease = false;
          lastTokenSent = now; // Resetear temporizador
          Serial.println("=== SATÉLITE LIBERÓ TOKEN ✓ ===");
          continue;
        }

        // Protocolo normal + orbital
        if (id == 1) prot1(valor);
        else if (id == 2) prot2(valor);
        else if (id == 3) prot3(valor);
        else if (id == 4) prot4(valor);
        else if (id == 5) prot5(valor);
        else if (id == 6) prot6(valor);
        else if (id == 7) prot7(valor);
        else if (id == 8) prot8(valor);
        else if (id == 9) prot9(valor);

        if (valor.startsWith("e")) {
          digitalWrite(errpin, HIGH);
          delay(100);
          digitalWrite(errpin, LOW);
        }
      }
    }
  }
}