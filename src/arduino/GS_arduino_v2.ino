#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

int errpin = 2;
unsigned long lastReceived = 0;
const unsigned long timeout = 5000;
String data;

//Inicio definición protocolo
void prot1(String valor) {  
  Serial.println(valor);    
}

void prot2(String valor) {  
  Serial.println(valor);    
}

void prot3(String valor) {  
  Serial.println(valor);    
}

void prot4(String valor) {              
  Serial.println("error_sensor_temperatura/humedad");
  Serial.println(valor);
}

void prot5(String valor) {              
  Serial.println("error_sensor_distancia");
  Serial.println(valor);
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
      mySerial.println(command);
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

        // Llamadas simples según ID
        if (id == 1) prot1(valor);
        else if (id == 2) prot2(valor);
        else if (id == 3) prot3(valor);
        else if (id == 4) prot4(valor);
        else if (id == 5) prot5(valor);
        
        // LED error si payload comienza con "e" (ej: "e:1")
        if (valor.startsWith("e")) {
          digitalWrite(errpin, HIGH);
          delay(500);
          digitalWrite(errpin, LOW);
        }
      }

      lastReceived = millis();
    }
  }

  //Error de comunicación (timeout)
  if (millis() - lastReceived > timeout) {
    Serial.println("timeout");
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
    delay(50);
  }
}