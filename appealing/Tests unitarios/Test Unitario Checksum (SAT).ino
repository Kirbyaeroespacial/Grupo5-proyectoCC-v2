#include <SoftwareSerial.h>

SoftwareSerial satSerial(10, 11); // RX=10, TX=11

// ==== MISMA FUNCIÓN CHECKSUM QUE TU CÓDIGO ====
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

void sendPacketWithChecksum(uint8_t type, const String &payload) {
  String msg = String(type) + ":" + payload;
  String chk = calcChecksum(msg);
  String fullMsg = msg + "*" + chk;
  satSerial.println(fullMsg);
  Serial.println("-> " + fullMsg);
}

void validateAndHandle(const String &data) {
  int pos = data.indexOf('*');
  if (pos < 0) return;

  String msg = data.substring(0, pos);
  String chkRecv = data.substring(pos + 1);
  String chkCalc = calcChecksum(msg);

  if (chkRecv == chkCalc)
    Serial.println("CMD OK: " + msg);
  else
    Serial.println("CMD BAD: " + data);
}

void setup() {
  Serial.begin(9600);
  satSerial.begin(9600);
  Serial.println("SAT mínimo listo");
}

void loop() {
  // Recibir comandos
  if (satSerial.available()) {
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length()) validateAndHandle(cmd);
  }

  // Enviar un paquete cada 1s
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 1000) {
    sendPacketWithChecksum(1, "123");
    lastSend = millis();
  }
}
