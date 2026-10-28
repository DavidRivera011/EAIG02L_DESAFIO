#include <ArduinoBLE.h>

// =====================================================
// BLUETOOTH BLE
// =====================================================
BLEService controlService(
  "19B10000-E8F2-537E-4F6C-D104768A1214"
);

BLEStringCharacteristic commandCharacteristic(
  "19B10001-E8F2-537E-4F6C-D104768A1214",
  BLEWrite,
  20
);

BLEStringCharacteristic statusCharacteristic(
  "19B10002-E8F2-537E-4F6C-D104768A1214",
  BLERead | BLENotify,
  128
);

// =====================================================
// SENSOR HC-SR04: CONEXIONES ORIGINALES
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

unsigned long ultimaNotificacion = 0;
const unsigned long INTERVALO_NOTIFICACION = 250;

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

  analogWrite(MOTOR_PWM_PIN, 0);

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
// SENSOR HC-SR04
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
// MODO AUTOMATICO
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
// ESTADO JSON PARA ANDROID
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

void publicarEstado() {
  String json = crearEstadoJSON();

  // La característica admite hasta 128 bytes.
  if (json.length() <= 128) {
    statusCharacteristic.writeValue(json);
  }
}

// =====================================================
// PROCESAR COMANDOS BLE
// R = DERECHA
// L = IZQUIERDA
// S = DETENER
// A = AUTOMATICO
// M = MANUAL
// =====================================================
void procesarComandoBLE() {
  if (!commandCharacteristic.written()) {
    return;
  }

  String comando = commandCharacteristic.value();
  comando.trim();
  comando.toUpperCase();

  Serial.print("Comando BLE: ");
  Serial.println(comando);

  if (comando == "R") {
    modoAutomatico = false;
    girarDerecha();

  } else if (comando == "L") {
    modoAutomatico = false;
    girarIzquierda();

  } else if (comando == "S") {
    modoAutomatico = false;
    detenerMotor();

  } else if (comando == "A") {
    modoAutomatico = true;
    controlarAutomaticamente();

  } else if (comando == "M") {
    modoAutomatico = false;
    detenerMotor();
  }

  publicarEstado();
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(VCC_PIN, OUTPUT);
  digitalWrite(VCC_PIN, HIGH);

  pinMode(GND_PIN, OUTPUT);
  digitalWrite(GND_PIN, LOW);

  setupMotorShield();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  Serial.println("EAI441 - ControlGiro BLE");
  Serial.println("Inicializando Bluetooth...");

  if (!BLE.begin()) {
    Serial.println("ERROR: No se pudo iniciar BLE.");

    while (true) {
      detenerMotor();
      delay(1000);
    }
  }

  BLE.setLocalName("ControlGiro-BLE");
  BLE.setAdvertisedService(controlService);

  controlService.addCharacteristic(commandCharacteristic);
  controlService.addCharacteristic(statusCharacteristic);

  BLE.addService(controlService);

  commandCharacteristic.writeValue("S");
  publicarEstado();

  BLE.advertise();

  Serial.println("BLE iniciado.");
  Serial.println("Nombre: ControlGiro-BLE");
  Serial.println("Esperando conexion...");
}

// =====================================================
// LOOP PRINCIPAL
// =====================================================
void loop() {
  BLE.poll();

  procesarComandoBLE();

  if (millis() - ultimaMedicion >=
      INTERVALO_MEDICION) {

    ultimaMedicion = millis();
    distancia = medirDistancia();

    if (modoAutomatico) {
      controlarAutomaticamente();
    }
  }

  if (millis() - ultimaNotificacion >=
      INTERVALO_NOTIFICACION) {

    ultimaNotificacion = millis();
    publicarEstado();
  }

  BLE.poll();
}
