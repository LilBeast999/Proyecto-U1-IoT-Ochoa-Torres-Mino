#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <coap-simple.h>        // "CoAP simple library" (Hirotaka Niisato)
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <LiquidCrystal.h>

const char* ssid = "iot";
const char* password = "password123";
const char* mqtt_server = "192.168.137.1";
const int   mqtt_port   = 8883;            // puerto TLS del broker


const char* mqtt_user = "esp32";
const char* mqtt_pass = "zapato123";

// Certificado de la CA (mosquitto/certs/ca.crt) para validar al broker por TLS.
const char* ca_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDlzCCAn+gAwIBAgIUaL9onXqEnSMOOCELR4eclRvmAa8wDQYJKoZIhvcNAQEL
BQAwWzELMAkGA1UEBhMCQ0wxDjAMBgNVBAgMBU1hdWxlMQ4wDAYDVQQHDAVUYWxj
YTEVMBMGA1UECgwMU21hcnRIb21lSW9UMRUwEwYDVQQDDAxTbWFydEhvbWUtQ0Ew
HhcNMjYwNjE3MTczMTU1WhcNMjcwNjE3MTczMTU1WjBbMQswCQYDVQQGEwJDTDEO
MAwGA1UECAwFTWF1bGUxDjAMBgNVBAcMBVRhbGNhMRUwEwYDVQQKDAxTbWFydEhv
bWVJb1QxFTATBgNVBAMMDFNtYXJ0SG9tZS1DQTCCASIwDQYJKoZIhvcNAQEBBQAD
ggEPADCCAQoCggEBAL9DjXUXiNdQ/tKLSPYrw2cb6CaoodZ+2FbWocofSBF5F2GR
R6ULH8qRwIT13QalFQAIu4T3aEHrRrnQCOpDb3B7n8K2lfnfkF3F7HlG0uN4U4xo
Owhg7iphENEWk1OOIGlusQbEXxyI1NaAMudwSgg8gcEmnDZvl486IexiVup9VSPy
mPVMPUd+xpnPG49rxHDxPawhFeARwCAysstRiLSGfayLPhpbHZM2JU0wYPO12+z7
b885csdCU3AM+kMrhtvplgJnzn8Z3bTWzTwoSZ++l3UIaP1ctsS8KRjv+iPbav+D
2lKoXW0CtwsM6alkbw1D8j2CFahucrcniA109s8CAwEAAaNTMFEwHQYDVR0OBBYE
FEXwJ7HzVusRxowHWyNc6B8O/CmuMB8GA1UdIwQYMBaAFEXwJ7HzVusRxowHWyNc
6B8O/CmuMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBAFLD7XC3
up6eBi4gQtCVwbHzPTPpt8ffOYYDpKh3AacqB7YwFnQfT/Q+Zkspg70+h2qWghMa
l93EiF0bBZ3M0lKkQNg0ONxluQH4XN4DAFTR67DdOf6KxDdYGzUoitcVBlNI9I6h
mBKHcuMxvz8TO0hHrvO/WzRL7sjqJJLrC+LhFVLC3ImwaW/iI3jHTixJwyXHemLF
jsOr3g1xmyOSzrdIOylH+T8Ruv/yeUf0INkPF045qMQWGS8REEbgx0vJR+1HNill
kQundKh7HiG1ba5L5sBFEoo2WylCg2H9btQdekldZr2ztE9BGN2abYxkUrVNfPfr
HvInHqRe7EiCi6M=
-----END CERTIFICATE-----
)EOF";

WiFiClientSecure espClient;
PubSubClient client(espClient);

// --- CoAP (F2): el ESP32 tambien es servidor CoAP del sensor de gas ---
// IP fija para que Node-RED siempre sepa donde hacer el GET (rango del hotspot).
IPAddress ip_local(192, 168, 137, 50);
IPAddress gateway (192, 168, 137, 1);
IPAddress mascara (255, 255, 255, 0);
IPAddress dns     (192, 168, 137, 1);
WiFiUDP udp;
Coap coap(udp);

LiquidCrystal lcd(22, 23, 5, 18, 19, 21);
Adafruit_SHT31 sht30 = Adafruit_SHT31();

const int pinGas = 34;
const int pinMic = 35;
// definicion de pines para actuadores
const int pinLedCerradura = 4;
const int pinLuzPatio = 12;
const int pinBuzzer = 13;        // buzzer activo controlado por el LLM (control/buzzer)

// variables de temporizacion
unsigned long ultimoEnvio = 0;
unsigned long ultimoScroll = 0;
unsigned long tiempoApertura = 0;
unsigned long inicioAlarma = 0;
unsigned long ultimoCambioLuz = 0;

// banderas de estado logico
bool cerraduraAbierta = false;
bool alarmaActiva = false;
bool estadoLuzPatio = false;
bool luzPatioManual = false;

String camaraRes = "Cam: Esperando...                "; 
String textoSensores = "Iniciando sensores...                ";
int scrollIndexSensores = 0;
int scrollIndexCamara = 0;

void callback(char* topic, byte* payload, unsigned int length) {
  String mensaje = "";
  for (int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }
  
  if (String(topic) == "smarthome/equipoXX/camara") {
    camaraRes = "Cam: " + mensaje + "                "; 
    scrollIndexCamara = 0; 
    
    // activacion de actuadores segun reconocimiento facial
    if (mensaje == "desconocido") {
      alarmaActiva = true;
      inicioAlarma = millis();
    } else if (mensaje != "no_hay_rostro_visible" && mensaje.length() > 0) {
      digitalWrite(pinLedCerradura, HIGH);
      tiempoApertura = millis();
      cerraduraAbierta = true;
    }
  } else if (String(topic) == "smarthome/equipoXX/luz_manual") {
    // control manual desde el dashboard anula temporalmente la alarma
    alarmaActiva = false; 
    if (mensaje == "1") {
      luzPatioManual = true;
      estadoLuzPatio = true;
      digitalWrite(pinLuzPatio, HIGH);
    } else if (mensaje == "0") {
      luzPatioManual = false;
      estadoLuzPatio = false;
      digitalWrite(pinLuzPatio, LOW);
    }
  } else if (String(topic) == "smarthome/equipoXX/control/buzzer") {
    // decision del LLM: activar/desactivar buzzer de alerta
    digitalWrite(pinBuzzer, (mensaje == "1") ? HIGH : LOW);
  }
}

void reconectar() {
  while (!client.connected()) {
    // conexion autenticada con usuario y contrasena sobre TLS
    if (client.connect("ESP32_SmartHome", mqtt_user, mqtt_pass)) {
      client.subscribe("smarthome/equipoXX/camara");
      client.subscribe("smarthome/equipoXX/luz_manual");
      client.subscribe("smarthome/equipoXX/control/buzzer");   // decision del LLM
    } else {
      delay(5000);
    }
  }
}

// Callback del recurso CoAP "gas": se ejecuta ante cada GET.
// Lee el sensor de gas EN EL MOMENTO (true request/response) y responde.
void callbackGas(CoapPacket &packet, IPAddress ip, int port) {
  int g = analogRead(pinGas);

  char payload[16];
  snprintf(payload, sizeof(payload), "%d", g);   // valor crudo del ADC

  // Respuesta CoAP 2.05 Content, text/plain, devolviendo el token recibido
  coap.sendResponse(ip, port, packet.messageid,
                    payload, strlen(payload),
                    COAP_CONTENT, COAP_TEXT_PLAIN,
                    packet.token, packet.tokenlen);
}

void setup() {
  lcd.begin(16, 2);
  lcd.print("Conectando...");

  // configuracion de pines y estado inicial seguro
  pinMode(pinLedCerradura, OUTPUT);
  pinMode(pinLuzPatio, OUTPUT);
  pinMode(pinBuzzer, OUTPUT);
  digitalWrite(pinLedCerradura, LOW);
  digitalWrite(pinLuzPatio, LOW);
  digitalWrite(pinBuzzer, LOW);

  // IP fija para el servidor CoAP (Node-RED hace GET a esta direccion)
  WiFi.config(ip_local, gateway, mascara, dns);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // Iniciar el servidor CoAP: recurso "gas" en UDP:5683
  coap.server(callbackGas, "gas");
  coap.start();

  // Sincronizar reloj por NTP: TLS necesita la hora real para validar
  // la vigencia del certificado del broker. Sin esto la conexion falla.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t ahora = time(nullptr);
  unsigned long t0 = millis();
  while (ahora < 1700000000 && millis() - t0 < 15000) {
    delay(500);
    ahora = time(nullptr);
  }

  // Cargar la CA para que el ESP32 valide la identidad del broker.
  espClient.setCACert(ca_cert);

  Wire.begin(32, 33);
  sht30.begin(0x44);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconectar();
  }
  client.loop();

  // Atender peticiones CoAP entrantes (servidor de gas, no bloqueante)
  coap.loop();

  // temporizador para cerrar la puerta automaticamente
  if (cerraduraAbierta && (millis() - tiempoApertura >= 4000)) {
    digitalWrite(pinLedCerradura, LOW);
    cerraduraAbierta = false;
  }

  // logica de parpadeo no bloqueante para intrusos
  if (alarmaActiva) {
    if (millis() - inicioAlarma >= 5000) {
      // fin del parpadeo, se restaura el estado definido por el dashboard
      alarmaActiva = false;
      digitalWrite(pinLuzPatio, luzPatioManual ? HIGH : LOW);
      estadoLuzPatio = luzPatioManual;
    } else if (millis() - ultimoCambioLuz >= 250) {
      // inversion del estado del led cada 250 ms
      ultimoCambioLuz = millis();
      estadoLuzPatio = !estadoLuzPatio;
      digitalWrite(pinLuzPatio, estadoLuzPatio ? HIGH : LOW);
    }
  }

  // control de pantalla lcd
  if (millis() - ultimoScroll > 400) {
    ultimoScroll = millis();
    
    lcd.clear();
    
    lcd.setCursor(0, 0);
    String mostrarCam = camaraRes.substring(scrollIndexCamara, scrollIndexCamara + 16);
    lcd.print(mostrarCam);
    
    scrollIndexCamara++;
    if (scrollIndexCamara > camaraRes.length() - 16) {
      scrollIndexCamara = 0;
    }

    lcd.setCursor(0, 1);
    String mostrarSens = textoSensores.substring(scrollIndexSensores, scrollIndexSensores + 16);
    lcd.print(mostrarSens);
    
    scrollIndexSensores++;
    if (scrollIndexSensores > textoSensores.length() - 16) {
      scrollIndexSensores = 0; 
    }
  }

  // lectura y transmision de sensores
  if (millis() - ultimoEnvio > 3000) {
    ultimoEnvio = millis();

    float t = sht30.readTemperature();
    float h = sht30.readHumidity();
    int g = analogRead(pinGas);
    int m = analogRead(pinMic);

    textoSensores = " Temperatura:" + String(t, 1) + " Humedad:" + String(h, 0) + " Gas:" + String(g) + " Mic:" + String(m) + "                ";

    StaticJsonDocument<256> doc;
    doc["equipo"] = "equipoXX"; 
    doc["temperatura"] = t;
    doc["humedad"] = h;
    doc["gas"] = g;
    doc["microfono"] = m;

    char buffer[256];
    serializeJson(doc, buffer);
    client.publish("smarthome/equipoXX/sensores", buffer); 
  }
}