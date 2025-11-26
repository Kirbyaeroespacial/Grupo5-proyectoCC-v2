#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

int errpin = 2;
char potent = A0;
unsigned long lastReceived = 0;
unsigned long last = 0;
const unsigned long delay_ang = 200;

// Gestión de turnos con RECUPERACIÓN
bool satHasToken = false;
unsigned long lastTokenSent = 0;
unsigned long lastMessageSent = 0;      // NUEVO: Último mensaje enviado
const unsigned long TOKEN_CYCLE = 3500;
const unsigned long TOKEN_TIMEOUT = 7000;      // NUEVO: Timeout para recuperar token
const unsigned long COMM_TIMEOUT = 10000;      // NUEVO: Timeout total sin comunicación
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
  Serial.println("-> " + fullMsg);
  lastMessageSent = millis(); // Actualizar tiempo de envío
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

// === NUEVA FUNCIÓN: RECUPERACIÓN POR TIMEOUT ===
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
    Serial.println("⚠⚠ TIMEOUT CRÍTICO: Sin comunicación, RESETEANDO sistema");
    satHasToken = false;
    waitingForRelease = false;
    lastTokenSent = now - TOKEN_CYCLE; // Forzar envío inmediato de token
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
  
  // Si llevamos mucho esperando respuesta después de enviar un comando
  if (!satHasToken && now - lastMessageSent > 5000 && lastMessageSent > lastReceived) {
    Serial.println("⚠ TIMEOUT: Comando no confirmado, posible pérdida");
  }
}

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  Serial.println("====================================");
  Serial.println("GROUND STATION LISTO");
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
      // Esperar un poco si el satélite está transmitiendo
      if (satHasToken && waitingForRelease) {
        Serial.println("⏳ Esperando a que satélite libere token...");
        delay(100);
      }
      sendWithChecksum(command);
      Serial.println("✓ Comando enviado: " + command);
    }
  }

  // PRIORIDAD 4: Ángulo del potenciómetro
  // Solo enviar si el satélite NO tiene el token
  if (!satHasToken && now - last > delay_ang) {
    int potval = analogRead(potent);
    int angle = map(potval, 0, 1023, 180, 0);
    sendWithChecksum("5:" + String(angle));
    last = now;
  }
  
  // PRIORIDAD 5: Recepción con validación
  if (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      String cleanMsg;
      
      if (!validateMessage(data, cleanMsg)) {
        corruptedFromSat++;
        Serial.println("⚠ MENSAJE CORRUPTO RECIBIDO");
        digitalWrite(errpin, HIGH);
        delay(100);
        digitalWrite(errpin, LOW);
        return;
      }
      
      // Actualizar tiempo de última recepción
      lastReceived = now;
      
      // Heartbeat especial (no tiene ":")
      if (cleanMsg == "g") {
        Serial.println("<- HEARTBEAT del satélite ✓");
        return;
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
          return;
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
          delay(500);
          digitalWrite(errpin, LOW);
        }
      }
    }
  }
}
