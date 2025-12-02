// Arduino test sketch para calcChecksum
void setup() {
  Serial.begin(9600);
  while (!Serial) ; // wait
  Serial.println("Arduino checksum unit test");

  String tests[] = {
    "1:100",
    "2:345",
    "67:0",
    "9:123:456:789:0",
    "4:e:1",
    "5:90",
    "1:0:0",
    "",
    "SOME:COMPLEX:MSG:42"
  };

  for (int i = 0; i < (int)(sizeof(tests)/sizeof(tests[0])); ++i) {
    String m = tests[i];
    String chk = calcChecksum(m);
    Serial.print("MSG: '"); Serial.print(m); Serial.print("' -> CHK: "); Serial.println(chk);
  }

  // Test de detección de corrupción
  String good = "1:100";
  String goodfull = good + "*" + calcChecksum(good);
  Serial.print("Good full: "); Serial.println(goodfull);

  String corruptedPayload = "1:101*" + calcChecksum(good);
  Serial.print("Corrupted payload with old chk: "); Serial.println(corruptedPayload);
  String clean;
  bool ok = validateMessage(corruptedPayload, clean);
  Serial.print("Validates? "); Serial.println(ok ? "YES" : "NO (expected NO)");
}

void loop() {
  // nothing
}

// --- copia aquí las funciones calcChecksum y validateMessage de tu proyecto ---
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

bool validateMessage(const String &data, String &cleanMsg) {
  int asterisco = data.indexOf('*');
  if (asterisco == -1) return false;
  cleanMsg = data.substring(0, asterisco);
  String chkRecv = data.substring(asterisco + 1);
  String chkCalc = calcChecksum(cleanMsg);
  return (chkRecv == chkCalc);
}
