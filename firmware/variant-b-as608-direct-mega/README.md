# Variant B — AS608 Connected Directly to Arduino Mega

## Architecture

`AS608 fingerprint sensor → Arduino Mega 2560`

`PIC16F877A → Arduino Mega 2560` for RTC, physical switches, indicator LEDs, and buzzer control.

In this version the Arduino Mega performs the fingerprint-library operations directly instead of routing fingerprint communication through the PIC.

## Main connections

- AS608 ↔ Mega 2560 Serial3 at 57600 baud.
  - Mega TX3: pin 14 → AS608 RX.
  - Mega RX3: pin 15 ← AS608 TX.
- PIC16F877A ↔ Mega 2560 Serial2 at 9600 baud.
  - Mega TX2: pin 16 → PIC RX.
  - Mega RX2: pin 17 ← PIC TX.
- The PIC handles DS3231 RTC, switches, LEDs, and buzzer.

## Important PIC reset wiring

**PIC16F877A MCLR/RESET physical pin 1 must be connected to +5 V through a 10 kΩ pull-up resistor. Do not leave MCLR floating.**

Use a common ground between the PIC, Arduino Mega, AS608, RTC, display, and other peripherals.

## Files

- `Mega2560_Direct_AS608_Attendance.ino` — attendance firmware with AS608 directly on Mega Serial3.
- `PIC16F877A_Peripheral_Controller.c` — PIC companion firmware for RTC/buttons/LED/buzzer functions.

## Arduino libraries

This version uses libraries including MCUFRIEND_kbv, Adafruit_GFX, TouchScreen, Adafruit_Fingerprint, RTClib, EEPROM, Wire, and SdFat.
