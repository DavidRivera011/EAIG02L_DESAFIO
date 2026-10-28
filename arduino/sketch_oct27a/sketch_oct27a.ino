#include <WiFiS3.h>
#include "arduino_secrets.h"

// ==========================================
// PINES: CONFIGURACION YA PROBADA EN TINKERCAD
// ==========================================
const int IN1 = 3;
const int IN2 = 2;

const int TRIG = 10;
const int ECHO = 9;

// ==========================================
// WIFI Y SERVIDOR HTTP LOCAL
// ==========================================
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

WiFiServer server(80);

bool modoAutomatico = false;
String estadoMotor = "DETENIDO";

float distancia = -1;

unsigned long ultimaMedicion = 0;
const unsigned long INTERVALO_MEDICION = 100;

// ==========================================
// CONFIGURACION INICIAL
// ==========================================
void setup() {
  Serial.begin(9600);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  digitalWrite(TRIG, LOW);
  detenerMotor();

  Serial.println("=== CONTROLGIRO ===");
  Serial.println("Inicializando Wi-Fi...");

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("ERROR: modulo Wi-Fi no disponible.");

    while (true) {
      detenerMotor();
      delay(1000);
    }
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
    Serial.println("ERROR: no se pudo conectar al Wi-Fi.");
    Serial.println("Revisa el hotspot y las credenciales.");

    detenerMotor();
    return;
  }

  Serial.println("Wi-Fi conectado correctamente.");

  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  Serial.print("IP del Arduino: ");
  Serial.println(WiFi.localIP());

  server.begin();

  Serial.println("Servidor HTTP iniciado en puerto 80.");
  Serial.println("R = Derecha | L = Izquierda | S = Detener");
  Serial.println("A = Automatico | M = Manual");
  Serial.println("==========================================");
}

// ==========================================
// BUCLE PRINCIPAL
// ==========================================
void loop() {
  procesarComandosSerial();

  if (WiFi.status() != WL_CONNECTED) {
    detenerMotor();
    return;
  }

  if (millis() - ultimaMedicion >= INTERVALO_MEDICION) {
    ultimaMedicion = millis();
    distancia = medirDistancia();

    if (modoAutomatico) {
      controlarAutomaticamente();
    }
  }

  atenderCliente();
}

// ==========================================
// COMANDOS DEL MONITOR SERIE
// ==========================================
void procesarComandosSerial() {
  while (Serial.available() > 0) {
    char comando = Serial.read();

    if (comando == '\n' || comando == '\r' || comando == ' ') {
      continue;
    }

    if (comando == 'R' || comando == 'r') {
      modoAutomatico = false;
      girarDerecha();
      Serial.println("Comando R: DERECHA");
    }
    else if (comando == 'L' || comando == 'l') {
      modoAutomatico = false;
      girarIzquierda();
      Serial.println("Comando L: IZQUIERDA");
    }
    else if (comando == 'S' || comando == 's') {
      modoAutomatico = false;
      detenerMotor();
      Serial.println("Comando S: DETENIDO");
    }
    else if (comando == 'A' || comando == 'a') {
      modoAutomatico = true;
      controlarAutomaticamente();
      Serial.println("Modo AUTOMATICO");
    }
    else if (comando == 'M' || comando == 'm') {
      modoAutomatico = false;
      detenerMotor();
      Serial.println("Modo MANUAL");
    }
    else {
      Serial.print("Comando desconocido: ");
      Serial.println(comando);
    }
  }
}

// ==========================================
// SENSOR HC-SR04
// ==========================================
float medirDistancia() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  unsigned long duracion = pulseIn(ECHO, HIGH, 30000);

  if (duracion == 0) {
    return -1;
  }

  return duracion * 0.0343 / 2.0;
}

// ==========================================
// CONTROL AUTOMATICO
// ==========================================
void controlarAutomaticamente() {
  if (distancia >= 150 && distancia <= 200) {
    girarDerecha();
  }
  else if (distancia >= 100 && distancia <= 140) {
    girarIzquierda();
  }
  else {
    detenerMotor();
  }
}

// ==========================================
// SERVIDOR HTTP
// ==========================================
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

  // Leer encabezados hasta la linea vacia.
  while (cliente.connected()) {
    String encabezado = cliente.readStringUntil('\n');

    if (encabezado == "\r" || encabezado.length() == 0) {
      break;
    }
  }

  String respuesta;
  int codigoHTTP = 200;

  if (solicitud.startsWith("GET /status ")) {
    respuesta = crearEstadoJSON();
  }
  else if (solicitud.startsWith("GET /cmd?value=R ")) {
    modoAutomatico = false;
    girarDerecha();
    respuesta = crearEstadoJSON();
  }
  else if (solicitud.startsWith("GET /cmd?value=L ")) {
    modoAutomatico = false;
    girarIzquierda();
    respuesta = crearEstadoJSON();
  }
  else if (solicitud.startsWith("GET /cmd?value=S ")) {
    modoAutomatico = false;
    detenerMotor();
    respuesta = crearEstadoJSON();
  }
  else if (solicitud.startsWith("GET /mode?value=A ")) {
    modoAutomatico = true;
    controlarAutomaticamente();
    respuesta = crearEstadoJSON();
  }
  else if (solicitud.startsWith("GET /mode?value=M ")) {
    modoAutomatico = false;
    detenerMotor();
    respuesta = crearEstadoJSON();
  }
  else {
    codigoHTTP = 404;
    respuesta = "{\"success\":false,\"message\":\"Ruta no encontrada\"}";
  }

  enviarRespuesta(cliente, codigoHTTP, respuesta);

  delay(2);
  cliente.stop();
}

// ==========================================
// RESPUESTA HTTP JSON
// ==========================================
void enviarRespuesta(
  WiFiClient &cliente,
  int codigoHTTP,
  const String &contenido
) {
  cliente.print("HTTP/1.1 ");
  cliente.print(codigoHTTP);

  if (codigoHTTP == 200) {
    cliente.println(" OK");
  } else {
    cliente.println(" Not Found");
  }

  cliente.println("Content-Type: application/json; charset=utf-8");
  cliente.println("Cache-Control: no-store");
  cliente.println("Connection: close");
  cliente.println("Access-Control-Allow-Origin: *");
  cliente.println();

  cliente.println(contenido);
}

// ==========================================
// ESTADO DEL SISTEMA EN JSON
// ==========================================
String crearEstadoJSON() {
  String modo = modoAutomatico ? "AUTOMATICO" : "MANUAL";

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

// ==========================================
// CONTROL DEL MOTOR L293D
// ==========================================
void girarDerecha() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  estadoMotor = "DERECHA";
}

void girarIzquierda() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  estadoMotor = "IZQUIERDA";
}

void detenerMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  estadoMotor = "DETENIDO";
}
