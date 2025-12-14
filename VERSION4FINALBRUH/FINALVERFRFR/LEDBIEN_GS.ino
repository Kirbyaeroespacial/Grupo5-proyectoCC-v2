// ================= GROUND STATION =================
// Código original + FIX LED ERROR + TIMEOUT COMUNICACIÓN
// Compatible 100% con Satélite y Python

#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

// ---------------- PINES ----------------
const int errpin = 2;
const int potent = A0;

// ---------------- TIEMPOS ----------------
unsigned long lastReceived = 0;
unsigned long last = 0;
const unsigned long timeout = 20000;
const unsigned long delay_ang = 20000;

// ---------------- TIMEOUT COMUNICACIÓN (NUEVO) ----------------
bool commTimeout = false;
unsigned long lastDataTime = 0;
unsigned long lastBlinkTime = 0;
bool errLedState = false;

const unsigned long COMM_TIMEOUT = 20000; // 20 s
const unsigned long BLINK_INTERVAL = 300; // ms

// ---------------- GESTIÓN TOKEN ----------------
bool satHasToken = false;
unsigned long lastTokenSent = 0;
const unsigned long TOKEN_CYCLE = 2500;

// ---------------- CHECKSUM ----------------
int corruptedFromSat = 0;
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000;

// ================= CHECKSUM ASCII =================
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
  String fullMsg = msg + "*" + calcChecksum(msg);
  mySerial.println(fullMsg);
  Serial.println("GS-> " + fullMsg);
}

bool validateMessage(const String &data, String &cleanMsg) {
  int asterisk = data.indexOf('*');
  if (asterisk == -1) return false;
  cleanMsg = data.substring(0, asterisk);
  String chkRecv = data.substring(asterisk + 1);
  return chkRecv == calcChecksum(cleanMsg);
}

// ================= PROTOCOLOS =================
void prot1(String v) { Serial.println("1:" + v); }
void prot2(String v) { Serial.println("2:" + v); }
void prot3(String v) { Serial.println("3:" + v); }
void prot4(String v) { Serial.println("4:" + v); }
void prot5(String v) { Serial.println("5:" + v); }
void prot6(String v) { Serial.println("6:" + v); }
void prot7(String v) { Serial.println("7:" + v); }
void prot8(String v) { Serial.println("8:e"); }

void prot9(String valor) {
  int s1 = valor.indexOf(':');
  int s2 = valor.indexOf(':', s1 + 1);
  int s3 = valor.indexOf(':', s2 + 1);
  if (s1 > 0 && s2 > 0 && s3 > 0) {
    Serial.print("Position: (X: ");
    Serial.print(valor.substring(s1 + 1, s2));
    Serial.print(" m, Y: ");
    Serial.print(valor.substring(s2 + 1, s3));
    Serial.print(" m, Z: ");
    Serial.print(valor.substring(s3 + 1));
    Serial.println(" m)");
  }
}

void prot10(String v) {
  Serial.print("Panel:");
  Serial.println(v);
}

// ================= SETUP =================
void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);

  pinMode(errpin, OUTPUT);
  digitalWrite(errpin, LOW);

  lastTokenSent = millis();
  lastStatsReport = millis();
  lastReceived = millis();
  lastDataTime = millis();   // 🔧 IMPORTANTE

  Serial.println("GS LISTO");
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();

  // ----------- TOKEN ----------
  if (!satHasToken && now - lastTokenSent > TOKEN_CYCLE) {
    sendWithChecksum("67:1");
    satHasToken = true;
    lastTokenSent = now;
  }

  // ----------- ESTADÍSTICAS ----------
  if (now - lastStatsReport > STATS_INTERVAL) {
    if (corruptedFromSat > 0) {
      Serial.println("99:" + String(corruptedFromSat));
      corruptedFromSat = 0;
    }
    lastStatsReport = now;
  }

  // ----------- COMANDOS DESDE PC ----------
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length()) {
      if (cmd.indexOf('*') != -1) mySerial.println(cmd);
      else sendWithChecksum(cmd);
    }
  }

  // ----------- POTENCIÓMETRO ----------
  if (now - last > delay_ang) {
    int potval = analogRead(potent);
    int angle = map(potval, 0, 1023, 180, 0);
    sendWithChecksum("5:" + String(angle));
    last = now;
  }

  // ----------- RECEPCIÓN ----------
  if (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    data.trim();

    if (data.length()) {
      String clean;
      if (!validateMessage(data, clean)) {
        corruptedFromSat++;
        digitalWrite(errpin, HIGH);
        delay(100);
        digitalWrite(errpin, LOW);
        return;
      }

      // ✔ DATO VÁLIDO → RESET TIMEOUT
      lastReceived = now;
      lastDataTime = now;
      commTimeout = false;
      digitalWrite(errpin, LOW);

      int sep = clean.indexOf(':');
      if (sep > 0) {
        int id = clean.substring(0, sep).toInt();
        String val = clean.substring(sep + 1);

        if (id == 67 && val == "0") {
          satHasToken = false;
          lastTokenSent = now;
          return;
        }

        if (id == 1) prot1(val);
        else if (id == 2) prot2(val);
        else if (id == 3) prot3(val);
        else if (id == 4) prot4(val);
        else if (id == 5) prot5(val);
        else if (id == 6) prot6(val);
        else if (id == 7) prot7(val);
        else if (id == 8) prot8(val);
        else if (id == 9) prot9(val);
        else if (id == 10) prot10(val);
      }
    }
  }

  // ================= TIMEOUT COMUNICACIÓN =================
  if (now - lastDataTime > COMM_TIMEOUT) {
    commTimeout = true;
  }

  // ================= LED PARPADEO =================
  if (commTimeout && now - lastBlinkTime > BLINK_INTERVAL) {
    errLedState = !errLedState;
    digitalWrite(errpin, errLedState);
    lastBlinkTime = now;
  }
}
