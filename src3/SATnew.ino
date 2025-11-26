#include <DHT.h>
#include <SoftwareSerial.h>
#include <Servo.h>

#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

SoftwareSerial satSerial(10, 11); // RX=10, TX=11 hacia Ground Station
Servo motor;
const uint8_t servoPin = 5;
int servoAngle = 90;

const uint8_t trigPin = 3;
const uint8_t echoPin = 4;
const unsigned long PULSE_TIMEOUT_US = 30000UL;

bool autoDistance = true;
bool sending = false;
unsigned long sendPeriod = 2000;
unsigned long lastSend = 0;

// Sistema de turnos
bool canTransmit = false;
unsigned long lastTokenTime = 0;
const unsigned long TOKEN_TIMEOUT = 8000;

// Checksum
String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i=0;i<msg.length();i++) xorSum ^= msg[i];
  String hex = String(xorSum, HEX);
  hex.toUpperCase();
  if (hex.length()==1) hex="0"+hex;
  return hex;
}

void sendPacket(uint8_t type, const String &payload){
  String msg = String(type) + ":" + payload;
  String chk = calcChecksum(msg);
  satSerial.println(msg+"*"+chk);
  Serial.println("-> "+msg+"*"+chk); // debug
}

int pingSensor() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(4);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  unsigned long dur = pulseIn(echoPin, HIGH, PULSE_TIMEOUT_US);
  if (dur==0) return 0;
  return (int)(dur*0.343/2.0);
}

void handleCommand(const String &cmd){
  if(cmd=="67:1"){ canTransmit=true; lastTokenTime=millis(); return;}
  else if(cmd=="67:0"){ canTransmit=false; return;}
  else if(cmd=="3:i") sending=true;
  else if(cmd=="3:p") sending=false;
  else if(cmd=="4:a") autoDistance=true;
  else if(cmd=="4:m") autoDistance=false;
  else if(cmd.startsWith("2:")) servoAngle=constrain(cmd.substring(2).toInt(),0,180);
}

void validateAndHandle(const String &data){
  int asterisco=data.indexOf('*');
  if(asterisco==-1) return;
  String msg=data.substring(0,asterisco);
  String chkRecv=data.substring(asterisco+1);
  if(chkRecv==calcChecksum(msg)) handleCommand(msg);
}

void setup(){
  Serial.begin(9600);
  satSerial.begin(9600);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  motor.attach(servoPin);
  motor.write(servoAngle);
  dht.begin();
}

void loop(){
  if(satSerial.available()){
    String cmd=satSerial.readStringUntil('\n');
    cmd.trim();
    if(cmd.length()) validateAndHandle(cmd);
  }

  unsigned long now=millis();
  if(!canTransmit && now-lastTokenTime>TOKEN_TIMEOUT) canTransmit=true;

  if(sending && canTransmit && now-lastSend>=sendPeriod){
    // Lectura sensores
    float h=dht.readHumidity();
    float t=dht.readTemperature();
    if(isnan(h)||isnan(t)) sendPacket(4,"e:1");
    else sendPacket(1,String((int)(h*100))+":"+String((int)(t*100)));

    int dist=pingSensor();
    if(dist==0) sendPacket(5,"e:1");
    else sendPacket(2,String(dist));

    sendPacket(6,String(servoAngle));

    // Liberar turno
    sendPacket(67,"0");
    canTransmit=false;
    lastSend=now;
  }
}
