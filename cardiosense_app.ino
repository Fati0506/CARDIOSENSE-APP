#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>   // Nueva librería moderna
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ------------------ CONFIGURACIÓN WIFI ------------------
#define WIFI_SSID "NEKAYSA_FBI 5G"    // 🟢 Cambia por tu red WiFi
#define WIFI_PASSWORD "Dodo*2211"     // 🟢 Cambia por tu contraseña

// ------------------ CONFIGURACIÓN FIREBASE ------------------
#define API_KEY "AIzaSyA8L9PKax1uqeMNfPNLDw1Lxefi5KlSbZo"
#define DATABASE_URL "https://cardiosense-app-default-rtdb.firebaseio.com/"  // Sin HTTPS al final

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ------------------ OBJETOS Y PINES ------------------
MAX30105 particleSensor;

// Pines SPI para TFT ST7735
#define TFT_CS   5
#define TFT_RST  22
#define TFT_DC   2
#define TFT_MOSI 23
#define TFT_SCLK 18

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// ------------------ VARIABLES ------------------
float beatsPerMinute = 0.0;
float spO2 = 0.0;
float temperatureC = 0.0;
long lastBeat = 0;
bool fingerDetected = false;
const long MIN_IR_VALUE = 5000;

// ------------------ CONEXIÓN WIFI ------------------
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  tft.setCursor(0, 80);
  tft.setTextColor(ST7735_CYAN);
  tft.setTextSize(1);
  tft.print("Conectando WiFi...");
  Serial.print("Conectando a WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi conectado!");
  Serial.println(WiFi.localIP());

  tft.fillRect(0, 80, 160, 10, ST7735_BLACK);
  tft.setCursor(0, 80);
  tft.setTextColor(ST7735_GREEN);
  tft.print("WiFi conectado!");
}

// ------------------ ENVIAR A FIREBASE ------------------
void enviarFirebase(float bpm, float spo2, float temp) {
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    Firebase.RTDB.setFloat(&fbdo, "cardiosense/BPM", bpm);
    Firebase.RTDB.setFloat(&fbdo, "cardiosense/SpO2", spo2);
    Firebase.RTDB.setFloat(&fbdo, "cardiosense/Temperatura", temp);
    Serial.println("📤 Datos enviados a Firebase!");
  } else {
    Serial.println("⚠️ No se pudo enviar a Firebase");
  }
}

// ------------------ FUNCIONES AUXILIARES ------------------
float calculateSpO2Simple(long redValue, long irValue) {
  float ratio = (float)redValue / irValue;
  float spO2 = 110.0 - (25.0 * ratio);
  return constrain(spO2, 0.0, 100.0);
}

boolean checkHeartBeat(long irValue) {
  static long prevIrValue = 0;
  static boolean prevState = false;
  static long lastBeatTime = 0;

  boolean beatDetected = (irValue > prevIrValue + 20) && prevState && (millis() - lastBeatTime > 300);
  if (beatDetected) lastBeatTime = millis();

  prevIrValue = irValue;
  prevState = (irValue > 5000);
  return beatDetected;
}

// ------------------ SETUP ------------------
void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando CardioSense...");

  // TFT
  tft.initR(INITR_GREENTAB);
  tft.setRotation(3);
  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(0, 0);
  tft.print("CardioSense IoT");

  // WiFi
  connectWiFi();

  // Sensor MAX30102
  Wire.begin(4, 19);
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("❌ MAX30102 no detectado");
    while (1);
  }

  particleSensor.setup(0x1F, 8, 3, 100, 411, 4096);
  particleSensor.enableDIETEMPRDY();

  // Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("✅ Firebase conectado!");
  tft.setCursor(0, 20);
  tft.setTextColor(ST7735_GREEN);
  tft.print("Firebase listo!");
}

// ------------------ LOOP ------------------
void loop() {
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();
  fingerDetected = (irValue > MIN_IR_VALUE);

  if (fingerDetected) {
    if (checkHeartBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      float bpm = 60 / (delta / 1000.0);
      if (bpm > 40 && bpm < 180) beatsPerMinute = bpm;
    }

    spO2 = calculateSpO2Simple(redValue, irValue);
    temperatureC = particleSensor.readTemperature();

    tft.fillRect(0, 30, 160, 50, ST7735_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST7735_GREEN);
    tft.setCursor(0, 30);
    tft.print("BPM: ");
    tft.print(beatsPerMinute, 1);

    tft.setTextSize(1);
    tft.setTextColor(ST7735_MAGENTA);
    tft.setCursor(0, 55);
    tft.print("SpO2: ");
    tft.print(spO2, 1);
    tft.print("%");

    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(0, 70);
    tft.print("Temp: ");
    tft.print(temperatureC, 1);
    tft.print(" C");

    static unsigned long lastSend = 0;
    if (millis() - lastSend > 5000) {
      enviarFirebase(beatsPerMinute, spO2, temperatureC);
      lastSend = millis();
    }

  } else {
    tft.fillRect(0, 30, 160, 50, ST7735_BLACK);
    tft.setTextColor(ST7735_RED);
    tft.setCursor(0, 40);
    tft.print("Coloca tu dedo...");
  }

  delay(100);
}
