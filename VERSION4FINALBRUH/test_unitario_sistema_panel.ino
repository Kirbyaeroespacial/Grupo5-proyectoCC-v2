/*
 * TEST UNITARIO - PANEL SOLAR DESPLEGABLE
 * 
 * Este código prueba SOLO el fotoresistor y el stepper motor
 * sin necesidad de LoRa, Ground Station ni Python.
 * 
 * CONEXIONES:
 * - Fotoresistor (LDR): A1 (con resistencia 10kΩ a GND)
 * - Stepper IN1: Pin 6
 * - Stepper IN2: Pin 7
 * - Stepper IN3: Pin 8
 * - Stepper IN4: Pin 9
 * - LED indicador: Pin 13 (LED interno del Arduino)
 * 
 * QUÉ HACE:
 * 1. Lee el valor del fotoresistor cada 2 segundos
 * 2. Muestra el valor en el Monitor Serial
 * 3. Determina el estado del panel según la luz
 * 4. Mueve el stepper a la posición correspondiente
 * 5. Enciende el LED cuando está en movimiento
 * 
 * PRUEBAS A REALIZAR:
 * - Ambiente oscuro: Panel debe ir a 100%
 * - Linterna cerca: Panel debe ir a 40%
 * - Tapar con la mano: Panel debe cambiar gradualmente
 */

#include <Stepper.h>

// === CONFIGURACIÓN DEL STEPPER ===
const int STEPS_PER_REV = 2048;  // 28BYJ-48 tiene 2048 pasos/revolución
Stepper stepperMotor(STEPS_PER_REV, 6, 7, 8, 9);

// === CONFIGURACIÓN DEL FOTORESISTOR ===
const uint8_t PHOTORESISTOR_PIN = A1;

// === CONFIGURACIÓN DEL LED INDICADOR ===
const uint8_t LED_PIN = 13;  // LED interno del Arduino

// === PARÁMETROS DEL PANEL ===
const int TOTAL_DEPLOYMENT_STEPS = 1024;  // Pasos para despliegue completo (ajustar)

// Estados posibles: 0%, 40%, 60%, 100%
int currentPanelState = 0;
int targetPanelState = 0;

// === UMBRALES DE LUZ ===
// IMPORTANTE: Estos valores pueden variar según tu fotoresistor y ambiente
// Abre el Monitor Serial y ajusta estos valores según lo que veas
const int LIGHT_NONE = 200;      // Menos de esto = sin luz
const int LIGHT_LOW = 500;       // Entre 200-500 = poca luz
const int LIGHT_MEDIUM = 700;    // Entre 500-700 = luz media
                                  // Más de 700 = mucha luz

// === VARIABLES DE TIMING ===
unsigned long lastCheck = 0;
const unsigned long CHECK_INTERVAL = 2000;  // Revisar cada 2 segundos

void setup() {
  Serial.begin(9600);
  
  // Configurar stepper
  stepperMotor.setSpeed(10);  // RPM (5-15 es razonable)
  
  // Configurar LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Configurar fotoresistor
  pinMode(PHOTORESISTOR_PIN, INPUT);
  
  Serial.println("========================================");
  Serial.println("   TEST UNITARIO - PANEL SOLAR");
  Serial.println("========================================");
  Serial.println();
  Serial.println("Leyendo fotoresistor en pin A1...");
  Serial.println("Stepper conectado en pines 6,7,8,9");
  Serial.println();
  Serial.println("CALIBRACIÓN:");
  Serial.println("- Observa los valores del sensor");
  Serial.println("- Ajusta umbrales si es necesario");
  Serial.println();
  Serial.println("UMBRALES ACTUALES:");
  Serial.print("  Sin luz: < ");
  Serial.println(LIGHT_NONE);
  Serial.print("  Poca luz: ");
  Serial.print(LIGHT_NONE);
  Serial.print(" - ");
  Serial.println(LIGHT_LOW);
  Serial.print("  Luz media: ");
  Serial.print(LIGHT_LOW);
  Serial.print(" - ");
  Serial.println(LIGHT_MEDIUM);
  Serial.print("  Mucha luz: > ");
  Serial.println(LIGHT_MEDIUM);
  Serial.println();
  Serial.println("========================================");
  Serial.println();
  
  delay(2000);
}

void loop() {
  unsigned long now = millis();
  
  // Revisar luz cada 2 segundos
  if (now - lastCheck >= CHECK_INTERVAL) {
    lastCheck = now;
    checkLightAndDeploy();
  }
}

void checkLightAndDeploy() {
  // Leer nivel de luz
  int lightLevel = analogRead(PHOTORESISTOR_PIN);
  
  // Mostrar lectura
  Serial.print("┌─ Lectura Sensor: ");
  Serial.print(lightLevel);
  Serial.print(" / 1023  (");
  Serial.print(map(lightLevel, 0, 1023, 0, 100));
  Serial.println("%)");
  
  // Determinar estado objetivo según luz
  int oldTarget = targetPanelState;
  
  if (lightLevel < LIGHT_NONE) {
    targetPanelState = 0;    // Sin luz -> retraer
    Serial.println("│  Condición: SIN LUZ");
  } else if (lightLevel < LIGHT_LOW) {
    targetPanelState = 100;  // Poca luz -> desplegar totalmente
    Serial.println("│  Condición: POCA LUZ");
  } else if (lightLevel < LIGHT_MEDIUM) {
    targetPanelState = 60;   // Luz media -> desplegar 60%
    Serial.println("│  Condición: LUZ MEDIA");
  } else {
    targetPanelState = 40;   // Mucha luz -> desplegar 40%
    Serial.println("│  Condición: MUCHA LUZ");
  }
  
  Serial.print("│  Estado actual: ");
  Serial.print(currentPanelState);
  Serial.println("%");
  
  Serial.print("│  Estado objetivo: ");
  Serial.print(targetPanelState);
  Serial.println("%");
  
  // Si cambió el estado, mover el motor
  if (targetPanelState != currentPanelState) {
    Serial.println("│");
    Serial.println("│  ⚠️  CAMBIO DETECTADO - Moviendo panel...");
    movePanelToTarget();
    Serial.println("│  ✓ Movimiento completado");
  } else {
    Serial.println("│  ℹ️  Sin cambios - Panel en posición");
  }
  
  Serial.println("└────────────────────────────────────");
  Serial.println();
}

void movePanelToTarget() {
  // Encender LED indicador
  digitalWrite(LED_PIN, HIGH);
  
  // Calcular pasos necesarios
  int currentSteps = (TOTAL_DEPLOYMENT_STEPS * currentPanelState) / 100;
  int targetSteps = (TOTAL_DEPLOYMENT_STEPS * targetPanelState) / 100;
  int stepsToMove = targetSteps - currentSteps;
  
  Serial.print("│    Pasos actuales: ");
  Serial.println(currentSteps);
  Serial.print("│    Pasos objetivo: ");
  Serial.println(targetSteps);
  Serial.print("│    Pasos a mover: ");
  Serial.print(stepsToMove);
  if (stepsToMove > 0) {
    Serial.println(" (DESPLEGANDO)");
  } else if (stepsToMove < 0) {
    Serial.println(" (RETRAYENDO)");
  } else {
    Serial.println(" (SIN MOVIMIENTO)");
  }
  
  // Mover el motor
  if (stepsToMove != 0) {
    Serial.println("│    🔄 Motor en movimiento...");
    
    // Mover en pasos pequeños para feedback visual
    int stepIncrement = (stepsToMove > 0) ? 64 : -64;
    int stepsRemaining = abs(stepsToMove);
    
    while (stepsRemaining > 0) {
      int stepNow = min(stepsRemaining, abs(stepIncrement));
      if (stepsToMove < 0) stepNow = -stepNow;
      
      stepperMotor.step(stepNow);
      stepsRemaining -= abs(stepNow);
      
      // Parpadear LED
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      
      Serial.print("│      ");
      int progress = 100 - (stepsRemaining * 100 / abs(stepsToMove));
      Serial.print(progress);
      Serial.println("%");
    }
    
    delay(100);  // Pequeña pausa para estabilizar
  }
  
  // Actualizar estado actual
  currentPanelState = targetPanelState;
  
  // Apagar LED
  digitalWrite(LED_PIN, LOW);
  
  // Mostrar estado final
  String estadoTexto;
  switch(currentPanelState) {
    case 0:   estadoTexto = "RETRAÍDO"; break;
    case 40:  estadoTexto = "DESPLEGADO 40%"; break;
    case 60:  estadoTexto = "DESPLEGADO 60%"; break;
    case 100: estadoTexto = "TOTALMENTE DESPLEGADO"; break;
    default:  estadoTexto = String(currentPanelState) + "%"; break;
  }
  
  Serial.print("│    ✅ Panel ahora: ");
  Serial.println(estadoTexto);
}

/*
 * GUÍA DE PRUEBAS:
 * 
 * 1. PRUEBA DE CALIBRACIÓN:
 *    - Abre el Monitor Serial (9600 baud)
 *    - Observa los valores del sensor en diferentes condiciones:
 *      * Luz ambiente normal
 *      * Con linterna apuntando
 *      * Tapando con la mano
 *      * En oscuridad total
 *    - Anota los valores y ajusta las constantes LIGHT_NONE, LIGHT_LOW, LIGHT_MEDIUM
 * 
 * 2. PRUEBA DE MOVIMIENTO:
 *    - Cambia gradualmente la luz sobre el sensor
 *    - Verifica que el motor se mueve
 *    - Observa el LED parpadeando durante el movimiento
 *    - Confirma que el motor se detiene en cada posición
 * 
 * 3. PRUEBA DE TRANSICIONES:
 *    - Oscuridad → Luz: Debe retroceder (100% → 40%)
 *    - Luz → Oscuridad: Debe avanzar (40% → 100%)
 *    - Verifica que no se mueve si la luz está estable
 * 
 * 4. AJUSTE DE PARÁMETROS:
 *    Si el motor se mueve muy rápido/lento:
 *    - Cambia stepperMotor.setSpeed(10) → probar 5, 10, 15
 *    
 *    Si el despliegue es muy corto/largo:
 *    - Cambia TOTAL_DEPLOYMENT_STEPS → probar 512, 1024, 2048
 * 
 * 5. VERIFICACIÓN DE HARDWARE:
 *    Si el motor no se mueve:
 *    - Verifica las conexiones (pines 6,7,8,9)
 *    - Asegura alimentación suficiente (fuente externa si es necesario)
 *    - Prueba invertir dos cables si gira al revés
 * 
 * VALORES TÍPICOS DE FOTORESISTOR:
 * - Oscuridad total: 0-100
 * - Sombra/mano tapando: 100-300
 * - Luz ambiente interior: 300-600
 * - Luz ventana/exterior: 600-900
 * - Linterna directa: 900-1023
 * 
 * AJUSTA LOS UMBRALES SEGÚN TUS LECTURAS REALES
 */
