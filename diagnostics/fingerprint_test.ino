/*
  Fingerprint Attendance System - AS608 Diagnostic
  Board: Arduino Mega 2560
  Sensor: AS608
  Final project diagnostic wiring:
    AS608 TX -> Mega RX3 pin 15
    AS608 RX -> Mega TX3 pin 14
    AS608 VCC -> 5V
    AS608 GND -> GND
  Baud: 57600

  Library:
    Adafruit Fingerprint Sensor Library
*/

#include <Adafruit_Fingerprint.h>

#define FINGER_BAUD 57600

Adafruit_Fingerprint finger(&Serial3);

void printMenu() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("AS608 FINGERPRINT DIAGNOSTIC"));
  Serial.println(F("Mega Serial3: RX3=15, TX3=14 @ 57600"));
  Serial.println(F("Commands:"));
  Serial.println(F("  V - verify sensor password"));
  Serial.println(F("  C - show stored template count"));
  Serial.println(F("  S - scan once"));
  Serial.println(F("  A - automatic scan mode"));
  Serial.println(F("  M - show this menu"));
  Serial.println(F("========================================"));
}

bool verifySensor() {
  bool ok = finger.verifyPassword();
  Serial.println(ok ? F("AS608: ONLINE") : F("AS608: NOT RESPONDING"));
  return ok;
}

void showTemplateCount() {
  if (finger.getTemplateCount() == FINGERPRINT_OK) {
    Serial.print(F("Stored fingerprints: "));
    Serial.println(finger.templateCount);
  } else {
    Serial.println(F("ERROR: Could not read template count."));
  }
}

void scanOnce() {
  int p = finger.getImage();

  if (p == FINGERPRINT_NOFINGER) {
    Serial.println(F("No finger detected."));
    return;
  }

  if (p != FINGERPRINT_OK) {
    Serial.print(F("getImage error code: "));
    Serial.println(p);
    return;
  }

  Serial.println(F("Image captured. Converting..."));

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.print(F("image2Tz error code: "));
    Serial.println(p);
    return;
  }

  p = finger.fingerFastSearch();

  if (p == FINGERPRINT_OK) {
    Serial.print(F("MATCH - ID: "));
    Serial.print(finger.fingerID);
    Serial.print(F("  Confidence: "));
    Serial.println(finger.confidence);
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println(F("Fingerprint not found in database."));
  } else {
    Serial.print(F("Search error code: "));
    Serial.println(p);
  }
}

bool autoScan = false;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial3.begin(FINGER_BAUD);
  finger.begin(FINGER_BAUD);
  delay(250);

  printMenu();

  if (verifySensor()) {
    showTemplateCount();
  }
}

void loop() {
  if (Serial.available()) {
    char c = toupper(Serial.read());

    while (Serial.available()) Serial.read();

    if (c == 'V') verifySensor();
    else if (c == 'C') showTemplateCount();
    else if (c == 'S') scanOnce();
    else if (c == 'A') {
      autoScan = !autoScan;
      Serial.print(F("Automatic scan: "));
      Serial.println(autoScan ? F("ON") : F("OFF"));
    }
    else if (c == 'M') printMenu();
  }

  if (autoScan) {
    int p = finger.getImage();

    if (p == FINGERPRINT_OK) {
      if (finger.image2Tz() == FINGERPRINT_OK &&
          finger.fingerFastSearch() == FINGERPRINT_OK) {
        Serial.print(F("MATCH - ID: "));
        Serial.print(finger.fingerID);
        Serial.print(F("  Confidence: "));
        Serial.println(finger.confidence);
      } else {
        Serial.println(F("Finger detected, but no valid match."));
      }

      while (finger.getImage() != FINGERPRINT_NOFINGER) {
        delay(50);
      }
    }

    delay(80);
  }
}
