@echo off
title Smart Attendance Extractor

cd /d "C:\Users\user\Desktop\attendance_exports"

echo ========================================
echo     SMART ATTENDANCE EXTRACTOR
echo ========================================
echo.

py -c "import serial" >nul 2>nul

if errorlevel 1 (
    echo PySerial is not installed.
    echo Installing PySerial...
    echo.

    py -m pip install pyserial

    echo.
    echo Installation finished.
    echo.
)

echo Starting attendance extractor...
echo.

py "C:\Users\user\Desktop\attendance_exports\attendance_extractor.py"

echo.
echo Program closed.
pause