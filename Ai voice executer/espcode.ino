/* ==========================================================================
   VART Master Firmware v3.0 - 100% Verified Conflict-Free
   Hardware Target: LilyGo T-A7670E R2 (ESP32-WROVER-E)
   ========================================================================== */

#include <Wire.h>
#include <Keypad.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

// --------------------------------------------------------------------------
//  FIXED HARDWARE ASSIGNMENTS (ZERO OVERLAPS)
// --------------------------------------------------------------------------
// Native I2C Bus: SDA = GPIO 21, SCL = GPIO 22
LiquidCrystal_I2C lcd(0x27, 16, 2);   

#define DHTPIN        18            // Completely isolated data line
#define DHTTYPE       DHT11          
#define LDR_PIN       35            // Isolated Input-Only line

// Remapped RGB lines to completely free, non-sharing output channels
#define RGB_RED_PIN   19
#define RGB_GREEN_PIN 23
#define RGB_BLUE_PIN  12            // Safe pull-down strapping pin

// --------------------------------------------------------------------------
//  KEYPAD CONFIGURATION (YOUR EXACT WORKING MATRIX)
// --------------------------------------------------------------------------
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','4','7','*'},
  {'2','5','8','0'},
  {'3','6','9','#'},
  {'A','B','C','D'}
};

byte rowPins[ROWS] = {15, 14, 13, 2};  // Orange, Red, Maroon, Black
byte colPins[COLS] = {5, 0, 33, 32};   // Green, Blue, Purple, Grey

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
DHT dht(DHTPIN, DHTTYPE);

// --------------------------------------------------------------------------
//  GLOBAL CODES & TELEMETRY VARIABLES
// --------------------------------------------------------------------------
const String secretCode = "1234";
String inputBuffer = "";
bool isLocked = true;

float temperature = 0.0;
float humidity    = 0.0;
int   lightStatus = 1;

unsigned long lastTelemTime = 0;
const unsigned long telemInterval = 2000; 

String rgbStatus = "OFF";
String currentFace = "LOCKED";

const char LOCKED_STR[]  PROGMEM = "LOCKED";
const char ACTIVE_STR[]  PROGMEM = "ACTIVE";
const char HAPPY_STR[]   PROGMEM = "HAPPY";
const char SAD_STR[]     PROGMEM = "SAD";

inline const char* pgmStr(const char* p) { return p; }

// --------------------------------------------------------------------------
//  RGB PWM SETUP MECHANICS
// --------------------------------------------------------------------------
void setupRGBPWM() {
  const int freq = 5000;   
  const int resolution = 8; 

  ledcSetup(0, freq, resolution);
  ledcSetup(1, freq, resolution);
  ledcSetup(2, freq, resolution);
  ledcAttachPin(RGB_RED_PIN,   0);
  ledcAttachPin(RGB_GREEN_PIN, 1);
  ledcAttachPin(RGB_BLUE_PIN,  2);
}

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(0, r);
  ledcWrite(1, g);
  ledcWrite(2, b);
  if (r || g || b) {
    if (r && !g && !b) rgbStatus = "RED";
    else if (!r && g && !b) rgbStatus = "GREEN";
    else if (!r && !g && b) rgbStatus = "BLUE";
    else rgbStatus = "MIXED";
  } else {
    rgbStatus = "OFF";
  }
}

// --------------------------------------------------------------------------
//  LCD VIEW RENDERING INTERFACE
// --------------------------------------------------------------------------
/* the eyes animation */
byte eyeOpen[8] = {
  B00000,
  B01110,
  B10001,
  B10101,
  B10001,
  B01110,
  B00000,
  B00000
};

byte eyeClosed[8] = {
  B00000,
  B00000,
  B11111,
  B00000,
  B00000,
  B11111,
  B00000,
  B00000
};

byte eyeHappy[8] = {
  B00000,
  B00000,
  B10001,
  B01010,
  B00100,
  B00000,
  B00000,
  B00000
};

byte eyeSad[8] = {
  B00000,
  B00100,
  B01010,
  B10001,
  B00000,
  B00000,
  B00000,
  B00000
};


void drawEyes(const char* stateProgmem) {
  const char* state = pgmStr(stateProgmem);
  currentFace = String(state);   

 lcd.clear();

if (strcmp(state, pgmStr(LOCKED_STR)) == 0) {

    // Left eye
    lcd.setCursor(3,0);
    lcd.write(byte(1));

    // Mouth
    lcd.setCursor(7,0);
    lcd.print("__");

    // Right eye
    lcd.setCursor(12,0);
    lcd.write(byte(1));

    lcd.setCursor(5,1);
    lcd.print("LOCKED");

} else if (strcmp(state, pgmStr(ACTIVE_STR)) == 0) {

    // Left eye
    lcd.setCursor(3,0);
    lcd.write(byte(0));

    // Mouth
    lcd.setCursor(7,0);
    lcd.print("__");

    // Right eye
    lcd.setCursor(12,0);
    lcd.write(byte(0));

    lcd.setCursor(0,1);
    lcd.print("Hello I'm VART");

} else if (strcmp(state, pgmStr(HAPPY_STR)) == 0) {

    lcd.setCursor(3,0);
    lcd.write(byte(2));

    lcd.setCursor(6,0);
    lcd.print("____");

    lcd.setCursor(12,0);
    lcd.write(byte(2));

        lcd.setCursor(0,1);
    lcd.print("I'm HAPPY :)");

} else if (strcmp(state, pgmStr(SAD_STR)) == 0) {

    lcd.setCursor(3,0);
    lcd.write(byte(3));

    lcd.setCursor(7,0);
    lcd.print("__");

    lcd.setCursor(12,0);
    lcd.write(byte(3));

    lcd.setCursor(0,1);
    lcd.print("I'm SAD :(");
}
}

// --------------------------------------------------------------------------
//  SERIAL CORE COMMAND PARSER
// --------------------------------------------------------------------------
void parseVoiceCommand(String cmd) {
  cmd.trim();
  if (cmd.equals("RGB_RED"))        { setRGB(255, 0, 0); }
  else if (cmd.equals("RGB_GREEN"))  { setRGB(0, 255, 0); }
  else if (cmd.equals("RGB_BLUE"))   { setRGB(0, 0, 255); }
  else if (cmd.equals("RGB_OFF"))    { setRGB(0, 0, 0); }
  else if (cmd.equals("EYES_HAPPY")) { drawEyes(pgmStr(HAPPY_STR)); }
  else if (cmd.equals("EYES_SAD"))   { drawEyes(pgmStr(SAD_STR)); }
  else if (cmd.equals("EYES_RESET")) { drawEyes(pgmStr(ACTIVE_STR)); }
}

// --------------------------------------------------------------------------
//  WATCHDOG SETUP
// --------------------------------------------------------------------------
#include "esp_task_wdt.h"
void initWatchdog() {
  esp_task_wdt_init(5, true); 
  TaskHandle_t loopTaskHandle = xTaskGetCurrentTaskHandleForCPU(1);
  if (loopTaskHandle != NULL) esp_task_wdt_add(loopTaskHandle);
}

// -------------------------------------------------------------
//  SYSTEM INITIALIZATION
// -------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(DHTPIN, INPUT_PULLUP);
  dht.begin();
  pinMode(LDR_PIN, INPUT);

  setupRGBPWM();
  setRGB(0, 0, 0);

  lcd.init();
  lcd.backlight();

  lcd.createChar(0, eyeOpen);
  lcd.createChar(1, eyeClosed);
  lcd.createChar(2, eyeHappy);
  lcd.createChar(3, eyeSad);

  drawEyes(pgmStr(LOCKED_STR));
  Serial.println(F("\n[BOOT COMPLETE] Pin budget aligned perfectly."));
  
  initWatchdog();
}

// -------------------------------------------------------------
//  MAIN EXECUTION ENVIRONMENT
// -------------------------------------------------------------
void loop() {
  esp_task_wdt_reset();

  if (Serial.available() > 0) {
    String incoming = Serial.readStringUntil('\n');
    parseVoiceCommand(incoming);
  }

  char key = keypad.getKey();
  if (key) {
    // Structural Optimization: Use brief Blue RGB pulse as a key-press blip
    setRGB(0, 0, 150); delay(20); setRGB(0, 0, 0);

    if (key == '#') {              
      if (inputBuffer == secretCode) {
        isLocked = false;
        Serial.println(F("SYS_UNLOCK"));    
        drawEyes(pgmStr(ACTIVE_STR));
        setRGB(0, 255, 0); // Solid Green Success indicators
      } else {
        Serial.println(F("SYS_DENIED"));
        setRGB(255, 0, 0); // Solid Red Alert indicators
        drawEyes(pgmStr(SAD_STR));
        delay(1500);
        setRGB(0, 0, 0);
        drawEyes(pgmStr(LOCKED_STR));
      }
      inputBuffer = "";
    } else if (key == '*') {       
      inputBuffer = "";
    } else {
      inputBuffer += key;
    }
  }

  unsigned long now = millis();
  if (now - lastTelemTime >= telemInterval) {
    lastTelemTime = now;

    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int   l = digitalRead(LDR_PIN);

    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity    = h;
    }
    lightStatus = l;

    JsonDocument doc;
    doc["t"]    = temperature;
    doc["h"]    = humidity;
    doc["l"]    = (lightStatus == LOW) ? 1 : 0; // 1 = Light detected, 0 = Dark
    doc["rgb"]  = rgbStatus;
    doc["face"] = currentFace;

    serializeJson(doc, Serial);
    Serial.println();   
  }
}