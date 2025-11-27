#include <DHT.h>
#include <Servo.h>
#include <SoftwareSerial.h> //CUIDADO CON ESTA LIBRERIA, MEJORA MUCHO EL FUNCIONAMIENTO DE LA COMUNICACIÓN, SIN EMBARGO, HABRÁ QUE RECABLEAR LOS COMMS A LOS PINES 8 y 9!!

#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

//Cosiñitas de la localización:
const double G = 6.67430e-11;  // Gravitational constant (m^3 kg^-1 s^-2)
const double M = 5.97219e24;   // Mass of Earth (kg)
const double R_EARTH = 6371000;  // Radius of Earth (meters)
const double ALTITUDE = 400000;  // Altitude of satellite above Earth's surface (meters)
const double EARTH_ROTATION_RATE = 7.2921159e-5;  // Earth's rotational rate (radians/second)
const double  TIME_COMPRESSION = 90.0; // Time compression factor (90x)

// Variables
double real_orbital_period;  // Real orbital period of the satellite (seconds)
double r;  // Total distance from Earth's center to satellite (meters)
//Fin cosiñitas de la localización



//Inicio cosas medias
#define TEMP_HISTORY 10
float tempHistory[TEMP_HISTORY];
int tempIndex = 0;
bool tempFilled = false;
float tempMedia = 0.0;
float medias[3] = {0, 0, 0}; // para comprobar 3 medias consecutivas
int mediaIndex = 0;



//Inicio serial/comms
SoftwareSerial satSerial(10,11);
bool sending = false;
bool can_send = false;
unsigned long lastSend = 0;
unsigned long sendPeriod = 2000UL;


//Inicio led
const int pinled = 12;
bool ledstate = false;
unsigned long ledTimer = 0;

//Inicio Servo
const int pinservo = 5;
Servo motor;
bool autoservo = true;
int servoAngle = 90;
int direccion = 1;
int manualTargetAngle = 90; //TS really necessary????
const int SERVO_STEP = 2;
const unsigned long ivalo_mov_servo = 40;
unsigned long ultimo_movimiento_servo = 0;

//Inicio sonar
const int trigPin = 3;
const int echoPin = 4;
const unsigned long puls_timeout_us = 30000UL;

//Definición checksum
uint8_t calculateChecksum (const String &msg){
  uint16_t sum = 0;
  for (size_t i = 0; i < msg.length(); i++){
    sum += msg[i];
  }
  return sum & 0xFF;
}

//INICIO DEFINICIÓN PROTOCOLO COMM
void sendPacket (int type, const String &payload){
  String msg = String(type) + ":" + payload;

  uint8_t cks = calculateChecksum(msg);
  
  satSerial.print(msg);
  satSerial.write(cks);
  satSerial.println();
  Serial.print("Enviado:" + payload);
  Serial.println(String("  con suma de bits:") + cks);
}
//Definición de los comandos
void command1 (const String &cmd){
  sendPeriod = max(200UL, cmd.substring(2).toInt());
}
void command2 (const String &cmd){
  manualTargetAngle = constrain(cmd.substring(2).toInt(), 0, 180);
  if (!autoservo){
    servoAngle = manualTargetAngle;
  }
}

void command3 (const String &cmd){
  if (cmd == "3:i" || cmd == "3:r") sending = true;
  else if (cmd == "3:p") sending = false;
}

void command4 (const String &cmd){
  if (cmd == "4:a") autoservo = true;
  else if (cmd == "4:m") autoservo = false;
}

void get_turn(const String &cmd){
  if (cmd == "67:1") can_send = true;
}
void sendHeartbeat(){
  sendPacket(99, "OK");
}

//Definición de otras acciones:
int pingSensor() {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(4);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    unsigned long dur = pulseIn(echoPin, HIGH, puls_timeout_us);
    if (dur == 0) return 0;
    return (int)(dur * 0.343 / 2.0);
}

void updateTempMedia(float nuevaTemp) {
  tempHistory[tempIndex] = nuevaTemp;
  tempIndex = (tempIndex + 1) % TEMP_HISTORY;
  if (tempIndex == 0) tempFilled = true;

  int n = tempFilled ? TEMP_HISTORY : tempIndex;
  float suma = 0;
  for (int i = 0; i < n; i++) suma += tempHistory[i];
  tempMedia = suma / n;

  medias[mediaIndex] = tempMedia;
  mediaIndex = (mediaIndex + 1) % 3;

  // Si las tres últimas medias >100
  bool alerta = true;
  for (int i = 0; i < 3; i++) {
    if (medias[i] <= 100.0) alerta = false;
  }
  if (alerta) sendPacket(8, "e");  // ID 8 -> alerta alta temperatura
}

void handleCMD (const String &cmd){
  Serial.println("Se ha recibido" + cmd);
  if (cmd.startsWith("1:")) command1(cmd);
  else if (cmd.startsWith("2:")) command2(cmd);// Q es esto, lo mismo que el 5!!! ???????? Rectificado
  else if (cmd.startsWith("3:")) command3(cmd);
  else if (cmd.startsWith("4:")) command4(cmd);
  else if (cmd.startsWith("67:")) get_turn(cmd);
  else if (cmd.startsWith("99:")) sendHeartbeat();
  
}

void setup(){
  Serial.begin(9600);
  satSerial.begin(9600);
  pinMode(pinled, OUTPUT);
  digitalWrite(pinled, LOW);
  pinMode(trigPin, OUTPUT);
  pinMode (echoPin, INPUT);
  dht.begin();
  motor.attach(pinservo);
  motor.write(servoAngle);
  Serial.println("SAT listo");

  //Cosiñas de localización
  r = R_EARTH + ALTITUDE;
  real_orbital_period = 2 * PI * sqrt(pow(r, 3) / (G * M));
}

void loop (){
  unsigned long now = millis();
  //Movimiento del Servo
  motor.write(servoAngle);
  if (autoservo && now - ultimo_movimiento_servo >= ivalo_mov_servo){
    ultimo_movimiento_servo = now;
    servoAngle += direccion * SERVO_STEP;
    if (servoAngle >= 180){
      servoAngle = 180;
      direccion = -1;
    }
    else if (servoAngle <= 0){
      servoAngle = 0;
      direccion = 1;
    }
  }

  //Lectura e itnerpretación comandos tierra
  if (satSerial.available()){
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length()){
      handleCMD(cmd);
    }
  }

  //Envio de datos
  if (now - lastSend >= sendPeriod && can_send && sending){
    can_send = false;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan (t)){
      sendPacket(4, "e:1");
    }
    else{
      sendPacket(1, String((int)(h*100)) + ":" + String((int)(t*100)));
      updateTempMedia(t);
    }
    int dist = pingSensor();
    if (dist == 0){
      sendPacket(5, "e:1");
    }
    else{
      sendPacket(2, String(dist));
    }
    if (!motor.attached()){
      sendPacket(6, "e:1");
    }
    else{
      sendPacket(6, String(servoAngle));
    }
    
    simulate_orbit(now, 0, 0);

    digitalWrite(pinled, HIGH);
    ledTimer = now;
    ledstate = true;
    lastSend = now;
  }

  if (ledstate && now - ledTimer > 80){
    digitalWrite(pinled, LOW);
    ledstate = false;
  }
}

//El void de simular oritas aquí abajo porque nos cae mal.
void simulate_orbit(unsigned long millis, double inclination, int ecef) {
    double time = (millis / 1000.0) * TIME_COMPRESSION;  // Real orbital time
    double angle = 2 * PI * (time / real_orbital_period);  // Angle in radians
    double x = r * cos(angle);  // X-coordinate (meters)
    double y = r * sin(angle) * cos(inclination);  // Y-coordinate (meters)
    double z = r * sin(angle) * sin(inclination);  // Z-coordinate (meters)

    if (ecef) {
        double theta = EARTH_ROTATION_RATE * time;
        double x_ecef = x * cos(theta) - y * sin(theta);
        double y_ecef = x * sin(theta) + y * cos(theta);
        x = x_ecef;
        y = y_ecef;
    }

    /* Do NOT Send the data to the serial port, lol
    Serial.print("Time: ");
    Serial.print(time);
    Serial.print(" s | Position: (X: ");
    Serial.print(x);
    Serial.print(" m, Y: ");
    Serial.print(y);
    Serial.print(" m, Z: ");
    Serial.print(z);
    Serial,println("m)");
    //Do send it to through the serial connection (softSerial)*/
    sendPacket(9, String(int(x)) + ":" + String(int(y)));
}






