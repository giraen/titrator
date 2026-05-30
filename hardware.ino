// ==========================================
// BUTTON DEBOUNCING
// ==========================================
struct Button {
  byte pin;
  bool lastRaw;
  bool stable;
  unsigned long timer;
};

Button buttons[] = {
  // pin, last raw reading, last stable state, timer
  { UP_PIN,  HIGH, HIGH, 0 },
  { DN_PIN,  HIGH, HIGH, 0 },
  { SEL_PIN, HIGH, HIGH, 0 },
};

const byte NUM_BUTTONS = 3;
const unsigned long DEBOUNCE_MS = 50;

void beep() {
  digitalWrite(SOUND_PIN, HIGH);
  delay(30);
  digitalWrite(SOUND_PIN, LOW);
}

int getButton() {
  int btn = 0;

  for (byte i = 0; i < NUM_BUTTONS; i++) {
    Button& b = buttons[i];
    bool raw = digitalRead(b.pin);

    if (raw != b.lastRaw) b.timer = millis();

    if ((millis() - b.timer) > DEBOUNCE_MS) {
      if (raw != b.stable) {
        b.stable = raw;
        if (b.stable == LOW) {
          btn = i + 1;
          beep();
        }
      }
    }

    b.lastRaw = raw;
  }

  return btn;
}

void manualStopISR() {
  phSpike = true;
}

bool runPumpTimer(unsigned long duration_ms) {
  unsigned long startTime = millis();
  analogWrite(PUMP_SWITCH_PIN, PUMP_SPEED);

  // Keep looping until the time runs out
  while ((millis() - startTime) < duration_ms) {
    
    // EMERGENCY STOP CHECK: If user presses SEL
    if (digitalRead(SEL_PIN) == LOW) {
      analogWrite(PUMP_SWITCH_PIN, 0); // Kill pump instantly
      
      displayMessage("- SYSTEM ABORT -", "Pump Halted!");
      delay(2000);
      
      // Wait for the user to let go of the button
      while(digitalRead(SEL_PIN) == LOW); 
      return false;
    }
  }

  // Normal stop
  digitalWrite(PUMP_SWITCH_PIN, LOW); 
  return true;
}

void primeTube() {
  displayMessage("Priming 5s...", "SEL to Abort");
  runPumpTimer(5000);
}

void readPh() {
  unsigned long current_time = millis();

  if (current_time - previous_time >= SAMPLE_INTERVAL) {
    float raw_voltage = analogRead(PH_SENSOR_PIN) * (5.0 / 1023.0);
    smoothed_voltage = (ALPHA * raw_voltage) + ((1.0 - ALPHA) * smoothed_voltage);
    float current_pH = 7.0 - ((smoothed_voltage - VOLTAGE_PH7) / PH_STEP);

    float delta_time_sec = (current_time - previous_time) / 1000.0;
    float derivative = (current_pH - previous_pH) / delta_time_sec;

    if (derivative >= SPIKE_THRESHOLD) {
      spike_counter++;
      if (spike_counter >= REQUIRED_CONSECUTIVE_SPIKES) phSpike = true;
    } else {
      spike_counter = 0;
    }

    previous_pH = current_pH;
    previous_time = current_time;
  }
}

void displayMessage(String line1, String line2) {
  if (line1 == lastDisplayedLine1 && line2 == lastDisplayedLine2) {
    return;
  }

  lastDisplayedLine1 = line1;
  lastDisplayedLine2 = line2;

  // CLEAR LCD
  while (line1.length() < 16) line1 += " ";
  while (line2.length() < 16) line2 += " ";

  // Print screen content
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16)); 
  
  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16));
}

SelResult selPressOrHold() {
  unsigned long pressTime = 0;

  // Drain any lingering press from a previous state
  while (digitalRead(SEL_PIN) == LOW) { delay(10); }
  delay(100);

  // Block until a fresh press
  while (digitalRead(SEL_PIN) == HIGH) { delay(10); }

  // Time the hold duration
  pressTime = millis();
  beep();
  while (digitalRead(SEL_PIN) == LOW) { delay(10); }

  return (millis() - pressTime) < 1000 ? SEL_SHORT : SEL_HOLD;
}

bool waitForConfirmation(String line1, String line2) {
  displayMessage(line1, line2);

  if (selPressOrHold() == SEL_HOLD) {
    displayMessage("Operation", "Cancelled!");
    delay(1500);
    return false;
  }

  displayMessage("Starting...", "");
  delay(500);
  return true;
}