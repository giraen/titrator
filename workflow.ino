bool valuesValid() {
  // Check for NaN
  if (isnan(FLOW_RATE_ML_PER_MIN) || isnan(VOLTAGE_PH7) || isnan(VOLTAGE_PH4)) {
    return false;
  }

  // Flow rate must be positive and within reasonable pump limits (e.g., 0 to 500 mL/min)
  if (FLOW_RATE_ML_PER_MIN <= 0.0 || FLOW_RATE_ML_PER_MIN > 500.0) {
    return false;
  }

  // Arduino analog pins can only read between 0V and 5V
  if (VOLTAGE_PH7 <= 0.0 || VOLTAGE_PH7 >= 5.0) {
    return false;
  }
  if (VOLTAGE_PH4 <= 0.0 || VOLTAGE_PH4 >= 5.0) {
    return false;
  }

  // pH 4 and pH 7 cannot have the exact same voltage, or PH_STEP will divide by zero!
  if (abs(VOLTAGE_PH7 - VOLTAGE_PH4) < 0.05) { 
    return false;
  }

  return true;
}

void saveSettings() {
  if (!valuesValid()) {
    displayMessage("Save Failed!", "Bad Values");
    delay(1500);
    return;
  }

  EEPROM.put(0, FLOW_RATE_ML_PER_MIN);
  EEPROM.put(4, VOLTAGE_PH7);
  EEPROM.put(8, VOLTAGE_PH4);
  EEPROM.put(12, 0x5A);
  
  displayMessage("Settings Saved", "To Memory!");
  delay(1500);
}

void runTitration() {
  if (!waitForConfirmation("Start Titration?", "SEL:Go  Hold:Exit")) return;

  displayMessage("Titration start", "");
  primeTube();
  
  spike_counter = 0;
  
  // Get an initial reading before turning on motors
  smoothed_voltage = analogRead(PH_SENSOR_PIN) * (5.0 / 1023.0);
  previous_pH = 7.0 - ((smoothed_voltage - VOLTAGE_PH7) / PH_STEP);

  // Arm the emergency stop interrupt
  attachInterrupt(digitalPinToInterrupt(SEL_PIN), manualStopISR, FALLING);
  delay(10);
  phSpike = false;
  
  unsigned long startTime = millis();
  analogWrite(PUMP_SWITCH_PIN, PUMP_SPEED);
  analogWrite(MAGNET_SWITCH_PIN, FAN_SPEED);

  // The Chemistry Loop
  while(!phSpike) {
    readPh();
    float current_pH = 7.0 - ((smoothed_voltage - VOLTAGE_PH7) / PH_STEP);

    char phStr[8];
    dtostrf(current_pH, 4, 2, phStr);
    char line2[17];
    snprintf(line2, sizeof(line2), "pH: %s", phStr);
    displayMessage("Titrating...", line2);
  }
  
  analogWrite(PUMP_SWITCH_PIN, 0);
  analogWrite(MAGNET_SWITCH_PIN, 0);
  detachInterrupt(digitalPinToInterrupt(SEL_PIN));

  unsigned long endTime = millis();
  unsigned long runTimeMs = endTime - startTime;

  float mlTransferred = (FLOW_RATE_ML_PER_MIN / 60000.0) * runTimeMs;

  char mlStr[8];
  dtostrf(mlTransferred, 4, 2, mlStr);
  char line2[17];
  snprintf(line2, sizeof(line2), "%s mL Added", mlStr);
  displayMessage("Equivalence Met", line2);

  delay(6000);
}

void runCalProbe() {
  if (!waitForConfirmation("Calibrate Probe?", "SEL:Go  Hold:Exit")) return;

  // --- pH 7 Calibration ---
  displayMessage("Place in pH 7.0", "Press SEL to set");
  delay(1000);

  while (true) {
    float raw_voltage = analogRead(PH_SENSOR_PIN) * (5.0 / 1023.0);
    smoothed_voltage = (ALPHA * raw_voltage) + ((1.0 - ALPHA) * smoothed_voltage);


    char voltStr[8];
    dtostrf(smoothed_voltage, 4, 2, voltStr);
    char line2[17];
    snprintf(line2, sizeof(line2), "Volts: %s", voltStr);
    displayMessage("Reading pH 7...", line2);
    delay(100);

    if (digitalRead(SEL_PIN) == LOW) {
      if (selPressOrHold() == SEL_SHORT) break;
    }
  }
  VOLTAGE_PH7 = smoothed_voltage; 

  char savedVoltStr[8];
  dtostrf(VOLTAGE_PH7, 4, 2, savedVoltStr);
  char savedLine2[17];
  snprintf(savedLine2, sizeof(savedLine2), "%s V", savedVoltStr);
  displayMessage("Saved pH 7", savedLine2);
  delay(1000);

  // --- pH 4 Calibration ---
  displayMessage("Place in pH 4.0", "Press SEL to set");
  delay(1000);

  while (true) {
    float raw_voltage = analogRead(PH_SENSOR_PIN) * (5.0 / 1023.0);
    smoothed_voltage = (ALPHA * raw_voltage) + ((1.0 - ALPHA) * smoothed_voltage);

    char voltStr[8];
    dtostrf(smoothed_voltage, 4, 2, voltStr);
    char line2[17];
    snprintf(line2, sizeof(line2), "Volts: %s", voltStr);
    displayMessage("Reading pH 4...", line2);
    delay(100);

    if (digitalRead(SEL_PIN) == LOW) {
      if (selPressOrHold() == SEL_SHORT) break;
    }
  }

  VOLTAGE_PH4 = smoothed_voltage; 
  PH_STEP = (VOLTAGE_PH4 - VOLTAGE_PH7) / (7.0 - 4.0);

  char savedVoltStr4[8];
  dtostrf(VOLTAGE_PH4, 4, 2, savedVoltStr4);
  char savedLine2_4[17];
  snprintf(savedLine2_4, sizeof(savedLine2_4), "%s V", savedVoltStr4);
  displayMessage("Saved pH 4", savedLine2_4);

  // Save to permanent memory!
  saveSettings(); 
}

void runCalPump() {
  if (!waitForConfirmation("1. Prime Tube?", "SEL:Go  Hold:Exit")) return;
  primeTube();

  if (!waitForConfirmation("2. Run 60 Secs?", "SEL:Go  Hold:Exit")) return;
  
  displayMessage("Catch Liquid!", "SEL to Abort");
  
  // ==========================================
  // THE 60-SECOND EMERGENCY STOP FIX:
  // If the user hits SEL here, the pump dies and it returns to the menu!
  // ==========================================
  if (!runPumpTimer(CALIBRATION_TIME_MS)) {
    return; 
  }

  if (!waitForConfirmation("3. Enter Volume?", "SEL:Go  Hold:Exit")) return;

  float displayVolume = FLOW_RATE_ML_PER_MIN; 
  unsigned long holdTimer = 0;
  bool editing = true;

  while(editing) {
    char volStr[8];
    dtostrf(displayVolume, 4, 1, volStr);
    char line2[17];
    snprintf(line2, sizeof(line2), "%s mL", volStr);
    displayMessage("Output mL:", line2);

    bool up = digitalRead(UP_PIN);
    bool dn = digitalRead(DN_PIN);
    bool sel = digitalRead(SEL_PIN);

    if (sel == LOW) {
      if (selPressOrHold() == SEL_SHORT) {
        FLOW_RATE_ML_PER_MIN = displayVolume; 
        editing = false; 
      }
    } else if (up == LOW) {
      displayVolume += 0.1;
      if (holdTimer == 0) holdTimer = millis(); 
      if (millis() - holdTimer > 500) delay(50); else delay(250);
    } else if (dn == LOW) {
      displayVolume -= 0.1;
      if (displayVolume < 0.0) displayVolume = 0.0; 
      if (holdTimer == 0) holdTimer = millis(); 
      if (millis() - holdTimer > 500) delay(50); else delay(250);
    }
    else { holdTimer = 0; }
  }

  char finalVolStr[8];
  dtostrf(FLOW_RATE_ML_PER_MIN, 4, 1, finalVolStr);
  char savedLine2[17];
  snprintf(savedLine2, sizeof(savedLine2), "%s mL/m", finalVolStr);
  displayMessage("Saved Flow Rate:", savedLine2);
  delay(1500);

  // Save to permanent memory!
  saveSettings();
}

void runCalibration() {
  if (!waitForConfirmation("Full Calibrate?", "SEL:Go  Hold:Exit")) return;
  runCalProbe();
  runCalPump();
  displayMessage("Full Calibration", "Complete!");
  delay(2000);
}

void runFlush() {
  displayMessage("Hold UP to Flush", "Press SEL to End");

  while (digitalRead(SEL_PIN) == LOW) { delay(10); }
  delay(50);

  while (digitalRead(SEL_PIN) == HIGH) {
    if (digitalRead(UP_PIN) == LOW) {
      digitalWrite(PUMP_SWITCH_PIN, HIGH);
    } else {
      digitalWrite(PUMP_SWITCH_PIN, LOW);
    }
    delay(10);
  }
  digitalWrite(PUMP_SWITCH_PIN, LOW);
  delay(500);
}

void viewSettings() {
  char flowStr[8], stepStr[8];
  dtostrf(FLOW_RATE_ML_PER_MIN, 4, 1, flowStr);
  dtostrf(PH_STEP, 5, 3, stepStr);

  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "Flow: %s", flowStr);
  snprintf(line2, sizeof(line2), "Step: %s", stepStr);
  
  displayMessage(line1, line2);
  delay(4000);
}