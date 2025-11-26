/* GROUND STATION - Arduino
   - Envía token periódicamente (67:1*CK)
   - Acepta mensajes con o sin checksum
   - Envía comandos desde USB al sat (agrega checksum automáticamente)
   - Evita enviar comandos cuando sat tiene token
*/

#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX (hacia LoRa)

int errpin = 2;
const int potent = A0; // pin A0
unsigned long lastReceived = 0;
unsigned long lastAngleSent = 0;
const unsigned long ANGLE_INTERVAL = 300UL;

// Token control
bool satHasToken = false;
unsigned long lastTokenSent = 0;
unsigned long lastMessageSent = 0;
const unsigned long TOKEN_CYCLE = 4000UL;
const unsigned long TOKEN_TIMEOUT = 10000UL;
const unsigned long COMM_TIMEOUT = 15000UL;
bool waitingForRelease = false;

// Stats
int corruptedFromSat = 0;
int noChecksumFromSat = 0;
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000UL;

// ---------------- checksum (XOR) ----------------
String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i = 0; i < msg.length(); ++i) xorSum ^= (uint8_t)msg[i];
  char hexbuf[3];
  sprintf(hexbuf, "%02X", xorSum);
  return String(hexbuf);
}

// enviar msg con checksum
void sendWithChecksum(const String &msg) {
  String chk = calcChecksum(msg);
  String full = msg + "*" + chk;
  mySerial.println(full);
  delay(60); // dar tiempo a módulo LoRa para transmitir
  Serial.println("TX> " + full);
  lastMessageSent = millis();
}

// validate: acepta sin '*' (legacy) pero lo marca
bool validateMessage(const String &data, String &cleanMsg) {
  String s = data;
  s.trim();
  if (s.length() == 0) return false;
  int ast = s.indexOf('*');
  if (ast == -1) {
    // aceptar legacy
    noChecksumFromSat++;
    cleanMsg = s;
    Serial.println("WARN: mensaje SIN checksum (aceptado)");
    return true;
  }
  cleanMsg = s.substring(0, ast);
  String chkRecv = s.substring(ast + 1);
  chkRecv.trim();
  chkRecv.toUpperCase();
  String chkCalc = calcChecksum(cleanMsg);
  bool ok = chkRecv.equalsIgnoreCase(chkCalc);
  if (!ok) {
    Serial.println("ERR CK recv=" + chkRecv + " calc=" + chkCalc);
  }
  return ok;
}

// protocol handlers (imprimen para Python/GUI)
void prot1(String val) { Serial.println("1:" + val); } // temp:hum
void prot2(String val) { Serial.println("2:" + val); } // dist
void prot3(String val) { Serial.println("3:" + val); }
void prot4(String val) { Serial.println("4:" + val); }
void prot5(String val) { Serial.println("5:" + val); }
void prot6(String val) { Serial.println("6:" + val); }
void prot7(String val) { Serial.println("7:" + val); }
void prot8(String val) { Serial.println("8:e"); }
void prot9(String val) {
  int s1 = val.indexOf(':');
  int s2 = val.indexOf(':', s1 + 1);
  int s3 = val.indexOf(':', s2 + 1);
  if (s1>0 && s2>0 && s3>0) {
    String t = val.substring(0,s1);
    String x = val.substring(s1+1,s2);
    String y = val.substring(s2+1,s3);
    String z = val.substring(s3+1);
    Serial.print("Position: (X: "); Serial.print(x);
    Serial.print(" m, Y: "); Serial.print(y);
    Serial.print(" m, Z: "); Serial.print(z);
    Serial.println(" m)");
  }
}

void checkAndRecover() {
  unsigned long now = millis();
  if (satHasToken && waitingForRelease && now - lastTokenSent > TOKEN_TIMEOUT) {
    Serial.println("WARN: token timeout -> recovering");
    satHasToken = false;
    waitingForRelease = false;
    lastTokenSent = now;
  }
  if (now - lastReceived > COMM_TIMEOUT) {
    Serial.println("WARN: comm timeout -> resetting token state");
    satHasToken = false;
    waitingForRelease = false;
    lastTokenSent = now - TOKEN_CYCLE;
    lastReceived = now;
    digitalWrite(errpin, HIGH); delay(80); digitalWrite(errpin, LOW);
  }
}

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  pinMode(errpin, OUTPUT);
  digitalWrite(errpin, LOW);
  lastTokenSent = millis();
  lastStatsReport = millis();
  lastReceived = millis();
  lastMessageSent = millis();
  Serial.println("=== GROUND READY (checksum compat) ===");
  delay(200);
}

void loop() {
  unsigned long now = millis();
  checkAndRecover();

  // Enviar token periódicamente si no está dado
  if (!satHasToken && !waitingForRelease && now - lastTokenSent > TOKEN_CYCLE) {
    Serial.println(">>> SENDING TOKEN 67:1");
    sendWithChecksum("67:1");
    satHasToken = true;
    waitingForRelease = true;
    lastTokenSent = now;
  }

  // Reporte de estadísticas
  if (now - lastStatsReport > STATS_INTERVAL) {
    if (corruptedFromSat > 0) {
      Serial.println("99:" + String(corruptedFromSat));
      corruptedFromSat = 0;
    }
    if (noChecksumFromSat > 0) {
      Serial.println("99:NC:" + String(noChecksumFromSat));
      noChecksumFromSat = 0;
    }
    lastStatsReport = now;
  }

  // Comandos por USB desde PC (Serial)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      // si sat tiene token, esperar un poco antes de mandar
      if (satHasToken && waitingForRelease) {
        Serial.println("INFO: sat tiene token, esperando...");
        delay(200);
      }
      sendWithChecksum(cmd);
      Serial.println("CMD SENT: " + cmd);
    }
  }

  // Enviar ángulo potenciómetro si no hay token (evitar molestar al sat cuando transmite)
  if (!satHasToken && now - lastAngleSent > ANGLE_INTERVAL) {
    int pv = analogRead(potent);
    int ang = map(pv, 0, 1023, 180, 0);
    sendWithChecksum("5:" + String(ang));
    lastAngleSent = now;
  }

  // Leer desde LoRa (mySerial)
  while (mySerial.available()) {
    String line = mySerial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    String clean;
    if (!validateMessage(line, clean)) {
      corruptedFromSat++;
      digitalWrite(errpin, HIGH); delay(40); digitalWrite(errpin, LOW);
      continue;
    }
    lastReceived = now;
    Serial.println("RX< " + clean);

    // Heartbeat or simple 'g' may come without ':'
    if (clean == "g") { Serial.println("HEARTBEAT"); continue; }

    int sepr = clean.indexOf(':');
    if (sepr > 0) {
      int id = clean.substring(0, sepr).toInt();
      String val = clean.substring(sepr + 1);

      // Token release
      if (id == 67 && val == "0") {
        satHasToken = false;
        waitingForRelease = false;
        lastTokenSent = now;
        Serial.println(">>> TOKEN RELEASED BY SAT");
        continue;
      }

      // Protocolo de datos
      switch (id) {
        case 1: prot1(val); break;
        case 2: prot2(val); break;
        case 3: prot3(val); break;
        case 4: prot4(val); break;
        case 5: prot5(val); break;
        case 6: prot6(val); break;
        case 7: prot7(val); break;
        case 8: prot8(val); break;
        case 9: prot9(val); break;
      }
      // si valor empieza 'e' => señal de error
      if (val.startsWith("e")) {
        digitalWrite(errpin, HIGH); delay(200); digitalWrite(errpin, LOW);
      }
    }
  }
}
