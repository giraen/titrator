#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==========================================
// PINS & CONSTANTS
// ==========================================
const byte SEL_PIN = 2;
const byte UP_PIN = 4;
const byte DN_PIN = 5;
const byte PUMP_SWITCH_PIN = 6;
const byte MAGNET_SWITCH_PIN = 9;
const byte PH_SENSOR_PIN = A0;
const byte SOUND_PIN = 8;
byte PUMP_SPEED = 255;
byte FAN_SPEED = 112;

// ==========================================
// CALIBRATION VALUES
// ==========================================
float FLOW_RATE_ML_PER_MIN = 75.0;
float VOLTAGE_PH7 = 2.50;
float VOLTAGE_PH4 = 3.05;
float PH_STEP = (VOLTAGE_PH4 - VOLTAGE_PH7) / (7.0 - 4.0);

// ==========================================
// EMA FILTER CONSTANT
// ==========================================
const float ALPHA = 0.15;
float smoothed_voltage = 2.50;

// ==========================================
// STATE MACHINE SETUP
// ==========================================
enum SystemState {
  MAIN_MENU, 
  CAL_MENU, 
  RUN_TITRATION, 
  RUN_CAL_FULL, 
  RUN_CAL_PROBE, 
  RUN_CAL_PUMP,
  VIEW_SETTINGS,
  RUN_FLUSH 
};

SystemState currentState = MAIN_MENU;
int menuIndex = 0;

const int MAIN_MENU_ITEMS = 4;
String mainMenu[MAIN_MENU_ITEMS] = {
  "1. Titrate",
  "2. Calibrate",
  "3. View settings",
  "4. Flush pump"
};

const int CAL_MENU_ITEMS = 4;
String calMenu[CAL_MENU_ITEMS] = {
  "1. Full cal",
  "2. Probe cal",
  "3. Pump cal",
  "4. Back"
};

// ==========================================
// INITIAL VALUES
// ==========================================
String lastDisplayedLine1 = "";
String lastDisplayedLine2 = "";

// ==========================================
// TRACKERS AND FLAGS
// ==========================================
volatile bool phSpike = false;
float previous_pH = 0.0;
byte spike_counter = 0;
const float SPIKE_THRESHOLD = 0.15;
const int REQUIRED_CONSECUTIVE_SPIKES = 3;

enum SelResult {
  SEL_SHORT,
  SEL_HOLD
};

// ==========================================
// TIMINGS
// ==========================================
unsigned long previous_time = 0;
const unsigned long SAMPLE_INTERVAL = 500;
const unsigned long CALIBRATION_TIME_MS = 60000;

void setup() {
  Serial.begin(9600);

  // Check Memory for Constant values
  byte magicByte;
  EEPROM.get(12, magicByte);

  if (magicByte != 0x5A) {
    FLOW_RATE_ML_PER_MIN = 75.0;
    VOLTAGE_PH7 = 2.50;
    VOLTAGE_PH4 = 3.05;

    EEPROM.put(0, FLOW_RATE_ML_PER_MIN);
    EEPROM.put(4, VOLTAGE_PH7);
    EEPROM.put(8, VOLTAGE_PH4);

    EEPROM.put(12, 0x5A);
  } else {
    EEPROM.get(0, FLOW_RATE_ML_PER_MIN);
    EEPROM.get(4, VOLTAGE_PH7);
    EEPROM.get(8, VOLTAGE_PH4);
  }

  PH_STEP = (VOLTAGE_PH4 - VOLTAGE_PH7) / (7.0 - 4.0);

  lcd.init();      
  lcd.backlight();

  // Reset state for pins
  pinMode(PUMP_SWITCH_PIN, OUTPUT);
  analogWrite(PUMP_SWITCH_PIN, 0);
  pinMode(MAGNET_SWITCH_PIN, OUTPUT);
  analogWrite(MAGNET_SWITCH_PIN, 0);
  pinMode(SOUND_PIN, OUTPUT);
  digitalWrite(SOUND_PIN, LOW);

  // Setup BTN Pins
  pinMode(SEL_PIN, INPUT_PULLUP);
  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DN_PIN, INPUT_PULLUP);

  displayMessage("System Ready", "Push to Start");

  float initial_raw_voltage = analogRead(PH_SENSOR_PIN) * (5.0 / 1023.0);
  smoothed_voltage = initial_raw_voltage;

  delay(3000);
}

void loop() {
  int btn = getButton();

  switch (currentState) {
    case MAIN_MENU:
      displayMessage("--- MAIN MENU ---", "> " + mainMenu[menuIndex]);

      // Scroll Mechanism
      if (btn == 1) {
        // Scroll up button
        menuIndex--;
        if (menuIndex < 0) menuIndex = MAIN_MENU_ITEMS - 1;
      } else if (btn == 2) {
        // Scroll down button
        menuIndex++; 
        if (menuIndex >= MAIN_MENU_ITEMS) menuIndex = 0; 
      } else if (btn == 3) {
        // Select button
        if (menuIndex == 0) currentState = RUN_TITRATION;
        if (menuIndex == 1) { currentState = CAL_MENU; menuIndex = 0; }
        if (menuIndex == 2) currentState = VIEW_SETTINGS;
        if (menuIndex == 3) currentState = RUN_FLUSH;
      }
      break;

    case CAL_MENU:
      displayMessage("- CALIBRATION -", "> " + calMenu[menuIndex]);

      if (btn == 1) {
        menuIndex--;
        if (menuIndex < 0) menuIndex = CAL_MENU_ITEMS - 1;
      }
      else if (btn == 2) {
        menuIndex++;
        if (menuIndex >= CAL_MENU_ITEMS) menuIndex = 0;
      }
      else if (btn == 3) {
        if (menuIndex == 0) currentState = RUN_CAL_FULL;
        else if (menuIndex == 1) currentState = RUN_CAL_PROBE;
        else if (menuIndex == 2) currentState = RUN_CAL_PUMP;
        else if (menuIndex == 3) { currentState = MAIN_MENU; menuIndex =0; }
      }
      break;

    case RUN_TITRATION:
      runTitration();
      currentState = MAIN_MENU;
      menuIndex = 0;
      break;
    
    case RUN_FLUSH:
      runFlush();
      currentState = MAIN_MENU;
      menuIndex = 0;
      break;

    case VIEW_SETTINGS:
      viewSettings();
      currentState = MAIN_MENU;
      menuIndex = 0;
      break;

    case RUN_CAL_FULL:
      runCalibration();
      currentState = MAIN_MENU;
      menuIndex = 0;
      break;

    case RUN_CAL_PROBE:
      runCalProbe();
      currentState = CAL_MENU;
      break;

    case RUN_CAL_PUMP:
      runCalPump();
      currentState = CAL_MENU;
      break;
  }
}