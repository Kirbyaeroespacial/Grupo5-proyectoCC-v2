#include <DHT.h>
#include <SoftwareSerial.h>

#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

SoftwareSerial satSerial(10, 11); // RX, TX
#define LEDPIN 12

bool sending = false;   // empieza sin enviar
unsigned long lastSend = 0;

// Umbrales para detección de datos fuera de rango (ajustables)
const float HUM_MIN = 0.0;
const float HUM_MAX = 100.0;
const float TEMP_MIN = -40.0; // límite razonable para detectar valores absurdos
const float TEMP_MAX = 80.0;  // límite razonable para detectar valores absurdos

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
        // Error de lectura (sensor no responde o lectura NaN)
        Serial.println("ERROR lectura DHT! -> enviando 3:1");
        satSerial.println("3:1");  // subcódigo 1 = fallo DHT
      } else {
        // Detectar valores fuera de rango (posible sensor desconectado / lecturas absurdas)
        if (h < HUM_MIN || h > HUM_MAX || t < TEMP_MIN || t > TEMP_MAX) {
          Serial.println("Lectura fuera de rango -> enviando 3:2");
          satSerial.println("3:2"); // subcódigo 2 = datos fuera de rango
        } else {
          int hi = (int)round(h * 100.0); // humedad *100 (igual que antes)
          int ti = (int)round(t * 100.0); // temperatura *100 (igual que antes)
          // Prefijo "1:" para indicar mensaje tipo 1 (temp+hum)
          satSerial.print("1:");
          satSerial.print(hi); satSerial.print(':'); satSerial.println(ti); // datos
          Serial.print("Enviado -> "); Serial.print("1:"); Serial.print(hi); Serial.print(':'); Serial.println(ti);
          digitalWrite(LEDPIN, HIGH); delay(80); digitalWrite(LEDPIN, LOW);
        }
      }
    } else {
      // Cuando sending == false, enviar heartbeat
      satSerial.println("g");
      Serial.println("Heartbeat: g");
    }
    lastSend = millis();
  }
}