#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX

int errpin = 2;
char potent = A0;
unsigned long lastReceived = 0;
unsigned long last = 0;
unsigned long periodo_ugood = 4000;
const unsigned long answer_timeout = 1000;
unsigned long last_imgood = 0;
unsigned long last_ugood = 0;
const unsigned long delay_ang = 200;
const unsigned long ratito = 500;
unsigned long last_sent = 0;
unsigned long last_led = 0;
bool can_send = false;

//Funcion validación checksum
bool validateChecksum(const String &data) {
    int len = data.length();
    if (len < 2) return false;

    uint8_t cksRecibido = (uint8_t)data[len - 1];

    // Mensaje sin checksum
    String msg = data.substring(0, len - 1);

    uint16_t sum = 0;
    for (int i = 0; i < msg.length(); i++) {
        sum += (uint8_t)msg[i];
    }

    uint8_t cksCalc = sum & 0xFF;

    return cksCalc == cksRecibido;
}

//Inicio definición protocolo
void prot1(String valor) {  
  Serial.println("1:" + valor);
}
void prot2(String valor) {  
  Serial.println("2:" + valor);
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
void prot9(String valor){
  Serial.println("9:" + valor);
}
//Fin definición protocolo

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  Serial.println("COMM LISTO");
  pinMode(errpin, OUTPUT);
  digitalWrite(errpin, LOW);
}


void loop() {
  unsigned long now = millis();

  // Comunicación de GS a SAT
  if (Serial.available() || now - last > delay_ang) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.startsWith("67:")) {
      mySerial.println(command);
      can_send = true;
      last_sent = now;
    } else {
      if (command.indexOf("i") != -1){
        can_send = true;
      }
      if (command.length() > 0 && can_send) {
        mySerial.println(command);
        if (now - last > delay_ang){
          int potval = analogRead(potent);
          int angle = map(potval, 0, 1023, 180, 0);
          mySerial.println("2:" + String(angle));
          last = now;
        }
        last_sent = now;
      }
    }
  }

  if (can_send && now - last_sent > ratito){
    can_send = false;
  }

  // Recepción de SAT a GS
  if (mySerial.available()) {
    String data = mySerial.readStringUntil('\n');
    // NO usar data.trim(), genera problemas
    // data.trim();

    if (data.length() > 0 && data.charAt(data.length() - 1) == '\r') {
      data = data.substring(0, data.length() - 1);
    }

    if (data.length() > 1) {
      if (!validateChecksum(data)){
        digitalWrite(errpin, HIGH);
        last_led = now;
        Serial.println("10:e");
        return;
      }
      
      int sepr = data.indexOf(':');
      if (sepr > 0) {
        int id = data.substring(0, sepr).toInt();
        String valor = data.substring(sepr + 1, data.length() - 1); // quitar checksum

        if (id == 1) prot1(valor);
        else if (id == 2) prot2(valor);
        else if (id == 3) prot3(valor);
        else if (id == 4) prot4(valor);
        else if (id == 5) prot5(valor);
        else if (id == 6) prot6(valor);
        else if (id == 7) prot7(valor);
        else if (id == 8) prot8(valor);
        else if (id == 9) prot9(valor);
        else if (id == 67) can_send = true;
        else if (id == 99) last_imgood = now;

        if (valor.startsWith("e")) {
          digitalWrite(errpin, HIGH);
          last_led = now;
        }
      }
      lastReceived = now;
    }
  }

  // Control LED error sin delay
  if (digitalRead(errpin) == HIGH && now - last_led > ratito){
    digitalWrite(errpin, LOW);
  }

  if (now - last_ugood > periodo_ugood) {
    mySerial.println("99:UG?");
    last_ugood = now;
  }

  if (now - last_imgood > answer_timeout){
    digitalWrite(errpin, HIGH);
    last_led = now;
  }
}
