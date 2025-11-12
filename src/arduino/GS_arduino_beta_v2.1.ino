#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

int errpin = 2;
unsigned long lastReceived = 0;
const unsigned long timeout = 5000;
String data;

//Inicio definición protocolo
void prot1(String valor) {  
  Serial.println("1:" + valor);  // <-- mantiene formato correcto para Python
}

void prot2(String valor) {  
  Serial.println("2:" + valor);  // <-- igual para distancia
}

void prot3(String valor) {  
  Serial.println("3:" + valor);
}

void prot4(String valor) {              
  Serial.println("4:" + valor);
}

void prot5(String valor) {              
  Serial.println("5:" + valor);
}

void prot6(String valor) {              
  Serial.println("6:" + valor);
}
void prot7(String valor) {              
  Serial.println("7:" + valor);
}

void prot8(String valor) {              
  Serial.println("8:e");
}
//Fin definición protocolo

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  Serial.println("COMM LISTO");
  pinMode(errpin, OUTPUT);
}

void loop() {
  // Comunicación de GS a SAT
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      mySerial.println(command); // Envío directo sin prefijo rx:
    }
  }

  // Recepción de SAT a GS
  if (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      int sepr = data.indexOf(':');
      if (sepr > 0) {
        int id = data.substring(0, sepr).toInt();
        String valor = data.substring(sepr + 1);

        if (id == 1) prot1(valor);
        else if (id == 2) prot2(valor);
        else if (id == 3) prot3(valor);
        else if (id == 4) prot4(valor);
        else if (id == 5) prot5(valor);
        else if (id == 6) prot6(valor);
        else if (id == 7) prot7(valor);
        else if (id == 8) prot8(valor);

        if (valor.startsWith("e")) {
          digitalWrite(errpin, HIGH);
          delay(500);
          digitalWrite(errpin, LOW);
        }
      }
      lastReceived = millis();
    }
  }

  if (millis() - lastReceived > timeout) {
    Serial.println("timeout");
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
    delay(50);
  }
}