/*
 * ============================================================================
 * ARDUINO MEGA 2560 - FINAL FULL BIOMETRIC ATTENDANCE TERMINAL
 * ============================================================================
 *
 * FINAL architecture:
 *   Arduino Mega <-> PIC16F877A MAIN/HARDWARE UART
 *   AS608        <-> PIC16F877A SOFTWARE UART
 *   RTC / Buttons / LEDs / buzzer -> PIC16F877A
 *
 * PIC RC6 pin 25 TX -> Mega RX2 pin 17
 * PIC RC7 pin 26 RX <- Mega TX2 pin 16
 * PIC <-> Mega baud = 2400
 *
 * AS608 is connected to PIC RD3/RD2 at 9600 baud.
 *
 * Mega:
 *   3.5" MCUFRIEND TFT
 *   resistive touch
 *   employee name EEPROM database
 *   attendance logic
 *   SD ATTEND.CSV
 *   recent logs
 *   PC Serial commands
 *
 * Libraries:
 *   MCUFRIEND_kbv
 *   Adafruit_GFX
 *   TouchScreen
 *   EEPROM
 *   SdFat
 */

#include <MCUFRIEND_kbv.h>
#include <Adafruit_GFX.h>
#include <TouchScreen.h>
#include <EEPROM.h>
#include <SdFat.h>

MCUFRIEND_kbv tft;

#define SCREEN_W 480
#define SCREEN_H 320

#define XP 7
#define XM A1
#define YP A2
#define YM 6

TouchScreen ts(XP, YP, XM, YM, 300);

#define MINPRESSURE 50
#define MAXPRESSURE 1000

#define UI_BG       0x000A
#define UI_BG2      0x002B
#define UI_CARD     0x084F
#define UI_CARD2    0x108F
#define UI_BORDER   0x1B9F
#define UI_CYAN     0x07FF
#define UI_BLUE     0x049F
#define UI_WHITE    0xFFFF
#define UI_TEXT     0xD71C
#define UI_TEXT2    0x9D15
#define UI_MUTED    0x630C
#define UI_GREEN    0x07E0
#define UI_GREEN2   0x47F0
#define UI_RED      0xF800
#define UI_ORANGE   0xFD20
#define UI_YELLOW   0xFFE0
#define UI_MAGENTA  0xF81F
#define UI_PURPLE   0xA81F

#define PIC_BAUD 2400

char picLine[96];
uint8_t picLinePos = 0;

bool picLinkReady = false;
bool fingerprintReady = false;
bool rtcReady = false;

unsigned long lastPicPacket = 0;

// PIC connection reporting state
bool previousPicLinkState = false;
bool picStateInitialized = false;

char picDate[11] = "----------";
char picTime[9]  = "--:--:--";

#define SD_CS_PIN   10
#define SD_MOSI_PIN 11
#define SD_MISO_PIN 12
#define SD_SCK_PIN  13

SoftSpiDriver<SD_MISO_PIN, SD_MOSI_PIN, SD_SCK_PIN> sdSoftSpi;

#define SD_CONFIG \
  SdSpiConfig( \
    SD_CS_PIN, \
    SHARED_SPI, \
    SD_SCK_MHZ(10), \
    &sdSoftSpi \
  )

SdFs sd;
bool sdReady = false;
const char ATTENDANCE_CSV[] = "ATTEND.CSV";

struct TouchCalibration
{
  uint16_t magic;
  int leftRaw;
  int rightRaw;
  int topRaw;
  int bottomRaw;
};

TouchCalibration touchCal;

#define TOUCH_MAGIC 0xA55A
#define CAL_X1 30
#define CAL_X2 450
#define CAL_Y1 30
#define CAL_Y2 290

#define DB_MAGIC 0xB10F
#define DB_VERSION 5

const int DB_HEADER_ADDR = 64;
const int DB_START_ADDR  = 80;
const uint8_t MAX_STORED_USERS = 50;
const uint8_t MAX_NAME_LEN = 18;

struct DatabaseHeader
{
  uint16_t magic;
  uint8_t version;
};

struct StoredEmployee
{
  uint8_t valid;
  uint16_t id;
  char name[MAX_NAME_LEN + 1];
  uint8_t checkedIn;
  uint32_t lastDateKey;
};

DatabaseHeader dbHeader;

struct AttendanceRecord
{
  uint16_t id;
  char name[MAX_NAME_LEN + 1];
  char date[11];
  char time[9];
  char status[11];
  bool valid;
};

#define MAX_LOGS 8
AttendanceRecord logs[MAX_LOGS];

enum Page
{
  PAGE_HOME,
  PAGE_ENROLL,
  PAGE_LOGS,
  PAGE_SETTINGS
};

enum AttendanceMode
{
  MODE_AUTO,
  MODE_IN,
  MODE_OUT
};

Page currentPage = PAGE_HOME;
AttendanceMode attendanceMode = MODE_AUTO;

uint16_t currentFingerID = 0;
uint16_t currentConfidence = 0;
char currentName[MAX_NAME_LEN + 1] = "---";
char currentStatus[11] = "READY";
char currentScanTime[9] = "--:--:--";

uint16_t totalToday = 0;
uint32_t currentDayKey = 0;

uint16_t enrollID = 1;
String pendingEnrollName = "";
bool deleteArmed = false;
bool enrollBusy = false;

int lateStartMinutes = 8 * 60 + 30;
int lateEndMinutes   = 9 * 60 + 30;

unsigned long resultHoldUntil = 0;
unsigned long lastClockDraw = 0;

/* ============================= UI HELPERS ============================== */

void uiText(int x, int y, const char *value, uint16_t color, uint8_t size)
{
  tft.setCursor(x, y);
  tft.setTextColor(color);
  tft.setTextSize(size);
  tft.print(value);
}

void panel(int x, int y, int w, int h, uint16_t border)
{
  tft.fillRoundRect(x + 3, y + 3, w, h, 8, UI_BG2);
  tft.fillRoundRect(x, y, w, h, 8, UI_CARD);
  tft.drawRoundRect(x, y, w, h, 8, border);
}

void pill(int x, int y, int w, const char *value, uint16_t color)
{
  tft.fillRoundRect(x, y, w, 21, 8, UI_CARD2);
  tft.drawRoundRect(x, y, w, 21, 8, color);
  uiText(x + 8, y + 6, value, UI_WHITE, 1);
}

void drawFingerprintIcon(int cx, int cy, uint16_t color)
{
  for (int r = 10; r <= 38; r += 7)
    tft.drawCircle(cx, cy, r, color);

  tft.fillRect(cx - 48, cy - 2, 17, 45, UI_BG);
  tft.fillRect(cx + 31, cy + 4, 17, 40, UI_BG);

  tft.drawFastVLine(cx, cy, 40, color);
  tft.drawFastVLine(cx - 7, cy + 8, 28, color);
  tft.drawFastVLine(cx + 7, cy + 8, 28, color);
}

/* ============================= TOUCH ============================== */

bool getRawTouch(int &rawX, int &rawY, int &pressure)
{
  TSPoint p = ts.getPoint();

  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);

  pressure = p.z;

  if (pressure < MINPRESSURE || pressure > MAXPRESSURE)
    return false;

  rawX = p.x;
  rawY = p.y;
  return true;
}

bool getTouch(int &x, int &y)
{
  int rx, ry, pressure;

  if (!getRawTouch(rx, ry, pressure))
    return false;

  x = map(rx, touchCal.leftRaw, touchCal.rightRaw, CAL_X1, CAL_X2);
  y = map(ry, touchCal.topRaw, touchCal.bottomRaw, CAL_Y1, CAL_Y2);

  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);

  return true;
}

void waitTouchRelease()
{
  int x, y, p;
  unsigned long start = millis();

  while (millis() - start < 1200)
  {
    if (!getRawTouch(x, y, p))
    {
      delay(35);
      return;
    }
    delay(10);
  }
}

void calibrationTarget(int x, int y)
{
  tft.fillScreen(UI_BG);

  uiText(120, 105, "TOUCH CALIBRATION", UI_CYAN, 2);
  uiText(109, 137, "Touch center of target", UI_TEXT, 1);

  tft.drawCircle(x, y, 12, UI_WHITE);
  tft.drawCircle(x, y, 6, UI_CYAN);
  tft.drawFastHLine(x - 17, y, 34, UI_CYAN);
  tft.drawFastVLine(x, y - 17, 34, UI_CYAN);
}

void captureTouch(int &rawX, int &rawY)
{
  int p;
  long sx = 0, sy = 0;
  int samples = 0;

  while (samples < 12)
  {
    int x, y;

    if (getRawTouch(x, y, p))
    {
      sx += x;
      sy += y;
      samples++;
      delay(20);
    }
  }

  rawX = sx / samples;
  rawY = sy / samples;

  waitTouchRelease();
  delay(250);
}

void calibrateTouch()
{
  int tlX, tlY, trX, trY, blX, blY, brX, brY;

  calibrationTarget(CAL_X1, CAL_Y1);
  captureTouch(tlX, tlY);

  calibrationTarget(CAL_X2, CAL_Y1);
  captureTouch(trX, trY);

  calibrationTarget(CAL_X1, CAL_Y2);
  captureTouch(blX, blY);

  calibrationTarget(CAL_X2, CAL_Y2);
  captureTouch(brX, brY);

  touchCal.magic = TOUCH_MAGIC;
  touchCal.leftRaw = (tlX + blX) / 2;
  touchCal.rightRaw = (trX + brX) / 2;
  touchCal.topRaw = (tlY + trY) / 2;
  touchCal.bottomRaw = (blY + brY) / 2;

  EEPROM.put(0, touchCal);

  tft.fillScreen(UI_BG);
  uiText(118, 120, "CALIBRATION SAVED", UI_GREEN, 2);
  delay(1000);
}

void loadTouchCalibration()
{
  EEPROM.get(0, touchCal);

  if (touchCal.magic != TOUCH_MAGIC)
    calibrateTouch();
}

/* ============================= DATABASE ============================== */

int employeeAddress(uint8_t slot)
{
  return DB_START_ADDR + (int)slot * (int)sizeof(StoredEmployee);
}

void initializeDatabase()
{
  EEPROM.get(DB_HEADER_ADDR, dbHeader);

  if (dbHeader.magic == DB_MAGIC && dbHeader.version == DB_VERSION)
    return;

  dbHeader.magic = DB_MAGIC;
  dbHeader.version = DB_VERSION;
  EEPROM.put(DB_HEADER_ADDR, dbHeader);

  StoredEmployee blank;
  memset(&blank, 0, sizeof(blank));

  for (uint8_t i = 0; i < MAX_STORED_USERS; i++)
    EEPROM.put(employeeAddress(i), blank);
}

int findEmployeeSlot(uint16_t id)
{
  StoredEmployee e;

  for (uint8_t i = 0; i < MAX_STORED_USERS; i++)
  {
    EEPROM.get(employeeAddress(i), e);

    if (e.valid && e.id == id)
      return i;
  }

  return -1;
}

int findFreeEmployeeSlot()
{
  StoredEmployee e;

  for (uint8_t i = 0; i < MAX_STORED_USERS; i++)
  {
    EEPROM.get(employeeAddress(i), e);

    if (!e.valid)
      return i;
  }

  return -1;
}

bool loadEmployee(uint16_t id, StoredEmployee &e)
{
  int slot = findEmployeeSlot(id);

  if (slot < 0)
    return false;

  EEPROM.get(employeeAddress(slot), e);
  return true;
}

bool saveEmployee(uint16_t id, const String &name)
{
  int slot = findEmployeeSlot(id);
  StoredEmployee e;

  if (slot < 0)
    slot = findFreeEmployeeSlot();

  if (slot < 0)
    return false;

  memset(&e, 0, sizeof(e));

  e.valid = 1;
  e.id = id;
  name.substring(0, MAX_NAME_LEN).toCharArray(e.name, sizeof(e.name));
  e.checkedIn = 0;
  e.lastDateKey = 0;

  EEPROM.put(employeeAddress(slot), e);
  return true;
}

bool deleteEmployee(uint16_t id)
{
  int slot = findEmployeeSlot(id);

  if (slot < 0)
    return false;

  StoredEmployee blank;
  memset(&blank, 0, sizeof(blank));
  EEPROM.put(employeeAddress(slot), blank);
  return true;
}

String employeeName(uint16_t id)
{
  StoredEmployee e;

  if (loadEmployee(id, e))
    return String(e.name);

  return String("ID ") + String(id);
}

uint16_t countUsers()
{
  uint16_t count = 0;
  StoredEmployee e;

  for (uint8_t i = 0; i < MAX_STORED_USERS; i++)
  {
    EEPROM.get(employeeAddress(i), e);
    if (e.valid) count++;
  }

  return count;
}

/* ============================= ATTENDANCE ============================== */

uint32_t dateKeyFromPIC()
{
  if (!rtcReady || strlen(picDate) != 10)
    return 0;

  uint16_t year = (uint16_t)picDate[0] - '0';
  year = year * 10 + (picDate[1] - '0');
  year = year * 10 + (picDate[2] - '0');
  year = year * 10 + (picDate[3] - '0');

  uint8_t month =
    (uint8_t)((picDate[5] - '0') * 10 + (picDate[6] - '0'));

  uint8_t day =
    (uint8_t)((picDate[8] - '0') * 10 + (picDate[9] - '0'));

  return (uint32_t)year * 10000UL +
         (uint32_t)month * 100UL +
         (uint32_t)day;
}

int minutesFromPICTime()
{
  if (!rtcReady || strlen(picTime) != 8)
    return 0;

  int hour = (picTime[0] - '0') * 10 + (picTime[1] - '0');
  int min  = (picTime[3] - '0') * 10 + (picTime[4] - '0');

  return hour * 60 + min;
}

String decideStatus(uint16_t id)
{
  StoredEmployee e;
  uint32_t today = dateKeyFromPIC();

  if (attendanceMode == MODE_IN)
    return "IN";

  if (attendanceMode == MODE_OUT)
    return "OUT";

  if (!loadEmployee(id, e))
    return "IN";

  if (today != 0 && e.lastDateKey != today)
    e.checkedIn = 0;

  if (e.checkedIn)
    return "OUT";

  int nowMinutes = minutesFromPICTime();

  if (nowMinutes < lateStartMinutes)
    return "IN";

  if (nowMinutes <= lateEndMinutes)
    return "LATE";

  return "VERY LATE";
}

void updatePresence(uint16_t id, const String &status)
{
  StoredEmployee e;
  int slot = findEmployeeSlot(id);

  if (slot < 0)
    return;

  EEPROM.get(employeeAddress(slot), e);

  e.lastDateKey = dateKeyFromPIC();
  e.checkedIn = (status != "OUT") ? 1 : 0;

  EEPROM.put(employeeAddress(slot), e);
}

/* ============================= SD / CSV ============================== */

bool ensureAttendanceCSV()
{
  if (!sdReady)
    return false;

  if (sd.exists(ATTENDANCE_CSV))
    return true;

  FsFile file = sd.open(ATTENDANCE_CSV, O_WRONLY | O_CREAT);

  if (!file)
    return false;

  file.println(F("ID,Name,Date,Time,Status"));
  file.close();
  return true;
}

void initSD()
{
  sdReady = sd.begin(SD_CONFIG);

  if (sdReady)
    sdReady = ensureAttendanceCSV();
}

void appendCSV(
  uint16_t id,
  const String &name,
  const char *date,
  const char *timeText,
  const String &status)
{
  if (!sdReady)
    return;

  FsFile file =
    sd.open(ATTENDANCE_CSV, O_WRONLY | O_CREAT | O_AT_END);

  if (!file)
    return;

  file.print(id);
  file.print(',');
  file.print(name);
  file.print(',');
  file.print(date);
  file.print(',');
  file.print(timeText);
  file.print(',');
  file.println(status);
  file.close();
}

void csvStatus()
{
  Serial.print(F("SD="));
  Serial.println(sdReady ? F("ONLINE") : F("OFFLINE"));

  if (sdReady && sd.exists(ATTENDANCE_CSV))
  {
    FsFile file = sd.open(ATTENDANCE_CSV, O_RDONLY);

    if (file)
    {
      Serial.print(F("ATTEND.CSV bytes="));
      Serial.println((unsigned long)file.fileSize());
      file.close();
    }
  }
}

void sendCSVToPC()
{
  if (!sdReady)
  {
    Serial.println(F("CSV_ERROR: SD OFFLINE"));
    return;
  }

  FsFile file = sd.open(ATTENDANCE_CSV, O_RDONLY);

  if (!file)
  {
    Serial.println(F("CSV_ERROR: FILE NOT FOUND"));
    return;
  }

  Serial.println(F("CSV_BEGIN"));

  int c;
  while ((c = file.read()) >= 0)
    Serial.write((char)c);

  Serial.println(F("CSV_END"));
  file.close();
}

void resetCSV()
{
  if (!sdReady)
    return;

  if (sd.exists(ATTENDANCE_CSV))
    sd.remove(ATTENDANCE_CSV);

  ensureAttendanceCSV();
  Serial.println(F("ATTEND.CSV RESET"));
}

/* ============================= LOGS ============================== */

void clearRecent()
{
  for (uint8_t i = 0; i < MAX_LOGS; i++)
    logs[i].valid = false;

  totalToday = 0;
}

void addAttendance(
  uint16_t id,
  const String &name,
  const String &status)
{
  for (int i = MAX_LOGS - 1; i > 0; i--)
    logs[i] = logs[i - 1];

  logs[0].id = id;
  name.substring(0, MAX_NAME_LEN).toCharArray(
    logs[0].name,
    sizeof(logs[0].name));

  strncpy(logs[0].date, rtcReady ? picDate : "----------", 10);
  logs[0].date[10] = '\0';

  strncpy(logs[0].time, rtcReady ? picTime : "--:--:--", 8);
  logs[0].time[8] = '\0';

  status.toCharArray(logs[0].status, sizeof(logs[0].status));
  logs[0].valid = true;

  totalToday++;

  appendCSV(
    id,
    name,
    logs[0].date,
    logs[0].time,
    status);
}

/* ============================= PIC OUTPUT ============================== */

/*
 * Send command to PIC hardware UART.
 *
 * Characters are deliberately separated by a small gap.  The PIC uses
 * software UART for AS608 at 9600, so the gap prevents a whole Mega command
 * from overrunning the PIC hardware-UART receive FIFO during a short
 * fingerprint transaction.
 */
void sendPICSlow(const char *command)
{
  while (*command)
  {
    Serial2.write((uint8_t)*command++);
    Serial2.flush();
    delay(12);
  }

  Serial2.write('\r');
  Serial2.flush();
  delay(12);

  Serial2.write('\n');
  Serial2.flush();
}

void sendPIC(const String &command)
{
  sendPICSlow(command.c_str());
}

void sendPIC(const char *command)
{
  sendPICSlow(command);
}

/* ============================= TOP BAR / CLOCK ============================== */

void drawTopBar()
{
  tft.fillRect(0, 0, SCREEN_W, 44, UI_BG2);
  tft.drawFastHLine(0, 43, SCREEN_W, UI_CYAN);

  tft.fillCircle(20, 21, 10, UI_CYAN);
  tft.fillCircle(20, 21, 4, UI_BG2);

  uiText(42, 7, "BIOMETRIC ATTENDANCE", UI_WHITE, 2);
  uiText(42, 26, "PIC16F877A + ARDUINO MEGA", UI_TEXT2, 1);

  pill(
    242, 11, 76,
    picLinkReady ? "PIC LINK" : "PIC OFF",
    picLinkReady ? UI_GREEN : UI_RED);

  pill(
    324, 11, 68,
    fingerprintReady ? "AS608" : "FP ERR",
    fingerprintReady ? UI_GREEN : UI_RED);

  pill(
    398, 11, 70,
    sdReady ? "SD OK" : "SD OFF",
    sdReady ? UI_GREEN : UI_RED);
}

void drawClockBlock()
{
  tft.fillRect(290, 53, 168, 47, UI_CARD);

  uiText(
    294, 56,
    rtcReady ? picTime : "--:--:--",
    rtcReady ? UI_WHITE : UI_RED,
    2);

  char displayDate[11] = "--/--/----";

  if (rtcReady && strlen(picDate) == 10)
  {
    displayDate[0] = picDate[8];
    displayDate[1] = picDate[9];
    displayDate[2] = '/';
    displayDate[3] = picDate[5];
    displayDate[4] = picDate[6];
    displayDate[5] = '/';
    displayDate[6] = picDate[0];
    displayDate[7] = picDate[1];
    displayDate[8] = picDate[2];
    displayDate[9] = picDate[3];
    displayDate[10] = '\0';
  }

  uiText(294, 80, displayDate, UI_TEXT2, 1);
}

/* ============================= PAGES ============================== */

void drawBottomNav()
{
  const char *labels[4] = {"HOME", "ENROLL", "LOGS", "SETTINGS"};

  for (int i = 0; i < 4; i++)
  {
    int x = i * 120;

    tft.fillRect(
      x,
      277,
      120,
      43,
      ((int)currentPage == i) ? UI_CARD2 : UI_BG2);

    tft.drawRect(x, 277, 120, 43, UI_BORDER);
    uiText(x + 27, 293, labels[i], UI_WHITE, 1);
  }
}

void drawHomePage()
{
  currentPage = PAGE_HOME;

  tft.fillScreen(UI_BG);
  drawTopBar();

  panel(12, 52, 258, 215, UI_CYAN);
  uiText(28, 64, "FINGERPRINT SCANNER", UI_TEXT2, 1);
  uiText(28, 82, "PLACE YOUR FINGER", UI_WHITE, 2);

  tft.fillRoundRect(71, 112, 140, 108, 10, UI_BG);
  tft.drawRoundRect(71, 112, 140, 108, 10, UI_BLUE);
  drawFingerprintIcon(141, 159, UI_CYAN);

  pill(
    79, 230, 124,
    fingerprintReady ? "READY TO SCAN" : "CHECK SENSOR",
    fingerprintReady ? UI_GREEN : UI_ORANGE);

  panel(280, 52, 188, 215, UI_BLUE);
  drawClockBlock();

  uiText(294, 110, "LAST USER", UI_MUTED, 1);

  String n = String(currentName);
  if (n.length() > 16) n = n.substring(0, 16);

  uiText(294, 127, n.c_str(), UI_WHITE, 2);

  char idBuf[18];
  if (currentFingerID)
    snprintf(idBuf, sizeof(idBuf), "ID #%03u", currentFingerID);
  else
    strcpy(idBuf, "ID ---");

  uiText(294, 153, idBuf, UI_CYAN, 1);

  uiText(294, 172, "STATUS", UI_MUTED, 1);

  uint16_t statusColor = UI_BLUE;

  if (!strcmp(currentStatus, "IN"))
    statusColor = UI_GREEN;
  else if (!strcmp(currentStatus, "OUT"))
    statusColor = UI_MAGENTA;
  else if (!strcmp(currentStatus, "LATE"))
    statusColor = UI_ORANGE;
  else if (!strcmp(currentStatus, "VERY LATE") ||
           !strcmp(currentStatus, "DENIED"))
    statusColor = UI_RED;

  pill(294, 185, 92, currentStatus, statusColor);

  char usersBuf[18];
  snprintf(usersBuf, sizeof(usersBuf), "USERS %u", countUsers());
  pill(294, 215, 92, usersBuf, UI_CYAN);

  char confBuf[18];
  snprintf(confBuf, sizeof(confBuf), "CONF %u", currentConfidence);
  pill(391, 215, 66, confBuf, UI_GREEN);

  tft.fillRoundRect(288, 244, 52, 24, 6,
    attendanceMode == MODE_AUTO ? UI_BLUE : UI_CARD2);
  tft.fillRoundRect(345, 244, 52, 24, 6,
    attendanceMode == MODE_IN ? UI_GREEN : UI_CARD2);
  tft.fillRoundRect(402, 244, 52, 24, 6,
    attendanceMode == MODE_OUT ? UI_MAGENTA : UI_CARD2);

  uiText(296, 252, "AUTO", UI_WHITE, 1);
  uiText(362, 252, "IN", UI_WHITE, 1);
  uiText(417, 252, "OUT", UI_WHITE, 1);

  drawBottomNav();
}

void drawEnrollPage()
{
  currentPage = PAGE_ENROLL;

  tft.fillScreen(UI_BG);
  drawTopBar();

  panel(24, 58, 432, 205, UI_CYAN);

  uiText(44, 70, "FINGERPRINT ENROLLMENT", UI_WHITE, 2);
  uiText(44, 96, "Physical switches or touch controls", UI_TEXT2, 1);

  uiText(44, 121, "NAME", UI_MUTED, 1);

  String shown = pendingEnrollName;
  if (shown.length() == 0)
    shown = "PC: NAME:Your Name";
  if (shown.length() > 18)
    shown = shown.substring(0, 18);

  uiText(
    108,
    118,
    shown.c_str(),
    pendingEnrollName.length() ? UI_GREEN2 : UI_ORANGE,
    1);

  uiText(44, 151, "ID", UI_MUTED, 1);

  char idBuf[8];
  snprintf(idBuf, sizeof(idBuf), "%03u", enrollID);

  tft.fillRoundRect(170, 137, 134, 55, 8, UI_BG);
  tft.drawRoundRect(170, 137, 134, 55, 8, UI_CYAN);
  uiText(205, 150, idBuf, UI_WHITE, 3);

  bool used = findEmployeeSlot(enrollID) >= 0;

  pill(
    319,
    152,
    110,
    used ? "ID IN USE" : "ID FREE",
    used ? UI_ORANGE : UI_GREEN);

  tft.fillRoundRect(55, 205, 72, 38, 7, UI_CARD2);
  tft.drawRoundRect(55, 205, 72, 38, 7, UI_CYAN);
  uiText(84, 218, "-", UI_WHITE, 2);

  tft.fillRoundRect(145, 205, 190, 38, 7,
    used ? UI_RED : UI_BLUE);

  uiText(
    used ? 208 : 192,
    218,
    used
      ? (deleteArmed ? "CONFIRM DELETE" : "DELETE")
      : "START ENROLL",
    UI_WHITE,
    1);

  tft.fillRoundRect(353, 205, 72, 38, 7, UI_CARD2);
  tft.drawRoundRect(353, 205, 72, 38, 7, UI_CYAN);
  uiText(382, 218, "+", UI_WHITE, 2);

  uiText(
    54,
    249,
    "ADD: Home  UP/DOWN: ID  OK: Start/Delete",
    UI_TEXT2,
    1);

  drawBottomNav();
}

void drawLogsPage()
{
  currentPage = PAGE_LOGS;

  tft.fillScreen(UI_BG);
  drawTopBar();

  uiText(22, 57, "RECENT ATTENDANCE", UI_CYAN, 2);

  panel(18, 88, 444, 176, UI_BLUE);

  uiText(30, 99, "ID", UI_MUTED, 1);
  uiText(74, 99, "NAME", UI_MUTED, 1);
  uiText(240, 99, "TIME", UI_MUTED, 1);
  uiText(325, 99, "STATUS", UI_MUTED, 1);

  int y = 118;

  for (uint8_t i = 0; i < MAX_LOGS; i++)
  {
    if (!logs[i].valid)
      continue;

    char idBuf[8];
    snprintf(idBuf, sizeof(idBuf), "%03u", logs[i].id);

    uiText(30, y, idBuf, UI_WHITE, 1);

    String n = String(logs[i].name);
    if (n.length() > 18)
      n = n.substring(0, 18);

    uiText(74, y, n.c_str(), UI_TEXT, 1);
    uiText(240, y, logs[i].time, UI_TEXT2, 1);

    uint16_t c = UI_GREEN;
    if (!strcmp(logs[i].status, "OUT")) c = UI_MAGENTA;
    if (!strcmp(logs[i].status, "LATE")) c = UI_ORANGE;
    if (!strcmp(logs[i].status, "VERY LATE")) c = UI_RED;

    uiText(325, y, logs[i].status, c, 1);

    y += 18;

    if (y > 250)
      break;
  }

  drawBottomNav();
}

void drawSettingsPage()
{
  currentPage = PAGE_SETTINGS;

  tft.fillScreen(UI_BG);
  drawTopBar();

  uiText(22, 57, "SYSTEM SETTINGS", UI_CYAN, 2);

  panel(20, 86, 440, 174, UI_BLUE);

  uiText(40, 100, "PIC LINK", UI_TEXT2, 1);
  uiText(350, 100, picLinkReady ? "ONLINE" : "OFFLINE",
    picLinkReady ? UI_GREEN : UI_RED, 1);

  uiText(40, 122, "AS608", UI_TEXT2, 1);
  uiText(350, 122, fingerprintReady ? "ONLINE" : "OFFLINE",
    fingerprintReady ? UI_GREEN : UI_RED, 1);

  uiText(40, 144, "RTC", UI_TEXT2, 1);
  uiText(350, 144, rtcReady ? "ONLINE" : "WAITING",
    rtcReady ? UI_GREEN : UI_ORANGE, 1);

  uiText(40, 166, "SD CARD", UI_TEXT2, 1);
  uiText(350, 166, sdReady ? "ONLINE" : "OFFLINE",
    sdReady ? UI_GREEN : UI_RED, 1);

  char lateBuf[32];
  snprintf(
    lateBuf,
    sizeof(lateBuf),
    "LATE %02d:%02d - %02d:%02d",
    lateStartMinutes / 60,
    lateStartMinutes % 60,
    lateEndMinutes / 60,
    lateEndMinutes % 60);

  uiText(40, 192, lateBuf, UI_ORANGE, 1);

  tft.fillRoundRect(40, 215, 115, 32, 6, UI_BLUE);
  uiText(56, 226, "RECAL TOUCH", UI_WHITE, 1);

  tft.fillRoundRect(180, 215, 115, 32, 6, UI_ORANGE);
  uiText(198, 226, "CLEAR LOG", UI_WHITE, 1);

  tft.fillRoundRect(320, 215, 115, 32, 6, UI_PURPLE);
  uiText(341, 226, "CSV STATUS", UI_WHITE, 1);

  drawBottomNav();
}

void setPage(Page page)
{
  if (page == PAGE_HOME) drawHomePage();
  else if (page == PAGE_ENROLL) drawEnrollPage();
  else if (page == PAGE_LOGS) drawLogsPage();
  else drawSettingsPage();
}

/* ============================= RESULT SCREENS ============================== */

void drawResult(bool granted, const char *name, const char *status)
{
  uint16_t c = granted ? UI_GREEN : UI_RED;

  tft.fillScreen(UI_BG);
  panel(45, 55, 390, 210, c);

  tft.drawCircle(115, 154, 48, c);
  tft.drawCircle(115, 154, 38, c);

  if (granted)
  {
    tft.drawLine(91, 154, 108, 171, c);
    tft.drawLine(108, 171, 140, 132, c);
  }
  else
  {
    tft.drawLine(92, 132, 138, 178, c);
    tft.drawLine(138, 132, 92, 178, c);
  }

  uiText(190, 84,
    granted ? "ACCESS GRANTED" : "ACCESS DENIED",
    c, 2);

  uiText(190, 124, "NAME", UI_MUTED, 1);
  uiText(190, 141, name, UI_WHITE, 2);

  uiText(190, 177, "STATUS", UI_MUTED, 1);
  pill(190, 191, 118, status, c);

  uiText(326, 194, currentScanTime, UI_TEXT2, 1);
}

void drawEnrollStatus(const char *title, const char *sub, uint16_t color)
{
  tft.fillScreen(UI_BG);
  panel(45, 62, 390, 190, color);
  uiText(80, 105, title, color, 2);
  uiText(80, 150, sub, UI_WHITE, 1);
}

/* ============================= MATCH RESULT ============================== */

void handleMatchedFingerprint(uint16_t id, uint16_t score)
{
  String name = employeeName(id);

  if (findEmployeeSlot(id) < 0)
    saveEmployee(id, name);

  String status = decideStatus(id);

  currentFingerID = id;
  currentConfidence = score;

  name.substring(0, MAX_NAME_LEN).toCharArray(
    currentName,
    sizeof(currentName));

  status.toCharArray(currentStatus, sizeof(currentStatus));

  strncpy(
    currentScanTime,
    rtcReady ? picTime : "--:--:--",
    sizeof(currentScanTime) - 1);

  currentScanTime[sizeof(currentScanTime) - 1] = '\0';

  if (rtcReady)
    updatePresence(id, status);

  addAttendance(id, name, status);
  picFeedbackOK();

  drawResult(true, currentName, currentStatus);
  resultHoldUntil = millis() + 1800UL;

  Serial.print(F("ATTENDANCE,"));
  Serial.print(id);
  Serial.print(',');
  Serial.print(name);
  Serial.print(',');
  Serial.print(picDate);
  Serial.print(',');
  Serial.print(picTime);
  Serial.print(',');
  Serial.println(status);
}

void handleUnknownFingerprint()
{
  currentFingerID = 0;
  currentConfidence = 0;

  strcpy(currentName, "UNKNOWN");
  strcpy(currentStatus, "DENIED");

  strncpy(
    currentScanTime,
    rtcReady ? picTime : "--:--:--",
    sizeof(currentScanTime) - 1);

  currentScanTime[sizeof(currentScanTime) - 1] = '\0';

  picFeedbackDeny();
  drawResult(false, "UNKNOWN", "DENIED");
  resultHoldUntil = millis() + 1500UL;
}

/* ============================= ENROLL / DELETE ============================== */

void startEnrollment()
{
  if (enrollBusy)
    return;

  if (pendingEnrollName.length() == 0)
  {
    drawEnrollStatus(
      "SET USER NAME FIRST",
      "PC Serial: NAME:Mohamed Rasmy",
      UI_ORANGE);

    delay(1200);
    drawEnrollPage();
    return;
  }

  enrollBusy = true;
  deleteArmed = false;

  sendPIC(String("ENROLL,") + String(enrollID));

  drawEnrollStatus(
    "STARTING ENROLLMENT",
    "Follow the fingerprint instructions",
    UI_CYAN);
}

void requestDelete()
{
  if (findEmployeeSlot(enrollID) < 0)
  {
    deleteArmed = false;
    drawEnrollPage();
    return;
  }

  if (!deleteArmed)
  {
    deleteArmed = true;
    drawEnrollPage();
    return;
  }

  deleteArmed = false;
  sendPIC(String("DELETE,") + String(enrollID));

  drawEnrollStatus(
    "DELETING",
    "Please wait...",
    UI_RED);
}

/* ============================= PHYSICAL BUTTONS ============================== */

void handlePICButton(const char *button)
{
  if (!strcmp(button, "BTN_ADD"))
  {
    deleteArmed = false;

    if (currentPage == PAGE_HOME)
    {
      pendingEnrollName = "";
      setPage(PAGE_ENROLL);
    }
    else
    {
      setPage(PAGE_HOME);
    }

    return;
  }

  if (currentPage != PAGE_ENROLL)
    return;

  if (!strcmp(button, "BTN_UP"))
  {
    if (enrollID < 163) enrollID++;
    deleteArmed = false;
    drawEnrollPage();
    return;
  }

  if (!strcmp(button, "BTN_DOWN"))
  {
    if (enrollID > 1) enrollID--;
    deleteArmed = false;
    drawEnrollPage();
    return;
  }

  if (!strcmp(button, "BTN_OK"))
  {
    if (findEmployeeSlot(enrollID) >= 0)
      requestDelete();
    else
      startEnrollment();

    return;
  }
}

/* ============================= PIC PARSER ============================== */

void processPICLine(char *line)
{
  lastPicPacket = millis();
  picLinkReady = true;

  if (!strcmp(line, "PIC_READY"))
  {
    Serial.println(F("PIC ONLINE"));
    return;
  }

  if (!strcmp(line, "PONG"))
  {
    Serial.println(F("PIC PONG"));
    return;
  }

  if (!strcmp(line, "AS608_OK"))
  {
    fingerprintReady = true;
    Serial.println(F("AS608 ONLINE THROUGH PIC"));
    return;
  }

  if (!strcmp(line, "AS608_ERROR"))
  {
    fingerprintReady = false;
    Serial.println(F("AS608 ERROR"));
    return;
  }

  if (!strncmp(line, "TIME,", 5))
  {
    if (strlen(line) >= 24 && line[15] == ',')
    {
      strncpy(picDate, line + 5, 10);
      picDate[10] = '\0';

      strncpy(picTime, line + 16, 8);
      picTime[8] = '\0';

      rtcReady = true;

      uint32_t key = dateKeyFromPIC();

      if (currentDayKey == 0)
        currentDayKey = key;
      else if (key != 0 && key != currentDayKey)
      {
        currentDayKey = key;
        totalToday = 0;
      }
    }
    return;
  }

  if (!strcmp(line, "BTN_ADD") ||
      !strcmp(line, "BTN_OK") ||
      !strcmp(line, "BTN_UP") ||
      !strcmp(line, "BTN_DOWN"))
  {
    handlePICButton(line);
    return;
  }

  if (!strncmp(line, "FP_MATCH,", 9))
  {
    char *comma = strchr(line + 9, ',');

    if (comma)
    {
      *comma = '\0';
      uint16_t id = atoi(line + 9);
      uint16_t score = atoi(comma + 1);
      handleMatchedFingerprint(id, score);
    }
    return;
  }

  if (!strcmp(line, "FP_NOT_FOUND"))
  {
    handleUnknownFingerprint();
    return;
  }

  if (!strcmp(line, "FP_FINGER"))
  {
    if (currentPage == PAGE_HOME)
      uiText(85, 247, "SCANNING...", UI_YELLOW, 1);
    return;
  }

  if (!strcmp(line, "FP_REMOVED"))
  {
    if (millis() >= resultHoldUntil && currentPage == PAGE_HOME)
      drawHomePage();
    return;
  }

  if (!strncmp(line, "ENROLL_PLACE1,", 14))
  {
    drawEnrollStatus(
      "PLACE FINGER",
      "First fingerprint scan...",
      UI_CYAN);
    return;
  }

  if (!strcmp(line, "ENROLL_REMOVE"))
  {
    drawEnrollStatus(
      "REMOVE FINGER",
      "Wait for the next instruction",
      UI_ORANGE);
    return;
  }

  if (!strcmp(line, "ENROLL_PLACE2"))
  {
    drawEnrollStatus(
      "PLACE SAME FINGER",
      "Second fingerprint scan...",
      UI_CYAN);
    return;
  }

  if (!strncmp(line, "ENROLL_OK,", 10))
  {
    uint16_t id = atoi(line + 10);

    saveEmployee(id, pendingEnrollName);
    enrollBusy = false;

    drawEnrollStatus(
      "ENROLLMENT SUCCESS",
      pendingEnrollName.c_str(),
      UI_GREEN);

    picFeedbackEnroll();

    delay(1400);
    pendingEnrollName = "";
    drawEnrollPage();
    return;
  }

  if (!strncmp(line, "ENROLL_FAIL,", 12))
  {
    enrollBusy = false;

    String msg = String("Error step ") + String(atoi(line + 12));

    drawEnrollStatus(
      "ENROLLMENT FAILED",
      msg.c_str(),
      UI_RED);

    delay(1400);
    drawEnrollPage();
    return;
  }

  if (!strncmp(line, "DELETE_OK,", 10))
  {
    uint16_t id = atoi(line + 10);
    deleteEmployee(id);

    drawEnrollStatus(
      "FINGERPRINT DELETED",
      "Sensor + EEPROM record removed",
      UI_GREEN);

    delay(1200);
    drawEnrollPage();
    return;
  }

  if (!strncmp(line, "DELETE_FAIL,", 12))
  {
    drawEnrollStatus(
      "DELETE FAILED",
      "Check AS608 / ID",
      UI_RED);

    delay(1200);
    drawEnrollPage();
    return;
  }

  if (!strncmp(line, "ACK,", 4))
  {
    Serial.print(F("PIC "));
    Serial.println(line);
    return;
  }

  if (!strcmp(line, "FP_COMM_ERROR"))
  {
    Serial.println(F("AS608 runtime communication error"));
    return;
  }

  if (!strcmp(line, "FP_BAD_IMAGE"))
  {
    Serial.println(F("Fingerprint image quality error"));
    return;
  }

  if (!strcmp(line, "FP_SEARCH_ERROR"))
  {
    Serial.println(F("Fingerprint search error"));
    return;
  }

  Serial.print(F("PIC -> "));
  Serial.println(line);
}

void pollPIC()
{
  while (Serial2.available())
  {
    char c = (char)Serial2.read();

    if (c == '\r')
      continue;

    if (c == '\n')
    {
      picLine[picLinePos] = '\0';

      if (picLinePos > 0)
      {
        // A complete line proves the PIC link is alive.
        lastPicPacket = millis();

        if (!picLinkReady)
        {
          picLinkReady = true;

          Serial.println();
          Serial.println(F("*** PIC CONNECTED ***"));
          Serial.println();
        }

        processPICLine(picLine);
      }

      picLinePos = 0;
      continue;
    }

    if (picLinePos < sizeof(picLine) - 1)
      picLine[picLinePos++] = c;
    else
      picLinePos = 0;
  }

  // PIC normally sends TIME packets about once per second.
  // No valid line for 4 seconds -> disconnected.
  if (picLinkReady &&
      millis() - lastPicPacket > 4000UL)
  {
    picLinkReady = false;
    rtcReady = false;

    Serial.println();
    Serial.println(F("*** PIC DISCONNECTED ***"));
    Serial.println(F("No PIC packet received for 4 seconds."));
    Serial.println();
  }
}

/* ============================= TOUCH HANDLER ============================== */

void handleTouch()
{
  static unsigned long lastTouch = 0;
  int x, y;

  if (!getTouch(x, y))
    return;

  if (millis() - lastTouch < 220)
    return;

  lastTouch = millis();

  if (y >= 277)
  {
    if (x < 120) setPage(PAGE_HOME);
    else if (x < 240) setPage(PAGE_ENROLL);
    else if (x < 360) setPage(PAGE_LOGS);
    else setPage(PAGE_SETTINGS);

    waitTouchRelease();
    return;
  }

  if (currentPage == PAGE_HOME)
  {
    if (y >= 240 && y <= 275)
    {
      if (x >= 280 && x < 343)
      {
        attendanceMode = MODE_AUTO;
        drawHomePage();
      }
      else if (x >= 343 && x < 400)
      {
        attendanceMode = MODE_IN;
        drawHomePage();
      }
      else if (x >= 400)
      {
        attendanceMode = MODE_OUT;
        drawHomePage();
      }

      waitTouchRelease();
      return;
    }
  }

  if (currentPage == PAGE_ENROLL && y >= 198 && y <= 250)
  {
    if (x >= 45 && x <= 140)
    {
      if (enrollID > 1) enrollID--;
      deleteArmed = false;
      drawEnrollPage();
    }
    else if (x >= 145 && x <= 340)
    {
      if (findEmployeeSlot(enrollID) >= 0)
        requestDelete();
      else
        startEnrollment();
    }
    else if (x >= 345 && x <= 440)
    {
      if (enrollID < 163) enrollID++;
      deleteArmed = false;
      drawEnrollPage();
    }

    waitTouchRelease();
    return;
  }

  if (currentPage == PAGE_SETTINGS && y >= 208 && y <= 255)
  {
    if (x >= 30 && x < 165)
    {
      waitTouchRelease();
      calibrateTouch();
      drawSettingsPage();
    }
    else if (x >= 165 && x < 310)
    {
      clearRecent();
      drawSettingsPage();
    }
    else if (x >= 310)
    {
      csvStatus();
      drawSettingsPage();
    }

    waitTouchRelease();
    return;
  }
}


/* ========================== PIC CONNECTION STATUS ========================= */

void printPICConnectionStatus()
{
  Serial.println();
  Serial.println(F("=============================="));
  Serial.println(F("       PIC CONNECTION"));
  Serial.println(F("=============================="));

  if (picLinkReady)
  {
    Serial.println(F("PIC STATUS: CONNECTED"));

    Serial.print(F("Last packet: "));
    Serial.print(millis() - lastPicPacket);
    Serial.println(F(" ms ago"));

    Serial.println(F("Mega RX2 pin 17 <- PIC pin 25 / RC6"));
    Serial.println(F("Mega TX2 pin 16 -> PIC pin 26 / RC7"));
    Serial.println(F("Baud: 2400"));

    if (rtcReady)
    {
      Serial.print(F("RTC: "));
      Serial.print(picDate);
      Serial.print(' ');
      Serial.println(picTime);
    }
    else
    {
      Serial.println(F("RTC: waiting for TIME packet"));
    }

    Serial.print(F("AS608 through PIC: "));
    Serial.println(
      fingerprintReady
        ? F("ONLINE")
        : F("NOT CONFIRMED")
    );
  }
  else
  {
    Serial.println(F("PIC STATUS: DISCONNECTED"));
    Serial.println(F("Check:"));
    Serial.println(F("1. PIC power"));
    Serial.println(F("2. Common GND"));
    Serial.println(F("3. PIC pin 25/RC6 -> Mega pin 17/RX2"));
    Serial.println(F("4. PIC pin 26/RC7 <- Mega pin 16/TX2"));
    Serial.println(F("5. Both sides = 2400 baud"));
  }

  Serial.println(F("=============================="));
  Serial.println();
}

/* ============================= PC COMMANDS ============================== */

bool validDateText(const String &s)
{
  return s.length() == 10 && s[4] == '-' && s[7] == '-';
}

bool validTimeText(const String &s)
{
  return s.length() == 8 && s[2] == ':' && s[5] == ':';
}

void printUsers()
{
  StoredEmployee e;

  Serial.println(F("ID,NAME,CHECKED_IN"));

  for (uint8_t i = 0; i < MAX_STORED_USERS; i++)
  {
    EEPROM.get(employeeAddress(i), e);

    if (!e.valid)
      continue;

    Serial.print(e.id);
    Serial.print(',');
    Serial.print(e.name);
    Serial.print(',');
    Serial.println(e.checkedIn);
  }
}

void printStatus()
{
  Serial.println(F("===== SYSTEM STATUS ====="));

  Serial.print(F("PIC: "));
  Serial.println(picLinkReady ? F("ONLINE") : F("OFFLINE"));

  Serial.print(F("AS608: "));
  Serial.println(fingerprintReady ? F("ONLINE") : F("OFFLINE"));

  Serial.print(F("RTC: "));
  Serial.println(rtcReady ? F("ONLINE") : F("WAITING"));

  Serial.print(F("DATE: "));
  Serial.println(picDate);

  Serial.print(F("TIME: "));
  Serial.println(picTime);

  Serial.print(F("SD: "));
  Serial.println(sdReady ? F("ONLINE") : F("OFFLINE"));

  Serial.print(F("USERS: "));
  Serial.println(countUsers());

  Serial.print(F("MODE: "));
  if (attendanceMode == MODE_AUTO) Serial.println(F("AUTO"));
  else if (attendanceMode == MODE_IN) Serial.println(F("IN"));
  else Serial.println(F("OUT"));
}

void processPCCommand(String cmd)
{
  cmd.trim();

  if (cmd.length() == 0)
    return;

  String upper = cmd;
  upper.toUpperCase();

  if (upper == "PIC_STATUS" ||
      upper == "PIC?" ||
      upper == "PIC")
  {
    unsigned long before = lastPicPacket;

    sendPIC("PING");

    unsigned long startWait = millis();

    while (millis() - startWait < 900UL)
    {
      pollPIC();

      if (lastPicPacket != before)
        break;

      delay(5);
    }

    printPICConnectionStatus();
    return;
  }

  if (upper.startsWith("NAME:"))
  {
    pendingEnrollName = cmd.substring(5);
    pendingEnrollName.trim();

    if (pendingEnrollName.length() > MAX_NAME_LEN)
      pendingEnrollName = pendingEnrollName.substring(0, MAX_NAME_LEN);

    Serial.print(F("ENROLL NAME = "));
    Serial.println(pendingEnrollName);

    if (currentPage == PAGE_ENROLL)
      drawEnrollPage();

    return;
  }

  if (upper.startsWith("SET_DATE:"))
  {
    String value = cmd.substring(9);
    value.trim();

    if (!validDateText(value))
    {
      Serial.println(F("Use SET_DATE:YYYY-MM-DD"));
      return;
    }

    sendPIC(String("SET_DATE,") + value);
    return;
  }

  if (upper.startsWith("SET_TIME:"))
  {
    String value = cmd.substring(9);
    value.trim();

    if (!validTimeText(value))
    {
      Serial.println(F("Use SET_TIME:HH:MM:SS"));
      return;
    }

    sendPIC(String("SET_TIME,") + value);
    return;
  }

  if (upper.startsWith("SET_LATE:"))
  {
    String value = cmd.substring(9);
    value.trim();

    if (value.length() >= 11)
    {
      int h1 = value.substring(0, 2).toInt();
      int m1 = value.substring(3, 5).toInt();
      int h2 = value.substring(6, 8).toInt();
      int m2 = value.substring(9, 11).toInt();

      lateStartMinutes = h1 * 60 + m1;
      lateEndMinutes = h2 * 60 + m2;

      Serial.println(F("LATE WINDOW UPDATED"));
    }

    return;
  }

  if (upper == "MODE:AUTO")
  {
    attendanceMode = MODE_AUTO;
    drawHomePage();
    return;
  }

  if (upper == "MODE:IN")
  {
    attendanceMode = MODE_IN;
    drawHomePage();
    return;
  }

  if (upper == "MODE:OUT")
  {
    attendanceMode = MODE_OUT;
    drawHomePage();
    return;
  }

  if (upper == "STATUS")
  {
    printStatus();
    return;
  }

  if (upper == "LIST")
  {
    printUsers();
    return;
  }

  if (upper == "CSV_STATUS")
  {
    csvStatus();
    return;
  }

  if (upper == "FETCH_CSV")
  {
    sendCSVToPC();
    return;
  }

  if (upper == "RESET_CSV")
  {
    resetCSV();
    return;
  }

  if (upper == "CLEAR_RECENT")
  {
    clearRecent();
    setPage(currentPage);
    return;
  }

  if (upper == "RECAL_TOUCH")
  {
    calibrateTouch();
    setPage(PAGE_HOME);
    return;
  }

  if (upper == "GET_TIME")
  {
    sendPIC("GET_TIME");
    return;
  }

  if (upper.startsWith("ENROLL:"))
  {
    enrollID = (uint16_t)cmd.substring(7).toInt();
    if (enrollID < 1) enrollID = 1;
    if (enrollID > 163) enrollID = 163;
    startEnrollment();
    return;
  }

  if (upper.startsWith("DELETE:"))
  {
    uint16_t id = (uint16_t)cmd.substring(7).toInt();
    sendPIC(String("DELETE,") + String(id));
    return;
  }

  Serial.println(F(
    "Commands: NAME:, SET_DATE:, SET_TIME:, MODE:AUTO/IN/OUT, "
    "SET_LATE:, STATUS, LIST, CSV_STATUS, FETCH_CSV, RESET_CSV, "
    "CLEAR_RECENT, RECAL_TOUCH, GET_TIME, PIC_STATUS, ENROLL:id, DELETE:id"));
}

void pollPC()
{
  if (!Serial.available())
    return;

  String cmd = Serial.readStringUntil('\n');
  processPCCommand(cmd);
}

/* ============================= TFT SETUP ============================== */

void setupTFT()
{
  uint16_t id = tft.readID();

  if (id == 0xD3D3 || id == 0x0000 || id == 0xFFFF)
    id = 0x9486;

  tft.begin(id);
  tft.setRotation(1);
  tft.setTextWrap(false);

  Serial.print(F("TFT ID=0x"));
  Serial.println(id, HEX);

  Serial.print(F("DISPLAY="));
  Serial.print(tft.width());
  Serial.print('x');
  Serial.println(tft.height());
}

/* ============================= SETUP / LOOP ============================== */

void setup()
{
  Serial.begin(115200);
  Serial.setTimeout(80);

  Serial2.begin(PIC_BAUD);

  delay(300);

  setupTFT();
  loadTouchCalibration();

  initializeDatabase();
  initSD();
  clearRecent();

  drawHomePage();

  Serial.println();
  Serial.println(F("===================================="));
  Serial.println(F(" BIOMETRIC ATTENDANCE FINAL SYSTEM"));
  Serial.println(F("===================================="));
  Serial.println(F("PIC Serial2 = 2400 baud"));
  Serial.println(F("Type PIC_STATUS to check PIC connection"));
  Serial.println(F("AS608 uses PIC software UART RD3/RD2 @ 9600"));
  Serial.println();

  delay(300);
  sendPIC("GET_TIME");
}

void loop()
{
  pollPIC();
  pollPC();
  handleTouch();

  unsigned long now = millis();

  if (now - lastClockDraw >= 1000UL)
  {
    lastClockDraw = now;

    if (currentPage == PAGE_HOME &&
        now >= resultHoldUntil)
    {
      drawTopBar();
      drawClockBlock();
    }
  }

  if (resultHoldUntil != 0 &&
      now >= resultHoldUntil &&
      currentPage == PAGE_HOME)
  {
    resultHoldUntil = 0;
    drawHomePage();
  }
}
