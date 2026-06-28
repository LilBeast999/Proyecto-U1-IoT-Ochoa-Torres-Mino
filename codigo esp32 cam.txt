#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// credenciales
const char* ssid = "iot";
const char* password = "password123";

// ip y puerto (HTTPS / TLS hacia Node-RED)
String serverName = "https://192.168.137.1:1880/upload-image";

// Certificado de la CA (mosquitto/certs/ca.crt) para validar a Node-RED por TLS.
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

// cliente TLS reutilizable para las peticiones HTTPS
WiFiClientSecure clienteSeguro;

// pines correspondientes al modelo ai thinker
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setup() {
  Serial.begin(115200);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // configuracion optimizada para evitar latencia de transmision
  if(psramFound()){
    config.frame_size = FRAMESIZE_QVGA; 
    config.jpeg_quality = 20; 
    config.fb_count = 1; 
  } else {
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 20;
    config.fb_count = 1;
  }

  // inicializa el hardware de la camara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("fallo la inicializacion de la camara");
    return;
  }

  // inicia la conexion a internet y espera hasta obtener ip
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nwifi conectado");

  // Sincronizar reloj por NTP: TLS valida la vigencia del certificado.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t ahora = time(nullptr);
  unsigned long t0 = millis();
  while (ahora < 1700000000 && millis() - t0 < 15000) {
    delay(500);
    ahora = time(nullptr);
  }

  // Cargar la CA para validar la identidad de Node-RED por TLS.
  clienteSeguro.setCACert(ca_cert);
}

void loop() {
  // obtiene el fotograma desde el buffer de hardware
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("fallo al capturar la imagen");
    delay(1000);
    return;
  }

  // asegura conexion activa antes del envio http
  if(WiFi.status() == WL_CONNECTED){
    HTTPClient http;

    // establece la ruta de destino sobre TLS (valida a Node-RED con la CA)
    http.begin(clienteSeguro, serverName);

    // define el tipo de archivo que viaja en el cuerpo del mensaje
    http.addHeader("Content-Type", "image/jpeg");
    
    // ejecuta la peticion post enviando el arreglo de bytes de la imagen
    int httpResponseCode = http.POST(fb->buf, fb->len);
    
    Serial.print("codigo de respuesta http: ");
    Serial.println(httpResponseCode);
    
    // cierra la conexion y libera recursos
    http.end();
  } else {
    Serial.println("wifi desconectado");
  }

  // devuelve el buffer a la camara para limpiar la memoria
  esp_camera_fb_return(fb);
  
  // controla la cadencia de disparos a 1 cada 2 segundos
  delay(1250);
}