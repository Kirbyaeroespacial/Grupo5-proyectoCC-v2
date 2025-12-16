#include <DHT.h>
#include <SoftwareSerial.h>
#include <Servo.h>
#include <Stepper.h>

// ============================================================
// CONFIGURACIÓN DE SENSORES Y ACTUADORES
// ============================================================

#define DHTPIN 2
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);
SoftwareSerial satSerial(10, 11); // RX=10, TX=11 para comunicación LoRa

// LEDs de estado
const uint8_t LEDPIN = 12;          // LED de transmisión (parpadea al enviar)
const uint8_t ALARM_LED_PIN = 13;   // LED de alarma (parpadea si hay timeout)

// Control de transmisión
bool sending = false;                 // ¿Está enviando telemetría?
unsigned long lastSend = 0;
unsigned long sendPeriod = 20000UL;   // Período de envío por defecto: 20 segundos

// Servo motor (orientación del satélite)
const uint8_t servoPin = 5;
Servo motor;

// Sensor ultrasónico (distancia)
const uint8_t trigPin = 3;
const uint8_t echoPin = 4;
const unsigned long PULSE_TIMEOUT_US = 30000UL;

// Modos del servo
bool autoDistance = true;             // true=barrido automático, false=manual
int servoAngle = 90;                  // Ángulo actual del servo
int servoDir = 1;                     // Dirección del barrido (1=adelante, -1=atrás)
int manualTargetAngle = 90;           // Ángulo objetivo en modo manual

const int SERVO_STEP = 2;             // Grados que avanza por iteración
const unsigned long SERVO_MOVE_INTERVAL = 40; // Intervalo entre movimientos (ms)
unsigned long lastServoMove = 0;

// Estado de LEDs
bool ledState = false;
unsigned long ledTimer = 0;

// ============================================================
// SISTEMA DE TURNOS (Gestión de canal LoRa)
// ============================================================
bool canTransmit = true;              // ¿Tiene permiso para transmitir?
unsigned long lastTokenTime = 0;
const unsigned long TOKEN_TIMEOUT = 6000; // Recuperación automática tras 6s

// ============================================================
// SISTEMA DE ALARMA DE TIMEOUT DE COMUNICACIÓN
// ============================================================
unsigned long lastCommandReceived = 0;
const unsigned long COMM_TIMEOUT = 30000; // 30 segundos sin comandos = alarma
bool commTimeout = false;             // ¿Hay timeout activo?
bool alarmState = false;              // Estado del LED de alarma (parpadeo)
unsigned long lastAlarmToggle = 0;
const unsigned long ALARM_BLINK_INTERVAL = 500; // Parpadeo cada 500ms

int corruptedCommands = 0;            // Contador de comandos corruptos

// ============================================================
// TEMPERATURA MEDIA (últimas 10 mediciones)
// ============================================================
#define TEMP_HISTORY 10
float tempHistory[TEMP_HISTORY];
int tempIndex = 0;
bool tempFilled = false;
float tempMedia = 0.0;
float medias[3] = {0, 0, 0};          // Para detectar 3 medias consecutivas >100°C
int mediaIndex = 0;
bool local_med = true;                // TRUE = calcular aquí, FALSE = calcular en ground station

// ============================================================
// SIMULACIÓN ORBITAL (Órbita elíptica con inclinación)
// ============================================================
const double G = 6.67430e-11;         // Constante gravitacional (m³/kg·s²)
const double M = 5.97219e24;          // Masa de la Tierra (kg)
const double R_EARTH = 6371000;       // Radio de la Tierra (m)
const double ALTITUDE = 4000000;      // Altitud del satélite (4000 km)
const double TIME_COMPRESSION = 260.0; // Compresión temporal 260x
const double EARTH_ROTATION_RATE = 7.2921159e-5; // Rotación terrestre (rad/s)
double real_orbital_period;           // Período orbital calculado
double r;                             // Distancia al centro de la Tierra
unsigned long orbitStartTime = 0;

// ============================================================
// PANEL SOLAR AUTOMATIZADO (Motor Stepper)
// ============================================================
const uint8_t PHOTORESISTOR_PIN = A1; // Sensor de luz
const int STEPS_PER_REV = 2048;       // Pasos por revolución del stepper
Stepper stepperMotor(STEPS_PER_REV, 6, 8, 7, 9);

int currentPanelState = 0;            // Estado actual del panel (0-100%)
int targetPanelState = 0;             // Estado objetivo del panel
bool panelStateChanged = false;       // ¿Cambió el estado del panel?

const int TOTAL_REVOLUTIONS = 1000;   // Revoluciones totales para desplegar 100%
const long TOTAL_DEPLOYMENT_STEPS = TOTAL_REVOLUTIONS * STEPS_PER_REV;

// Control de despliegue inicial
bool initialDeploymentDone = false;   // ¿Completó despliegue inicial al 100%?
bool initialDeploymentStarted = false;

unsigned long lastLightCheck = 0;
const unsigned long LIGHT_CHECK_INTERVAL = 3000; // Revisar luz cada 3s

// Umbrales de luz (valores analógicos 0-1023)
const int LIGHT_MIN = 400;            // Luz baja → panel 100%
const int LIGHT_MAX = 900;            // Luz alta → panel 0%
const int LIGHT_RANGE = LIGHT_MAX - LIGHT_MIN;

// Control no-bloqueante del stepper
bool stepperMoving = false;           // ¿Está el stepper en movimiento?
long stepperRemaining = 0;            // Pasos restantes
int stepperDirection = 1;             // Dirección (1=desplegar, -1=retraer)
unsigned long lastStepperMove = 0;
const unsigned long STEPPER_INTERVAL = 2; // Mover cada 2ms
const int STEPS_PER_ITERATION = 8;    // Pasos por iteración

// ============================================================
// TELEMETRÍA BINARIA (Envío eficiente de datos)
// ============================================================
#pragma pack(push, 1) // Alineación byte a byte (sin padding)
struct TelemetryFrame {
  uint8_t header;         // 0xAA - Identificador de inicio de trama
  uint16_t humidity;      // Humedad × 100 (ej: 5023 = 50.23%)
  int16_t temperature;    // Temperatura × 100 (ej: 2156 = 21.56°C)
  uint16_t tempAvg;       // Temperatura media × 100
  uint16_t distance;      // Distancia en mm
  uint8_t servoAngle;     // Ángulo del servo (0-180)
  uint16_t time_s;        // Tiempo orbital en segundos (módulo 65535)
  // Coordenadas orbitales (int32 dividido en 4 bytes)
  uint8_t x_b0, x_b1, x_b2, x_b3; // X en metros
  uint8_t y_b0, y_b1, y_b2, y_b3; // Y en metros
  uint8_t z_b0, z_b1, z_b2, z_b3; // Z en metros
  uint8_t panelState;     // Estado del panel solar (0-100%)
  uint8_t checksum;       // XOR de todos los bytes anteriores
};
#pragma pack(pop)
const size_t TELEMETRY_FRAME_SIZE = sizeof(TelemetryFrame);

// ============================================================
// CHECKSUM ASCII (Para comandos de texto)
// ============================================================
String calcChecksum(const String &msg) {
  uint8_t xorSum = 0;
  for (unsigned int i = 0; i < msg.length(); i++) {
    xorSum ^= msg[i];
  }
  String hex = String(xorSum, HEX);
  hex.toUpperCase();
  if (hex.length() == 1)
    hex = "0" + hex; // Añadir cero si es de un solo dígito
  return hex;
}

// Envío de comandos ASCII con checksum (ej: "8:e*5C")
void sendPacketWithChecksum(uint8_t type, const String &payload) {
  String msg = String(type) + ":" + payload;
  String chk = calcChecksum(msg);
  String fullMsg = msg + "*" + chk;
  satSerial.println(fullMsg);
  Serial.println("-> " + fullMsg);
}

// ============================================================
// ENVÍO DE TELEMETRÍA BINARIA
// ============================================================
// Empaqueta todos los datos en una estructura binaria y la envía
void sendTelemetryBinary(
  uint16_t hum100,
  int16_t temp100,
  uint16_t avg100,
  uint16_t dist,
  uint8_t servo,
  uint16_t time_s,
  int32_t x,
  int32_t y,
  int32_t z,
  uint8_t panel
) {
  TelemetryFrame frame;
  frame.header = 0xAA;              // Marca de inicio
  frame.humidity = hum100;
  frame.temperature = temp100;
  frame.tempAvg = avg100;
  frame.distance = dist;
  frame.servoAngle = servo;
  frame.time_s = time_s;
  
  // Descomponer coordenadas int32 en 4 bytes
  frame.x_b0 = (x) & 0xFF;
  frame.x_b1 = (x >> 8) & 0xFF;
  frame.x_b2 = (x >> 16) & 0xFF;
  frame.x_b3 = (x >> 24) & 0xFF;
  
  frame.y_b0 = (y) & 0xFF;
  frame.y_b1 = (y >> 8) & 0xFF;
  frame.y_b2 = (y >> 16) & 0xFF;
  frame.y_b3 = (y >> 24) & 0xFF;
  
  frame.z_b0 = (z) & 0xFF;
  frame.z_b1 = (z >> 8) & 0xFF;
  frame.z_b2 = (z >> 16) & 0xFF;
  frame.z_b3 = (z >> 24) & 0xFF;
  
  frame.panelState = panel;

  // Calcular checksum XOR de toda la trama
  uint8_t *ptr = (uint8_t*)&frame;
  uint8_t cs = 0;
  for (size_t i = 0; i < TELEMETRY_FRAME_SIZE - 1; i++) {
    cs ^= ptr[i];
  }
  frame.checksum = cs;

  // Debug: mostrar trama por Serial
  Serial.print("TX: H=");
  Serial.print(frame.header, HEX);
  Serial.print(" Hum=");
  Serial.print(frame.humidity);
  Serial.print(" T=");
  Serial.print(frame.temperature);
  Serial.print(" CS=");
  Serial.print(frame.checksum, HEX);
  Serial.print(" Size=");
  Serial.println(TELEMETRY_FRAME_SIZE);

  // Enviar trama binaria por LoRa
  satSerial.write((uint8_t*)&frame, TELEMETRY_FRAME_SIZE);
}

// ============================================================
// SENSOR ULTRASÓNICO (Medición de distancia)
// ============================================================
int pingSensor() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(4);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long dur = pulseIn(echoPin, HIGH, PULSE_TIMEOUT_US);
  if (dur == 0)
    return 0; // Sin eco = sin objeto detectado
  return (int)(dur * 0.343 / 2.0); // Velocidad del sonido en mm
}

// ============================================================
// PROCESAMIENTO DE COMANDOS
// ============================================================
void handleCommand(const String &cmd) {
  Serial.println(cmd);
  
  // Actualizar timestamp de última comunicación (para alarma de timeout)
  lastCommandReceived = millis();
  commTimeout = false;

  // === TOKENS DE TURNO ===
  if (cmd == "67:1") {
    canTransmit = true; // Permiso para transmitir
    lastTokenTime = millis();
    return;
  } else if (cmd == "67:0") {
    canTransmit = false; // Debe esperar su turno
    return;
  }

  // === CONFIGURACIÓN DE PERÍODO ===
  if (cmd.startsWith("1:")) {
    unsigned long newPeriod = (unsigned long)cmd.substring(2).toInt();
    if (newPeriod < 200) newPeriod = 200; // Mínimo 200ms
    sendPeriod = newPeriod;
    Serial.print("DEBUG: nuevo sendPeriod: "); Serial.println(sendPeriod);
  } 
  // === ÁNGULO MANUAL ===
  else if (cmd.startsWith("2:")) {
    manualTargetAngle = constrain(cmd.substring(2).toInt(), 0, 180);
    if (!autoDistance) { // Si está en modo manual, mover inmediatamente
      motor.write(manualTargetAngle);
      servoAngle = manualTargetAngle;
    }
  } 
  // === CONTROL DE TRANSMISIÓN ===
  else if (cmd == "3:i" || cmd == "3:r") {
    sending = true; // Iniciar/reanudar transmisión
  } else if (cmd == "3:p") {
    sending = false; // Pausar transmisión
  } 
  // === MODOS DEL SERVO ===
  else if (cmd == "4:a") {
    autoDistance = true; // Modo automático (barrido)
    Serial.println("Modo AUTO activado");
  } else if (cmd == "4:m") {
    autoDistance = false; // Modo manual
    motor.write(manualTargetAngle);
    servoAngle = manualTargetAngle;
    Serial.println("Modo MANUAL activado");
  } 
  // === ÁNGULO DEL POTENCIÓMETRO (Ground Station) ===
  else if (cmd.startsWith("5:")) {
    int ang = constrain(cmd.substring(2).toInt(), 0, 180);
    manualTargetAngle = ang;
    if (!autoDistance) { // Solo mover si está en modo manual
      motor.write(manualTargetAngle);
      servoAngle = manualTargetAngle;
      Serial.print("Servo manual a: ");
      Serial.println(manualTargetAngle);
    }
  }
  // === CONTROL DE TEMPERATURA MEDIA (TOGGLE) ===
  else if (cmd == "42:1") {
    local_med = !local_med; // Toggle: cambiar entre local y remoto
    if (local_med) {
      Serial.println("Temp media: LOCAL (calculada en satélite)");
    } else {
      Serial.println("Temp media: REMOTO (calculada en ground station)");
    }
  }
}

// ============================================================
// VALIDACIÓN DE COMANDOS CON CHECKSUM
// ============================================================
void validateAndHandle(const String &data) {
  int asterisco = data.indexOf('*');
  if (asterisco == -1) {
    Serial.println("CMD sin checksum, descartado");
    corruptedCommands++;
    return;
  }

  String msg = data.substring(0, asterisco);      // Mensaje sin checksum
  String chkRecv = data.substring(asterisco + 1); // Checksum recibido
  String chkCalc = calcChecksum(msg);             // Checksum calculado

  if (chkRecv == chkCalc) {
    handleCommand(msg); // Checksum válido → ejecutar comando
  } else {
    Serial.println("CMD corrupto, descartado");
    corruptedCommands++;
  }
}

// ============================================================
// TEMPERATURA MEDIA (últimas 10 mediciones)
// ============================================================
void updateTempMedia(float nuevaTemp) {
  // Solo actualizar si el cálculo es local
  if (!local_med) {
    return;
  }
  
  tempHistory[tempIndex] = nuevaTemp;
  tempIndex = (tempIndex + 1) % TEMP_HISTORY;
  if (tempIndex == 0)
    tempFilled = true; // Ya completó un ciclo de 10 mediciones

  // Calcular promedio
  int n = tempFilled ? TEMP_HISTORY : tempIndex;
  float suma = 0;
  for (int i = 0; i < n; i++)
    suma += tempHistory[i];

  tempMedia = suma / n;
  medias[mediaIndex] = tempMedia;
  mediaIndex = (mediaIndex + 1) % 3;

  // Verificar si las últimas 3 medias son >100°C
  bool alerta = true;
  for (int i = 0; i < 3; i++) {
    if (medias[i] <= 100.0)
      alerta = false;
  }

  if (alerta)
    sendPacketWithChecksum(8, "e"); // Enviar alerta de temperatura alta
}

// ============================================================
// ÓRBITA ELÍPTICA CON INCLINACIÓN Y ROTACIÓN TERRESTRE
// ============================================================
double eccentricity = 0.2;            // Excentricidad de la órbita (0=circular, 0.2=elíptica)
double inclination = 51.6;            // Inclinación orbital en grados (como la ISS)
const double ecef = 1;                // 1=marco ECEF (rotación terrestre), 0=marco ECI

void compute_orbit(uint16_t &time_s_out, int32_t &x_out, int32_t &y_out, int32_t &z_out) {
    unsigned long currentMillis = millis();
    double time = (currentMillis / 1000.0) * TIME_COMPRESSION; // Tiempo comprimido
    
    // Calcular anomalía media (posición angular en órbita circular equivalente)
    double M_anomaly = 2.0 * PI * (time / real_orbital_period);
    
    // Resolver ecuación de Kepler para anomalía excéntrica (órbita elíptica)
    double E = M_anomaly;
    for (int i = 0; i < 10; i++) { // Método de Newton-Raphson (10 iteraciones)
        E = E - (E - eccentricity * sin(E) - M_anomaly) / (1.0 - eccentricity * cos(E));
    }
    
    // Calcular anomalía verdadera (ángulo real desde el periapsis)
    double true_anomaly = 2.0 * atan2(
        sqrt(1.0 + eccentricity) * sin(E / 2.0),
        sqrt(1.0 - eccentricity) * cos(E / 2.0)
    );
    
    // Distancia al centro de la Tierra (varía en órbitas elípticas)
    double r_orbit = r * (1.0 - eccentricity * eccentricity) / (1.0 + eccentricity * cos(true_anomaly));
    
    // Posición en el plano orbital
    double x_orbital = r_orbit * cos(true_anomaly);
    double y_orbital = r_orbit * sin(true_anomaly);
    double z_orbital = 0.0;
    
    // Aplicar inclinación orbital (rotación sobre eje X)
    double inclination_rad = inclination * PI / 180.0;
    double y_inclined = y_orbital * cos(inclination_rad) - z_orbital * sin(inclination_rad);
    double z_inclined = y_orbital * sin(inclination_rad) + z_orbital * cos(inclination_rad);
    
    double x = x_orbital;
    double y = y_inclined;
    double z = z_inclined;
    
    // Si ECEF está activo, aplicar rotación terrestre
    if (ecef) {
        double theta = EARTH_ROTATION_RATE * time; // Ángulo de rotación de la Tierra
        double x_ecef = x * cos(theta) - y * sin(theta);
        double y_ecef = x * sin(theta) + y * cos(theta);
        x = x_ecef;
        y = y_ecef;
    }
    
    // Convertir a int32 (metros) y uint16 (segundos módulo 65535)
    x_out = (int32_t)x;
    y_out = (int32_t)y;
    z_out = (int32_t)z;
    time_s_out = (uint16_t)((uint32_t)time & 0xFFFF);
}

// ============================================================
// PANEL SOLAR: CHEQUEO DE LUZ Y AJUSTE AUTOMÁTICO
// ============================================================
void checkLightAndDeploy() {
  // No ajustar hasta que el despliegue inicial esté completo
  if (!initialDeploymentDone) {
    return;
  }
  
  unsigned long now = millis();
  
  // Solo revisar cada LIGHT_CHECK_INTERVAL (3 segundos)
  if (now - lastLightCheck < LIGHT_CHECK_INTERVAL) {
    return;
  }
  
  lastLightCheck = now;
  
  // Leer fotorresistor (0-1023, más luz = mayor valor)
  int lightLevel = analogRead(PHOTORESISTOR_PIN);
  Serial.print("Luz: ");
  Serial.print(lightLevel);
  
  int oldTarget = targetPanelState;
  
  // Lógica de mapeo: menos luz → más panel desplegado
  if (lightLevel <= LIGHT_MIN) {
    targetPanelState = 100; // Luz muy baja → desplegar 100%
    Serial.println(" -> Panel 100% (luz baja)");
  } 
  else if (lightLevel >= LIGHT_MAX) {
    targetPanelState = 0; // Luz muy alta → retraer 0%
    Serial.println(" -> Panel 0% (luz alta)");
  } 
  else {
    // Mapeo lineal entre LIGHT_MIN y LIGHT_MAX
    int mappedValue = map(lightLevel, LIGHT_MIN, LIGHT_MAX, 100, 0);
    targetPanelState = constrain(mappedValue, 0, 100);
    Serial.print(" -> Panel ");
    Serial.print(targetPanelState);
    Serial.println("%");
  }
  
  // Si cambió el objetivo y el stepper está disponible, iniciar movimiento
  if (targetPanelState != currentPanelState && !stepperMoving) {
    movePanelToTarget();
  } else if (stepperMoving) {
    Serial.println("(Stepper ocupado, esperando...)");
  }
}

// ============================================================
// PANEL SOLAR: MOVIMIENTO NO-BLOQUEANTE DEL STEPPER
// ============================================================
void movePanelToTarget() {
  if (stepperMoving) {
    Serial.println("Stepper ya en movimiento, ignorando comando");
    return;
  }
  
  Serial.print("Iniciando movimiento panel: ");
  Serial.print(currentPanelState);
  Serial.print("% -> ");
  Serial.print(targetPanelState);
  Serial.println("%");
  
  // Calcular pasos necesarios
  long currentSteps = ((long)currentPanelState * TOTAL_DEPLOYMENT_STEPS) / 100;
  long targetSteps = ((long)targetPanelState * TOTAL_DEPLOYMENT_STEPS) / 100;
  long stepsToMove = targetSteps - currentSteps;
  
  if (stepsToMove == 0) {
    Serial.println("Sin movimiento necesario");
    return;
  }
  
  stepperMotor.setSpeed(6); // RPM del motor
  
  Serial.print("Pasos totales a mover: ");
  Serial.println(stepsToMove);
  
  // Configurar variables para movimiento no-bloqueante
  stepperRemaining = abs(stepsToMove);
  stepperDirection = (stepsToMove > 0) ? 1 : -1; // 1=desplegar, -1=retraer
  stepperMoving = true;
  lastStepperMove = millis();
}

// ============================================================
// ACTUALIZACIÓN NO-BLOQUEANTE DEL STEPPER
// ============================================================
// Se llama en cada loop() para avanzar el stepper gradualmente
void updateStepper() {
  if (!stepperMoving) {
    return; // No hay movimiento activo
  }
  
  unsigned long now = millis();
  
  // Solo mover cada STEPPER_INTERVAL (2ms)
  if (now - lastStepperMove >= STEPPER_INTERVAL) {
    lastStepperMove = now;
    
    if (stepperRemaining > 0) {
      // Mover STEPS_PER_ITERATION pasos (8 pasos por iteración)
      long stepsThisIteration = min((long)STEPS_PER_ITERATION, stepperRemaining);
      stepperMotor.step(stepperDirection * stepsThisIteration);
      stepperRemaining -= stepsThisIteration;
      
      // Reportar progreso cada revolución completa
      static long lastReported = 0;
      long stepsCompleted = abs(((long)targetPanelState * TOTAL_DEPLOYMENT_STEPS) / 100 - 
                                ((long)currentPanelState * TOTAL_DEPLOYMENT_STEPS) / 100) - stepperRemaining;
      if (stepsCompleted / 2048 > lastReported) {
        lastReported = stepsCompleted / 2048;
        Serial.print("Stepper: ");
        Serial.print(lastReported);
        Serial.print(" rev completadas, restan ");
        Serial.print(stepperRemaining);
        Serial.println(" pasos");
      }
    }
    
    // Si terminó el movimiento
    if (stepperRemaining == 0) {
      stepperMoving = false;
      currentPanelState = targetPanelState; // Actualizar estado actual
      
      // Detectar si acabó el despliegue inicial
      if (!initialDeploymentDone && currentPanelState == 100) {
        initialDeploymentDone = true;
        Serial.println("DESPLIEGUE INICIAL COMPLETO - Ahora ajustando según luz");
      } else {
        Serial.println("Panel movido OK - No bloqueante");
      }
      
      panelStateChanged = true; // Marcar para envío inmediato de telemetría
    }
  }
}

// ============================================================
// GESTIÓN DE ALARMA DE TIMEOUT DE COMUNICACIÓN
// ============================================================
void updateCommTimeout() {
  unsigned long now = millis();
  
  // Verificar si pasaron 30 segundos sin comandos
  if (now - lastCommandReceived > COMM_TIMEOUT) {
    if (!commTimeout) {
      commTimeout = true;
      Serial.println("¡ALARMA! Timeout de comunicación");
    }
    
    // Parpadear LED de alarma cada 500ms
    if (now - lastAlarmToggle > ALARM_BLINK_INTERVAL) {
      alarmState = !alarmState;
      digitalWrite(ALARM_LED_PIN, alarmState ? HIGH : LOW);
      lastAlarmToggle = now;
    }
  } else {
    // Comunicación OK - apagar alarma
    if (commTimeout) {
      commTimeout = false;
      Serial.println("Comunicación restaurada");
    }
    digitalWrite(ALARM_LED_PIN, LOW);
    alarmState = false;
  }
}

// ============================================================
// SETUP: INICIALIZACIÓN DEL SATÉLITE
// ============================================================
void setup() {
  Serial.begin(9600);
  satSerial.begin(9600);

  // Configurar LEDs
  pinMode(LEDPIN, OUTPUT);
  digitalWrite(LEDPIN, LOW);
  
  pinMode(ALARM_LED_PIN, OUTPUT);
  digitalWrite(ALARM_LED_PIN, LOW);

  // Configurar sensor ultrasónico
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Iniciar sensor DHT11
  dht.begin();

  // Iniciar servo en posición central
  motor.attach(servoPin);
  motor.write(servoAngle);

  // Inicializar timestamps
  lastTokenTime = millis();
  lastCommandReceived = millis();

  // Calcular órbita
  r = R_EARTH + ALTITUDE; // Distancia total al centro de la Tierra
  real_orbital_period = 2.0 * PI * sqrt(pow(r, 3) / (G * M)); // Tercera ley de Kepler
  orbitStartTime = millis();
  
  // Configurar stepper y fotorresistor
  stepperMotor.setSpeed(6);
  pinMode(PHOTORESISTOR_PIN, INPUT);

  // Info de debug
  Serial.print("Tamaño frame: ");
  Serial.println(TELEMETRY_FRAME_SIZE);
  Serial.print("Total revoluciones panel: ");
  Serial.println(TOTAL_REVOLUTIONS);
  Serial.print("Total pasos panel: ");
  Serial.println(TOTAL_DEPLOYMENT_STEPS);
  Serial.println("SAT listo (binario + Stepper + Timeout restaurado)");
  Serial.print("Temp media inicial: ");
  Serial.println(local_med ? "LOCAL" : "REMOTO");
  
  // ===== DESPLIEGUE INICIAL AUTOMÁTICO AL 100% =====
  Serial.println("🛰️ INICIANDO DESPLIEGUE COMPLETO DEL PANEL...");
  targetPanelState = 100;  // Forzar despliegue al 100%
  initialDeploymentStarted = true;
  movePanelToTarget();  // Comenzar movimiento no-bloqueante
}

// ============================================================
// LOOP PRINCIPAL: ORQUESTADOR DE TODAS LAS FUNCIONES
// ============================================================
void loop() {
  unsigned long now = millis();

  // ⚠️ IMPORTANTE: Actualizar stepper primero (debe ser no-bloqueante)
  updateStepper();
  
  // Actualizar alarma de timeout de comunicación
  updateCommTimeout();

  // Revisar luz y ajustar panel (solo después de despliegue inicial)
  checkLightAndDeploy();

  // === SERVO EN MODO AUTOMÁTICO (Barrido 0-180°) ===
  if (autoDistance && now - lastServoMove >= SERVO_MOVE_INTERVAL) {
    lastServoMove = now;
    servoAngle += servoDir * SERVO_STEP;
    if (servoAngle >= 180) {
      servoAngle = 180;
      servoDir = -1; // Cambiar dirección
    } else if (servoAngle <= 0) {
      servoAngle = 0;
      servoDir = 1; // Cambiar dirección
    }
    motor.write(servoAngle);
  }
  
  // === RECEPCIÓN DE COMANDOS POR LoRa ===
  if (satSerial.available()) {
    String cmd = satSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length())
      validateAndHandle(cmd); // Validar checksum y ejecutar
  }
  
  // === RECUPERACIÓN POR TIMEOUT DE TOKEN ===
  if (!canTransmit && now - lastTokenTime > TOKEN_TIMEOUT) {
    canTransmit = true;
    Serial.println("Timeout: recuperando transmisión");
  }
  
  // === CICLO PRINCIPAL DE TRANSMISIÓN ===
  if (now - lastSend >= sendPeriod) {
    lastSend = now;
    if (sending && canTransmit) {
      // Leer sensores
      float h = dht.readHumidity();
      float t = dht.readTemperature();
      bool temp_ok = !(isnan(h) || isnan(t));
      
      if (temp_ok && local_med) {
        updateTempMedia(t); // Solo actualizar si el cálculo es local
      }
      
      if (!temp_ok) {
        sendPacketWithChecksum(4, "e:1"); // Enviar error de sensor
      }

      int dist = pingSensor();
      bool dist_ok = (dist != 0);

      // Calcular posición orbital
      uint16_t orb_time;
      int32_t orb_x, orb_y, orb_z;
      compute_orbit(orb_time, orb_x, orb_y, orb_z);

      // Preparar datos para telemetría binaria
      uint16_t hum100 = temp_ok ? (uint16_t)((int)(h * 100.0f)) : 0;
      int16_t temp100 = temp_ok ? (int16_t)((int)(t * 100.0f)) : 0;
      // Si local_med=false, enviar 0 (o 0xFFFF) para indicar que no se calcula aquí
      uint16_t avg100 = local_med ? (uint16_t)((int)(tempMedia * 100.0f)) : 0;
      uint16_t dist_field = dist_ok ? (uint16_t)dist : 0;
      uint8_t servo_field = (motor.attached()) ? (uint8_t)servoAngle : 0xFF;

      // Enviar trama binaria
      sendTelemetryBinary(
        hum100,
        temp100,
        avg100,
        dist_field,
        servo_field,
        orb_time,
        orb_x,
        orb_y,
        orb_z,
        (uint8_t)currentPanelState
      );

      delay(100); // Pequeña pausa para estabilidad

      // Liberar turno
      sendPacketWithChecksum(67, "0");
      canTransmit = false;

      panelStateChanged = false;
    }

    // Encender LED de transmisión
    digitalWrite(LEDPIN, HIGH);
    ledTimer = now;
    ledState = true;
  }
  
  // === ENVÍO INMEDIATO SI CAMBIÓ EL PANEL ===
  // Enviar telemetría extra cuando el panel cambia de estado
  if (panelStateChanged && canTransmit && sending && (now - lastSend > 1000)) {
    lastSend = now;
    // Leer sensores
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(t) && local_med) updateTempMedia(t);
    int dist = pingSensor();

    // Calcular posición orbital
    uint16_t orb_time;
    int32_t orb_x, orb_y, orb_z;
    compute_orbit(orb_time, orb_x, orb_y, orb_z);

    // Preparar datos
    uint16_t hum100 = isnan(h) ? 0 : (uint16_t)((int)(h * 100.0f));
    int16_t temp100 = isnan(t) ? 0 : (int16_t)((int)(t * 100.0f));
    uint16_t avg100 = local_med ? (uint16_t)((int)(tempMedia * 100.0f)) : 0;
    uint16_t dist_field = dist == 0 ? 0 : (uint16_t)dist;
    uint8_t servo_field = (motor.attached()) ? (uint8_t)servoAngle : 0xFF;

    // Enviar trama binaria
    sendTelemetryBinary(
      hum100,
      temp100,
      avg100,
      dist_field,
      servo_field,
      orb_time,
      orb_x,
      orb_y,
      orb_z,
      (uint8_t)currentPanelState
    );

    delay(100);

    // Liberar turno
    sendPacketWithChecksum(67, "0");
    canTransmit = false;
    panelStateChanged = false;
  }
  
  // === APAGAR LED DE TRANSMISIÓN ===
  if (ledState && now - ledTimer > 80) {
    digitalWrite(LEDPIN, LOW);
    ledState = false;
  }
}
