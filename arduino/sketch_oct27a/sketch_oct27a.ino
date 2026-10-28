#include <WiFiS3.h>
#include "arduino_secrets.h"

// =====================================================
// WIFI Y SERVIDOR HTTP LOCAL
// =====================================================
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

WiFiServer server(80);
bool servidorIniciado = false;

// =====================================================
// SENSOR HC-SR04: CONEXIONES DEL PROYECTO ORIGINAL
// =====================================================
const int VCC_PIN = A0;
const int TRIG_PIN = A1;
const int ECHO_PIN = A2;
const int GND_PIN = A3;

// =====================================================
// SHIELD L293D: MOTOR M1
// =====================================================
const int MOTOR_PWM_PIN = 11;
const int MOTOR_DIR_CLK = 4;
const int MOTOR_DIR_EN = 7;
const int MOTOR_DIR_SER = 8;
const int MOTOR_DIR_LATCH = 12;

const uint8_t MOTOR1_A = 2;
const uint8_t MOTOR1_B = 3;

const int MOTOR_SPEED = 160;

// =====================================================
// ESTADO DEL SISTEMA
// =====================================================
bool modoAutomatico = false;
String estadoMotor = "DETENIDO";
float distancia = -1.0;

uint8_t motorDirectionBits = 0;

unsigned long ultimaMedicion = 0;
const unsigned long INTERVALO_MEDICION = 100;

// =====================================================
// CONTROL DEL REGISTRO DEL SHIELD
// =====================================================
void writeMotorRegister() {
  digitalWrite(MOTOR_DIR_LATCH, LOW);

  shiftOut(
    MOTOR_DIR_SER,
    MOTOR_DIR_CLK,
    MSBFIRST,
    motorDirectionBits
  );

  digitalWrite(MOTOR_DIR_LATCH, HIGH);
}

void setupMotorShield() {
  pinMode(MOTOR_PWM_PIN, OUTPUT);
  pinMode(MOTOR_DIR_CLK, OUTPUT);
  pinMode(MOTOR_DIR_EN, OUTPUT);
  pinMode(MOTOR_DIR_SER, OUTPUT);
  pinMode(MOTOR_DIR_LATCH, OUTPUT);

  digitalWrite(MOTOR_DIR_EN, LOW);
  digitalWrite(MOTOR_DIR_LATCH, LOW);
  digitalWrite(MOTOR_DIR_CLK, LOW);
  digitalWrite(MOTOR_DIR_SER, LOW);

  analogWrite(MOTOR_PWM_PIN, 0);

  motorDirectionBits = 0;
  writeMotorRegister();
}

// =====================================================
// CONTROL DEL MOTOR M1
// =====================================================
void aplicarEstadoMotor(const String& nuevoEstado) {
  if (nuevoEstado != "DERECHA" &&
      nuevoEstado != "IZQUIERDA" &&
      nuevoEstado != "DETENIDO") {
    return;
  }

  if (nuevoEstado == estadoMotor) {
    return;
  }

  // Detener el motor antes de cambiar su dirección.
  analogWrite(MOTOR_PWM_PIN, 0);

  // Limpiar los bits de dirección correspondientes a M1.
  motorDirectionBits &= ~(
    (1 << MOTOR1_A) |
    (1 << MOTOR1_B)
  );

  if (nuevoEstado == "DERECHA") {
    motorDirectionBits |= (1 << MOTOR1_A);
  } else if (nuevoEstado == "IZQUIERDA") {
    motorDirectionBits |= (1 << MOTOR1_B);
  }

  writeMotorRegister();

  if (nuevoEstado != "DETENIDO") {
    analogWrite(MOTOR_PWM_PIN, MOTOR_SPEED);
  } else {
    analogWrite(MOTOR_PWM_PIN, 0);
  }

  estadoMotor = nuevoEstado;

  Serial.print("Motor M1: ");
  Serial.println(estadoMotor);
}

void girarDerecha() {
  aplicarEstadoMotor("DERECHA");
}

void girarIzquierda() {
  aplicarEstadoMotor("IZQUIERDA");
}

void detenerMotor() {
  aplicarEstadoMotor("DETENIDO");
}

// =====================================================
// SENSOR ULTRASONICO HC-SR04
// =====================================================
float medirDistancia() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long duracion =
      pulseIn(ECHO_PIN, HIGH, 30000);

  if (duracion == 0) {
    return -1.0;
  }

  return duracion * 0.0343 / 2.0;
}

// =====================================================
// MODO AUTOMATICO DEL DESAFIO
// 150-200 cm: DERECHA
// 100-140 cm: IZQUIERDA
// Otros valores: DETENIDO
// =====================================================
void controlarAutomaticamente() {
  if (distancia >= 150.0 && distancia <= 200.0) {
    girarDerecha();
  } else if (distancia >= 100.0 && distancia <= 140.0) {
    girarIzquierda();
  } else {
    detenerMotor();
  }
}

// =====================================================
// CREAR RESPUESTA JSON PARA LA APP
// =====================================================
String crearEstadoJSON() {
  String modo =
      modoAutomatico ? "AUTOMATICO" : "MANUAL";

  String valorDistancia;

  if (distancia < 0) {
    valorDistancia = "-1";
  } else {
    valorDistancia = String(distancia, 1);
  }

  String json = "{\"success\":true,\"distanceCm\":";
  json += valorDistancia;
  json += ",\"motorState\":\"";
  json += estadoMotor;
  json += "\",\"mode\":\"";
  json += modo;
  json += "\"}";

  return json;
}

// =====================================================
// ENVIAR RESPUESTA HTTP
// =====================================================
void enviarRespuesta(
  WiFiClient &cliente,
  int codigoHTTP,
  const String &contenido
) {
  cliente.print("HTTP/1.1 ");
  cliente.print(codigoHTTP);

  if (codigoHTTP == 200) {
    cliente.println(" OK");
  } else if (codigoHTTP == 404) {
    cliente.println(" Not Found");
  } else {
    cliente.println(" Bad Request");
  }

  cliente.println(
      "Content-Type: application/json; charset=utf-8"
  );
  cliente.println("Cache-Control: no-store");
  cliente.println("Connection: close");
  cliente.println("Access-Control-Allow-Origin: *");
  cliente.println();

  cliente.println(contenido);
}

// =====================================================
// SERVIDOR HTTP PARA LA APP ANDROID
// =====================================================
void atenderCliente() {
  WiFiClient cliente = server.available();

  if (!cliente) {
    return;
  }

  cliente.setTimeout(1000);

  String solicitud = cliente.readStringUntil('\n');
  solicitud.trim();

  Serial.print("Solicitud HTTP: ");
  Serial.println(solicitud);

  // Leer encabezados HTTP.
  while (cliente.connected()) {
    String encabezado =
        cliente.readStringUntil('\n');

    if (encabezado == "\r" ||
        encabezado.length() == 0) {
      break;
    }
  }

  String respuesta;
  int codigoHTTP = 200;

  if (solicitud.startsWith("GET /status ")) {
    respuesta = crearEstadoJSON();

  } else if (
      solicitud.startsWith("GET /cmd?value=R ")) {
    modoAutomatico = false;
    girarDerecha();
    respuesta = crearEstadoJSON();

  } else if (
      solicitud.startsWith("GET /cmd?value=L ")) {
    modoAutomatico = false;
    girarIzquierda();
    respuesta = crearEstadoJSON();

  } else if (
      solicitud.startsWith("GET /cmd?value=S ")) {
    modoAutomatico = false;
    detenerMotor();
    respuesta = crearEstadoJSON();

  } else if (
      solicitud.startsWith("GET /mode?value=A ")) {
    modoAutomatico = true;
    controlarAutomaticamente();
    respuesta = crearEstadoJSON();

  } else if (
      solicitud.startsWith("GET /mode?value=M ")) {
    modoAutomatico = false;
    detenerMotor();
    respuesta = crearEstadoJSON();

  } else {
    codigoHTTP = 404;
    respuesta =
        "{\"success\":false,\"message\":\"Ruta no encontrada\"}";
  }

  enviarRespuesta(cliente, codigoHTTP, respuesta);

  delay(2);
  cliente.stop();
}

// =====================================================
// CONEXION WIFI
// =====================================================
bool conectarWiFi() {
  Serial.println("Inicializando Wi-Fi...");

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(
        "ERROR: modulo Wi-Fi no disponible."
    );
    return false;
  }

  unsigned long inicio = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - inicio < 30000) {

    Serial.print("Conectando a: ");
    Serial.println(ssid);

    WiFi.begin(ssid, pass);

    unsigned long intento = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - intento < 5000) {
      delay(250);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(
        "ERROR: no se pudo conectar al Wi-Fi."
    );
    Serial.println(
        "Revisa el hotspot y las credenciales."
    );
    return false;
  }

  Serial.println("Wi-Fi conectado correctamente.");

  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  Serial.print("IP del Arduino: ");
  Serial.println(WiFi.localIP());

  server.begin();
  servidorIniciado = true;

  Serial.println(
      "Servidor HTTP iniciado en puerto 80."
  );
  Serial.println(
      "R = Derecha | L = Izquierda | S = Detener"
  );
  Serial.println(
      "A = Automatico | M = Manual"
  );
  Serial.println("==========================================");

  return true;
}

// =====================================================
// COMANDOS OPCIONALES DEL MONITOR SERIE
// =====================================================
void procesarComandosSerial() {
  while (Serial.available() > 0) {
    char comando = Serial.read();

    if (comando == '\n' ||
        comando == '\r' ||
        comando == ' ') {
      continue;
    }

    if (comando == 'R' || comando == 'r') {
      modoAutomatico = false;
      girarDerecha();
      Serial.println("Comando R: DERECHA");

    } else if (comando == 'L' || comando == 'l') {
      modoAutomatico = false;
      girarIzquierda();
      Serial.println("Comando L: IZQUIERDA");

    } else if (comando == 'S' || comando == 's') {
      modoAutomatico = false;
      detenerMotor();
      Serial.println("Comando S: DETENIDO");

    } else if (comando == 'A' || comando == 'a') {
      modoAutomatico = true;
      controlarAutomaticamente();
      Serial.println("Modo AUTOMATICO");

    } else if (comando == 'M' || comando == 'm') {
      modoAutomatico = false;
      detenerMotor();
      Serial.println("Modo MANUAL");

    } else {
      Serial.print("Comando desconocido: ");
      Serial.println(comando);
    }
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  // Alimentación del sensor según el proyecto original.
  pinMode(VCC_PIN, OUTPUT);
  digitalWrite(VCC_PIN, HIGH);

  pinMode(GND_PIN, OUTPUT);
  digitalWrite(GND_PIN, LOW);

  // Inicializar el shield y el motor.
  setupMotorShield();

  // Inicializar el sensor.
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  Serial.println("EAI441 - ControlGiro");
  Serial.println("Arduino UNO R4 WiFi");

  if (!conectarWiFi()) {
    detenerMotor();
  }
}

// =====================================================
// LOOP PRINCIPAL
// =====================================================
void loop() {
  procesarComandosSerial();

  if (WiFi.status() != WL_CONNECTED) {
    detenerMotor();
    servidorIniciado = false;

    if (conectarWiFi()) {
      servidorIniciado = true;
    }

    delay(100);
    return;
  }

  if (!servidorIniciado) {
    server.begin();
    servidorIniciado = true;

    Serial.println(
        "Servidor HTTP reiniciado en puerto 80."
    );
  }

  if (millis() - ultimaMedicion >=
      INTERVALO_MEDICION) {

    ultimaMedicion = millis();
    distancia = medirDistancia();

    if (modoAutomatico) {
      controlarAutomaticamente();
    }
  }

  atenderCliente();
}
