#include <SoftwareSerial.h>

SoftwareSerial satSerial(10, 11); // Conexión con el satélite
#define PCSerial Serial // USB al PC

void setup() {
  PCSerial.begin(9600);
  satSerial.begin(9600);
  PCSerial.println("Estación de tierra lista");
}

void loop() {
  // Reenviar datos satélite → PC
  while(satSerial.available()){
    char c = satSerial.read();
    PCSerial.write(c);
  }

  // Reenviar datos PC → satélite
  while(PCSerial.available()){
    char c = PCSerial.read();
    satSerial.write(c);
  }
}