#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX (azul, blanco)

int errpin = 2;
unsigned long lastReceived = 0;
const unsigned long timeout = 5000; // 5 segundos

// -------------------------
// FUNCIONES DEL PROTOCOLO
// -------------------------
void prot1(String valor) {  // Temperatura : Humedad
  Serial.println(valor);    // imprime "1111:2222"
}

void prot2(String valor) {  // Distancia
  Serial.println(valor);    // imprime "123"
}

void prot3(String valor) {  // Error de datos
  Serial.println(valor);    // imprime "tipo1"
}

void prot4() {              // Error sensor
  Serial.println("error_sensor");
}

void prot5() {              // Error sensor distancia
  Serial.println("error_sensor_distancia");
}

// -------------------------
// SETUP
// -------------------------
void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  Serial.println("COMM LISTO");
  pinMode(errpin, OUTPUT);
}

// -------------------------
// LOOP PRINCIPAL
// -------------------------
void loop() {
  // ---- De PC a satélite ----
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      mySerial.println(command);
    }
  }

  // ---- De satélite a estación ----
  if (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      int sep = data.indexOf(':');
      if (sep > 0) {
        int id = data.substring(0, sep).toInt();
        String valor = data.substring(sep + 1);

        // Llamadas simples según ID
        if (id == 1) prot1(valor);
        else if (id == 2) prot2(valor);
        else if (id == 3) prot3(valor);
        else if (id == 4) prot4();
        else if (id == 5) prot5();
           // LED error si mensaje = "e"
        if (valor.equals("e")) {
          digitalWrite(errpin, HIGH);
          delay(500);
          digitalWrite(errpin, LOW);
        }
      }

      lastReceived = millis(); // reinicia contador
    }
  }

  // ---- Timeout ----
  if (millis() - lastReceived > timeout) {
    Serial.println("timeout");
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
    delay(50);
  }
}