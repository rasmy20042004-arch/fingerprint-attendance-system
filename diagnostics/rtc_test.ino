/*
  Fingerprint Attendance System - RTC/PIC Diagnostic
  Board: Arduino Mega 2560

  Final project architecture:
    DS3231 is connected to PIC16F877A.
    Mega gets date/time from the PIC through Serial2.

  PIC <-> Mega:
    PIC RC6/TX pin 25 -> Mega RX2 pin 17
    PIC RC7/RX pin 26 <- Mega TX2 pin 16
    Baud: 9600

  This sketch:
    - Requests time from PIC using GET_TIME
    - Displays TIME packets
    - Lets you forward SET_DATE and SET_TIME commands from Serial Monitor

  Serial Monitor examples:
    GET_TIME
    SET_DATE,2026-10-07
    SET_TIME,18:30:00
*/

#define PIC_BAUD 9600

String picLine;
unsigned long lastRequest = 0;

void sendPIC(const String &s) {
  Serial2.print(s);
  Serial2.print("\r\n");
  Serial.print(F("TX -> PIC: "));
  Serial.println(s);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(PIC_BAUD);
  delay(300);

  Serial.println(F("========================================"));
  Serial.println(F("RTC / PIC DIAGNOSTIC"));
  Serial.println(F("Mega RX2=17, TX2=16 @ 9600"));
  Serial.println(F("Type GET_TIME, SET_DATE,... or SET_TIME,..."));
  Serial.println(F("========================================"));

  sendPIC("GET_TIME");
}

void loop() {
  // Read PIC responses
  while (Serial2.available()) {
    char c = (char)Serial2.read();

    if (c == '\r') continue;

    if (c == '\n') {
      if (picLine.length()) {
        Serial.print(F("PIC -> MEGA: "));
        Serial.println(picLine);

        if (picLine.startsWith("TIME,")) {
          Serial.println(F("RTC packet received correctly."));
        }
        picLine = "";
      }
    } else if (picLine.length() < 80) {
      picLine += c;
    }
  }

  // Forward commands from Serial Monitor to PIC
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length()) sendPIC(cmd);
  }

  // Request time every 2 seconds
  if (millis() - lastRequest >= 2000UL) {
    lastRequest = millis();
    sendPIC("GET_TIME");
  }
}
