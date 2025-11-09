#include <Servo.h>

//Definición pines
int serv = 8;
int echo = 9;
int trig = 10;
char potent = A0;

Servo motor;

unsigned long lastMillis = 0;
const unsigned long delay1 = 1000;

void setup(){
  //Sensor
  pinMode (trig, OUTPUT);
  pinMode (echo, INPUT);
  Serial.begin(9600);
  
  //Servo
  motor.attach(serv);
  
}
           
void loop(){
  int potval = analogRead(potent);
  int angle = map(potval, 0, 1023, 180, 0);
  motor.write(angle);
  
  unsigned long now = millis();
  if (now - lastMillis >= delay1){ 
    int dist = ping(trig, echo);
    Serial.print ("Distancia medida: ");
    Serial.print(dist);
    Serial.println(" mm");
    lastMillis = now;
  }
  
}

int ping (int trig, int echo){
  long interv, distmm;
  
  digitalWrite (trig, LOW);
  delayMicroseconds(4);
  digitalWrite (trig, HIGH);
  digitalWrite (trig, LOW);
  
  interv = pulseIn(echo, HIGH);
  distmm = interv * 100/ 292/ 2;
  return distmm;
}
//Se ha realizado un cambio!!!!