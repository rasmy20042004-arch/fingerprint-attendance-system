#include <MCUFRIEND_kbv.h>
#include <Adafruit_GFX.h>
#include <TouchScreen.h>
#include <Adafruit_Fingerprint.h>
#include <RTClib.h>
#include <Wire.h>
#include <EEPROM.h>
#include <SdFat.h>
MCUFRIEND_kbv tft;
#define FINGER_BAUD 57600
Adafruit_Fingerprint finger(&Serial3);
#define PIC_BAUD 9600
DateTime picRtcBase(2026, 1, 1, 0, 0, 0);
unsigned long picRtcSyncMillis = 0;
bool picLinkReady = false;
unsigned long lastPicPacketMillis = 0;
char picRxLine[64];
uint8_t picRxPos = 0;
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
const char ATTENDANCE_CSV[] =
  "ATTEND.CSV";
#define SCREEN_W 480
#define SCREEN_H 320
#define XP 7
#define XM A1
#define YP A2
#define YM 6
TouchScreen ts(XP, YP, XM, YM, 300);
#define MINPRESSURE 50
#define MAXPRESSURE 1000
#define UI_BG          0x0841   // dark navy
#define UI_BG2         0x1082   // dark blue-grey
#define UI_CARD        0x18C3   // main card
#define UI_CARD2       0x2104   // secondary card
#define UI_CARD3       0x2965   // selected card
#define UI_BORDER      0x5DDF   // light blue border
#define UI_BORDER2     0x39CE   // muted blue border
#define UI_CYAN        0x07FF   // cyan accent
#define UI_BLUE        0x041F   // standard blue
#define UI_BLUE2       0x5DFF   // light blue
#define UI_WHITE       0xFFFF   // white
#define UI_TEXT        0xE71C   // soft white
#define UI_TEXT2       0xBDF7   // light grey-blue
#define UI_MUTED       0x7BEF   // muted grey-blue
#define UI_GREEN       0x07E0   // green success
#define UI_GREEN2      0x87E0   // light green
#define UI_RED         0xF800   // red error/delete
#define UI_ORANGE      0xFD20   // orange warning
#define UI_YELLOW      0xFFE0   // yellow highlight
#define UI_MAGENTA     UI_BLUE2 // mapped to normal blue
#define UI_TEAL        0x0410   // teal-blue
#define UI_PURPLE      UI_BLUE  // mapped to standard blue
#define UI_DARK        0x0841   // dark navy
#define UI_GLOW        UI_CYAN
#define UI_LIME        UI_GREEN2
#define UI_GRID        0x18C6
#define UI_NAV         0x1082   // dark blue navigation
#define UI_GOLD        UI_ORANGE
#define UI_SOFT        0x3186   // soft purple-grey
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
bool ensureAttendanceCSV()
{
  if (!sdReady)
    return false;
  if (
    sd.exists(
      ATTENDANCE_CSV))
  {
    return true;
  }
  FsFile file;
  if (
    !file.open(
      ATTENDANCE_CSV,
      O_WRONLY |
      O_CREAT |
      O_AT_END))
  {
    Serial.println(
      F("SD ERROR: Cannot create ATTEND.CSV"));
    return false;
  }
  file.println(
    F("ID,Name,Date,Time,Status"));
  file.close();
  Serial.println(
    F("Created ATTEND.CSV"));
  return true;
}
bool initSDCard()
{
  pinMode(
    SD_CS_PIN,
    OUTPUT);
  digitalWrite(
    SD_CS_PIN,
    HIGH);
  Serial.println(
    F("Initializing SD card with SoftSPI..."));
  sdReady =
    sd.begin(
      SD_CONFIG);
  if (!sdReady)
  {
    Serial.println(
      F("SD CARD: OFFLINE"));
    Serial.println(
      F("Check FAT32 card and TFT shield pins 10,11,12,13"));
    return false;
  }
  Serial.println(
    F("SD CARD: ONLINE"));
  if (
    !ensureAttendanceCSV())
  {
    Serial.println(
      F("SD CARD: CSV initialization failed"));
    return false;
  }
  return true;
}
String safeCSVField(
  const String &input)
{
  String s =
    input;
  s.replace(
    ",",
    " ");
  s.replace(
    "\r",
    " ");
  s.replace(
    "\n",
    " ");
  return s;
}
bool appendAttendanceCSV(
  uint16_t id,
  const String &name,
  const char *dateText,
  const char *timeText,
  const char *statusText)
{
  if (!sdReady)
    return false;
  if (
    !ensureAttendanceCSV())
  {
    return false;
  }
  FsFile file;
  if (
    !file.open(
      ATTENDANCE_CSV,
      O_WRONLY |
      O_CREAT |
      O_AT_END))
  {
    Serial.println(
      F("SD ERROR: Cannot append ATTEND.CSV"));
    return false;
  }
  String safeName =
    safeCSVField(
      name);
  file.print(
    id);
  file.print(
    ',');
  file.print(
    safeName);
  file.print(
    ',');
  file.print(
    dateText);
  file.print(
    ',');
  file.print(
    timeText);
  file.print(
    ',');
  file.println(
    statusText);
  file.flush();
  file.close();
  Serial.print(
    F("CSV SAVED: ID="));
  Serial.print(
    id);
  Serial.print(
    F(" STATUS="));
  Serial.println(
    statusText);
  return true;
}
void sendAttendanceCSVToSerial()
{
  if (!sdReady)
  {
    Serial.println(
      F("CSV_ERROR,SD_NOT_READY"));
    return;
  }
  FsFile file;
  if (
    !file.open(
      ATTENDANCE_CSV,
      O_RDONLY))
  {
    Serial.println(
      F("CSV_ERROR,FILE_NOT_FOUND"));
    return;
  }
  Serial.println(
    F("CSV_BEGIN"));
  char buffer[64];
  char lastByte = '\n';
  while (
    file.available())
  {
    int count =
      file.read(
        buffer,
        sizeof(buffer));
    if (count <= 0)
      break;
    lastByte =
      buffer[count - 1];
    Serial.write(
      (const uint8_t *)buffer,
      count);
  }
  if (
    lastByte != '\n')
  {
    Serial.println();
  }
  file.close();
  Serial.println(
    F("CSV_END"));
}
void printCSVStatus()
{
  Serial.print(
    F("CSV_STATUS,SD="));
  Serial.print(
    sdReady
      ? F("ONLINE")
      : F("OFFLINE"));
  if (!sdReady)
  {
    Serial.println();
    return;
  }
  FsFile file;
  if (
    !file.open(
      ATTENDANCE_CSV,
      O_RDONLY))
  {
    Serial.println(
      F(",FILE=MISSING"));
    return;
  }
  Serial.print(
    F(",FILE=ATTEND.CSV,SIZE="));
  Serial.println(
    (unsigned long)file.fileSize());
  file.close();
}
bool resetAttendanceCSV()
{
  if (!sdReady)
    return false;
  if (
    sd.exists(
      ATTENDANCE_CSV))
  {
    sd.remove(
      ATTENDANCE_CSV);
  }
  bool ok =
    ensureAttendanceCSV();
  if (ok)
  {
    Serial.println(
      F("CSV RESET COMPLETE"));
  }
  else
  {
    Serial.println(
      F("CSV RESET FAILED"));
  }
  return ok;
}
#define DB_MAGIC 0xB10F
#define DB_VERSION 4
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
enum Page : uint8_t
{
  PAGE_HOME,
  PAGE_ENROLL,
  PAGE_LOGS,
  PAGE_USERS
};
Page currentPage = PAGE_HOME;
enum ScanState
{
  STATE_READY,
  STATE_SCANNING,
  STATE_MATCHED,
  STATE_DENIED,
  STATE_ERROR
};
ScanState scanState = STATE_READY;
enum AttendanceMode : uint8_t
{
  MODE_AUTO,
  MODE_IN,
  MODE_OUT
};
AttendanceMode attendanceMode = MODE_AUTO;
int lateStartMinutes = 8 * 60 + 30;
int lateEndMinutes   = 9 * 60 + 30;
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
uint16_t totalToday = 0;
uint32_t currentDayKey = 0;
uint16_t currentFingerID = 0;
uint16_t currentConfidence = 0;
char currentName[MAX_NAME_LEN + 1] = "---";
char currentScanTime[9] = "--:--:--";
char currentStatus[11] = "READY";
uint16_t lastVerifiedID = 0;
uint16_t lastVerifiedConfidence = 0;
char lastVerifiedName[MAX_NAME_LEN + 1] = "NO USER YET";
char lastVerifiedTime[9] = "--:--:--";
char lastVerifiedStatus[11] = "---";
uint16_t enrollID = 1;
String pendingEnrollName = "";
#define USER_LIST_MAX 127
#define USERS_VISIBLE_ROWS 5
uint16_t userListIDs[USER_LIST_MAX];
uint8_t userListCount = 0;
int userListSelected = 0;
int userListTop = 0;
bool userListNeedsRefresh = true;
bool userDeleteArmed = false;
bool rtcReady = false;
bool fingerprintReady = false;
uint16_t templateCount = 0;
unsigned long statsTimer = 0;
uint8_t readyPulse = 0;
unsigned long clockTimer = 0;
unsigned long scanTimer = 0;
unsigned long animationTimer = 0;
unsigned long resultHoldUntil = 0;
unsigned long rtcRequestTimer = 0;
unsigned long lastFingerErrorPrint = 0;
unsigned long fingerprintReconnectTimer = 0;
bool fingerOnSensor = false;
int scanLineY = 137;
int scanDirection = 2;
int lastClockHour = -1;
int lastClockMinute = -1;
int lastClockSecond = -1;
uint32_t lastClockDateKey = 0;
bool lastClockRtcReady = false;
int lastScannerOrbit = -1;

// ============================================================
// PHYSICAL SWITCH CONTROL
// ============================================================
// SW1 = PIC RB0 / BTN_ADD   -> HOME / BACK
// SW2 = PIC RB1 / BTN_OK    -> OK / SELECT
// SW3 = PIC RB2 / BTN_UP    -> PREVIOUS / UP
// SW4 = PIC RB3 / BTN_DOWN  -> NEXT / DOWN
// During an active enrollment SW4 becomes an immediate CANCEL.

int switchFocus = 0;
bool enrollmentInProgress = false;
bool operationCancelRequested = false;
bool deleteConfirmActive = false;
int8_t deleteConfirmChoice = -1;
struct ButtonRect
{
  int x;
  int y;
  int w;
  int h;
};
const ButtonRect HOME_MODE_AUTO = {306, 232, 44, 20};
const ButtonRect HOME_MODE_IN   = {356, 232, 44, 20};
const ButtonRect HOME_MODE_OUT  = {406, 232, 44, 20};
const ButtonRect ENROLL_MINUS    = {48, 171, 72, 38};
const ButtonRect ENROLL_PLUS     = {360, 171, 72, 38};
const ButtonRect ENROLL_SET_NAME = {330, 118, 112, 30};
const ButtonRect ENROLL_AUTO_ID  = {38, 235, 110, 28};
const ButtonRect ENROLL_START    = {155, 235, 170, 28};
const ButtonRect ENROLL_DELETE   = {332, 235, 110, 28};
const ButtonRect ENROLL_MINUS_HIT = {38, 164, 92, 52};
const ButtonRect ENROLL_PLUS_HIT  = {350, 164, 92, 52};
const ButtonRect USERS_ADD     = {26, 244, 120, 28};
const ButtonRect USERS_REFRESH = {180, 244, 120, 28};
const ButtonRect USERS_DELETE  = {334, 244, 120, 28};
const ButtonRect CONFIRM_YES    = {92, 210, 136, 40};
const ButtonRect CONFIRM_NO     = {252, 210, 136, 40};
const ButtonRect OP_CANCEL      = {332, 226, 108, 30};
void text(
  int x,
  int y,
  const char *str,
  uint16_t color,
  uint8_t size)
{
  tft.setCursor(x, y);
  tft.setTextColor(color);
  tft.setTextSize(size);
  tft.print(str);
}

int textWidthPx(
  const char *str,
  uint8_t size)
{
  if (str == NULL)
    return 0;

  return (int)strlen(str) * 6 * size;
}

void textCentered(
  int x,
  int y,
  int w,
  const char *str,
  uint16_t color,
  uint8_t size)
{
  int tw = textWidthPx(str, size);
  int tx = x + (w - tw) / 2;

  if (tx < x + 2)
    tx = x + 2;

  text(tx, y, str, color, size);
}

void textRight(
  int rightX,
  int y,
  const char *str,
  uint16_t color,
  uint8_t size)
{
  int tx = rightX - textWidthPx(str, size);

  if (tx < 0)
    tx = 0;

  text(tx, y, str, color, size);
}

String fitText(
  String value,
  uint8_t maxChars)
{
  if (value.length() <= maxChars)
    return value;

  if (maxChars <= 2)
    return value.substring(0, maxChars);

  return value.substring(0, maxChars - 2) + "..";
}

void panel(
  int x,
  int y,
  int w,
  int h,
  uint16_t fill,
  uint16_t border)
{
  tft.fillRoundRect(x, y, w, h, 8, fill);
  tft.drawRoundRect(x, y, w, h, 8, border);
}
void drawButtonXY(
  int bx,
  int by,
  int bw,
  int bh,
  const char *label,
  uint16_t border,
  bool selected)
{
  uint16_t fill =
    selected ? border : UI_CARD2;

  uint16_t line =
    selected ? UI_YELLOW : border;

  uint16_t fg =
    selected ? UI_BG : UI_TEXT;

  tft.fillRoundRect(
    bx,
    by,
    bw,
    bh,
    7,
    fill);

  tft.drawRoundRect(
    bx,
    by,
    bw,
    bh,
    7,
    line);

  if (selected)
  {
    tft.fillRoundRect(
      bx + 3,
      by + 3,
      4,
      bh - 6,
      2,
      UI_YELLOW);
  }

  int ty = by + (bh - 8) / 2;

  textCentered(
    bx,
    ty,
    bw,
    label,
    fg,
    1);
}


#define drawButton(B,L,C,S) \
  drawButtonXY( \
    (B).x, \
    (B).y, \
    (B).w, \
    (B).h, \
    (L), \
    (C), \
    (S))
void drawStatusBadge(
  int x,
  int y,
  const char *label,
  bool ok)
{
  uint16_t c =
    ok ? UI_GREEN2 : UI_RED;
  tft.fillRoundRect(
    x, y, 66, 18,
    7,
    UI_CARD2);
  tft.drawRoundRect(
    x, y, 66, 18,
    7,
    c);
  tft.fillCircle(
    x + 9,
    y + 9,
    3,
    c);
  text(
    x + 17,
    y + 6,
    label,
    UI_WHITE,
    1);
}
void drawCyberBackground()
{
  tft.fillScreen(UI_BG);

  // Clean professional top accent.
  tft.fillRect(0, 0, 160, 4, UI_CYAN);
  tft.fillRect(160, 0, 160, 4, UI_BLUE2);
  tft.fillRect(320, 0, 160, 4, UI_BLUE);

  // Soft content separators.
  tft.drawFastHLine(12, 66, SCREEN_W - 24, UI_GRID);
  tft.drawFastHLine(12, 270, SCREEN_W - 24, UI_GRID);

  // Small blue corner accents.
  tft.fillRoundRect(14, 68, 42, 3, 1, UI_CYAN);
  tft.fillRoundRect(424, 68, 42, 3, 1, UI_BLUE2);
}


void drawTechCorners(
  int x,
  int y,
  int w,
  int h,
  uint16_t color)
{
  const int s = 14;
  tft.drawFastHLine(x, y, s, color);
  tft.drawFastVLine(x, y, s, color);
  tft.drawFastHLine(x + w - s, y, s, color);
  tft.drawFastVLine(x + w - 1, y, s, color);
  tft.drawFastHLine(x, y + h - 1, s, color);
  tft.drawFastVLine(x, y + h - s, s, color);
  tft.drawFastHLine(x + w - s, y + h - 1, s, color);
  tft.drawFastVLine(x + w - 1, y + h - s, s, color);
}
void drawSignalDot(
  int x,
  int y,
  uint16_t color)
{
  tft.fillCircle(
    x,
    y,
    3,
    color);
  tft.drawCircle(
    x,
    y,
    6,
    color);
}
void drawSectionTitle(
  int x,
  int y,
  const char *small,
  const char *large,
  uint16_t accent)
{
  text(
    x,
    y,
    small,
    UI_MUTED,
    1);
  text(
    x,
    y + 13,
    large,
    UI_WHITE,
    2);
  tft.fillRect(
    x,
    y + 34,
    54,
    3,
    accent);
}
void drawStartupSplash()
{
  tft.fillScreen(UI_BG);
  tft.fillRect(
    0,
    0,
    SCREEN_W,
    4,
    UI_CYAN);
  tft.fillRect(
    0,
    4,
    SCREEN_W,
    2,
    UI_BLUE);
  tft.fillRoundRect(
    185,
    24,
    110,
    110,
    22,
    UI_CARD);
  tft.drawRoundRect(
    185,
    24,
    110,
    110,
    22,
    UI_BORDER);
  for (
    int r = 14;
    r <= 40;
    r += 7)
  {
    tft.drawCircle(
      240,
      73,
      r,
      (r % 14 == 0)
        ? UI_CYAN
        : UI_BLUE2);
  }
  tft.fillRect(
    198,
    72,
    18,
    42,
    UI_CARD);
  tft.fillRect(
    264,
    72,
    18,
    42,
    UI_CARD);
  text(
    124,
    151,
    "SMART ATTENDANCE",
    UI_WHITE,
    3);
  text(
    154,
    185,
    "BIOMETRIC TERMINAL",
    UI_CYAN,
    2);
  text(
    171,
    213,
    "Initializing secure services",
    UI_TEXT2,
    1);
  drawStatusPill(
    28,
    247,
    98,
    fingerprintReady
      ? "FP READY"
      : "FP CHECK",
    UI_CYAN);
  drawStatusPill(
    137,
    247,
    98,
    rtcReady
      ? "RTC READY"
      : "RTC WAIT",
    rtcReady
      ? UI_WHITE
      : UI_BLUE2);
  drawStatusPill(
    246,
    247,
    98,
    sdReady
      ? "SD READY"
      : "SD CHECK",
    sdReady
      ? UI_CYAN
      : UI_BLUE2);
  drawStatusPill(
    355,
    247,
    98,
    picLinkReady
      ? "PIC READY"
      : "PIC WAIT",
    picLinkReady
      ? UI_WHITE
      : UI_BLUE2);
  tft.drawRoundRect(
    92,
    285,
    296,
    8,
    4,
    UI_BORDER2);
  for (
    int w = 0;
    w <= 286;
    w += 22)
  {
    tft.fillRoundRect(
      97,
      288,
      w,
      2,
      1,
      UI_CYAN);
    delay(12);
  }
  delay(180);
}
void drawAccessResult(
  bool granted,
  const String &name,
  const String &status)
{
  uint16_t accent =
    granted ? UI_CYAN : UI_WHITE;

  tft.fillScreen(UI_BG);
  tft.fillRect(0, 0, SCREEN_W, 4, UI_CYAN);
  tft.fillRect(0, 4, SCREEN_W, 2, UI_BLUE);

  tft.fillRoundRect(
    30,
    28,
    420,
    254,
    16,
    UI_CARD);

  tft.drawRoundRect(
    30,
    28,
    420,
    254,
    16,
    UI_BORDER);

  // Icon block.
  tft.fillCircle(
    104,
    145,
    48,
    UI_BG2);

  tft.drawCircle(
    104,
    145,
    48,
    accent);

  tft.drawCircle(
    104,
    145,
    39,
    UI_BORDER2);

  if (granted)
  {
    tft.drawLine(78, 145, 96, 163, accent);
    tft.drawLine(96, 163, 132, 120, accent);
  }
  else
  {
    tft.drawLine(80, 121, 128, 169, accent);
    tft.drawLine(128, 121, 80, 169, accent);
  }

  text(
    176,
    52,
    granted
      ? "ATTENDANCE RECORDED"
      : "UNKNOWN FINGERPRINT",
    UI_WHITE,
    2);

  text(
    176,
    78,
    granted
      ? "Biometric identity verified"
      : "Fingerprint not recognized",
    UI_TEXT2,
    1);

  tft.drawFastHLine(
    176,
    98,
    238,
    UI_BORDER2);

  text(
    176,
    112,
    "USER",
    UI_MUTED,
    1);

  String shown = fitText(name, 18);

  text(
    176,
    128,
    shown.c_str(),
    UI_WHITE,
    2);

  char idText[18];

  if (granted)
    sprintf(idText, "ID #%03u", currentFingerID);
  else
    strcpy(idText, "ID ---");

  text(
    176,
    160,
    idText,
    UI_CYAN,
    1);

  textRight(
    414,
    160,
    currentScanTime,
    UI_TEXT,
    1);

  tft.fillRoundRect(
    176,
    184,
    238,
    44,
    9,
    UI_BG2);

  tft.drawRoundRect(
    176,
    184,
    238,
    44,
    9,
    UI_BORDER2);

  text(
    188,
    194,
    "STATUS",
    UI_MUTED,
    1);

  text(
    188,
    210,
    status.c_str(),
    accent,
    1);

  if (granted)
  {
    char confText[18];

    sprintf(
      confText,
      "MATCH %u",
      currentConfidence);

    textRight(
      402,
      210,
      confText,
      UI_CYAN,
      1);
  }
  else
  {
    textRight(
      402,
      210,
      "TRY AGAIN",
      UI_TEXT2,
      1);
  }

  textCentered(
    176,
    249,
    238,
    granted
      ? "Returning to scanner..."
      : "Access denied - returning...",
    UI_MUTED,
    1);
}

void glassPanel(
  int x,
  int y,
  int w,
  int h,
  uint16_t border)
{
  tft.fillRoundRect(
    x + 2,
    y + 3,
    w,
    h,
    10,
    UI_DARK);
  tft.fillRoundRect(
    x,
    y,
    w,
    h,
    10,
    UI_CARD);
  tft.drawRoundRect(
    x,
    y,
    w,
    h,
    10,
    border);
  if (
    w > 12 &&
    h > 12)
  {
    tft.drawFastHLine(
      x + 12,
      y + 1,
      w - 24,
      UI_BORDER2);
  }
}
void drawStatusPill(
  int x,
  int y,
  int w,
  const char *label,
  uint16_t color)
{
  tft.fillRoundRect(
    x,
    y,
    w,
    20,
    8,
    color);

  tft.drawRoundRect(
    x,
    y,
    w,
    20,
    8,
    UI_YELLOW);

  tft.fillCircle(
    x + 10,
    y + 10,
    3,
    UI_YELLOW);

  int labelX = x + 18;
  int labelW = w - 22;

  textCentered(
    labelX,
    y + 6,
    labelW,
    label,
    UI_BG,
    1);
}


void drawMetricCard(
  int x,
  int y,
  int w,
  const char *label,
  const char *value,
  uint16_t accent)
{
  tft.fillRoundRect(
    x + 2,
    y + 2,
    w,
    36,
    7,
    UI_DARK);
  tft.fillRoundRect(
    x,
    y,
    w,
    36,
    7,
    UI_CARD2);
  tft.drawRoundRect(
    x,
    y,
    w,
    36,
    7,
    accent);
  tft.fillRect(
    x,
    y,
    4,
    36,
    accent);
  text(
    x + 10,
    y + 6,
    label,
    UI_MUTED,
    1);
  text(
    x + 10,
    y + 19,
    value,
    UI_WHITE,
    1);
}
void drawNavIcon(
  int index,
  int cx,
  int cy,
  uint16_t color)
{
  if (index == 0)
  {
    tft.drawLine(cx - 7, cy, cx, cy - 7, color);
    tft.drawLine(cx, cy - 7, cx + 7, cy, color);
    tft.drawRect(cx - 5, cy, 10, 8, color);
  }
  else if (index == 1)
  {
    tft.drawCircle(cx, cy - 4, 4, color);
    tft.drawRoundRect(cx - 8, cy + 1, 16, 8, 4, color);
  }
  else if (index == 2)
  {
    tft.drawRect(cx - 7, cy - 7, 14, 14, color);
    tft.drawFastHLine(cx - 4, cy - 3, 8, color);
    tft.drawFastHLine(cx - 4, cy + 1, 8, color);
    tft.drawFastHLine(cx - 4, cy + 5, 8, color);
  }
  else
  {
    tft.drawCircle(cx - 5, cy - 4, 4, color);
    tft.drawCircle(cx + 6, cy - 4, 4, color);
    tft.drawRoundRect(cx - 11, cy + 1, 12, 8, 4, color);
    tft.drawRoundRect(cx + 1, cy + 1, 12, 8, 4, color);
  }
}
void drawStepDots(
  int activeStep)
{
  const int startX = 132;
  const int y = 78;
  const int gap = 72;
  for (int i = 1; i <= 4; i++)
  {
    int x =
      startX + (i - 1) * gap;
    uint16_t c =
      i <= activeStep
        ? UI_CYAN
        : UI_BORDER;
    tft.fillCircle(
      x,
      y,
      5,
      c);
    tft.drawCircle(
      x,
      y,
      8,
      c);
    if (i < 4)
    {
      tft.drawFastHLine(
        x + 9,
        y,
        gap - 18,
        i < activeStep
          ? UI_CYAN
          : UI_BORDER);
    }
  }
}
int freeRam()
{
  extern int __heap_start;
  extern int *__brkval;
  int v;
  return
    (int)&v -
    (__brkval == 0
      ? (int)&__heap_start
      : (int)__brkval);
}
void showScannerPrompt(
  const char *message,
  uint16_t color)
{
  if (currentPage != PAGE_HOME)
    return;

  tft.fillRoundRect(
    42,
    232,
    214,
    23,
    8,
    UI_BG2);

  tft.drawRoundRect(
    42,
    232,
    214,
    23,
    8,
    color);

  tft.fillCircle(
    57,
    243,
    3,
    color);

  textCentered(
    67,
    239,
    180,
    message,
    color,
    1);
}

void printFingerprintErrorThrottled(
  const __FlashStringHelper *message)
{
  if (
    millis() -
    lastFingerErrorPrint <
    4000UL)
  {
    return;
  }
  lastFingerErrorPrint =
    millis();
  Serial.print(
    F("AS608: "));
  Serial.println(
    message);
}
void markFingerprintTemporaryError(
  const char *screenMessage,
  const __FlashStringHelper *serialMessage)
{
  scanState =
    STATE_ERROR;
  resultHoldUntil =
    millis() + 900UL;
  showScannerPrompt(
    screenMessage,
    UI_ORANGE);
  printFingerprintErrorThrottled(
    serialMessage);
}
void tryReconnectFingerprint()
{
  if (fingerprintReady)
    return;
  if (
    millis() -
    fingerprintReconnectTimer <
    5000UL)
  {
    return;
  }
  fingerprintReconnectTimer =
    millis();
  if (
    finger.verifyPassword())
  {
    fingerprintReady =
      true;
    finger.getTemplateCount();
    templateCount =
      finger.templateCount;
    Serial.println(
      F("AS608 RECONNECTED"));
    if (
      currentPage ==
      PAGE_HOME)
    {
      drawHomePage();
    }
  }
}
bool getRawTouch(
  int &rawX,
  int &rawY,
  int &pressure)
{
  TSPoint p = ts.getPoint();
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);
  pressure = p.z;
  if (
    pressure < MINPRESSURE ||
    pressure > MAXPRESSURE)
  {
    return false;
  }
  rawX = p.y;
  rawY = p.x;
  return true;
}
bool getTouch(
  int &screenX,
  int &screenY)
{
  int rawX;
  int rawY;
  int pressure;
  if (!getRawTouch(rawX, rawY, pressure))
    return false;
  screenX =
    map(
      rawX,
      touchCal.leftRaw,
      touchCal.rightRaw,
      CAL_X1,
      CAL_X2);
  screenY =
    map(
      rawY,
      touchCal.topRaw,
      touchCal.bottomRaw,
      CAL_Y1,
      CAL_Y2);
  screenX =
    constrain(
      screenX,
      0,
      SCREEN_W - 1);
  screenY =
    constrain(
      screenY,
      0,
      SCREEN_H - 1);
  return true;
}
bool pointInButtonXY(
  int x,
  int y,
  int bx,
  int by,
  int bw,
  int bh)
{
  return
    x >= bx &&
    x < bx + bw &&
    y >= by &&
    y < by + bh;
}
#define pointInButton(X,Y,B) \
  pointInButtonXY( \
    (X), \
    (Y), \
    (B).x, \
    (B).y, \
    (B).w, \
    (B).h)
void waitTouchRelease()
{
  int rx;
  int ry;
  int p;
  unsigned long start = millis();
  while (millis() - start < 1000)
  {
    if (!getRawTouch(rx, ry, p))
    {
      delay(35);
      return;
    }
    delay(10);
  }
}
void calibrationTarget(
  int x,
  int y)
{
  tft.fillScreen(UI_BG);
  text(
    120,
    105,
    "TOUCH CALIBRATION",
    UI_CYAN,
    2);
  text(
    109,
    137,
    "Touch center of target",
    UI_TEXT,
    1);
  tft.drawCircle(x, y, 12, UI_WHITE);
  tft.drawCircle(x, y, 6, UI_CYAN);
  tft.drawFastHLine(
    x - 17,
    y,
    34,
    UI_CYAN);
  tft.drawFastVLine(
    x,
    y - 17,
    34,
    UI_CYAN);
}
void captureTouch(
  int &rawX,
  int &rawY)
{
  int pressure;
  long sx = 0;
  long sy = 0;
  int samples = 0;
  while (samples < 12)
  {
    int x;
    int y;
    if (getRawTouch(x, y, pressure))
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
  int tlX, tlY;
  int trX, trY;
  int blX, blY;
  int brX, brY;
  calibrationTarget(CAL_X1, CAL_Y1);
  captureTouch(tlX, tlY);
  calibrationTarget(CAL_X2, CAL_Y1);
  captureTouch(trX, trY);
  calibrationTarget(CAL_X1, CAL_Y2);
  captureTouch(blX, blY);
  calibrationTarget(CAL_X2, CAL_Y2);
  captureTouch(brX, brY);
  touchCal.magic = TOUCH_MAGIC;
  touchCal.leftRaw =
    (tlX + blX) / 2;
  touchCal.rightRaw =
    (trX + brX) / 2;
  touchCal.topRaw =
    (tlY + trY) / 2;
  touchCal.bottomRaw =
    (blY + brY) / 2;
  EEPROM.put(0, touchCal);
  tft.fillScreen(UI_BG);
  text(
    118,
    120,
    "CALIBRATION SAVED",
    UI_GREEN,
    2);
  text(
    148,
    157,
    "Starting system...",
    UI_TEXT,
    1);
  delay(1200);
}
void loadTouchCalibration()
{
  EEPROM.get(0, touchCal);
  if (touchCal.magic != TOUCH_MAGIC)
  {
    calibrateTouch();
  }
}
int employeeAddress(uint8_t slot)
{
  return DB_START_ADDR +
         (int)slot *
         (int)sizeof(StoredEmployee);
}
void initializeDatabase()
{
  EEPROM.get(
    DB_HEADER_ADDR,
    dbHeader);
  if (
    dbHeader.magic == DB_MAGIC &&
    dbHeader.version == DB_VERSION)
  {
    return;
  }
  dbHeader.magic = DB_MAGIC;
  dbHeader.version = DB_VERSION;
  EEPROM.put(
    DB_HEADER_ADDR,
    dbHeader);
  StoredEmployee blank;
  memset(&blank, 0, sizeof(blank));
  for (
    uint8_t i = 0;
    i < MAX_STORED_USERS;
    i++)
  {
    EEPROM.put(
      employeeAddress(i),
      blank);
  }
}
int findEmployeeSlot(uint16_t id)
{
  StoredEmployee e;
  for (
    uint8_t i = 0;
    i < MAX_STORED_USERS;
    i++)
  {
    EEPROM.get(
      employeeAddress(i),
      e);
    if (
      e.valid &&
      e.id == id)
    {
      return i;
    }
  }
  return -1;
}
int findFreeEmployeeSlot()
{
  StoredEmployee e;
  for (
    uint8_t i = 0;
    i < MAX_STORED_USERS;
    i++)
  {
    EEPROM.get(
      employeeAddress(i),
      e);
    if (!e.valid)
      return i;
  }
  return -1;
}
bool loadEmployeeRaw(
  uint16_t id,
  void *employeeOut)
{
  if (employeeOut == NULL)
    return false;
  int slot =
    findEmployeeSlot(id);
  if (slot < 0)
    return false;
  StoredEmployee *e =
    (StoredEmployee *)employeeOut;
  EEPROM.get(
    employeeAddress(slot),
    *e);
  return true;
}
#define loadEmployee(ID,E) \
  loadEmployeeRaw( \
    (ID), \
    (void *)&(E))
bool saveEmployee(
  uint16_t id,
  const String &name)
{
  int slot =
    findEmployeeSlot(id);
  StoredEmployee e;
  if (slot < 0)
  {
    slot =
      findFreeEmployeeSlot();
    if (slot < 0)
      return false;
    memset(
      &e,
      0,
      sizeof(e));
    e.valid = 1;
    e.id = id;
    e.checkedIn = 0;
    e.lastDateKey = 0;
  }
  else
  {
    EEPROM.get(
      employeeAddress(slot),
      e);
  }
  String n = name;
  n.trim();
  if (n.length() == 0)
    n = "USER_" + String(id);
  if (n.length() > MAX_NAME_LEN)
    n = n.substring(0, MAX_NAME_LEN);
  memset(
    e.name,
    0,
    sizeof(e.name));
  n.toCharArray(
    e.name,
    sizeof(e.name));
  EEPROM.put(
    employeeAddress(slot),
    e);
  return true;
}
bool deleteEmployeeRecord(
  uint16_t id)
{
  int slot =
    findEmployeeSlot(id);
  if (slot < 0)
    return false;
  StoredEmployee e;
  memset(
    &e,
    0,
    sizeof(e));
  EEPROM.put(
    employeeAddress(slot),
    e);
  return true;
}
String getEmployeeName(
  uint16_t id)
{
  StoredEmployee e;
  if (
    loadEmployee(
      id,
      e))
  {
    return String(e.name);
  }
  return "USER_" + String(id);
}
bool setEmployeePresence(
  uint16_t id,
  bool checkedIn,
  uint32_t dateKey)
{
  StoredEmployee e;
  if (!loadEmployee(id, e))
  {
    if (
      !saveEmployee(
        id,
        "USER_" + String(id)))
    {
      return false;
    }
    if (!loadEmployee(id, e))
      return false;
  }
  int slot =
    findEmployeeSlot(id);
  if (slot < 0)
    return false;
  e.checkedIn =
    checkedIn ? 1 : 0;
  e.lastDateKey =
    dateKey;
  EEPROM.put(
    employeeAddress(slot),
    e);
  return true;
}
bool wasEmployeeCheckedInToday(
  uint16_t id,
  uint32_t dateKey)
{
  StoredEmployee e;
  if (!loadEmployee(id, e))
    return false;
  if (
    e.lastDateKey !=
    dateKey)
  {
    return false;
  }
  return
    e.checkedIn != 0;
}
void printEmployeeDatabase()
{
  Serial.println();
  Serial.println(
    F("========== STORED USERS =========="));
  StoredEmployee e;
  uint16_t count = 0;
  for (
    uint8_t i = 0;
    i < MAX_STORED_USERS;
    i++)
  {
    EEPROM.get(
      employeeAddress(i),
      e);
    if (!e.valid)
      continue;
    count++;
    Serial.print(
      F("ID #"));
    Serial.print(e.id);
    Serial.print(
      F("  NAME="));
    Serial.print(e.name);
    Serial.print(
      F("  CHECKED_IN="));
    Serial.print(
      e.checkedIn
        ? F("YES")
        : F("NO"));
    Serial.print(
      F("  DATEKEY="));
    Serial.println(
      e.lastDateKey);
  }
  Serial.print(
    F("TOTAL DB USERS = "));
  Serial.println(count);
  Serial.println(
    F("=================================="));
}

// ============================================================
// LIST ALL FINGERPRINT IDS STORED INSIDE AS608
// Serial command: LIST_FP
// ============================================================

void listFingerprintIDs()
{
  Serial.println();
  Serial.println(
    F("========== AS608 FINGERPRINT USERS =========="));

  if (!fingerprintReady)
  {
    Serial.println(
      F("ERROR: AS608 OFFLINE"));
    Serial.println(
      F("============================================="));
    return;
  }

  uint8_t countResult =
    finger.getTemplateCount();

  if (countResult == FINGERPRINT_OK)
  {
    templateCount =
      finger.templateCount;

    Serial.print(
      F("SENSOR TEMPLATE COUNT = "));
    Serial.println(
      templateCount);
  }
  else
  {
    Serial.println(
      F("WARNING: Could not read sensor template count"));
  }

  uint16_t found = 0;

  for (
    uint16_t id = 1;
    id <= 127;
    id++)
  {
    uint8_t result =
      finger.loadModel(id);

    if (result == FINGERPRINT_OK)
    {
      found++;

      Serial.print(
        F("ID #"));
      Serial.print(id);

      int slot =
        findEmployeeSlot(id);

      if (slot >= 0)
      {
        Serial.print(
          F("  NAME="));
        Serial.print(
          getEmployeeName(id));
        Serial.print(
          F("  SOURCE=AS608+EEPROM"));
      }
      else
      {
        Serial.print(
          F("  NAME=NOT_SAVED"));
        Serial.print(
          F("  SOURCE=AS608_ONLY"));
      }

      Serial.println();
    }
    else if (
      result ==
      FINGERPRINT_PACKETRECIEVEERR)
    {
      Serial.print(
        F("WARNING: Communication error while checking ID #"));
      Serial.println(id);
    }

    // Small delay keeps the 9600-baud sensor scan stable.
    delay(2);
  }

  Serial.print(
    F("TOTAL FINGERPRINT IDS FOUND = "));
  Serial.println(found);
  Serial.println(
    F("============================================="));
}

// ============================================================
// CLEAR ALL MEGA EEPROM USER RECORDS
// This does NOT erase attendance CSV history.
// ============================================================

void clearAllEmployeeRecords()
{
  StoredEmployee blank;
  memset(
    &blank,
    0,
    sizeof(blank));

  for (
    uint8_t i = 0;
    i < MAX_STORED_USERS;
    i++)
  {
    EEPROM.put(
      employeeAddress(i),
      blank);
  }
}

// ============================================================
// DELETE ALL USERS
// Serial command: DELETE_ALL
//
// Erases:
//   1. All fingerprint templates inside AS608
//   2. All ID/name records inside Mega EEPROM
//
// Does NOT erase ATTEND.CSV history.
// ============================================================

void deleteAllUsersFromSerial()
{
  Serial.println();
  Serial.println(
    F("========== DELETE ALL USERS =========="));

  if (!fingerprintReady)
  {
    Serial.println(
      F("ERROR: AS608 OFFLINE - NOTHING DELETED"));
    Serial.println(
      F("======================================"));
    return;
  }

  Serial.println(
    F("Deleting every fingerprint from AS608..."));

  uint8_t result =
    finger.emptyDatabase();

  if (result != FINGERPRINT_OK)
  {
    Serial.print(
      F("DELETE ALL FAILED. SENSOR CODE = 0x"));
    Serial.println(
      result,
      HEX);
    Serial.println(
      F("EEPROM USER DATABASE WAS NOT CLEARED."));
    Serial.println(
      F("======================================"));
    return;
  }

  clearAllEmployeeRecords();

  templateCount = 0;
  userListCount = 0;
  userListSelected = 0;
  userListTop = 0;
  switchFocus = 0;
  userDeleteArmed = false;
  userListNeedsRefresh = true;

  enrollID = 1;
  pendingEnrollName = "";

  currentFingerID = 0;
  currentConfidence = 0;
  strcpy(currentName, "---");
  strcpy(currentScanTime, "--:--:--");
  strcpy(currentStatus, "READY");

  lastVerifiedID = 0;
  lastVerifiedConfidence = 0;
  strcpy(lastVerifiedName, "NO USER YET");
  strcpy(lastVerifiedTime, "--:--:--");
  strcpy(lastVerifiedStatus, "---");

  Serial.println(
    F("AS608: ALL FINGERPRINTS DELETED"));
  Serial.println(
    F("MEGA EEPROM: ALL USER NAMES/IDS DELETED"));
  Serial.println(
    F("ATTEND.CSV: NOT DELETED"));
  Serial.println(
    F("DELETE ALL COMPLETE"));
  Serial.println(
    F("======================================"));
}

DateTime currentRTC()
{
  return picRtcBase;
}
void handlePICButton(const char *command);
void processPICLine(char *line)
{
  if (strncmp(line, "TIME,", 5) == 0 && strlen(line) >= 24)
  {
    int y  = (line[5]-'0')*1000 + (line[6]-'0')*100 +
             (line[7]-'0')*10 + (line[8]-'0');
    int mo = (line[10]-'0')*10 + (line[11]-'0');
    int d  = (line[13]-'0')*10 + (line[14]-'0');
    int h  = (line[16]-'0')*10 + (line[17]-'0');
    int mi = (line[19]-'0')*10 + (line[20]-'0');
    int s  = (line[22]-'0')*10 + (line[23]-'0');
    if (y >= 2000 && y <= 2099 &&
        mo >= 1 && mo <= 12 &&
        d >= 1 && d <= 31 &&
        h >= 0 && h <= 23 &&
        mi >= 0 && mi <= 59 &&
        s >= 0 && s <= 59)
    {
      picRtcBase = DateTime(y, mo, d, h, mi, s);
      picRtcSyncMillis = millis();
      lastPicPacketMillis = millis();
      picLinkReady = true;
      rtcReady = true;
    }
    return;
  }
  if (strcmp(line, "BTN_ADD") == 0 ||
      strcmp(line, "BTN_OK") == 0 ||
      strcmp(line, "BTN_UP") == 0 ||
      strcmp(line, "BTN_DOWN") == 0)
  {
    Serial.print(F("PIC BUTTON: "));
    Serial.println(line);
    handlePICButton(line);
    return;
  }
  if (strncmp(line, "ACK,", 4) == 0 ||
      strncmp(line, "ERR,", 4) == 0)
  {
    Serial.print(F("PIC: "));
    Serial.println(line);
  }
}
void sendPICFeedback(
  const char *eventName)
{
  Serial.print(
    F("MEGA -> PIC: FEEDBACK,"));
  Serial.println(
    eventName);
  Serial2.print(
    F("FEEDBACK,"));
  Serial2.println(
    eventName);
  Serial2.flush();
}
void pollPIC()
{
  while (Serial2.available())
  {
    char c = (char)Serial2.read();
    if (c == '\r') continue;
    if (c == '\n')
    {
      picRxLine[picRxPos] = '\0';
      if (picRxPos > 0) processPICLine(picRxLine);
      picRxPos = 0;
      continue;
    }
    if (picRxPos < sizeof(picRxLine) - 1)
      picRxLine[picRxPos++] = c;
    else
      picRxPos = 0;
  }
  if (picLinkReady &&
      millis() - lastPicPacketMillis > 3000UL)
  {
    picLinkReady = false;
    rtcReady = false;
  }
}
bool isLeapYear(int y)
{
  return
    (y % 4 == 0 && y % 100 != 0) ||
    (y % 400 == 0);
}
bool validDateString(
  String s)
{
  if (
    s.length() != 10 ||
    s.charAt(4) != '-' ||
    s.charAt(7) != '-')
  {
    return false;
  }
  int y =
    s.substring(0, 4).toInt();
  int m =
    s.substring(5, 7).toInt();
  int d =
    s.substring(8, 10).toInt();
  if (
    y < 2000 ||
    m < 1 ||
    m > 12 ||
    d < 1)
  {
    return false;
  }
  int maxDay = 31;
  if (
    m == 4 ||
    m == 6 ||
    m == 9 ||
    m == 11)
  {
    maxDay = 30;
  }
  else if (m == 2)
  {
    maxDay =
      isLeapYear(y)
        ? 29
        : 28;
  }
  return d <= maxDay;
}
bool parseTimeString(
  String s,
  int &hh,
  int &mm,
  int &ss)
{
  s.trim();
  int c1 =
    s.indexOf(':');
  if (c1 < 0)
    return false;
  int c2 =
    s.indexOf(
      ':',
      c1 + 1);
  hh =
    s.substring(
      0,
      c1).toInt();
  if (c2 < 0)
  {
    mm =
      s.substring(
        c1 + 1).toInt();
    ss = 0;
  }
  else
  {
    mm =
      s.substring(
        c1 + 1,
        c2).toInt();
    ss =
      s.substring(
        c2 + 1).toInt();
  }
  if (
    hh < 0 ||
    hh > 23 ||
    mm < 0 ||
    mm > 59 ||
    ss < 0 ||
    ss > 59)
  {
    return false;
  }
  return true;
}
uint32_t dateKey(
  const DateTime &now)
{
  return
    (uint32_t)now.year() *
    10000UL +
    (uint32_t)now.month() *
    100UL +
    (uint32_t)now.day();
}
int minuteOfDay(
  const DateTime &now)
{
  return
    now.hour() * 60 +
    now.minute();
}
String modeText()
{
  if (attendanceMode == MODE_IN)
    return "IN";
  if (attendanceMode == MODE_OUT)
    return "OUT";
  return "AUTO";
}

int switchFocusCount()
{
  if (currentPage == PAGE_HOME)
    return 6;   // AUTO, IN, OUT, ENROLL, LOGS, USERS

  if (currentPage == PAGE_ENROLL)
    return 9;   // SET NAME, ID-, ID+, AUTO ID, START, DELETE, HOME, LOGS, USERS

  if (currentPage == PAGE_LOGS)
    return 3;   // HOME, ENROLL, USERS

  // USERS: every stored fingerprint row + 6 actions
  return (int)userListCount + 6;
}

String switchFocusLabel()
{
  if (currentPage == PAGE_HOME)
  {
    static const char *items[] = {
      "AUTO", "IN", "OUT", "ENROLL", "LOGS", "USERS"
    };
    return String(items[constrain(switchFocus, 0, 5)]);
  }

  if (currentPage == PAGE_ENROLL)
  {
    static const char *items[] = {
      "SET NAME", "ID-", "ID+", "AUTO ID", "START", "DELETE", "HOME", "LOGS", "USERS"
    };
    return String(items[constrain(switchFocus, 0, 8)]);
  }

  if (currentPage == PAGE_LOGS)
  {
    static const char *items[] = {
      "HOME", "ENROLL", "USERS"
    };
    return String(items[constrain(switchFocus, 0, 2)]);
  }

  if (switchFocus < (int)userListCount)
  {
    uint16_t id = userListIDs[switchFocus];
    return String("USER #") + String(id);
  }

  int action = switchFocus - (int)userListCount;
  static const char *items[] = {
    "ADD USER", "REFRESH", "DELETE", "HOME", "ENROLL", "LOGS"
  };
  return String(items[constrain(action, 0, 5)]);
}

void drawSwitchLegend()
{
  tft.fillRect(0, 44, SCREEN_W, 22, UI_BG);

  tft.fillRoundRect(
    12,
    46,
    456,
    17,
    7,
    UI_CARD2);

  tft.drawRoundRect(
    12,
    46,
    456,
    17,
    7,
    UI_BORDER2);

  // Enrollment always gives SW4 an immediate cancel function.
  if (enrollmentInProgress)
  {
    textCentered(
      18,
      51,
      438,
      "SW4 = CANCEL ENROLLMENT",
      UI_YELLOW,
      1);

    return;
  }

  if (deleteConfirmActive)
  {
    textCentered(
      18,
      51,
      438,
      "SW2 = DELETE     SW4 = CANCEL",
      UI_ORANGE,
      1);

    return;
  }

  // HOME PAGE = four direct page shortcuts.
  if (currentPage == PAGE_HOME)
  {
    text(20, 51, "S1 HOME", UI_CYAN, 1);
    text(102, 51, "S2 ENROLL", UI_BLUE2, 1);
    text(208, 51, "S3 LOGS", UI_ORANGE, 1);
    text(310, 51, "S4 USERS", UI_GREEN2, 1);
    return;
  }

  // Other pages retain navigation/select behaviour.
  text(20, 51, "S1 HOME", UI_CYAN, 1);
  text(84, 51, "S2 OK", UI_GREEN2, 1);
  text(139, 51, "S3 PREV", UI_ORANGE, 1);
  text(207, 51, "S4 NEXT", UI_BLUE2, 1);

  String selected = fitText(switchFocusLabel(), 16);
  String focus = String("SEL: ") + selected;

  tft.fillRoundRect(
    282,
    48,
    178,
    13,
    5,
    UI_BG2);

  textCentered(
    282,
    51,
    178,
    focus.c_str(),
    UI_YELLOW,
    1);
}



void setMode(
  uint8_t mode)
{
  attendanceMode =
    (AttendanceMode)mode;
  Serial.print(
    F("ATTENDANCE MODE = "));
  Serial.println(
    modeText());
}
void drawHeader()
{
  tft.fillRect(
    0,
    0,
    SCREEN_W,
    44,
    UI_NAV);

  tft.drawFastHLine(
    0,
    43,
    SCREEN_W,
    UI_CYAN);

  // Multicolor brand icon.
  tft.fillRoundRect(
    12,
    8,
    26,
    26,
    7,
    UI_BLUE);

  tft.drawRoundRect(
    12,
    8,
    26,
    26,
    7,
    UI_CYAN);

  tft.drawCircle(25, 21, 7, UI_CYAN);
  tft.drawCircle(25, 21, 4, UI_BLUE2);
  tft.fillCircle(25, 21, 2, UI_YELLOW);

  text(
    46,
    7,
    "SMART ATTENDANCE",
    UI_TEXT,
    2);

  text(
    46,
    27,
    "BIOMETRIC TERMINAL",
    UI_CYAN,
    1);

  // FP status.
  tft.fillRoundRect(
    242,
    10,
    38,
    22,
    7,
    fingerprintReady ? UI_TEAL : UI_RED);

  tft.drawRoundRect(
    242,
    10,
    38,
    22,
    7,
    UI_BORDER2);

  tft.fillCircle(
    250,
    21,
    3,
    fingerprintReady ? UI_GREEN2 : UI_YELLOW);

  text(
    257,
    17,
    "FP",
    UI_TEXT,
    1);

  // RTC status.
  tft.fillRoundRect(
    285,
    10,
    40,
    22,
    7,
    rtcReady ? UI_BLUE : UI_ORANGE);

  tft.drawRoundRect(
    285,
    10,
    40,
    22,
    7,
    UI_BORDER2);

  tft.fillCircle(
    293,
    21,
    3,
    rtcReady ? UI_GREEN2 : UI_YELLOW);

  text(
    300,
    17,
    "RTC",
    UI_TEXT,
    1);

  // Clock/date card.
  tft.fillRoundRect(
    330,
    6,
    142,
    32,
    9,
    UI_CARD);

  tft.drawRoundRect(
    330,
    6,
    142,
    32,
    9,
    UI_CYAN);

  text(338, 9, "TIME", UI_YELLOW, 1);
  textRight(464, 9, "DATE", UI_CYAN, 1);

  lastClockHour = -1;
  lastClockMinute = -1;
  lastClockSecond = -1;
  lastClockDateKey = 0;
  lastClockRtcReady = false;

  drawSwitchLegend();
}


void updateClock()
{
  if (
    currentPage == PAGE_HOME &&
    (
      scanState == STATE_MATCHED ||
      scanState == STATE_DENIED ||
      scanState == STATE_ERROR
    ))
  {
    return;
  }

  if (!rtcReady)
  {
    if (
      lastClockRtcReady ||
      lastClockHour != -2)
    {
      tft.fillRect(
        336,
        20,
        130,
        14,
        UI_CARD);

      textCentered(
        336,
        23,
        130,
        "WAITING FOR RTC",
        UI_BLUE2,
        1);

      lastClockHour = -2;
      lastClockMinute = -2;
      lastClockSecond = -2;
      lastClockDateKey = 0;
      lastClockRtcReady = false;
    }

    return;
  }

  DateTime now = currentRTC();
  uint32_t dk = dateKey(now);

  if (currentDayKey == 0)
  {
    currentDayKey = dk;
  }
  else if (dk != currentDayKey)
  {
    currentDayKey = dk;
    totalToday = 0;
  }

  if (
    now.hour() != lastClockHour ||
    now.minute() != lastClockMinute ||
    now.second() != lastClockSecond)
  {
    char timeBuffer[9];

    sprintf(
      timeBuffer,
      "%02d:%02d:%02d",
      now.hour(),
      now.minute(),
      now.second());

    tft.fillRect(
      338,
      21,
      58,
      12,
      UI_CARD);

    text(
      338,
      23,
      timeBuffer,
      UI_WHITE,
      1);

    lastClockHour = now.hour();
    lastClockMinute = now.minute();
    lastClockSecond = now.second();
  }

  if (dk != lastClockDateKey)
  {
    char dateBuffer[11];

    sprintf(
      dateBuffer,
      "%02d/%02d/%04d",
      now.day(),
      now.month(),
      now.year());

    tft.fillRect(
      400,
      21,
      66,
      12,
      UI_CARD);

    textRight(
      466,
      23,
      dateBuffer,
      UI_CYAN,
      1);

    lastClockDateKey = dk;
  }

  lastClockRtcReady = true;
}

void drawFingerprint(
  uint16_t color)
{
  int cx = 137;
  int cy = 145;
  tft.drawCircle(
    cx, cy,
    11,
    color);
  tft.drawCircle(
    cx, cy,
    18,
    color);
  tft.drawCircle(
    cx, cy,
    25,
    color);
  tft.drawCircle(
    cx, cy,
    32,
    color);
  tft.drawCircle(
    cx, cy,
    39,
    color);
  tft.fillRect(
    91,
    143,
    17,
    43,
    UI_BG);
  tft.fillRect(
    167,
    148,
    16,
    41,
    UI_BG);
  tft.drawFastVLine(
    cx,
    145,
    40,
    color);
  tft.drawFastVLine(
    cx - 7,
    153,
    28,
    color);
  tft.drawFastVLine(
    cx + 7,
    153,
    30,
    color);
}
void drawHomePage()
{
  drawCyberBackground();
  drawHeader();

  // ---------------- LEFT: SCANNER ----------------
  tft.fillRoundRect(
    16,
    70,
    266,
    196,
    14,
    UI_CARD);

  tft.drawRoundRect(
    16,
    70,
    266,
    196,
    14,
    UI_CYAN);

  tft.fillRoundRect(
    28,
    80,
    116,
    18,
    7,
    UI_TEAL);

  text(
    36,
    86,
    "BIOMETRIC SCANNER",
    UI_TEXT,
    1);

  text(
    28,
    104,
    "PLACE YOUR FINGER",
    UI_YELLOW,
    2);

  tft.fillRoundRect(
    28,
    122,
    242,
    2,
    1,
    UI_BLUE2);

  // Colorful scanner target.
  tft.fillCircle(
    149,
    173,
    48,
    UI_BG2);

  tft.drawCircle(
    149,
    173,
    48,
    UI_CYAN);

  tft.drawCircle(
    149,
    173,
    40,
    UI_BLUE);

  tft.drawCircle(
    149,
    173,
    32,
    UI_BLUE2);

  for (int r = 9; r <= 28; r += 6)
  {
    uint16_t c = UI_CYAN;

    if (r >= 15)
      c = UI_BLUE2;

    if (r >= 21)
      c = UI_BLUE2;

    if (r >= 27)
      c = UI_YELLOW;

    tft.drawCircle(
      149,
      170,
      r,
      c);
  }

  tft.fillRect(
    116,
    168,
    13,
    34,
    UI_BG2);

  tft.fillRect(
    169,
    172,
    13,
    31,
    UI_BG2);

  tft.drawFastVLine(
    149,
    170,
    31,
    UI_GREEN2);

  tft.drawFastVLine(
    143,
    176,
    22,
    UI_CYAN);

  tft.drawFastVLine(
    155,
    176,
    22,
    UI_BLUE2);

  // Scanner prompt.
  tft.fillRoundRect(
    42,
    232,
    214,
    23,
    8,
    fingerprintReady ? UI_TEAL : UI_RED);

  tft.drawRoundRect(
    42,
    232,
    214,
    23,
    8,
    fingerprintReady ? UI_GREEN2 : UI_YELLOW);

  tft.fillCircle(
    57,
    243,
    3,
    fingerprintReady ? UI_GREEN2 : UI_YELLOW);

  textCentered(
    67,
    239,
    180,
    fingerprintReady
      ? "WAITING FOR FINGER"
      : "FINGERPRINT OFFLINE",
    UI_TEXT,
    1);

  // ---------------- RIGHT: ACTIVITY ----------------
  tft.fillRoundRect(
    292,
    70,
    172,
    196,
    14,
    UI_BLUE);

  tft.drawRoundRect(
    292,
    70,
    172,
    196,
    14,
    UI_BLUE2);

  tft.fillRoundRect(
    306,
    80,
    110,
    18,
    7,
    UI_BLUE);

  text(
    317,
    86,
    "LAST ACTIVITY",
    UI_TEXT,
    1);

  tft.fillRoundRect(
    306,
    104,
    144,
    34,
    9,
    UI_BG2);

  tft.drawRoundRect(
    306,
    104,
    144,
    34,
    9,
    UI_CYAN);

  text(
    316,
    111,
    "USER",
    UI_YELLOW,
    1);

  drawEmployeeInfo();
  drawBottomNav();
  updateClock();

  lastScannerOrbit = -1;
}


void drawEmployeeInfo()
{
  tft.fillRect(
    314,
    122,
    128,
    14,
    UI_BG2);

  String shownName = fitText(
    String(lastVerifiedName),
    17);

  text(
    314,
    123,
    shownName.c_str(),
    lastVerifiedID > 0 ? UI_TEXT : UI_TEXT2,
    1);

  // ID card.
  tft.fillRoundRect(
    306,
    145,
    66,
    35,
    8,
    UI_BLUE);

  tft.drawRoundRect(
    306,
    145,
    66,
    35,
    8,
    UI_CYAN);

  // Time card.
  tft.fillRoundRect(
    378,
    145,
    72,
    35,
    8,
    UI_TEAL);

  tft.drawRoundRect(
    378,
    145,
    72,
    35,
    8,
    UI_GREEN2);

  text(315, 151, "ID", UI_YELLOW, 1);
  text(387, 151, "TIME", UI_YELLOW, 1);

  char idText[12];

  if (lastVerifiedID > 0)
    sprintf(idText, "#%03u", lastVerifiedID);
  else
    strcpy(idText, "---");

  textCentered(
    306,
    165,
    66,
    idText,
    UI_TEXT,
    1);

  textCentered(
    378,
    165,
    72,
    lastVerifiedTime,
    UI_TEXT,
    1);

  uint16_t statusColor = UI_BLUE2;

  if (strcmp(lastVerifiedStatus, "IN") == 0)
    statusColor = UI_GREEN;
  else if (strcmp(lastVerifiedStatus, "OUT") == 0)
    statusColor = UI_BLUE;
  else if (strcmp(lastVerifiedStatus, "LATE") == 0)
    statusColor = UI_ORANGE;
  else if (strcmp(lastVerifiedStatus, "VERY LATE") == 0)
    statusColor = UI_RED;

  drawStatusPill(
    306,
    188,
    144,
    lastVerifiedID > 0
      ? lastVerifiedStatus
      : "READY",
    statusColor);

  drawModeButtons();
}


void drawModeButtons()
{
  text(
    306,
    216,
    "ATTENDANCE MODE",
    UI_YELLOW,
    1);

  drawButton(
    HOME_MODE_AUTO,
    "AUTO",
    UI_CYAN,
    attendanceMode == MODE_AUTO);

  drawButton(
    HOME_MODE_IN,
    "IN",
    UI_GREEN2,
    attendanceMode == MODE_IN);

  drawButton(
    HOME_MODE_OUT,
    "OUT",
    UI_ORANGE,
    attendanceMode == MODE_OUT);
}


void drawBottomNav()
{
  tft.fillRect(
    0,
    278,
    SCREEN_W,
    42,
    UI_NAV);

  tft.drawFastHLine(
    0,
    278,
    SCREEN_W,
    UI_CYAN);

  const char *labels[] =
  {
    "HOME",
    "ENROLL",
    "LOGS",
    "USERS"
  };

  const uint16_t accents[] =
  {
    UI_CYAN,
    UI_BLUE2,
    UI_ORANGE,
    UI_GREEN2
  };

  for (int i = 0; i < 4; i++)
  {
    int x = i * 120;
    bool selected = (int)currentPage == i;
    uint16_t accent = accents[i];

    if (selected)
    {
      tft.fillRoundRect(
        x + 8,
        284,
        104,
        28,
        9,
        accent);

      tft.drawRoundRect(
        x + 8,
        284,
        104,
        28,
        9,
        UI_YELLOW);

      tft.fillRoundRect(
        x + 42,
        313,
        36,
        3,
        1,
        UI_YELLOW);
    }
    else
    {
      tft.fillRoundRect(
        x + 8,
        284,
        104,
        28,
        9,
        UI_CARD2);

      tft.drawRoundRect(
        x + 8,
        284,
        104,
        28,
        9,
        accent);
    }

    drawNavIcon(
      i,
      x + 32,
      298,
      selected ? UI_BG : accent);

    textCentered(
      x + 46,
      294,
      60,
      labels[i],
      selected ? UI_BG : UI_TEXT,
      1);
  }
}


void addAttendance(
  uint16_t id,
  const String &name,
  const String &status)
{
  for (
    int i = MAX_LOGS - 1;
    i > 0;
    i--)
  {
    logs[i] =
      logs[i - 1];
  }
  logs[0].id = id;
  String n = name;
  if (n.length() > MAX_NAME_LEN)
    n = n.substring(0, MAX_NAME_LEN);
  n.toCharArray(
    logs[0].name,
    sizeof(logs[0].name));
  if (rtcReady)
  {
    DateTime now =
      currentRTC();
    sprintf(
      logs[0].date,
      "%04d-%02d-%02d",
      now.year(),
      now.month(),
      now.day());
    sprintf(
      logs[0].time,
      "%02d:%02d:%02d",
      now.hour(),
      now.minute(),
      now.second());
  }
  else
  {
    strcpy(
      logs[0].date,
      "----------");
    strcpy(
      logs[0].time,
      "--:--:--");
  }
  status.toCharArray(
    logs[0].status,
    sizeof(logs[0].status));
  logs[0].valid =
    true;
  totalToday++;
  appendAttendanceCSV(
    id,
    name,
    logs[0].date,
    logs[0].time,
    logs[0].status);
}
String decideAttendanceStatus(
  uint16_t id)
{
  if (attendanceMode == MODE_IN)
    return "IN";
  if (attendanceMode == MODE_OUT)
    return "OUT";
  if (!rtcReady)
    return "IN";
  DateTime now =
    currentRTC();
  uint32_t dk =
    dateKey(now);
  if (
    wasEmployeeCheckedInToday(
      id,
      dk))
  {
    return "OUT";
  }
  int mins =
    minuteOfDay(now);
  if (mins < lateStartMinutes)
    return "IN";
  if (mins <= lateEndMinutes)
    return "LATE";
  return "VERY LATE";
}
void fingerprintMatched()
{
  currentFingerID =
    finger.fingerID;
  currentConfidence =
    finger.confidence;
  String name =
    getEmployeeName(
      currentFingerID);
  if (
    findEmployeeSlot(
      currentFingerID) < 0)
  {
    saveEmployee(
      currentFingerID,
      name);
  }
  String status =
    decideAttendanceStatus(
      currentFingerID);
  strncpy(
    currentName,
    name.c_str(),
    sizeof(currentName) - 1);
  currentName[
    sizeof(currentName) - 1
  ] = '\0';
  strncpy(
    currentStatus,
    status.c_str(),
    sizeof(currentStatus) - 1);
  currentStatus[
    sizeof(currentStatus) - 1
  ] = '\0';
  if (rtcReady)
  {
    DateTime now =
      currentRTC();
    sprintf(
      currentScanTime,
      "%02d:%02d:%02d",
      now.hour(),
      now.minute(),
      now.second());
    bool nowCheckedIn =
      status != "OUT";
    setEmployeePresence(
      currentFingerID,
      nowCheckedIn,
      dateKey(now));
  }
  else
  {
    strcpy(
      currentScanTime,
      "--:--:--");
  }
  lastVerifiedID =
    currentFingerID;
  lastVerifiedConfidence =
    currentConfidence;
  strncpy(
    lastVerifiedName,
    currentName,
    sizeof(lastVerifiedName) - 1);
  lastVerifiedName[
    sizeof(lastVerifiedName) - 1
  ] = '\0';
  strncpy(
    lastVerifiedTime,
    currentScanTime,
    sizeof(lastVerifiedTime) - 1);
  lastVerifiedTime[
    sizeof(lastVerifiedTime) - 1
  ] = '\0';
  strncpy(
    lastVerifiedStatus,
    currentStatus,
    sizeof(lastVerifiedStatus) - 1);
  lastVerifiedStatus[
    sizeof(lastVerifiedStatus) - 1
  ] = '\0';
  scanState =
    STATE_MATCHED;
  addAttendance(
    currentFingerID,
    name,
    status);
  sendPICFeedback(
    "OK");
  resultHoldUntil =
    millis() + 1800UL;
  if (
    currentPage ==
    PAGE_HOME)
  {
    drawAccessResult(
      true,
      name,
      status);
  }
  Serial.println();
  Serial.println(
    F("========== FINGERPRINT MATCH =========="));
  Serial.print(
    F("ID: "));
  Serial.println(
    currentFingerID);
  Serial.print(
    F("Name: "));
  Serial.println(
    currentName);
  Serial.print(
    F("Status: "));
  Serial.println(
    currentStatus);
  Serial.print(
    F("Confidence: "));
  Serial.println(
    currentConfidence);
  Serial.println(
    F("======================================="));
}
void fingerprintDenied()
{
  currentFingerID = 0;
  currentConfidence = 0;
  strcpy(
    currentName,
    "UNKNOWN");
  if (rtcReady)
  {
    DateTime now =
      currentRTC();
    sprintf(
      currentScanTime,
      "%02d:%02d:%02d",
      now.hour(),
      now.minute(),
      now.second());
  }
  else
  {
    strcpy(
      currentScanTime,
      "--:--:--");
  }
  strcpy(
    currentStatus,
    "DENIED");
  scanState =
    STATE_DENIED;
  sendPICFeedback(
    "DENY");
  resultHoldUntil =
    millis() + 1400UL;
  if (
    currentPage ==
    PAGE_HOME)
  {
    drawAccessResult(
      false,
      "UNKNOWN",
      "DENIED");
  }
  Serial.println(
    F("AS608: fingerprint not enrolled"));
}
void resetScanner()
{
  scanState =
    STATE_READY;
  currentFingerID = 0;
  currentConfidence = 0;
  strcpy(
    currentName,
    "---");
  strcpy(
    currentScanTime,
    "--:--:--");
  strcpy(
    currentStatus,
    "READY");
  if (
    currentPage ==
    PAGE_HOME)
  {
    drawHomePage();
  }
}
void scanFingerprint()
{
  if (!fingerprintReady)
  {
    tryReconnectFingerprint();
    return;
  }
  if (
    currentPage !=
    PAGE_HOME)
  {
    return;
  }
  uint8_t p =
    finger.getImage();
  if (
    p ==
    FINGERPRINT_NOFINGER)
  {
    fingerOnSensor =
      false;
    if (
      scanState != STATE_READY &&
      millis() >= resultHoldUntil)
    {
      resetScanner();
    }
    return;
  }
  if (
    p ==
    FINGERPRINT_PACKETRECIEVEERR)
  {
    markFingerprintTemporaryError(
      "SENSOR RETRY",
      F("communication retry"));
    return;
  }
  if (
    p ==
    FINGERPRINT_IMAGEFAIL)
  {
    markFingerprintTemporaryError(
      "TRY AGAIN",
      F("image capture failed"));
    return;
  }
  if (
    p !=
    FINGERPRINT_OK)
  {
    return;
  }
  if (fingerOnSensor)
    return;
  fingerOnSensor =
    true;
  scanState =
    STATE_SCANNING;
  p =
    finger.image2Tz();
  if (
    p ==
    FINGERPRINT_PACKETRECIEVEERR)
  {
    markFingerprintTemporaryError(
      "SENSOR RETRY",
      F("template conversion communication error"));
    return;
  }
  if (
    p ==
    FINGERPRINT_IMAGEMESS ||
    p ==
    FINGERPRINT_FEATUREFAIL ||
    p ==
    FINGERPRINT_INVALIDIMAGE)
  {
    markFingerprintTemporaryError(
      "TRY AGAIN",
      F("finger image quality too low"));
    return;
  }
  if (
    p !=
    FINGERPRINT_OK)
  {
    markFingerprintTemporaryError(
      "TRY AGAIN",
      F("template conversion failed"));
    return;
  }
  p =
    finger.fingerFastSearch();
  if (
    p ==
    FINGERPRINT_OK)
  {
    fingerprintMatched();
    return;
  }
  if (
    p ==
    FINGERPRINT_NOTFOUND)
  {
    fingerprintDenied();
    return;
  }
  if (
    p ==
    FINGERPRINT_PACKETRECIEVEERR)
  {
    markFingerprintTemporaryError(
      "SENSOR RETRY",
      F("search communication error"));
    return;
  }
  markFingerprintTemporaryError(
    "TRY AGAIN",
    F("search failed"));
}
void drawEnrollPage()
{
  drawCyberBackground();
  drawHeader();

  tft.fillRoundRect(
    18,
    70,
    444,
    198,
    14,
    UI_CARD);

  tft.drawRoundRect(
    18,
    70,
    444,
    198,
    14,
    UI_BLUE2);

  tft.fillRoundRect(
    34,
    80,
    180,
    18,
    7,
    UI_BLUE);

  text(
    45,
    86,
    "ENROLL FINGERPRINT",
    UI_TEXT,
    1);

  text(
    34,
    104,
    "Set a name and ID, then scan the same finger twice.",
    UI_CYAN,
    1);

  String existing = getEmployeeName(enrollID);
  bool hasName = findEmployeeSlot(enrollID) >= 0;

  tft.fillRoundRect(
    38,
    118,
    280,
    30,
    8,
    UI_BG2);

  tft.drawRoundRect(
    38,
    118,
    280,
    30,
    8,
    UI_CYAN);

  text(
    48,
    124,
    "NAME",
    UI_YELLOW,
    1);

  String shownName;

  if (pendingEnrollName.length() > 0)
    shownName = pendingEnrollName;
  else if (hasName)
    shownName = existing;
  else
    shownName = "NOT SET";

  shownName = fitText(shownName, 22);

  text(
    92,
    124,
    shownName.c_str(),
    pendingEnrollName.length() > 0
      ? UI_GREEN2
      : UI_TEXT,
    1);

  drawButton(
    ENROLL_SET_NAME,
    "SET NAME",
    UI_BLUE2,
    currentPage == PAGE_ENROLL &&
      switchFocus == 0);

  text(
    38,
    158,
    "FINGERPRINT ID",
    UI_YELLOW,
    1);

  drawButton(
    ENROLL_MINUS,
    "-",
    UI_ORANGE,
    currentPage == PAGE_ENROLL &&
      switchFocus == 1);

  tft.fillRoundRect(
    155,
    171,
    170,
    38,
    9,
    UI_BLUE);

  tft.drawRoundRect(
    155,
    171,
    170,
    38,
    9,
    UI_CYAN);

  char idBuffer[10];
  sprintf(idBuffer, "%03u", enrollID);

  textCentered(
    155,
    179,
    170,
    idBuffer,
    UI_YELLOW,
    2);

  drawButton(
    ENROLL_PLUS,
    "+",
    UI_GREEN2,
    currentPage == PAGE_ENROLL &&
      switchFocus == 2);

  drawStatusPill(
    176,
    211,
    128,
    hasName ? "ID IN USE" : "ID AVAILABLE",
    hasName ? UI_ORANGE : UI_GREEN2);

  drawButton(
    ENROLL_AUTO_ID,
    "AUTO ID",
    UI_CYAN,
    currentPage == PAGE_ENROLL &&
      switchFocus == 3);

  drawButton(
    ENROLL_START,
    "START ENROLL",
    UI_GREEN2,
    currentPage == PAGE_ENROLL &&
      switchFocus == 4);

  drawButton(
    ENROLL_DELETE,
    "DELETE ID",
    UI_RED,
    currentPage == PAGE_ENROLL &&
      switchFocus == 5);

  drawBottomNav();
  updateClock();
}


void updateEnrollID()
{
  tft.fillRoundRect(
    155,
    171,
    170,
    38,
    9,
    UI_BG2);

  tft.drawRoundRect(
    155,
    171,
    170,
    38,
    9,
    UI_BORDER);

  char buffer[10];

  sprintf(
    buffer,
    "%03d",
    enrollID);

  textCentered(
    155,
    179,
    170,
    buffer,
    UI_WHITE,
    2);
}

void enrollMessage(
  const char *line1,
  const char *line2,
  uint16_t color)
{
  // Clear only the page-content area; header and switch bar remain stable.
  tft.fillRect(
    18,
    70,
    444,
    198,
    UI_BG);

  tft.fillRoundRect(
    28,
    80,
    424,
    178,
    14,
    UI_CARD);

  tft.drawRoundRect(
    28,
    80,
    424,
    178,
    14,
    UI_BORDER);

  int step = 2;

  if (strstr(line1, "NAME") != NULL)
    step = 1;
  else if (strstr(line1, "REMOVE") != NULL)
    step = 3;
  else if (strstr(line1, "ENROLLED") != NULL)
    step = 4;

  drawStepDots(step);

  String title = fitText(String(line1), 26);
  String subtitle = fitText(String(line2), 44);

  textCentered(
    48,
    108,
    384,
    title.c_str(),
    color,
    2);

  textCentered(
    48,
    145,
    384,
    subtitle.c_str(),
    UI_WHITE,
    1);

  textCentered(
    48,
    169,
    384,
    "Follow the biometric workflow shown here",
    UI_TEXT2,
    1);

  tft.fillRoundRect(
    46,
    192,
    388,
    24,
    8,
    UI_BG2);

  textCentered(
    46,
    199,
    388,
    "SW4 cancels immediately",
    UI_CYAN,
    1);

  drawButton(
    OP_CANCEL,
    "CANCEL",
    UI_BORDER2,
    false);
}

bool operationCancelTouched()
{
  int x;
  int y;
  if (!getTouch(x, y))
    return false;
  if (
    pointInButton(
      x,
      y,
      OP_CANCEL))
  {
    waitTouchRelease();
    operationCancelRequested = true;
    return true;
  }
  waitTouchRelease();
  return false;
}

bool operationCancelled()
{
  // The enrollment routines are blocking, so PIC UART must also be
  // serviced here or SW4 would not be seen until enrollment finishes.
  pollPIC();

  if (operationCancelRequested)
    return true;

  if (operationCancelTouched())
    return true;

  return false;
}

bool finishEnrollmentCancelIfRequested()
{
  if (!operationCancelRequested)
    return false;

  Serial.println(F("ENROLLMENT CANCELLED BY SWITCH/TOUCH"));

  enrollmentInProgress = false;
  operationCancelRequested = false;
  pendingEnrollName = "";

  setPage(PAGE_HOME);
  return true;
}

bool waitForFingerImage(
  unsigned long timeout)
{
  unsigned long start =
    millis();
  while (
    millis() - start <
    timeout)
  {
    if (operationCancelled())
      return false;
    uint8_t p =
      finger.getImage();
    if (
      p ==
      FINGERPRINT_OK)
    {
      return true;
    }
    delay(25);
  }
  return false;
}
bool waitForFingerRemoval(
  unsigned long timeout)
{
  unsigned long start =
    millis();
  while (
    millis() - start <
    timeout)
  {
    if (operationCancelled())
      return false;
    uint8_t p =
      finger.getImage();
    if (
      p ==
      FINGERPRINT_NOFINGER)
    {
      return true;
    }
    delay(25);
  }
  return false;
}
uint16_t findFreeFingerprintID()
{
  if (!fingerprintReady)
    return 0;
  for (
    uint16_t id = 1;
    id <= 127;
    id++)
  {
    uint8_t p =
      finger.loadModel(id);
    if (
      p !=
      FINGERPRINT_OK)
    {
      return id;
    }
  }
  return 0;
}
bool waitForEnrollmentName(
  String &nameOut)
{
  Serial.println();
  Serial.println(
    F("========== ENTER USER NAME =========="));
  Serial.println(
    F("Type the name and press Send."));
  Serial.println(
    F("Examples:"));
  Serial.println(
    F("  Mohamed Rasmy"));
  Serial.println(
    F("  NAME:Mohamed Rasmy"));
  Serial.println(
    F("Type CANCEL to stop."));
  Serial.println(
    F("Serial Monitor: 115200 + Newline"));
  Serial.println(
    F("====================================="));
  enrollMessage(
    "ENTER NAME ON PC",
    "Serial Monitor: 115200 + Newline",
    UI_CYAN);
  unsigned long start =
    millis();
  while (
    millis() - start <
    60000UL)
  {
    if (operationCancelled())
      return false;
    if (Serial.available())
    {
      String s =
        Serial.readStringUntil('\n');
      s.trim();
      if (s.length() == 0)
        continue;
      String upper = s;
      upper.toUpperCase();
      if (upper == "CANCEL")
      {
        operationCancelRequested = true;
        return false;
      }
      if (upper.startsWith("NAME:"))
      {
        s =
          s.substring(5);
        s.trim();
      }
      else if (
        upper.startsWith(
          "ENROLL_NAME:"))
      {
        s =
          s.substring(12);
        s.trim();
      }
      if (s.length() == 0)
        continue;
      if (s.length() > MAX_NAME_LEN)
        s =
          s.substring(
            0,
            MAX_NAME_LEN);
      nameOut = s;
      Serial.print(
        F("NAME RECEIVED: "));
      Serial.println(
        nameOut);
      return true;
    }
    delay(10);
  }
  return false;
}
void enrollFingerprint()
{
  if (!fingerprintReady)
  {
    enrollMessage(
      "SENSOR ERROR",
      "AS608 not detected",
      UI_RED);
    delay(1500);
    drawEnrollPage();
    return;
  }
  if (
    finger.loadModel(enrollID) ==
    FINGERPRINT_OK)
  {
    enrollMessage(
      "ID ALREADY USED",
      "Delete this ID or select another",
      UI_ORANGE);
    delay(1600);
    drawEnrollPage();
    return;
  }

  enrollmentInProgress = true;
  operationCancelRequested = false;
  drawSwitchLegend();

  if (pendingEnrollName.length() == 0)
  {
    if (
      !waitForEnrollmentName(
        pendingEnrollName))
    {
      if (finishEnrollmentCancelIfRequested())
        return;

      enrollmentInProgress = false;
      enrollMessage(
        "CANCELLED",
        "Enrollment stopped",
        UI_ORANGE);
      delay(900);
      drawEnrollPage();
      return;
    }
  }
  Serial.print(
    F("ENROLLING NAME: "));
  Serial.println(
    pendingEnrollName);
  enrollMessage(
    "PLACE FINGER",
    pendingEnrollName.c_str(),
    UI_CYAN);
  if (!waitForFingerImage(20000))
  {
    if (finishEnrollmentCancelIfRequested())
      return;

    enrollmentInProgress = false;
    enrollMessage(
      "TIMEOUT / CANCEL",
      "No first scan stored",
      UI_RED);
    delay(1200);
    drawEnrollPage();
    return;
  }
  if (
    finger.image2Tz(1) !=
    FINGERPRINT_OK)
  {
    enrollmentInProgress = false;
    enrollMessage(
      "SCAN FAILED",
      "First image conversion failed",
      UI_RED);
    delay(1400);
    drawEnrollPage();
    return;
  }
  enrollMessage(
    "REMOVE FINGER",
    "Wait for sensor to clear",
    UI_ORANGE);
  if (!waitForFingerRemoval(10000))
  {
    if (finishEnrollmentCancelIfRequested())
      return;

    enrollmentInProgress = false;
    enrollMessage(
      "REMOVE / CANCEL",
      "Enrollment stopped",
      UI_RED);
    delay(1200);
    drawEnrollPage();
    return;
  }
  delay(350);
  enrollMessage(
    "PLACE AGAIN",
    "Scan the same finger again",
    UI_CYAN);
  if (!waitForFingerImage(20000))
  {
    if (finishEnrollmentCancelIfRequested())
      return;

    enrollmentInProgress = false;
    enrollMessage(
      "TIMEOUT / CANCEL",
      "Second scan not received",
      UI_RED);
    delay(1200);
    drawEnrollPage();
    return;
  }
  if (
    finger.image2Tz(2) !=
    FINGERPRINT_OK)
  {
    enrollmentInProgress = false;
    enrollMessage(
      "SCAN FAILED",
      "Second image conversion failed",
      UI_RED);
    delay(1400);
    drawEnrollPage();
    return;
  }
  if (
    finger.createModel() !=
    FINGERPRINT_OK)
  {
    enrollmentInProgress = false;
    enrollMessage(
      "NOT MATCHING",
      "Use exactly the same finger",
      UI_RED);
    delay(1600);
    drawEnrollPage();
    return;
  }
  if (
    finger.storeModel(
      enrollID) !=
    FINGERPRINT_OK)
  {
    enrollmentInProgress = false;
    enrollMessage(
      "STORE FAILED",
      "AS608 memory error",
      UI_RED);
    delay(1600);
    drawEnrollPage();
    return;
  }
  if (
    !saveEmployee(
      enrollID,
      pendingEnrollName))
  {
    enrollmentInProgress = false;
    enrollMessage(
      "PRINT SAVED",
      "Name DB full - check Serial",
      UI_ORANGE);
    Serial.println(
      F("WARNING: fingerprint saved but EEPROM user DB is full"));
    delay(1800);
    drawEnrollPage();
    return;
  }
  finger.getTemplateCount();
  templateCount =
    finger.templateCount;
  userListNeedsRefresh =
    true;
  enrollmentInProgress = false;
  operationCancelRequested = false;
  enrollMessage(
    "ENROLLED!",
    pendingEnrollName.c_str(),
    UI_GREEN);
  Serial.print(
    F("ENROLLMENT COMPLETE -> ID #"));
  Serial.print(
    enrollID);
  Serial.print(
    F("  NAME="));
  Serial.println(
    pendingEnrollName);
  delay(1600);
  pendingEnrollName = "";
  uint16_t next =
    findFreeFingerprintID();
  if (next > 0)
    enrollID = next;
  drawEnrollPage();
}
bool confirmDelete(
  uint16_t id,
  const String &name)
{
  tft.fillScreen(UI_BG);
  panel(
    48,
    55,
    384,
    210,
    UI_CARD,
    UI_RED);
  text(
    110,
    78,
    "CONFIRM DELETE",
    UI_RED,
    2);
  char idBuf[20];
  sprintf(
    idBuf,
    "FINGERPRINT ID #%d",
    id);
  text(
    120,
    120,
    idBuf,
    UI_WHITE,
    1);
  text(
    120,
    145,
    name.c_str(),
    UI_CYAN,
    2);
  text(
    110,
    175,
    "This removes fingerprint + saved name",
    UI_TEXT2,
    1);
  drawButton(
    CONFIRM_YES,
    "DELETE",
    UI_RED,
    false);
  drawButton(
    CONFIRM_NO,
    "CANCEL",
    UI_CYAN,
    false);

  text(
    120,
    262,
    "SW2 = DELETE     SW4 = CANCEL",
    UI_TEXT2,
    1);

  deleteConfirmActive = true;
  deleteConfirmChoice = -1;

  unsigned long start =
    millis();
  while (
    millis() - start <
    15000UL)
  {
    pollPIC();

    if (deleteConfirmChoice == 1)
    {
      deleteConfirmActive = false;
      deleteConfirmChoice = -1;
      return true;
    }

    if (deleteConfirmChoice == 0)
    {
      deleteConfirmActive = false;
      deleteConfirmChoice = -1;
      return false;
    }

    int x;
    int y;
    if (!getTouch(x, y))
    {
      delay(10);
      continue;
    }
    if (
      pointInButton(
        x,
        y,
        CONFIRM_YES))
    {
      waitTouchRelease();
      deleteConfirmActive = false;
      deleteConfirmChoice = -1;
      return true;
    }
    if (
      pointInButton(
        x,
        y,
        CONFIRM_NO))
    {
      waitTouchRelease();
      deleteConfirmActive = false;
      deleteConfirmChoice = -1;
      return false;
    }
    waitTouchRelease();
  }

  deleteConfirmActive = false;
  deleteConfirmChoice = -1;
  return false;
}
void deleteSelectedID()
{
  if (!fingerprintReady)
  {
    enrollMessage(
      "SENSOR ERROR",
      "AS608 not detected",
      UI_RED);
    delay(1200);
    drawEnrollPage();
    return;
  }
  if (
    finger.loadModel(enrollID) !=
    FINGERPRINT_OK)
  {
    enrollMessage(
      "ID IS EMPTY",
      "Nothing to delete",
      UI_ORANGE);
    delay(1200);
    drawEnrollPage();
    return;
  }
  String name =
    getEmployeeName(
      enrollID);
  if (
    !confirmDelete(
      enrollID,
      name))
  {
    drawEnrollPage();
    return;
  }
  uint8_t result =
    finger.deleteModel(
      enrollID);
  if (
    result ==
    FINGERPRINT_OK)
  {
    deleteEmployeeRecord(
      enrollID);
    finger.getTemplateCount();
    templateCount =
      finger.templateCount;
    userListNeedsRefresh =
      true;
    tft.fillScreen(UI_BG);
    text(
      145,
      120,
      "USER DELETED",
      UI_GREEN,
      2);
    text(
      170,
      158,
      name.c_str(),
      UI_WHITE,
      1);
    Serial.print(
      F("DELETED ID #"));
    Serial.print(enrollID);
    Serial.print(
      F("  NAME="));
    Serial.println(name);
    delay(1200);
    uint16_t next =
      findFreeFingerprintID();
    if (next > 0)
      enrollID = next;
  }
  else
  {
    tft.fillScreen(UI_BG);
    text(
      140,
      120,
      "DELETE FAILED",
      UI_RED,
      2);
    delay(1200);
  }
  drawEnrollPage();
}
void drawLogsPage()
{
  drawCyberBackground();
  drawHeader();

  tft.fillRoundRect(
    22,
    73,
    195,
    22,
    8,
    UI_ORANGE);

  text(
    34,
    80,
    "RECENT ATTENDANCE",
    UI_BG,
    1);

  char todayBuf[20];
  sprintf(todayBuf, "TODAY %u", totalToday);

  drawStatusPill(
    366,
    72,
    88,
    todayBuf,
    UI_GREEN2);

  tft.fillRoundRect(
    18,
    100,
    444,
    166,
    14,
    UI_CARD);

  tft.drawRoundRect(
    18,
    100,
    444,
    166,
    14,
    UI_ORANGE);

  tft.fillRoundRect(
    28,
    110,
    424,
    22,
    6,
    UI_BLUE);

  text(36, 117, "ID", UI_YELLOW, 1);
  text(78, 117, "NAME", UI_CYAN, 1);
  text(248, 117, "TIME", UI_GREEN2, 1);
  text(350, 117, "STATUS", UI_ORANGE, 1);

  for (int i = 0; i < MAX_LOGS; i++)
  {
    int y = 137 + i * 15;

    if (i % 2 == 0)
    {
      tft.fillRect(
        29,
        y - 3,
        422,
        14,
        UI_BG2);
    }

    if (!logs[i].valid)
    {
      text(36, y, "---", UI_MUTED, 1);
      continue;
    }

    char idText[8];
    sprintf(idText, "%03u", logs[i].id);

    text(36, y, idText, UI_CYAN, 1);

    String n = fitText(String(logs[i].name), 20);
    text(78, y, n.c_str(), UI_TEXT, 1);

    text(248, y, logs[i].time, UI_GREEN2, 1);

    String status = fitText(String(logs[i].status), 12);

    uint16_t sc = UI_CYAN;

    if (status == "IN")
      sc = UI_GREEN2;
    else if (status == "OUT")
      sc = UI_BLUE2;
    else if (status == "LATE")
      sc = UI_ORANGE;
    else if (status == "VERY LATE")
      sc = UI_RED;

    text(350, y, status.c_str(), sc, 1);
  }

  drawBottomNav();
  updateClock();
}


uint16_t selectedUserID()
{
  if (
    userListCount == 0 ||
    userListSelected < 0 ||
    userListSelected >= userListCount)
  {
    return 0;
  }
  return
    userListIDs[
      userListSelected];
}
void normalizeUserSelection()
{
  if (userListCount == 0)
  {
    userListSelected = 0;
    userListTop = 0;
    return;
  }
  if (userListSelected < 0)
    userListSelected = 0;
  if (userListSelected >= userListCount)
    userListSelected =
      userListCount - 1;
  if (userListSelected < userListTop)
    userListTop =
      userListSelected;
  if (
    userListSelected >=
    userListTop + USERS_VISIBLE_ROWS)
  {
    userListTop =
      userListSelected -
      USERS_VISIBLE_ROWS + 1;
  }
  int maxTop =
    userListCount -
    USERS_VISIBLE_ROWS;
  if (maxTop < 0)
    maxTop = 0;
  if (userListTop > maxTop)
    userListTop = maxTop;
  if (userListTop < 0)
    userListTop = 0;
}
void refreshUserList()
{
  userListCount = 0;
  userListSelected = 0;
  userListTop = 0;
  userDeleteArmed = false;
  if (!fingerprintReady)
  {
    userListNeedsRefresh = false;
    return;
  }
  for (
    uint16_t id = 1;
    id <= 127 &&
    userListCount < USER_LIST_MAX;
    id++)
  {
    if (
      finger.loadModel(id) ==
      FINGERPRINT_OK)
    {
      userListIDs[
        userListCount++] =
        id;
    }
  }
  finger.getTemplateCount();
  templateCount =
    finger.templateCount;
  userListNeedsRefresh =
    false;
}
bool deleteUserDirect(
  uint16_t id)
{
  if (
    !fingerprintReady ||
    id < 1 ||
    id > 127)
  {
    return false;
  }
  uint8_t result =
    finger.deleteModel(id);
  if (
    result !=
    FINGERPRINT_OK)
  {
    return false;
  }
  deleteEmployeeRecord(id);
  finger.getTemplateCount();
  templateCount =
    finger.templateCount;
  userListNeedsRefresh =
    true;
  userDeleteArmed =
    false;
  return true;
}
void drawUsersPage()
{
  if (userListNeedsRefresh)
  {
    drawCyberBackground();
    drawHeader();

    tft.fillRoundRect(
      64,
      112,
      352,
      90,
      14,
      UI_BLUE);

    tft.drawRoundRect(
      64,
      112,
      352,
      90,
      14,
      UI_CYAN);

    textCentered(
      64,
      132,
      352,
      "READING USERS",
      UI_YELLOW,
      2);

    textCentered(
      64,
      165,
      352,
      "Scanning AS608 template IDs...",
      UI_GREEN2,
      1);

    refreshUserList();
  }

  normalizeUserSelection();

  drawCyberBackground();
  drawHeader();

  tft.fillRoundRect(
    22,
    73,
    178,
    22,
    8,
    UI_GREEN);

  text(
    34,
    80,
    "REGISTERED USERS",
    UI_BG,
    1);

  char countText[24];
  sprintf(countText, "%u fingerprints", userListCount);

  drawStatusPill(
    344,
    72,
    110,
    countText,
    UI_CYAN);

  tft.fillRoundRect(
    18,
    100,
    444,
    132,
    14,
    UI_CARD);

  tft.drawRoundRect(
    18,
    100,
    444,
    132,
    14,
    UI_GREEN2);

  tft.fillRoundRect(
    28,
    110,
    424,
    22,
    6,
    UI_TEAL);

  text(38, 117, "SEL", UI_YELLOW, 1);
  text(78, 117, "ID", UI_CYAN, 1);
  text(132, 117, "NAME", UI_TEXT, 1);
  text(370, 117, "SOURCE", UI_GREEN2, 1);

  if (userListCount == 0)
  {
    textCentered(
      28,
      163,
      424,
      "NO FINGERPRINTS STORED",
      UI_ORANGE,
      2);
  }
  else
  {
    for (int row = 0; row < USERS_VISIBLE_ROWS; row++)
    {
      int index = userListTop + row;

      if (index >= userListCount)
        break;

      uint16_t id = userListIDs[index];
      int y = 139 + row * 17;
      bool selected = index == userListSelected;

      if (selected)
      {
        tft.fillRoundRect(
          29,
          y - 4,
          422,
          16,
          5,
          UI_BLUE);

        tft.drawRoundRect(
          29,
          y - 4,
          422,
          16,
          5,
          UI_YELLOW);
      }
      else if (row % 2 == 0)
      {
        tft.fillRect(
          30,
          y - 3,
          420,
          14,
          UI_BG2);
      }

      text(
        40,
        y,
        selected ? ">" : " ",
        selected ? UI_YELLOW : UI_MUTED,
        1);

      char idText[10];
      sprintf(idText, "#%03u", id);

      text(76, y, idText, UI_CYAN, 1);

      bool hasName = findEmployeeSlot(id) >= 0;
      String name = fitText(getEmployeeName(id), 20);

      text(
        132,
        y,
        name.c_str(),
        hasName ? UI_TEXT : UI_ORANGE,
        1);

      text(
        370,
        y,
        hasName ? "NAMED" : "FP ONLY",
        hasName ? UI_GREEN2 : UI_ORANGE,
        1);
    }
  }

  tft.fillRect(
    24,
    234,
    432,
    8,
    UI_BG);

  if (userDeleteArmed)
  {
    textCentered(
      24,
      234,
      432,
      "SW2 AGAIN = CONFIRM DELETE",
      UI_ORANGE,
      1);
  }
  else
  {
    textCentered(
      24,
      234,
      432,
      "SW3/SW4 SELECT  |  SW2 OK",
      UI_CYAN,
      1);
  }

  int actionBase = (int)userListCount;

  drawButton(
    USERS_ADD,
    "ADD USER",
    UI_GREEN2,
    currentPage == PAGE_USERS &&
      switchFocus == actionBase);

  drawButton(
    USERS_REFRESH,
    "REFRESH",
    UI_CYAN,
    currentPage == PAGE_USERS &&
      switchFocus == actionBase + 1);

  drawButton(
    USERS_DELETE,
    "DELETE",
    UI_RED,
    currentPage == PAGE_USERS &&
      switchFocus == actionBase + 2);

  drawBottomNav();
  updateClock();
}


void deleteSelectedUserTouch()
{
  uint16_t id =
    selectedUserID();
  if (id == 0)
    return;
  String name =
    getEmployeeName(id);
  if (
    !confirmDelete(
      id,
      name))
  {
    drawUsersPage();
    return;
  }
  if (
    deleteUserDirect(
      id))
  {
    userListNeedsRefresh =
      true;
    drawUsersPage();
  }
  else
  {
    tft.fillScreen(UI_BG);
    text(
      145,
      135,
      "DELETE FAILED",
      UI_RED,
      2);
    delay(900);
    drawUsersPage();
  }
}
void setPage(
  uint8_t page)
{
  currentPage =
    (Page)page;

  switchFocus = 0;
  userDeleteArmed = false;

  if (page == PAGE_HOME)
  {
    drawHomePage();
  }
  else if (page == PAGE_ENROLL)
  {
    drawEnrollPage();
  }
  else if (page == PAGE_LOGS)
  {
    drawLogsPage();
  }
  else if (page == PAGE_USERS)
  {
    drawUsersPage();
  }
}

void activateSwitchSelection()
{
  if (currentPage == PAGE_HOME)
  {
    switch (switchFocus)
    {
      case 0:
        setMode(MODE_AUTO);
        drawEmployeeInfo();
        break;
      case 1:
        setMode(MODE_IN);
        drawEmployeeInfo();
        break;
      case 2:
        setMode(MODE_OUT);
        drawEmployeeInfo();
        break;
      case 3:
        setPage(PAGE_ENROLL);
        return;
      case 4:
        setPage(PAGE_LOGS);
        return;
      case 5:
        userListNeedsRefresh = true;
        setPage(PAGE_USERS);
        return;
    }

    drawSwitchLegend();
    return;
  }

  if (currentPage == PAGE_ENROLL)
  {
    switch (switchFocus)
    {
      case 0:
      {
        String newName = pendingEnrollName;

        enrollmentInProgress = true;
        operationCancelRequested = false;
        drawSwitchLegend();

        if (waitForEnrollmentName(newName))
        {
          pendingEnrollName = newName;
          enrollmentInProgress = false;
          operationCancelRequested = false;
        }
        else if (finishEnrollmentCancelIfRequested())
        {
          return;
        }
        else
        {
          enrollmentInProgress = false;
        }

        drawEnrollPage();
        return;
      }

      case 1:
        if (enrollID > 1)
          enrollID--;
        pendingEnrollName = "";
        drawEnrollPage();
        return;

      case 2:
        if (enrollID < 127)
          enrollID++;
        pendingEnrollName = "";
        drawEnrollPage();
        return;

      case 3:
      {
        uint16_t freeId = findFreeFingerprintID();
        if (freeId > 0)
          enrollID = freeId;
        pendingEnrollName = "";
        drawEnrollPage();
        return;
      }

      case 4:
        enrollFingerprint();
        return;

      case 5:
        deleteSelectedID();
        return;

      case 6:
        setPage(PAGE_HOME);
        return;

      case 7:
        setPage(PAGE_LOGS);
        return;

      case 8:
        userListNeedsRefresh = true;
        setPage(PAGE_USERS);
        return;
    }
  }

  if (currentPage == PAGE_LOGS)
  {
    if (switchFocus == 0)
      setPage(PAGE_HOME);
    else if (switchFocus == 1)
      setPage(PAGE_ENROLL);
    else
    {
      userListNeedsRefresh = true;
      setPage(PAGE_USERS);
    }
    return;
  }

  // USERS page
  if (switchFocus < (int)userListCount)
  {
    userListSelected = switchFocus;
    normalizeUserSelection();
    userDeleteArmed = false;
    drawUsersPage();
    return;
  }

  int action = switchFocus - (int)userListCount;

  if (action == 0)
  {
    uint16_t freeId = findFreeFingerprintID();
    if (freeId > 0)
      enrollID = freeId;
    pendingEnrollName = "";
    setPage(PAGE_ENROLL);
    return;
  }

  if (action == 1)
  {
    userListNeedsRefresh = true;
    drawUsersPage();
    return;
  }

  if (action == 2)
  {
    deleteSelectedUserTouch();
    return;
  }

  if (action == 3)
  {
    setPage(PAGE_HOME);
    return;
  }

  if (action == 4)
  {
    setPage(PAGE_ENROLL);
    return;
  }

  setPage(PAGE_LOGS);
}

void moveSwitchFocus(int direction)
{
  int count = switchFocusCount();

  if (count <= 0)
    return;

  switchFocus += direction;

  if (switchFocus < 0)
    switchFocus = count - 1;

  if (switchFocus >= count)
    switchFocus = 0;

  if (
    currentPage == PAGE_USERS &&
    switchFocus < (int)userListCount)
  {
    userListSelected = switchFocus;
    normalizeUserSelection();
    userDeleteArmed = false;
    drawUsersPage();
    return;
  }

  // Refresh only the page areas that visually show focus.
  if (currentPage == PAGE_HOME)
  {
    drawModeButtons();
  }
  else if (currentPage == PAGE_ENROLL)
  {
    drawEnrollPage();
    return;
  }
  else if (currentPage == PAGE_USERS)
  {
    drawUsersPage();
    return;
  }

  drawSwitchLegend();
}


void handlePICButton(
  const char *command)
{
  if (command == NULL)
    return;

  // ----------------------------------------------------------
  // MODAL DELETE CONFIRMATION
  // ----------------------------------------------------------
  if (deleteConfirmActive)
  {
    if (strcmp(command, "BTN_OK") == 0)
      deleteConfirmChoice = 1;
    else if (
      strcmp(command, "BTN_DOWN") == 0 ||
      strcmp(command, "BTN_ADD") == 0)
      deleteConfirmChoice = 0;

    return;
  }

  // ----------------------------------------------------------
  // ACTIVE ENROLLMENT
  // ----------------------------------------------------------
  // SW4 cancels immediately while enrollment is active.
  if (enrollmentInProgress)
  {
    if (strcmp(command, "BTN_DOWN") == 0)
    {
      operationCancelRequested = true;
      Serial.println(F("SW4: ENROLL CANCEL REQUESTED"));
    }

    return;
  }

  // ----------------------------------------------------------
  // HOME PAGE: DIRECT PAGE SHORTCUTS
  // ----------------------------------------------------------
  //
  // SW1 = HOME
  // SW2 = ENROLL
  // SW3 = LOGS
  // SW4 = USERS
  //
  if (currentPage == PAGE_HOME)
  {
    if (strcmp(command, "BTN_ADD") == 0)
    {
      setPage(PAGE_HOME);
      return;
    }

    if (strcmp(command, "BTN_OK") == 0)
    {
      setPage(PAGE_ENROLL);
      return;
    }

    if (strcmp(command, "BTN_UP") == 0)
    {
      setPage(PAGE_LOGS);
      return;
    }

    if (strcmp(command, "BTN_DOWN") == 0)
    {
      userListNeedsRefresh = true;
      setPage(PAGE_USERS);
      return;
    }

    return;
  }

  // ----------------------------------------------------------
  // OTHER PAGES
  // ----------------------------------------------------------

  // SW1 = HOME / BACK
  if (strcmp(command, "BTN_ADD") == 0)
  {
    setPage(PAGE_HOME);
    return;
  }

  // SW2 = OK / SELECT
  if (strcmp(command, "BTN_OK") == 0)
  {
    activateSwitchSelection();
    return;
  }

  // SW3 = PREVIOUS / UP
  if (strcmp(command, "BTN_UP") == 0)
  {
    moveSwitchFocus(-1);
    return;
  }

  // SW4 = NEXT / DOWN
  if (strcmp(command, "BTN_DOWN") == 0)
  {
    moveSwitchFocus(+1);
    return;
  }
}


void handleTouch()
{
  static unsigned long lastTouch =
    0;
  if (
    currentPage == PAGE_HOME &&
    (
      scanState == STATE_MATCHED ||
      scanState == STATE_DENIED ||
      scanState == STATE_ERROR
    ) &&
    millis() < resultHoldUntil)
  {
    return;
  }
  int x;
  int y;
  if (!getTouch(x, y))
    return;
  if (
    millis() - lastTouch <
    220)
  {
    return;
  }
  lastTouch =
    millis();
  Serial.print(
    F("Touch X="));
  Serial.print(x);
  Serial.print(
    F(" Y="));
  Serial.println(y);
  if (
    currentPage ==
    PAGE_HOME)
  {
    if (
      pointInButton(
        x,
        y,
        HOME_MODE_AUTO))
    {
      setMode(MODE_AUTO);
      drawEmployeeInfo();
      waitTouchRelease();
      return;
    }
    if (
      pointInButton(
        x,
        y,
        HOME_MODE_IN))
    {
      setMode(MODE_IN);
      drawEmployeeInfo();
      waitTouchRelease();
      return;
    }
    if (
      pointInButton(
        x,
        y,
        HOME_MODE_OUT))
    {
      setMode(MODE_OUT);
      drawEmployeeInfo();
      waitTouchRelease();
      return;
    }
  }
  if (y >= 275)
  {
    if (x < 120)
    {
      setPage(
        PAGE_HOME);
    }
    else if (x < 240)
    {
      setPage(
        PAGE_ENROLL);
    }
    else if (x < 360)
    {
      setPage(
        PAGE_LOGS);
    }
    else
    {
      setPage(
        PAGE_USERS);
    }
    waitTouchRelease();
    return;
  }
  if (
    currentPage ==
    PAGE_ENROLL)
  {
    if (
      pointInButton(
        x,
        y,
        ENROLL_SET_NAME))
    {
      waitTouchRelease();
      String newName =
        pendingEnrollName;
      if (
        waitForEnrollmentName(
          newName))
      {
        pendingEnrollName =
          newName;
        Serial.print(
          F("ENROLL NAME SET = "));
        Serial.println(
          pendingEnrollName);
      }
      drawEnrollPage();
      return;
    }
    if (
      pointInButton(
        x,
        y,
        ENROLL_MINUS_HIT))
    {
      waitTouchRelease();
      if (enrollID > 1)
        enrollID--;
      pendingEnrollName = "";
      Serial.print(
        F("ROLL / FP ID = "));
      Serial.println(
        enrollID);
      drawEnrollPage();
      return;
    }
    if (
      pointInButton(
        x,
        y,
        ENROLL_PLUS_HIT))
    {
      waitTouchRelease();
      if (enrollID < 127)
        enrollID++;
      pendingEnrollName = "";
      Serial.print(
        F("ROLL / FP ID = "));
      Serial.println(
        enrollID);
      drawEnrollPage();
      return;
    }
    if (
      pointInButton(
        x,
        y,
        ENROLL_AUTO_ID))
    {
      waitTouchRelease();
      uint16_t freeId =
        findFreeFingerprintID();
      if (freeId > 0)
      {
        enrollID = freeId;
        pendingEnrollName = "";
      }
      drawEnrollPage();
      return;
    }
    if (
      pointInButton(
        x,
        y,
        ENROLL_START))
    {
      waitTouchRelease();
      enrollFingerprint();
      return;
    }
    if (
      pointInButton(
        x,
        y,
        ENROLL_DELETE))
    {
      waitTouchRelease();
      deleteSelectedID();
      return;
    }
  }
  else if (
    currentPage ==
    PAGE_USERS)
  {
    if (
      y >= 134 &&
      y < 224)
    {
      int row =
        (y - 134) / 18;
      int index =
        userListTop + row;
      if (
        index >= 0 &&
        index < userListCount)
      {
        userListSelected =
          index;
        userDeleteArmed =
          false;
        drawUsersPage();
        waitTouchRelease();
        return;
      }
    }
    if (
      pointInButton(
        x,
        y,
        USERS_ADD))
    {
      waitTouchRelease();
      uint16_t freeId =
        findFreeFingerprintID();
      if (freeId > 0)
        enrollID = freeId;
      pendingEnrollName =
        "";
      setPage(
        PAGE_ENROLL);
      return;
    }
    if (
      pointInButton(
        x,
        y,
        USERS_REFRESH))
    {
      waitTouchRelease();
      userListNeedsRefresh =
        true;
      drawUsersPage();
      return;
    }
    if (
      pointInButton(
        x,
        y,
        USERS_DELETE))
    {
      waitTouchRelease();
      deleteSelectedUserTouch();
      return;
    }
  }
  waitTouchRelease();
}
void scannerAnimation()
{
  if (currentPage != PAGE_HOME)
    return;

  static const int px[8] =
  {
    149, 183, 197, 183,
    149, 115, 101, 115
  };

  static const int py[8] =
  {
    129, 143, 173, 203,
    217, 203, 173, 143
  };

  static int orbit = 0;

  if (scanState == STATE_READY)
  {
    if (
      lastScannerOrbit >= 0 &&
      lastScannerOrbit < 8)
    {
      tft.fillCircle(
        px[lastScannerOrbit],
        py[lastScannerOrbit],
        4,
        UI_CARD);
    }

    tft.fillCircle(
      px[orbit],
      py[orbit],
      4,
      UI_CYAN);

    tft.drawCircle(
      px[orbit],
      py[orbit],
      6,
      UI_BORDER2);

    lastScannerOrbit = orbit;

    orbit++;

    if (orbit >= 8)
      orbit = 0;

    return;
  }

  if (scanState != STATE_SCANNING)
    return;

  tft.drawFastHLine(
    112,
    scanLineY,
    74,
    UI_BG2);

  scanLineY += scanDirection;

  if (scanLineY >= 207)
    scanDirection = -2;

  if (scanLineY <= 137)
    scanDirection = 2;

  tft.drawFastHLine(
    116,
    scanLineY - 1,
    66,
    UI_BLUE);

  tft.drawFastHLine(
    112,
    scanLineY,
    74,
    UI_WHITE);

  tft.drawFastHLine(
    116,
    scanLineY + 1,
    66,
    UI_CYAN);
}

void printHelp()
{
  Serial.println();
  Serial.println(
    F("=============================================="));
  Serial.println(
    F(" BIOMETRIC TERMINAL v8.5 SERIAL COMMANDS"));
  Serial.println(
    F("=============================================="));
  Serial.println(
    F("HELP"));
  Serial.println(
    F("STATUS"));
  Serial.println(
    F("PIC_OK       (test white LED + success buzzer)"));
  Serial.println(
    F("PIC_DENY     (test red LED + reject buzzer)"));
  Serial.println(
    F("PIC_WON / PIC_WOFF"));
  Serial.println(
    F("PIC_RON / PIC_ROFF"));
  Serial.println(
    F("PIC_BON / PIC_BOFF"));
  Serial.println(
    F("MODE:AUTO"));
  Serial.println(
    F("MODE:IN"));
  Serial.println(
    F("MODE:OUT"));
  Serial.println(
    F("LIST              (list Mega EEPROM users)"));
  Serial.println(
    F("LIST_FP           (scan and list all AS608 fingerprint IDs)"));
  Serial.println(
    F("DELETE_ALL        (delete ALL AS608 fingerprints + EEPROM users)"));
  Serial.println(
    F("RENAME:12,Mohamed Rasmy"));
  Serial.println(
    F("NAME:Mohamed Rasmy"));
  Serial.println(
    F("ID:+   ID:-   ID:25"));
  Serial.println(
    F("ENROLL:12"));
  Serial.println(
    F("DELETE:12"));
  Serial.println(
    F("SET_LATE:08:30-09:30"));
  Serial.println(
    F("RTC?"));
  Serial.println(
    F("SET_TIME:14:35:00"));
  Serial.println(
    F("SET_DATE:2026-09-20"));
  Serial.println(
    F("SET:2026-09-20 14:35:00"));
  Serial.println(
    F("CLEAR_LOGS   (clears recent RAM/screen list only)"));
  Serial.println(
    F("CSV_STATUS"));
  Serial.println(
    F("FETCH_CSV"));
  Serial.println(
    F("RESET_CSV"));
  Serial.println(
    F("RECAL_TOUCH"));
  Serial.println();
  Serial.println(
    F("During enrollment type NAME:Your Name or just the name."));
  Serial.println();
  Serial.println(
    F("PHYSICAL SWITCHES:"));
  Serial.println(
    F(" SW1 = HOME/BACK"));
  Serial.println(
    F(" SW2 = OK/SELECT"));
  Serial.println(
    F(" SW3 = PREVIOUS/UP"));
  Serial.println(
    F(" SW4 = NEXT/DOWN; CANCEL during enrollment"));
  Serial.println(
    F("=============================================="));
}
void printSystemStatus()
{
  Serial.println();
  Serial.println(
    F("========== SYSTEM STATUS =========="));
  Serial.print(
    F("AS608: "));
  Serial.println(
    fingerprintReady
      ? F("ONLINE")
      : F("OFFLINE"));
  Serial.print(
    F("RTC: "));
  Serial.println(
    rtcReady
      ? F("ONLINE")
      : F("OFFLINE"));
  Serial.print(
    F("SD CARD: "));
  Serial.println(
    sdReady
      ? F("ONLINE")
      : F("OFFLINE"));
  Serial.print(
    F("MODE: "));
  Serial.println(
    modeText());
  Serial.print(
    F("LATE START: "));
  Serial.print(
    lateStartMinutes / 60);
  Serial.print(':');
  if (
    lateStartMinutes % 60 <
    10)
  {
    Serial.print('0');
  }
  Serial.println(
    lateStartMinutes % 60);
  Serial.print(
    F("VERY LATE AFTER: "));
  Serial.print(
    lateEndMinutes / 60);
  Serial.print(':');
  if (
    lateEndMinutes % 60 <
    10)
  {
    Serial.print('0');
  }
  Serial.println(
    lateEndMinutes % 60);
  if (fingerprintReady)
  {
    finger.getTemplateCount();
    Serial.print(
      F("FINGERPRINT TEMPLATES: "));
    Serial.println(
      finger.templateCount);
  }
  Serial.println(
    F("==================================="));
}
bool setRTCDate(
  String s)
{
  s.trim();
  if (!validDateString(s))
    return false;
  Serial2.print(
    F("SET_DATE,"));
  Serial2.println(
    s);
  Serial2.flush();
  return true;
}
bool setRTCTime(
  String s)
{
  s.trim();
  int hh;
  int mm;
  int ss;
  if (
    !parseTimeString(
      s,
      hh,
      mm,
      ss))
  {
    return false;
  }
  Serial2.print(
    F("SET_TIME,"));
  Serial2.println(
    s);
  Serial2.flush();
  return true;
}
bool setRTCDateTime(
  String s)
{
  s.trim();
  int space =
    s.indexOf(' ');
  if (space < 0)
    return false;
  String ds =
    s.substring(
      0,
      space);
  String ts =
    s.substring(
      space + 1);
  ds.trim();
  ts.trim();
  if (!validDateString(ds))
    return false;
  int hh;
  int mm;
  int ss;
  if (
    !parseTimeString(
      ts,
      hh,
      mm,
      ss))
  {
    return false;
  }
  Serial2.print(
    F("SET_DATE,"));
  Serial2.println(
    ds);
  Serial2.flush();
  delay(50);
  Serial2.print(
    F("SET_TIME,"));
  Serial2.println(
    ts);
  Serial2.flush();
  return true;
}
void deleteIDFromSerial(
  uint16_t id)
{
  if (
    id < 1 ||
    id > 127)
  {
    Serial.println(
      F("ERROR: ID must be 1..127"));
    return;
  }
  if (!fingerprintReady)
  {
    Serial.println(
      F("ERROR: AS608 offline"));
    return;
  }
  uint8_t p =
    finger.deleteModel(id);
  if (
    p ==
    FINGERPRINT_OK)
  {
    deleteEmployeeRecord(id);
    userListNeedsRefresh =
      true;
    Serial.print(
      F("DELETED ID #"));
    Serial.println(id);
  }
  else
  {
    Serial.println(
      F("DELETE FAILED / ID MAY BE EMPTY"));
  }
}
bool setLateWindow(
  String payload)
{
  int dash =
    payload.indexOf('-');
  if (dash < 0)
    return false;
  String a =
    payload.substring(
      0,
      dash);
  String b =
    payload.substring(
      dash + 1);
  a.trim();
  b.trim();
  int h1, m1, s1;
  int h2, m2, s2;
  if (
    !parseTimeString(
      a,
      h1,
      m1,
      s1))
  {
    return false;
  }
  if (
    !parseTimeString(
      b,
      h2,
      m2,
      s2))
  {
    return false;
  }
  int first =
    h1 * 60 + m1;
  int second =
    h2 * 60 + m2;
  if (second < first)
    return false;
  lateStartMinutes =
    first;
  lateEndMinutes =
    second;
  return true;
}
void serialStartEnrollment(
  uint16_t id)
{
  if (
    id < 1 ||
    id > 127)
  {
    Serial.println(
      F("ERROR: ENROLL ID must be 1..127"));
    return;
  }
  enrollID = id;
  setPage(
    PAGE_ENROLL);
  enrollFingerprint();
}
void processSerialCommand(
  String cmd)
{
  cmd.trim();
  if (cmd.length() == 0)
    return;
  String upper =
    cmd;
  upper.toUpperCase();
  if (upper == "HELP")
  {
    printHelp();
    return;
  }
  if (upper == "STATUS")
  {
    printSystemStatus();
    return;
  }
  if (upper == "RTC?")
  {
    Serial2.println(F("GET_TIME"));
    Serial2.flush();
    return;
  }
  if (upper == "PIC_OK")
  {
    sendPICFeedback("OK");
    return;
  }
  if (upper == "PIC_DENY")
  {
    sendPICFeedback("DENY");
    return;
  }
  if (upper == "PIC_WON")
  {
    Serial2.println(F("TEST,WON"));
    Serial2.flush();
    return;
  }
  if (upper == "PIC_WOFF")
  {
    Serial2.println(F("TEST,WOFF"));
    Serial2.flush();
    return;
  }
  if (upper == "PIC_RON")
  {
    Serial2.println(F("TEST,RON"));
    Serial2.flush();
    return;
  }
  if (upper == "PIC_ROFF")
  {
    Serial2.println(F("TEST,ROFF"));
    Serial2.flush();
    return;
  }
  if (upper == "PIC_BON")
  {
    Serial2.println(F("TEST,BON"));
    Serial2.flush();
    return;
  }
  if (upper == "PIC_BOFF")
  {
    Serial2.println(F("TEST,BOFF"));
    Serial2.flush();
    return;
  }
  if (upper == "CSV_STATUS")
  {
    printCSVStatus();
    return;
  }
  if (upper == "FETCH_CSV")
  {
    sendAttendanceCSVToSerial();
    return;
  }
  if (upper == "RESET_CSV")
  {
    resetAttendanceCSV();
    if (
      currentPage ==
      PAGE_USERS)
    {
      drawUsersPage();
    }
    return;
  }
  if (upper == "LIST")
  {
    printEmployeeDatabase();
    return;
  }
  if (upper == "LIST_FP")
  {
    listFingerprintIDs();
    return;
  }
  if (
    upper == "DELETE_ALL" ||
    upper == "DELETE_ALL_USERS")
  {
    deleteAllUsersFromSerial();

    if (currentPage == PAGE_USERS)
    {
      drawUsersPage();
    }
    else if (currentPage == PAGE_HOME)
    {
      drawHomePage();
    }

    return;
  }
  if (
    upper.startsWith(
      "RENAME:"))
  {
    String payload =
      cmd.substring(7);
    int comma =
      payload.indexOf(',');
    if (comma < 1)
    {
      Serial.println(
        F("ERROR: use RENAME:12,Your Name"));
      return;
    }
    uint16_t id =
      payload.substring(
        0,
        comma).toInt();
    String newName =
      payload.substring(
        comma + 1);
    newName.trim();
    if (
      id < 1 ||
      id > 127 ||
      newName.length() == 0)
    {
      Serial.println(
        F("ERROR: use RENAME:12,Your Name"));
      return;
    }
    if (
      fingerprintReady &&
      finger.loadModel(id) !=
      FINGERPRINT_OK)
    {
      Serial.println(
        F("ERROR: fingerprint ID is not stored in AS608"));
      return;
    }
    if (
      saveEmployee(
        id,
        newName))
    {
      Serial.print(
        F("RENAMED ID #"));
      Serial.print(id);
      Serial.print(
        F(" -> "));
      Serial.println(
        newName);
      userListNeedsRefresh =
        true;
      if (
        currentPage ==
        PAGE_USERS)
      {
        drawUsersPage();
      }
    }
    else
    {
      Serial.println(
        F("ERROR: EEPROM user database full"));
    }
    return;
  }
  if (
    upper.startsWith(
      "NAME:"))
  {
    String value =
      cmd.substring(5);
    value.trim();
    if (value.length() == 0)
    {
      Serial.println(
        F("ERROR: NAME cannot be empty"));
      return;
    }
    if (value.length() > MAX_NAME_LEN)
      value =
        value.substring(
          0,
          MAX_NAME_LEN);
    pendingEnrollName =
      value;
    Serial.print(
      F("ENROLL NAME SET = "));
    Serial.println(
      pendingEnrollName);
    if (
      currentPage ==
      PAGE_ENROLL)
    {
      drawEnrollPage();
    }
    return;
  }
  if (
    upper.startsWith(
      "ID:"))
  {
    String value =
      cmd.substring(3);
    value.trim();
    if (value == "+")
    {
      if (enrollID < 127)
        enrollID++;
    }
    else if (value == "-")
    {
      if (enrollID > 1)
        enrollID--;
    }
    else
    {
      int requested =
        value.toInt();
      if (
        requested < 1 ||
        requested > 127)
      {
        Serial.println(
          F("ERROR: ID must be 1..127"));
        return;
      }
      enrollID =
        requested;
    }
    pendingEnrollName = "";
    Serial.print(
      F("ROLL / FP ID = "));
    Serial.println(
      enrollID);
    if (
      currentPage ==
      PAGE_ENROLL)
    {
      drawEnrollPage();
    }
    return;
  }
  if (upper == "MODE:AUTO")
  {
    setMode(MODE_AUTO);
    if (
      currentPage ==
      PAGE_HOME)
    {
      drawEmployeeInfo();
    }
    return;
  }
  if (upper == "MODE:IN")
  {
    setMode(MODE_IN);
    if (
      currentPage ==
      PAGE_HOME)
    {
      drawEmployeeInfo();
    }
    return;
  }
  if (upper == "MODE:OUT")
  {
    setMode(MODE_OUT);
    if (
      currentPage ==
      PAGE_HOME)
    {
      drawEmployeeInfo();
    }
    return;
  }
  if (
    upper.startsWith(
      "ENROLL:"))
  {
    uint16_t id =
      cmd.substring(7).toInt();
    serialStartEnrollment(id);
    return;
  }
  if (
    upper.startsWith(
      "DELETE:"))
  {
    uint16_t id =
      cmd.substring(7).toInt();
    deleteIDFromSerial(id);
    return;
  }
  if (
    upper.startsWith(
      "SET_LATE:"))
  {
    String payload =
      cmd.substring(9);
    if (
      setLateWindow(
        payload))
    {
      Serial.println(
        F("LATE WINDOW UPDATED"));
      if (
        currentPage ==
        PAGE_USERS)
      {
        drawUsersPage();
      }
    }
    else
    {
      Serial.println(
        F("ERROR: use SET_LATE:08:30-09:30"));
    }
    return;
  }
  if (
    upper.startsWith(
      "SET_TIME:"))
  {
    String value =
      cmd.substring(9);
    Serial.println(
      setRTCTime(value)
        ? F("RTC TIME UPDATED")
        : F("RTC TIME UPDATE FAILED"));
    return;
  }
  if (
    upper.startsWith(
      "SET_DATE:"))
  {
    String value =
      cmd.substring(9);
    Serial.println(
      setRTCDate(value)
        ? F("RTC DATE UPDATED")
        : F("RTC DATE UPDATE FAILED"));
    return;
  }
  if (
    upper.startsWith(
      "SET:"))
  {
    String value =
      cmd.substring(4);
    Serial.println(
      setRTCDateTime(value)
        ? F("RTC DATE/TIME UPDATED")
        : F("RTC DATE/TIME UPDATE FAILED"));
    return;
  }
  if (upper == "CLEAR_LOGS")
  {
    for (
      int i = 0;
      i < MAX_LOGS;
      i++)
    {
      logs[i].valid =
        false;
    }
    totalToday = 0;
    Serial.println(
      F("RECENT RAM/SCREEN LOGS CLEARED - ATTEND.CSV WAS NOT DELETED"));
    if (
      currentPage ==
      PAGE_LOGS)
    {
      drawLogsPage();
    }
    return;
  }
  if (upper == "RECAL_TOUCH")
  {
    touchCal.magic = 0;
    EEPROM.put(
      0,
      touchCal);
    calibrateTouch();
    setPage(
      PAGE_HOME);
    return;
  }
  Serial.print(
    F("UNKNOWN COMMAND: "));
  Serial.println(cmd);
  printHelp();
}
void pollSerialMonitor()
{
  if (!Serial.available())
    return;
  String cmd =
    Serial.readStringUntil('\n');
  processSerialCommand(cmd);
}
void setupTFT()
{
  uint16_t id =
    tft.readID();
  Serial.print(
    F("TFT ID = 0x"));
  Serial.println(
    id,
    HEX);
  if (
    id == 0xD3D3 ||
    id == 0x0000 ||
    id == 0xFFFF)
  {
    id = 0x9486;
  }
  tft.begin(id);
  tft.setRotation(1);
  tft.setTextWrap(false);
  Serial.print(
    F("Display: "));
  Serial.print(
    tft.width());
  Serial.print(
    F(" x "));
  Serial.println(
    tft.height());
}
void setup()
{
  Serial.begin(115200);
  Serial.setTimeout(70);
  Serial.println();
  Serial.println(
    F("================================"));
  Serial.println(
    F(" BIOMETRIC ATTENDANCE TERMINAL"));
  Serial.println(
    F("          VERSION 9.0"));
  Serial.println(
    F("================================"));
  setupTFT();
  loadTouchCalibration();
  initSDCard();
  initializeDatabase();
  Serial2.begin(PIC_BAUD);
  delay(100);
  rtcReady = false;
  picLinkReady = false;
  Serial3.begin(FINGER_BAUD);
  finger.begin(FINGER_BAUD);
  delay(200);
  fingerprintReady =
    finger.verifyPassword();
  if (fingerprintReady)
  {
    Serial.println(
      F("AS608 ONLINE @ 57600 ON SERIAL3"));
    finger.getTemplateCount();
    Serial.print(
      F("Stored fingerprints: "));
    Serial.println(
      finger.templateCount);
    templateCount =
      finger.templateCount;
    uint16_t freeId =
      findFreeFingerprintID();
    if (freeId > 0)
      enrollID = freeId;
  }
  else
  {
    Serial.println(
      F("AS608 NOT FOUND"));
    Serial.println(
      F("Check AS608 TX->Mega15(RX3), RX->Mega14(TX3), GND, VCC, 57600 baud"));
  }
  for (
    int i = 0;
    i < MAX_LOGS;
    i++)
  {
    logs[i].valid =
      false;
  }
  if (rtcReady)
  {
    currentDayKey =
      dateKey(
        currentRTC());
  }
  drawStartupSplash();
  printHelp();
  setPage(
    PAGE_HOME);
  Serial2.println(
    F("GET_TIME"));
  rtcRequestTimer =
    millis();
}
void loop()
{
  unsigned long now =
    millis();
  pollSerialMonitor();
  pollPIC();
  handleTouch();
  if (
    now - clockTimer >=
    1000UL)
  {
    clockTimer = now;
    updateClock();
  }
  if (!fingerprintReady)
  {
    tryReconnectFingerprint();
  }
  if (
    now - scanTimer >=
    100UL)
  {
    scanTimer = now;
    scanFingerprint();
  }
  if (
    now - animationTimer >=
    90UL)
  {
    animationTimer = now;
    scannerAnimation();
  }
}
