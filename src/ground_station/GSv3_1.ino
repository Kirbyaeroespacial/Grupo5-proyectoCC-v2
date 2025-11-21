#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

int errpin = 2;
char potent = A0;
unsigned long lastReceived = 0;
unsigned long last = 0;
const unsigned long timeout = 5000;
const unsigned long delay_ang = 200;

// Gestión de turnos
bool satHasToken = false;
unsigned long lastTokenSent = 0;
const unsigned long TOKEN_CYCLE = 2500;

// Checksum: contadores de errores
int corruptedFromSat = 0;  // Mensajes corruptos del satélite
unsigned long lastStatsReport = 0;
const unsigned long STATS_INTERVAL = 10000;  // Reporta cada 10s

// === CHECKSUM: Calcula XOR de todos los caracteres ===
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

// === ENVÍO con checksum al satélite ===
void sendWithChecksum(const String &msg) {
  String chk = calcChecksum(msg);
  String fullMsg = msg + "*" + chk;
  mySerial.println(fullMsg);
}

// === VALIDACIÓN de mensajes del satélite ===
bool validateMessage(const String &data, String &cleanMsg) {
  int asterisco = data.indexOf('*');
  if (asterisco == -1) return false;  // Sin checksum
  
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

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  Serial.println("COMM LISTO con checksum");
  pinMode(errpin, OUTPUT);
  lastTokenSent = millis();
  lastStatsReport = millis();
}

void loop() {
  unsigned long now = millis();

  // === GESTIÓN DE TURNOS ===
  if (!satHasToken && now - lastTokenSent > TOKEN_CYCLE) {
    sendWithChecksum("67:1");  // Dale token al satélite CON checksum
    satHasToken = true;
    lastTokenSent = now;
  }

  // === ESTADÍSTICAS cada 10s ===
  if (now - lastStatsReport > STATS_INTERVAL) {
    if (corruptedFromSat > 0) {
      Serial.println("99:" + String(corruptedFromSat));  // Envía stats a Python
      corruptedFromSat = 0;  // Reset contador
    }
    lastStatsReport = now;
  }

  // Comunicación de GS a SAT (comandos de usuario CON checksum)
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      sendWithChecksum(command);  // Añade checksum antes de enviar
    }
  }

  // Envío periódico de ángulo del potenciómetro CON checksum
  if (now - last > delay_ang) {
    int potval = analogRead(potent);
    int angle = map(potval, 0, 1023, 180, 0);
    sendWithChecksum("5:" + String(angle));
    last = now;
  }
  
  // === RECEPCIÓN DE SAT A GS con validación ===
  if (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      String cleanMsg;
      
      // Validar checksum
      if (!validateMessage(data, cleanMsg)) {
        // Checksum inválido: descartar y contar error
        corruptedFromSat++;
        digitalWrite(errpin, HIGH);
        delay(100);
        digitalWrite(errpin, LOW);
        return;
      }
      
      // Checksum OK: procesar mensaje
      int sepr = cleanMsg.indexOf(':');
      if (sepr > 0) {
        int id = cleanMsg.substring(0, sepr).toInt();
        String valor = cleanMsg.substring(sepr + 1);

        // Token recibido: el satélite terminó de transmitir
        if (id == 67 && valor == "0") {
          satHasToken = false;
          lastTokenSent = now;
          return;
        }

        // Protocolo normal
        if (id == 1) prot1(valor);
        else if (id == 2) prot2(valor);
        else if (id == 3) prot3(valor);
        else if (id == 4) prot4(valor);
        else if (id == 5) prot5(valor);
        else if (id == 6) prot6(valor);
        else if (id == 7) prot7(valor);
        else if (id == 8) prot8(valor);

        // Indicador de error visual
        if (valor.startsWith("e")) {
          digitalWrite(errpin, HIGH);
          delay(500);
          digitalWrite(errpin, LOW);
        }
      }
      lastReceived = now;
    }
  }

  // Timeout de comunicación
  if (now - lastReceived > timeout) {
    Serial.println("timeout");
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
    delay(50);
  }
}