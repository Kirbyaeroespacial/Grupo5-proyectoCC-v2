#include <DHT.h>
#include <SoftwareSerial.h>

#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

SoftwareSerial satSerial(10, 11); // RX, TX
#define LEDPIN 12

bool sending = false;   // empieza sin enviar
unsigned long lastSend = 0;

void setup() {
  Serial.begin(9600);
  satSerial.begin(9600);
  dht.begin();
  pinMode(LEDPIN, OUTPUT);
  delay(1000);
  Serial.println("Satelite listo, esperando comandos...");
  satSerial.println("LISTO"); // mensaje inicial a Tierra/PC
}

void loop() {
  // Leer comandos
  if (satSerial.available()) {
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if (cmd.length() == 0) return;

    Serial.print("Cmd recibido: "); Serial.println(cmd);

    if (cmd == "p") {
      sending = false;
      Serial.println("-> PAUSADO");
      satSerial.println("ACK:P");
    } else if (cmd == "r") {
      sending = true;
      Serial.println("-> REANUDADO");
      satSerial.println("ACK:R");
    } else if (cmd == "i") {
      sending = true;
      Serial.println("-> INICIADO");
      satSerial.println("ACK:I");
    } else {
      satSerial.print("ACK:?"); satSerial.println(cmd);
    }
  }

  // Enviar datos o heartbeat cada 2 seg
  if (millis() - lastSend >= 2000) {
    if (sending) {
      float h = dht.readHumidity();
      float t = dht.readTemperature();

      if (isnan(h) || isnan(t)) {
        Serial.println("ERROR lectura DHT!");
        satSerial.println("e");  // error sensor
      } else {
        int hi = (int)round(h*100);
        int ti = (int)round(t*100);
        satSerial.print(hi); satSerial.print(':'); satSerial.println(ti); // datos
        Serial.print("Enviado -> "); Serial.print(hi); Serial.print(':'); Serial.println(ti);
        digitalWrite(LEDPIN, HIGH); delay(80); digitalWrite(LEDPIN, LOW);
      }
    } else {
      // Cuando sending == false, enviar heartbeat
      satSerial.println("g");
      Serial.println("Heartbeat: g");
    }
    lastSend = millis();
  }
}
