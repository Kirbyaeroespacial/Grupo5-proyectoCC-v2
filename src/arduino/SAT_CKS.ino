#include <SoftwareSerial.h>
SoftwareSerial mySerial(10, 11); // RX, TX (GS <-> SAT)

int errpin = 2;
const int potent = A0;
unsigned long lastReceived = 0;
unsigned long last = 0;
const unsigned long timeout = 5000;
const unsigned long delay_ang = 200;

// FLAG de debugging: si true, se corrompe deliberadamente el payload AFTER calcular checksum (prueba)
const bool DEBUG_INYECTAR_CORRUPCION = false;

// --- checksum: suma de bytes % 256 ---
uint8_t checksum_bytes(const uint8_t *buf, size_t len) {
  unsigned int suma = 0;
  for (size_t i = 0; i < len; ++i) {
    suma += buf[i];
  }
  return (uint8_t)(suma & 0xFF);
}

// Overload para String
uint8_t checksum_of_string(const String &s) {
  // convertir a bytes (UTF-8). En Arduino String internamente usa bytes compatibles ASCII
  size_t len = s.length();
  unsigned int suma = 0;
  for (size_t i = 0; i < len; ++i) {
    // cast a unsigned char para evitar signos
    suma += (uint8_t)s[i];
  }
  return (uint8_t)(suma & 0xFF);
}

// Enviar mensaje con formato texto: <mensaje>|<CHK>\n al enlace mySerial
// Opcionalmente inyecta corrupción en payload DESPUÉS de calcular checksum (solo para pruebas).
void sendWithChecksumToSat(const String &msg) {
  uint8_t chk = checksum_of_string(msg);

  if (DEBUG_INYECTAR_CORRUPCION && msg.length() > 0) {
    // Crear copia, corromper un byte, y enviar copia con el checksum original (simula corrupción en el enlace)
    String corrupted = msg;
    // alterar primer carácter bit 0
    char c = corrupted.charAt(0);
    c ^= 0x01;
    corrupted.setCharAt(0, c);
    // enviar la versión corrompida pero con el CHK calculado sobre la original (simula error en vuelo)
    mySerial.print(corrupted);
    mySerial.print("|");
    mySerial.println(chk);
  } else {
    // modo normal: enviar msg | chk
    mySerial.print(msg);
    mySerial.print("|");
    mySerial.println(chk); // println añade '\r\n' o '\n' según Serial config; Python usa readline()
  }
}

// --- protocolo: funciones que envían al monitor serie (GS) como antes ---
// Estas funciones se usan al recibir mensajes válidos desde el SAT (mySerial).
void prot1(String valor) {  Serial.println("1:" + valor); }
void prot2(String valor) {  Serial.println("2:" + valor); }
void prot3(String valor) {  Serial.println("3:" + valor); }
void prot4(String valor) {  Serial.println("4:" + valor); }
void prot5(String valor) {  Serial.println("5:" + valor); }
void prot6(String valor) {  Serial.println("6:" + valor); }
void prot7(String valor) {  Serial.println("7:" + valor); }
void prot8(String valor) {  Serial.println("8:e"); }

// Parsear línea entrante desde mySerial: "id:valor|CHK" o "id:valor" (si no hay chk)
bool verify_and_route_from_sat(const String &line_in) {
  // line_in ya viene sin \n
  String line = line_in;
  line.trim();
  if (line.length() == 0) return false;

  // separar checksum al final si existe '|'
  int sep_chk = line.lastIndexOf('|');
  String payload;
  String chk_str;
  bool has_chk = false;
  if (sep_chk >= 0) {
    payload = line.substring(0, sep_chk);
    chk_str = line.substring(sep_chk + 1);
    chk_str.trim();
    has_chk = chk_str.length() > 0;
  } else {
    payload = line;
  }

  // Si no hay checksum: considerarlo inválido (según enunciado queremos descartar mensajes corruptos)
  if (!has_chk) {
    Serial.println("RECV NO_CHECKSUM -> IGNORADO");
    return false;
  }

  // calcular checksum local sobre payload
  uint8_t chk_calc = checksum_of_string(payload);
  // parse checksum recibido
  int chk_rec = chk_str.toInt(); // robusto: si no es numérico devolverá 0
  if ((uint8_t)chk_rec != chk_calc) {
    // checksum no coincide → mensaje corrupto: indicar en salida y encender pin de error
    Serial.print("RECV CORRUPTO: ");
    Serial.println(line);
    // señal de error físico
    digitalWrite(errpin, HIGH);
    delay(200);
    digitalWrite(errpin, LOW);
    return false;
  }

  // Si checksum OK, procesar payload: "id:valor"
  int sepr = payload.indexOf(':');
  if (sepr <= 0) {
    Serial.print("RECV FORMATO_INVALIDO: ");
    Serial.println(payload);
    return false;
  }
  int id = payload.substring(0, sepr).toInt();
  String valor = payload.substring(sepr + 1);

  // Routear a la función correspondiente
  if (id == 1) prot1(valor);
  else if (id == 2) prot2(valor);
  else if (id == 3) prot3(valor);
  else if (id == 4) prot4(valor);
  else if (id == 5) prot5(valor);
  else if (id == 6) prot6(valor);
  else if (id == 7) prot7(valor);
  else if (id == 8) prot8(valor);
  else {
    Serial.print("RECV ID_DESCONOCIDO: ");
    Serial.println(id);
  }

  // opcional: blink si valor empieza por 'e' (comportamiento original)
  if (valor.startsWith("e")) {
    digitalWrite(errpin, HIGH);
    delay(500);
    digitalWrite(errpin, LOW);
  }

  lastReceived = millis();
  return true;
}

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  Serial.println("COMM LISTO (con checksum)");
  pinMode(errpin, OUTPUT);
}

void loop() {
  // 1) Forward desde Monitor Serial (GS local) hacia SAT (mySerial) - ahora con checksum
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      // Si desde PC envías ya un string con |CHK, podrías forwardearlo tal cual.
      // Aquí asumimos que lo que envía el usuario por Serial es el payload (ej: "3:123")
      sendWithChecksumToSat(command);
    }
  }

  // 2) Envío periódico (ejemplo sensor pot -> id 5) con checksum
  if (millis() - last > delay_ang) {
    int potval = analogRead(potent);
    int angle = map(potval, 0, 1023, 180, 0);
    String msg = "5:" + String(angle);
    sendWithChecksumToSat(msg);
    last = millis();
  }

  // 3) Recepción desde SAT -> verificar checksum y routear a protocolo
  if (mySerial.available()) {
    String data_in = mySerial.readStringUntil('\n');
    data_in.trim();
    if (data_in.length() > 0) {
      verify_and_route_from_sat(data_in);
    }
  }

  // 4) Timeout de enlace (comportamiento original)
  if (millis() - lastReceived > timeout) {
    Serial.println("timeout");
    digitalWrite(errpin, HIGH);
    delay(100);
    digitalWrite(errpin, LOW);
    delay(50);
  }
}

