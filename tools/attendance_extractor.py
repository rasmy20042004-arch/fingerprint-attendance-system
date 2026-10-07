import serial
import time
from pathlib import Path
from datetime import datetime

# ============================================================
# CONFIGURATION
# ============================================================

COM_PORT = "COM10"
BAUD_RATE = 115200

OUTPUT_FOLDER = Path("attendance_exports")


# ============================================================
# CONNECT TO ARDUINO
# ============================================================

def connect_arduino():
    print(f"Connecting to Arduino Mega on {COM_PORT}...")

    try:
        ser = serial.Serial(
            COM_PORT,
            BAUD_RATE,
            timeout=1
        )

        # Arduino may reset when serial opens
        time.sleep(2)

        ser.reset_input_buffer()

        print("CONNECTED")
        print()

        return ser

    except serial.SerialException as e:
        print("ERROR: Could not open COM10")
        print(e)
        return None


# ============================================================
# SEND COMMAND
# ============================================================

def send_command(ser, command):
    ser.write((command.strip() + "\n").encode("utf-8"))
    ser.flush()

    print(f">> {command}")


# ============================================================
# DOWNLOAD ATTENDANCE CSV
# ============================================================

def download_attendance_csv(ser):
    print()
    print("Requesting ATTEND.CSV...")
    print()

    ser.reset_input_buffer()

    send_command(
        ser,
        "FETCH_CSV"
    )

    csv_lines = []

    receiving = False

    start_time = time.time()

    while True:

        if time.time() - start_time > 30:
            print("ERROR: CSV download timeout")
            return None

        if ser.in_waiting > 0:

            line = ser.readline().decode(
                "utf-8",
                errors="ignore"
            ).strip()

            if not line:
                continue

            print(line)

            if line == "CSV_BEGIN":
                receiving = True
                csv_lines = []
                continue

            if line == "CSV_END":
                if receiving:
                    print()
                    print("CSV DOWNLOAD COMPLETE")
                    break

            if receiving:
                csv_lines.append(line)

    if not csv_lines:
        print("No CSV data received")
        return None

    OUTPUT_FOLDER.mkdir(
        parents=True,
        exist_ok=True
    )

    timestamp = datetime.now().strftime(
        "%Y-%m-%d_%H-%M-%S"
    )

    filename = (
        OUTPUT_FOLDER /
        f"ATTEND_{timestamp}.csv"
    )

    with open(
        filename,
        "w",
        encoding="utf-8",
        newline=""
    ) as file:

        for line in csv_lines:
            file.write(line + "\n")

    print()
    print("================================")
    print("FILE SAVED")
    print(filename.resolve())
    print("================================")

    return filename


# ============================================================
# SYSTEM STATUS
# ============================================================

def get_status(ser):
    print()
    print("Getting system status...")
    print()

    ser.reset_input_buffer()

    send_command(
        ser,
        "STATUS"
    )

    start = time.time()

    while time.time() - start < 3:

        if ser.in_waiting:

            line = ser.readline().decode(
                "utf-8",
                errors="ignore"
            ).strip()

            if line:
                print(line)


# ============================================================
# USER LIST
# ============================================================

def get_users(ser):
    print()
    print("Getting stored users...")
    print()

    ser.reset_input_buffer()

    send_command(
        ser,
        "LIST"
    )

    start = time.time()

    while time.time() - start < 3:

        if ser.in_waiting:

            line = ser.readline().decode(
                "utf-8",
                errors="ignore"
            ).strip()

            if line:
                print(line)


# ============================================================
# RTC
# ============================================================

def set_rtc(ser):
    date_text = input(
        "Enter date YYYY-MM-DD: "
    ).strip()

    time_text = input(
        "Enter time HH:MM:SS: "
    ).strip()

    command = (
        f"SET:{date_text} {time_text}"
    )

    send_command(
        ser,
        command
    )

    time.sleep(1)

    while ser.in_waiting:

        line = ser.readline().decode(
            "utf-8",
            errors="ignore"
        ).strip()

        if line:
            print(line)


# ============================================================
# SEND CUSTOM COMMAND
# ============================================================

def custom_command(ser):
    cmd = input(
        "Enter Arduino command: "
    ).strip()

    if not cmd:
        return

    ser.reset_input_buffer()

    send_command(
        ser,
        cmd
    )

    start = time.time()

    while time.time() - start < 3:

        if ser.in_waiting:

            line = ser.readline().decode(
                "utf-8",
                errors="ignore"
            ).strip()

            if line:
                print(line)


# ============================================================
# MAIN
# ============================================================

def main():

    ser = connect_arduino()

    if ser is None:
        return

    try:

        while True:

            print()
            print("======================================")
            print(" SMART ATTENDANCE - COM10")
            print("======================================")
            print("1 - Download ATTEND.CSV")
            print("2 - System status")
            print("3 - Show stored users")
            print("4 - Set RTC date/time")
            print("5 - Send custom command")
            print("6 - Exit")
            print("======================================")

            choice = input(
                "Select option: "
            ).strip()

            if choice == "1":

                download_attendance_csv(
                    ser
                )

            elif choice == "2":

                get_status(
                    ser
                )

            elif choice == "3":

                get_users(
                    ser
                )

            elif choice == "4":

                set_rtc(
                    ser
                )

            elif choice == "5":

                custom_command(
                    ser
                )

            elif choice == "6":

                print("Closing...")
                break

            else:

                print("Invalid option")

    except KeyboardInterrupt:

        print()
        print("Stopped")

    finally:

        if ser.is_open:
            ser.close()

        print("COM10 closed")


# ============================================================
# START
# ============================================================

if __name__ == "__main__":
    main()