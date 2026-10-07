/*
  Fingerprint Attendance System - PIC <-> Arduino Mega Serial Diagnostic

  Arduino Mega 2560:
    RX2 pin 17 <- PIC RC6/TX physical pin 25
    TX2 pin 16 -> PIC RC7/RX physical pin 26
    GND <-> GND

  Baud: 9600

  This sketch is a transparent USB <-> PIC bridge.
  Anything typed in Serial Monitor is sent to the PIC.
  Anything received from the PIC is printed to Serial Monitor.
*/

#define PIC_BAUD 9600

String rxLine;

void setup() {
  Serial.begin(115200);
  Serial2.begin(PIC_BAUD);
  delay(300);

  Serial.println(F("========================================"));
  Serial.println(F("PIC <-> MEGA SERIAL TEST"));
  Serial.println(F("Serial2 RX2=17, TX2=16 @ 9600"));
  Serial.println(F("Common GND is REQUIRED."));
  Serial.println(F("Try typing: PING"));
  Serial.println(F("========================================"));

  Serial2.print("PING\r\n");
}

void loop() {
  // PIC -> PC
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    Serial.write(c);
  }

  // PC -> PIC
  while (Serial.available()) {
    char c = (char)Serial.read();
    Serial2.write(c);
  }
}
