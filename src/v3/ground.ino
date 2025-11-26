// GROUND_STATION.ino — corregido
#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

int errpin = 2;
const int potent = A0;
unsigned long lastReceived = 0;
unsigned long last = 0;
const unsigned long delay_ang = 200;

// Gestión de turnos con RECUPERACIÓN
bool satHasToken = false;
unsigned long lastTokenSent = 0;
unsigned long lastMessageSent = 0;
const unsigned long TOKEN_CYCLE = 3500;
const unsigned long TOKEN_TIMEOUT = 7000;
const unsigned long COMM_TIMEOUT = 10000;
bool waitingForRelease = false;

// Checksum: contadores
int corruptedFromSat = 0;
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000;

// Blink no bloqueante para error
unsigned long blinkStart = 0;
bool blinkState = false;
const unsigned long BLINK_PERIOD = 200;

// === CHECKSUM ===
String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i = 0; i < msg.length(); i++) {
    xorSum ^= (uint8_t)msg[i];
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
  lastMessageSent = millis();
}

bool validateMessage(const String &data, String &cleanMsg) {
  int asterisco = data.indexOf('*');
  if (asterisco == -1) return false;
  cleanMsg = data.substring(0, asterisco);
  String chkRecv = data.substring(asterisco + 1);
  String chkCalc = calcChecksum(cleanMsg);
  return (chkRecv == chkCalc);
}

// Protocolo de aplicación (salida por Serial para GUI)
void prot1(String valor) { Serial.println("1:" + valor); }
void prot2(String valor) { Serial.println("2:" + valor); }
void prot3(String valor) { Serial.println("3:" + valor); }
void prot4(String valor) { Serial.println("4:" + valor); }
void prot5(String valor) { Serial.println("5:" + valor); }
void prot6(String valor) { Serial.println("6:" + valor); }
void prot7(String valor) { Serial.println("7:" + valor); }
void prot8(String valor) { Serial.println("8:e"); }

void prot9(String valor) {
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

  if (satHasToken && waitingForRelease && now - lastTokenSent > TOKEN_TIMEOUT) {
    Serial.println("⚠ TIMEOUT: Satélite no liberó token, RECUPERANDO");
    satHasToken = false;
    waitingForRelease = false;
    lastTokenSent = now;
  }

  if (now - lastReceived > COMM_TIMEOUT) {
    Serial.println("⚠⚠ TIMEOUT CRÍTICO: Sin comunicación, RESETEANDO sistema");
    satHasToken = false;
    waitingForRelease = false;
    lastTokenSent = now - TOKEN_CYCLE;
    lastReceived = now;
    // iniciar parpadeo no bloqueante
    blinkStart = now;
    blinkState = true;
    digitalWrite(errpin, HIGH);
  }

  if (!satHasToken && now - lastMessageSent > 5000 && lastMessageSent > lastReceived) {
    Serial.println("⚠ TIMEOUT: Comando no confirmado, posible pérdida");
  }

  // parpadeo no bloqueante cuando hay blinkState activo
  if (blinkState) {
    if ((now - blinkStart) % (BLINK_PERIOD * 2) < BLINK_PERIOD) digitalWrite(errpin, HIGH);
    else digitalWrite(errpin, LOW);
    // tiempo máximo de parpadeo 3s
    if (now - blinkStart > 3000) { blinkState = false; digitalWrite(errpin, LOW); }
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
  lastTokenSent = millis();
  lastStatsReport = millis();
  lastReceived = millis();
  lastMessageSent = millis();
}

void loop() {
  unsigned long now = millis();
  checkAndRecoverFromTimeout();

  if (!satHasToken && !waitingForRelease && now - lastTokenSent > TOKEN_CYCLE) {
    Serial.println("=== ENVIANDO TOKEN AL SATÉLITE ===");
    sendWithChecksum("67:1");
    satHasToken = true;
    waitingForRelease = true;
    lastTokenSent = now;
  }

  if (now - lastStatsReport > STATS_INTERVAL) {
    if (corruptedFromSat > 0) {
      Serial.println("99:" + String(corruptedFromSat));
      corruptedFromSat = 0;
    }
    lastStatsReport = now;
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      if (satHasToken && waitingForRelease) {
        Serial.println("⏳ Esperando a que satélite libere token...");
        delay(100);
      }
      sendWithChecksum(command);
      Serial.println("✓ Comando enviado: " + command);
    }
  }

  if (!satHasToken && now - last > delay_ang) {
    int potval = analogRead(potent);
    int angle = map(potval, 0, 1023, 180, 0);
    sendWithChecksum("5:" + String(angle));
    last = now;
  }

  if (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    data.trim();
    if (data.length() > 0) {
      String cleanMsg;
      if (!validateMessage(data, cleanMsg)) {
        corruptedFromSat++;
        Serial.println("⚠ MENSAJE CORRUPTO RECIBIDO");
        // parpadeo breve de error
        digitalWrite(errpin, HIGH);
        delay(80);
        digitalWrite(errpin, LOW);
        // no return: seguimos procesando
      } else {
        lastReceived = now;
        if (cleanMsg == "g") {
          Serial.println("<- HEARTBEAT del satélite ✓");
        } else {
          int sepr = cleanMsg.indexOf(':');
          if (sepr > 0) {
            int id = cleanMsg.substring(0, sepr).toInt();
            String valor = cleanMsg.substring(sepr + 1);
            if (id == 67 && valor == "0") {
              satHasToken = false;
              waitingForRelease = false;
              lastTokenSent = now;
              Serial.println("=== SATÉLITE LIBERÓ TOKEN ✓ ===");
            } else {
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
                blinkStart = now;
                blinkState = true;
              }
            }
          }
        }
      }
    }
  }
}
