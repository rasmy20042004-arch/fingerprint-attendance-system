# Variant A — AS608 Connected to PIC16F877A

## Architecture

`AS608 fingerprint sensor → PIC16F877A → Arduino Mega 2560`

The PIC communicates with the AS608 using a software UART and communicates with the Arduino Mega using its hardware UART. The Mega provides the TFT/touch interface, employee database, SD logging, and attendance UI.

## Main connections

- AS608 ↔ PIC16F877A: RD3/RD2 software UART at 9600 baud.
- PIC16F877A ↔ Mega 2560: RC6/RC7 ↔ Serial2 at 2400 baud.
- DS3231, switches, LEDs, and buzzer are handled by the PIC.
- The AS608 must first be changed from 57600 baud to 9600 baud using the included one-time migration firmware.

## Important PIC reset wiring

**PIC16F877A MCLR/RESET physical pin 1 must be connected to +5 V through a 10 kΩ pull-up resistor. Do not leave MCLR floating.**

Also connect both VDD pins to +5 V, both VSS pins to GND, and use a common ground between the PIC, Mega, sensor, RTC, and peripherals.

## Files

- `PIC16F877A_AS608_Controller.c` — PIC firmware controlling the fingerprint sensor and peripherals.
- `Mega2560_Attendance_Terminal.ino` — Mega TFT/UI/SD attendance terminal.
- `AS608_Baud_57600_to_9600_ONE_TIME.c` — one-time sensor baud-rate migration utility.

See `../../hardware/variant-a-pinouts.txt` for the full pinout.
