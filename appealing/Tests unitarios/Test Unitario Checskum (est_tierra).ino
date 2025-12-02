#include <SoftwareSerial.h>
SoftwareSerial tierra(2, 3); // RX, TX

String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i = 0; i < msg.length(); i++)
    xorSum ^= msg[i];
  String hex = String(xorSum, HEX);
  hex.toUpperCase();
  if (hex.length() == 1) hex = "0" + hex;
  return hex;
}

void sendPacketWithChecksum(uint8_t type, const String &payload) {
  String msg = String(type) + ":" + payload;
  String chk = calcChecksum(msg);
  tierra.println(msg + "*" + chk);
  Serial.println("-> " + msg + "*" + chk);
}

void validateAndHandle(const String &data) {
  int pos = data.indexOf('*');
  if (pos < 0) {
    Serial.println("Sin checksum");
    return;
  }

  String msg = data.substring(0, pos);
  String chkRecv = data.substring(pos + 1);
  String chkCalc = calcChecksum(msg);

  if (chkRecv == chkCalc) {
    Serial.println("OK SAT: " + msg);
    sendPacketWithChecksum(67, "1");  // ACK mínimo
  } else {
    Serial.println("BAD SAT: " + data);
  }
}

void setup() {
  Serial.begin(9600);
  tierra.begin(9600);
  Serial.println("Tierra mínima lista");
}

void loop() {
  if (tierra.available()) {
    String s = tierra.readStringUntil('\n');
    s.trim();
    if (s.length()) validateAndHandle(s);
  }
}
