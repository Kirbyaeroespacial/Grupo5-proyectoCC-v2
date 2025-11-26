#include <DHT.h>
#include <SoftwareSerial.h>
#include <Servo.h>

#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

SoftwareSerial satSerial(10, 11); // RX=10, TX=11
#define LEDPIN 12
Servo motor;
#define SERVO_PIN 5

// Ultrasonido
#define TRIG_PIN 3
#define ECHO_PIN 4
#define PULSE_TIMEOUT_US 30000UL

bool sending = false;
unsigned long lastSend = 0;
unsigned long sendPeriod = 2000UL;

// Servo automático/manual
bool autoDistance = true;
int servoAngle = 90;
int servoDir = 1;
int manualTargetAngle = 90;
const int SERVO_STEP = 2;
const unsigned long SERVO_MOVE_INTERVAL = 40;
unsigned long lastServoMove = 0;

// Turnos
bool canTransmit = false;
unsigned long lastTokenTime = 0;
const unsigned long TOKEN_TIMEOUT = 8000;

// Temp media
#define TEMP_HISTORY 10
float tempHistory[TEMP_HISTORY];
int tempIndex = 0;
bool tempFilled = false;
float tempMedia = 0.0;

// Órbita
const double G = 6.67430e-11;
const double M = 5.97219e24;
const double R_EARTH = 6371000;
const double ALTITUDE = 400000;
const double TIME_COMPRESSION = 90.0;
double real_orbital_period;
double r;
unsigned long orbitStartTime = 0;

// === Funciones ===
String calcChecksum(const String &msg){
  uint8_t xorSum = 0;
  for(unsigned int i=0;i<msg.length();i++) xorSum ^= msg[i];
  String hex = String(xorSum, HEX);
  hex.toUpperCase();
  if(hex.length()==1) hex="0"+hex;
  return hex;
}

void sendPacket(uint8_t type, const String &payload){
  String msg = String(type) + ":" + payload;
  String fullMsg = msg + "*" + calcChecksum(msg);
  satSerial.println(fullMsg);
  Serial.println("-> "+fullMsg);
}

int pingSensor(){
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(4);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long dur = pulseIn(ECHO_PIN,HIGH,PULSE_TIMEOUT_US);
  if(dur==0) return 0;
  return (int)(dur*0.343/2.0);
}

void handleCommand(const String &cmd){
  if(cmd=="67:1"){ canTransmit=true; lastTokenTime=millis(); return; }
  else if(cmd=="67:0"){ canTransmit=false; return; }
  else if(cmd.startsWith("1:")) sendPeriod = max(200UL, cmd.substring(2).toInt());
  else if(cmd.startsWith("2:")){
    manualTargetAngle = constrain(cmd.substring(2).toInt(),0,180);
    if(!autoDistance){ motor.write(manualTargetAngle); servoAngle=manualTargetAngle; }
  }
  else if(cmd=="3:i"||cmd=="3:r") sending=true;
  else if(cmd=="3:p") sending=false;
  else if(cmd=="4:a") autoDistance=true;
  else if(cmd=="4:m"){ autoDistance=false; motor.write(manualTargetAngle); servoAngle=manualTargetAngle; }
  else if(cmd.startsWith("5:")){
    int ang = constrain(cmd.substring(2).toInt(),0,180);
    manualTargetAngle=ang;
    if(!autoDistance) servoAngle=manualTargetAngle;
  }
}

void validateAndHandle(const String &data){
  int aster = data.indexOf('*');
  if(aster==-1) return;
  String msg=data.substring(0,aster);
  String chk=data.substring(aster+1);
  if(chk==calcChecksum(msg)) handleCommand(msg);
}

void updateTempMedia(float temp){
  tempHistory[tempIndex]=temp;
  tempIndex=(tempIndex+1)%TEMP_HISTORY;
  if(tempIndex==0) tempFilled=true;
  int n=tempFilled?TEMP_HISTORY:tempIndex;
  float suma=0;
  for(int i=0;i<n;i++) suma+=tempHistory[i];
  tempMedia=suma/n;
  if(tempMedia>100) sendPacket(8,"e"); // alerta
}

void simulateOrbit(){
  unsigned long now=millis();
  double t = ((now-orbitStartTime)/1000.0)*TIME_COMPRESSION;
  double angle = 2*PI*(t/real_orbital_period);
  long x = (long)(r*cos(angle));
  long y = (long)(r*sin(angle));
  long z = 0;
  sendPacket(9,String((long)t)+":"+String(x)+":"+String(y)+":"+String(z));
}

void setup(){
  Serial.begin(9600);
  satSerial.begin(9600);
  pinMode(LEDPIN,OUTPUT);
  digitalWrite(LEDPIN,LOW);
  pinMode(TRIG_PIN,OUTPUT); pinMode(ECHO_PIN,INPUT);
  dht.begin();
  motor.attach(SERVO_PIN); motor.write(servoAngle);
  lastTokenTime=millis();
  r=R_EARTH+ALTITUDE;
  real_orbital_period=2*PI*sqrt(pow(r,3)/(G*M));
  orbitStartTime=millis();
  Serial.println("Satélite listo");
}

void loop(){
  unsigned long now=millis();
  // Servo automático
  if(autoDistance && now-lastServoMove>=SERVO_MOVE_INTERVAL){
    lastServoMove=now;
    servoAngle+=servoDir*SERVO_STEP;
    if(servoAngle>=180){ servoAngle=180; servoDir=-1; }
    else if(servoAngle<=0){ servoAngle=0; servoDir=1; }
    motor.write(servoAngle);
  }

  // Leer comandos
  if(satSerial.available()){
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    if(cmd.length()) validateAndHandle(cmd);
  }

  // Timeout
  if(!canTransmit && now-lastTokenTime>TOKEN_TIMEOUT){ canTransmit=true; }

  // Envío datos
  if(now-lastSend>=sendPeriod && sending && canTransmit){
    float h=dht.readHumidity(), t=dht.readTemperature();
    if(isnan(h)||isnan(t)) sendPacket(4,"e:1");
    else{
      sendPacket(1,String((int)(h*100))+":"+String((int)(t*100)));
      updateTempMedia(t);
      sendPacket(7,String((int)(tempMedia*100)));
    }
    int dist=pingSensor();
    if(dist==0) sendPacket(5,"e:1");
    else sendPacket(2,String(dist));
    if(!motor.attached()) sendPacket(6,"e:1");
    else sendPacket(6,String(servoAngle));
    simulateOrbit();
    sendPacket(67,"0");
    canTransmit=false;
    lastSend=now;
  }
}
