#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

int errpin = 2;
char potent = A0;
unsigned long lastReceived = 0;
unsigned long last = 0;
const unsigned long delay_ang = 300;

// Gestión de turnos SIMPLIFICADA
bool satHasToken = false;
unsigned long lastTokenSent = 0;
unsigned long lastMessageSent = 0;
const unsigned long TOKEN_CYCLE = 4000; // 4 segundos
const unsigned long TOKEN_TIMEOUT = 10000;
const unsigned long COMM_TIMEOUT = 15000;
bool waitingForRelease = false;

// Checksum
int corruptedFromSat = 0;
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000;

// === CHECKSUM CORREGIDO (IGUAL QUE SATÉLITE) ===
String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i = 0; i < msg.length(); i++) {
    xorSum ^= (uint8_t)msg[i];
  }
  char hex[3];
  sprintf(hex, "%02X", xorSum);
  return String(hex);
}

void sendWithChecksum(const String &msg) {
  String chk = calcChecksum(msg);
  String fullMsg = msg + "*" + chk;
  mySerial.println(fullMsg);
  Serial.println("TX> " + fullMsg);
  lastMessageSent = millis();
  delay(50); // Delay crítico para LoRa
}

bool validateMessage(const String &data, String &cleanMsg) {
  int asterisco = data.indexOf('*');
  if (asterisco == -1) {
    Serial.println("⚠ Sin checksum");
    return false;
  }
  
  cleanMsg = data.substring(0, asterisco);
  String chkRecv = data.substring(asterisco + 1);
  chkRecv.trim();
  String chkCalc = calcChecksum(cleanMsg);
  
  bool valid = chkRecv.equalsIgnoreCase(chkCalc);
  if (!valid) {
    Serial.println("⚠ BAD CHK: recv=" + chkRecv + " calc=" + chkCalc);
  }
  return valid;
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

void checkAndRecover() {
  unsigned long now = millis();
  
  // Recuperar token si satélite no responde
  if (satHasToken && waitingForRelease && now - lastTokenSent > TOKEN_TIMEOUT) {
    Serial.println("⚠ TOKEN TIMEOUT - Recovering");
    satHasToken = false;
    waitingForRelease = false;
    lastTokenSent = now;
  }
  
  // Reset total si no hay comunicación
  if (now - lastReceived > COMM_TIMEOUT) {
    Serial.println("⚠⚠ COMM TIMEOUT - Full reset");
    satHasToken = false;
    waitingForRelease = false;
    lastTokenSent = now - TOKEN_CYCLE;
    lastReceived = now;
    
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
  }
}

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  Serial.println("=============================");
  Serial.println("GROUND STATION READY");
  Serial.println("Starting token system...");
  Serial.println("=============================");
  
  pinMode(errpin, OUTPUT);
  digitalWrite(errpin, LOW);
  
  lastTokenSent = millis();
  lastStatsReport = millis();
  lastReceived = millis();
  lastMessageSent = millis();
  
  delay(1000); // Estabilización inicial
}

void loop() {
  unsigned long now = millis();

  // PRIORIDAD 1: Verificar timeouts
  checkAndRecover();

  // PRIORIDAD 2: Enviar token periódicamente
  if (!satHasToken && !waitingForRelease && now - lastTokenSent > TOKEN_CYCLE) {
    Serial.println("=== SENDING TOKEN ===");
    sendWithChecksum("67:1");
    satHasToken = true;
    waitingForRelease = true;
    lastTokenSent = now;
  }

  // PRIORIDAD 3: Estadísticas
  if (now - lastStatsReport > STATS_INTERVAL) {
    if (corruptedFromSat > 0) {
      Serial.println("99:" + String(corruptedFromSat));
      corruptedFromSat = 0;
    }
    lastStatsReport = now;
  }

  // PRIORIDAD 4: Comandos de usuario
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      // Esperar si satélite está transmitiendo
      if (satHasToken && waitingForRelease) {
        Serial.println("⏳ Waiting for satellite...");
        delay(200);
      }
      sendWithChecksum(command);
      Serial.println("✓ Command sent: " + command);
    }
  }

  // PRIORIDAD 5: Ángulo del potenciómetro (solo si NO está ocupado)
  if (!satHasToken && now - last > delay_ang) {
    int potval = analogRead(potent);
    int angle = map(potval, 0, 1023, 180, 0);
    sendWithChecksum("5:" + String(angle));
    last = now;
  }
  
  // PRIORIDAD 6: Recepción con validación
  while (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    data.trim();

    if (data.length() == 0) continue;
    
    String cleanMsg;
    
    if (!validateMessage(data, cleanMsg)) {
      corruptedFromSat++;
      digitalWrite(errpin, HIGH);
      delay(50);
      digitalWrite(errpin, LOW);
      continue;
    }
    
    lastReceived = now;
    Serial.println("RX< " + cleanMsg);
    
    // Heartbeat (sin ":")
    if (cleanMsg == "g") {
      Serial.println("✓ HEARTBEAT");
      continue;
    }
    
    int sepr = cleanMsg.indexOf(':');
    if (sepr > 0) {
      int id = cleanMsg.substring(0, sepr).toInt();
      String valor = cleanMsg.substring(sepr + 1);

      // Token liberado
      if (id == 67 && valor == "0") {
        satHasToken = false;
        waitingForRelease = false;
        lastTokenSent = now;
        Serial.println("=== TOKEN RELEASED ✓ ===");
        continue;
      }

      // Protocolo de datos
      switch(id) {
        case 1: prot1(valor); break;
        case 2: prot2(valor); break;
        case 3: prot3(valor); break;
        case 4: prot4(valor); break;
        case 5: prot5(valor); break;
        case 6: prot6(valor); break;
        case 7: prot7(valor); break;
        case 8: prot8(valor); break;
        case 9: prot9(valor); break;
      }

      if (valor.startsWith("e")) {
        digitalWrite(errpin, HIGH);
        delay(300);
        digitalWrite(errpin, LOW);
      }
    }
  }
}
