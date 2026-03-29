#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <ESP32Servo.h>

// Helpers
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// WiFi credentials (replace with your own in local config)
#define WIFI_SSID "<YOUR_WIFI_SSID>"
#define WIFI_PASSWORD "<YOUR_WIFI_PASSWORD>"

// Firebase credentials (replace with your own in local config)
#define API_KEY "<YOUR_FIREBASE_API_KEY>"
#define DATABASE_URL "<YOUR_FIREBASE_DB_URL>"
#define USER_EMAIL "<YOUR_EMAIL>"
#define USER_PASSWORD "<YOUR_PASSWORD>"

// === Window Sensors ===
#define DHT_PIN 14
#define MQ5_PIN 34
#define RAIN_PIN 35
#define SERVO_PIN 13

// === LED Pins ===
#define LED1_PIN 4
#define LED2_PIN 12
#define LED3_PIN 27

// === Water Level Sensor ===
#define WATER_LEVEL_PIN 32

// === Sensor Thresholds ===
#define SEUIL_GAZ 2000
#define SEUIL_PLUIE 1500
#define SEUIL_TEMP 30.0

// === Objects ===
DHT dht(DHT_PIN, DHT11);
Servo servoFenetre;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
int interval = 5000; // Send data every 5 seconds

void setup() {
  Serial.begin(115200);
  
  dht.begin();
  servoFenetre.attach(SERVO_PIN, 500, 2400);

  pinMode(MQ5_PIN, INPUT);
  pinMode(RAIN_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nWiFi Connected!");

  // Firebase setup
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  config.token_status_callback = tokenStatusCallback;

  Firebase.reconnectNetwork(true);
  Firebase.begin(&config, &auth);
  Firebase.setDoubleDigits(2);

  Serial.println("Firebase initialized!");
}

void loop() {
  if (Firebase.ready() && (millis() - sendDataPrevMillis > interval || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int gasLevel = analogRead(MQ5_PIN);
    int rainLevel = analogRead(RAIN_PIN);
    int waterLevel = analogRead(WATER_LEVEL_PIN);

    bool gasDetected = gasLevel > SEUIL_GAZ;
    bool rainDetected = rainLevel < SEUIL_PLUIE;
    int waterLevelPercent = map(waterLevel, 0, 4095, 0, 100);

    Serial.println("=== Sensor Readings ===");
    Serial.printf("Temperature: %.2f°C\n", temperature);
    Serial.printf("Humidity: %.2f%%\n", humidity);
    Serial.printf("Gas Level: %d (Detected: %s)\n", gasLevel, gasDetected ? "YES" : "NO");
    Serial.printf("Rain Level: %d (Detected: %s)\n", rainLevel, rainDetected ? "YES" : "NO");
    Serial.printf("Water Level: %d%%\n", waterLevelPercent);

    // Upload data to Firebase
    if (!isnan(temperature)) Firebase.RTDB.setFloat(&fbdo, "data/temperature", temperature);
    if (!isnan(humidity)) Firebase.RTDB.setFloat(&fbdo, "data/moisture", humidity);
    Firebase.RTDB.setBool(&fbdo, "data/gas", gasDetected);
    Firebase.RTDB.setBool(&fbdo, "data/rain", rainDetected);
    Firebase.RTDB.setInt(&fbdo, "data/waterLevel", waterLevelPercent);

    controlWindow(gasDetected, rainDetected, temperature);

    Serial.println("=========================\n");
  }
  delay(100);
}

void controlWindow(bool gasDetected, bool rainDetected, float temperature) {
  if (gasDetected) servoFenetre.write(0);  // Open
  else if (rainDetected) servoFenetre.write(90);  // Close
  else if (temperature > SEUIL_TEMP) servoFenetre.write(0);  // Open
  else servoFenetre.write(90);  // Close
}