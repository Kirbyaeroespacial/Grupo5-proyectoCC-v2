#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

int errpin = 2;
unsigned long lastReceived = 0;
const unsigned long timeout = 5000;
String data;

void prot1(String valor) { Serial.println("1:" + valor); }
void prot2(String valor) { Serial.println("2:" + valor); }
void prot3(String valor) { Serial.println("3:" + valor); }
void prot4(String valor) { Serial.println("4:" + valor); }
void prot5(String valor) { Serial.println("5:" + valor); }
void prot6(String valor) { Serial.println("6:" + valor); }
void prot7(String valor) { Serial.println("7:" + valor); }

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  pinMode(errpin, OUTPUT);
  Serial.println("GROUNDSTATION LISTO");
}

void loop() {
  // Enviar comandos a satélite
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) mySerial.println(cmd);
  }

  // Recibir de satélite
  if (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    data.trim();
    if (data.length() > 0) {
      int sep = data.indexOf(':');
      if (sep > 0) {
        int id = data.substring(0, sep).toInt();
        String valor = data.substring(sep + 1);
        if (id == 1) prot1(valor);
        else if (id == 2) prot2(valor);
        else if (id == 3) prot3(valor);
        else if (id == 4) prot4(valor);
        else if (id == 5) prot5(valor);
        else if (id == 6) prot6(valor);
        else if (id == 7) prot7(valor);

        if (valor.startsWith("e")) {
          digitalWrite(errpin, HIGH);
          delay(300);
          digitalWrite(errpin, LOW);
        }
      }
      lastReceived = millis();
    }
  }

  // Timeout
  if (millis() - lastReceived > timeout) {
    Serial.println("timeout");
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
    delay(50);
  }
}
