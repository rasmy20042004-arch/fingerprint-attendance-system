/*
 * ============================================================================
 * PIC16F877A - FINAL FULL BIOMETRIC ATTENDANCE CONTROLLER
 * ============================================================================
 * Crystal : 10 MHz
 * Compiler: MPLAB X + XC8
 *
 * FINAL UART ARCHITECTURE:
 *
 * 1) MAIN / HARDWARE UART -> ARDUINO MEGA
 *    PIC RC6 / physical pin 25 TX -> Mega RX2 pin 17
 *    PIC RC7 / physical pin 26 RX <- Mega TX2 pin 16
 *    Baud = 2400
 *
 * 2) SOFTWARE UART -> AS608 FINGERPRINT
 *    PIC RD3 / physical pin 22 TX -> AS608 RX
 *    PIC RD2 / physical pin 21 RX <- AS608 TX
 *    Baud = 9600
 *
 * IMPORTANT:
 *    The AS608 must be changed from 57600 to 9600 BEFORE using this firmware.
 *    Use the included AS608_Baud_57600_to_9600.c with the OLD working wiring,
 *    then power-cycle the sensor and change to the FINAL wiring above.
 *
 * DS3231 RTC:
 *    RC3 / pin 18 -> SCL
 *    RC4 / pin 23 -> SDA
 *
 * Physical switches (ACTIVE LOW, external 10k pull-ups):
 *    RB0 / pin 33 -> ADD
 *    RB1 / pin 34 -> OK / DELETE
 *    RB2 / pin 35 -> UP
 *    RB3 / pin 36 -> DOWN
 *
 * Indicators:
 *    RD0 / pin 19 -> WHITE LED
 *    RD1 / pin 20 -> RED LED
 *    RC2 / pin 17 -> ACTIVE BUZZER
 * ============================================================================
 */

#include <xc.h>
#include <stdint.h>
#include <string.h>

#define _XTAL_FREQ 10000000UL

#pragma config FOSC = HS
#pragma config WDTE = OFF
#pragma config PWRTE = ON
#pragma config BOREN = ON
#pragma config LVP = OFF
#pragma config CPD = OFF
#pragma config WRT = OFF
#pragma config CP = OFF

#define LED_WHITE PORTDbits.RD0
#define LED_RED   PORTDbits.RD1
#define BUZZER    PORTCbits.RC2

#define BTN_ADD   PORTBbits.RB0
#define BTN_OK    PORTBbits.RB1
#define BTN_UP    PORTBbits.RB2
#define BTN_DOWN  PORTBbits.RB3

// AS608 software UART pins
#define FP_TX     PORTDbits.RD3
#define FP_RX     PORTDbits.RD2

uint8_t fpBuffer[40];
uint8_t fpIndex = 0;

uint8_t scanEnabled = 1;
uint8_t fingerPresent = 0;
uint8_t scanPauseTicks = 0;
uint8_t rtcTicks = 0;
uint8_t scanTicks = 0;
uint8_t fpErrorTicks = 0;
uint8_t fingerprintOnline = 0;
uint16_t fpRetryTicks = 0;

#define MEGA_RX_LINE_SIZE 48
char megaRxLine[MEGA_RX_LINE_SIZE];
uint8_t megaRxPos = 0;

static void MegaUART_Write(uint8_t data);

/* ======================= ARDUINO HARDWARE UART ======================== */
/*
 * PIC RC6/TX pin 25 -> Mega RX2 pin 17
 * PIC RC7/RX pin 26 <- Mega TX2 pin 16
 * 2400 baud, 8N1
 *
 * 10 MHz PIC16F877A:
 * BRGH = 0
 * SPBRG = 64
 * Actual baud ~= 2403.8
 */

static void MegaUART_Init(void)
{
    TRISCbits.TRISC6 = 0;
    TRISCbits.TRISC7 = 1;

    TXSTAbits.SYNC = 0;
    TXSTAbits.BRGH = 0;
    SPBRG = 64;

    RCSTAbits.SPEN = 1;
    TXSTAbits.TXEN = 1;
    RCSTAbits.CREN = 1;

    __delay_ms(50);
}

static void MegaUART_Write(uint8_t data)
{
    while (!PIR1bits.TXIF);
    TXREG = data;
}

static void MegaUART_Print(const char *s)
{
    while (*s)
        MegaUART_Write((uint8_t)*s++);
}

static void MegaUART_NewLine(void)
{
    MegaUART_Write('\r');
    MegaUART_Write('\n');
}

static void MegaUART_SendUInt(uint16_t value)
{
    char tmp[6];
    uint8_t i = 0;
    uint8_t j;

    if (value == 0)
    {
        MegaUART_Write('0');
        return;
    }

    while (value > 0 && i < sizeof(tmp))
    {
        tmp[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    for (j = i; j > 0; j--)
        MegaUART_Write((uint8_t)tmp[j - 1]);
}

static void MegaUART_Send2(uint8_t value)
{
    MegaUART_Write((uint8_t)('0' + (value / 10U)));
    MegaUART_Write((uint8_t)('0' + (value % 10U)));
}

static void MegaUART_Send4(uint16_t value)
{
    MegaUART_Write((uint8_t)('0' + ((value / 1000U) % 10U)));
    MegaUART_Write((uint8_t)('0' + ((value / 100U) % 10U)));
    MegaUART_Write((uint8_t)('0' + ((value / 10U) % 10U)));
    MegaUART_Write((uint8_t)('0' + (value % 10U)));
}

static uint8_t MegaUART_ReadByte(uint8_t *out)
{
    if (RCSTAbits.OERR)
    {
        RCSTAbits.CREN = 0;
        RCSTAbits.CREN = 1;
    }

    if (!PIR1bits.RCIF)
        return 0;

    *out = RCREG;
    return 1;
}

/* ============================= DS3231 I2C ============================== */

static void I2C_Wait(void)
{
    while ((SSPCON2 & 0x1F) || (SSPSTAT & 0x04));
}

static void I2C_Init(void)
{
    TRISCbits.TRISC3 = 1;
    TRISCbits.TRISC4 = 1;

    SSPCON = 0b00101000;
    SSPCON2 = 0x00;
    SSPADD = (uint8_t)((_XTAL_FREQ / (4UL * 100000UL)) - 1UL);
    SSPSTAT = 0x80;
}

static void I2C_Start(void)   { I2C_Wait(); SSPCON2bits.SEN = 1; }
static void I2C_ReStart(void) { I2C_Wait(); SSPCON2bits.RSEN = 1; }
static void I2C_Stop(void)    { I2C_Wait(); SSPCON2bits.PEN = 1; }

static void I2C_Write(uint8_t data)
{
    I2C_Wait();
    SSPBUF = data;
}

static uint8_t I2C_Read(uint8_t sendAck)
{
    uint8_t value;

    I2C_Wait();
    SSPCON2bits.RCEN = 1;
    I2C_Wait();
    value = SSPBUF;

    I2C_Wait();
    SSPCON2bits.ACKDT = sendAck ? 0 : 1;
    SSPCON2bits.ACKEN = 1;

    return value;
}

static uint8_t BCD_To_Dec(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0F));
}

static uint8_t Dec_To_BCD(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static void RTC_Read(
    uint8_t *sec,
    uint8_t *min,
    uint8_t *hour,
    uint8_t *day,
    uint8_t *month,
    uint8_t *year)
{
    I2C_Start();
    I2C_Write(0xD0);
    I2C_Write(0x00);

    I2C_ReStart();
    I2C_Write(0xD1);

    *sec   = BCD_To_Dec(I2C_Read(1) & 0x7F);
    *min   = BCD_To_Dec(I2C_Read(1) & 0x7F);
    *hour  = BCD_To_Dec(I2C_Read(1) & 0x3F);

    (void)I2C_Read(1);

    *day   = BCD_To_Dec(I2C_Read(1) & 0x3F);
    *month = BCD_To_Dec(I2C_Read(1) & 0x1F);
    *year  = BCD_To_Dec(I2C_Read(0));

    I2C_Stop();
}

static void RTC_SetTime(uint8_t hour, uint8_t min, uint8_t sec)
{
    I2C_Start();
    I2C_Write(0xD0);
    I2C_Write(0x00);

    I2C_Write(Dec_To_BCD(sec));
    I2C_Write(Dec_To_BCD(min));
    I2C_Write(Dec_To_BCD(hour));

    I2C_Stop();
}

static void RTC_SetDate(uint8_t day, uint8_t month, uint8_t year)
{
    I2C_Start();
    I2C_Write(0xD0);
    I2C_Write(0x04);

    I2C_Write(Dec_To_BCD(day));
    I2C_Write(Dec_To_BCD(month));
    I2C_Write(Dec_To_BCD(year));

    I2C_Stop();
}

static void RTC_SendToMega(void)
{
    uint8_t sec, min, hour, day, month, year;

    RTC_Read(&sec, &min, &hour, &day, &month, &year);

    MegaUART_Print("TIME,");
    MegaUART_Send4((uint16_t)(2000U + year));
    MegaUART_Write('-');
    MegaUART_Send2(month);
    MegaUART_Write('-');
    MegaUART_Send2(day);
    MegaUART_Write(',');
    MegaUART_Send2(hour);
    MegaUART_Write(':');
    MegaUART_Send2(min);
    MegaUART_Write(':');
    MegaUART_Send2(sec);
    MegaUART_NewLine();
}

/* ============================= OUTPUTS ============================== */

static void Outputs_Off(void)
{
    LED_WHITE = 0;
    LED_RED = 0;
    BUZZER = 0;
}

static void Feedback_OK(void)
{
    LED_RED = 0;
    LED_WHITE = 1;

    BUZZER = 1;
    __delay_ms(80);
    BUZZER = 0;

    __delay_ms(260);
    LED_WHITE = 0;
}

static void Feedback_Deny(void)
{
    uint8_t i;

    LED_WHITE = 0;
    LED_RED = 1;

    for (i = 0; i < 2; i++)
    {
        BUZZER = 1;
        __delay_ms(90);
        BUZZER = 0;
        __delay_ms(100);
    }

    __delay_ms(180);
    LED_RED = 0;
}

static void Feedback_Enroll(void)
{
    uint8_t i;

    LED_RED = 0;
    LED_WHITE = 1;

    for (i = 0; i < 2; i++)
    {
        BUZZER = 1;
        __delay_ms(55);
        BUZZER = 0;
        __delay_ms(75);
    }

    __delay_ms(180);
    LED_WHITE = 0;
}

/* ======================= AS608 SOFTWARE UART ======================== */
/*
 * PIC RD3 / pin 22 TX -> AS608 RX
 * PIC RD2 / pin 21 RX <- AS608 TX
 * AS608 baud = 9600
 *
 * IMPORTANT:
 * Sensor MUST already be configured for 9600 baud.
 */

static void FP_UART_Init(void)
{
    TRISEbits.PSPMODE = 0;

    TRISDbits.TRISD3 = 0;
    TRISDbits.TRISD2 = 1;

    // UART idle HIGH
    FP_TX = 1;

    __delay_ms(50);
}

static void FP_Write(uint8_t value)
{
    uint8_t i;

    // Start bit
    FP_TX = 0;
    __delay_us(104);

    // 8 data bits, LSB first
    for (i = 0; i < 8; i++)
    {
        FP_TX = (value & 0x01U) ? 1 : 0;
        __delay_us(104);
        value >>= 1;
    }

    // Stop bit
    FP_TX = 1;
    __delay_us(104);
}

static uint8_t FP_ReadByteWait(uint8_t *out, uint32_t wait10us)
{
    uint8_t i;
    uint8_t data = 0;

    // Wait for start bit (line goes LOW)
    while (FP_RX != 0)
    {
        if (wait10us == 0)
            return 0;

        __delay_us(10);
        wait10us--;
    }

    // Move to center of first data bit: 1.5 bit times
    __delay_us(156);

    for (i = 0; i < 8; i++)
    {
        if (FP_RX)
            data |= (uint8_t)(1U << i);

        __delay_us(104);
    }

    *out = data;
    return 1;
}

static void FP_Clear(void)
{
    uint8_t i;

    fpIndex = 0;

    for (i = 0; i < sizeof(fpBuffer); i++)
        fpBuffer[i] = 0;
}

/*
 * Send command and synchronously receive AS608 ACK.
 * No interrupt is used so the software-UART sampling timing stays clean.
 */
static uint8_t FP_Send(
    const uint8_t *packet,
    uint8_t packetLen,
    uint8_t expectedBytes,
    uint16_t timeoutMs)
{
    uint8_t i;
    uint8_t c;
    uint8_t synced = 0;
    uint32_t firstWait;

    FP_Clear();

    __delay_ms(20);

    for (i = 0; i < packetLen; i++)
        FP_Write(packet[i]);

    // First response byte may take some time.
    firstWait = (uint32_t)timeoutMs * 100UL;  // 10 us units

    while (fpIndex < expectedBytes)
    {
        uint32_t waitTicks =
            (fpIndex == 0U) ? firstWait : 5000UL; // 50 ms between bytes

        if (!FP_ReadByteWait(&c, waitTicks))
            return 0;

        // Synchronize to EF 01 packet header and ignore stray noise.
        if (!synced)
        {
            if (fpIndex == 0U)
            {
                if (c != 0xEF)
                    continue;

                fpBuffer[fpIndex++] = c;
                continue;
            }

            if (fpIndex == 1U)
            {
                if (c == 0x01)
                {
                    fpBuffer[fpIndex++] = c;
                    synced = 1;
                }
                else
                {
                    fpIndex = 0;

                    if (c == 0xEF)
                        fpBuffer[fpIndex++] = c;
                }

                continue;
            }
        }

        if (fpIndex < sizeof(fpBuffer))
            fpBuffer[fpIndex++] = c;
        else
            return 0;
    }

    if (fpIndex < 10U)
        return 0;

    if (fpBuffer[0] != 0xEF ||
        fpBuffer[1] != 0x01 ||
        fpBuffer[6] != 0x07)
        return 0;

    return 1;
}

static const uint8_t FP_VERIFY_PACKET[] =
{
    0xEF,0x01,0xFF,0xFF,0xFF,0xFF,
    0x01,0x00,0x07,0x13,
    0x00,0x00,0x00,0x00,
    0x00,0x1B
};

static const uint8_t FP_GET_IMAGE_PACKET[] =
{
    0xEF,0x01,0xFF,0xFF,0xFF,0xFF,
    0x01,0x00,0x03,0x01,0x00,0x05
};

static const uint8_t FP_IMAGE2TZ1_PACKET[] =
{
    0xEF,0x01,0xFF,0xFF,0xFF,0xFF,
    0x01,0x00,0x04,0x02,0x01,0x00,0x08
};

static const uint8_t FP_IMAGE2TZ2_PACKET[] =
{
    0xEF,0x01,0xFF,0xFF,0xFF,0xFF,
    0x01,0x00,0x04,0x02,0x02,0x00,0x09
};

static const uint8_t FP_CREATE_PACKET[] =
{
    0xEF,0x01,0xFF,0xFF,0xFF,0xFF,
    0x01,0x00,0x03,0x05,0x00,0x09
};

static const uint8_t FP_SEARCH_PACKET[] =
{
    0xEF,0x01,0xFF,0xFF,0xFF,0xFF,
    0x01,0x00,0x08,0x1B,0x01,
    0x00,0x00,0x00,0xA3,
    0x00,0xC8
};

static uint8_t FP_Verify(void)
{
    if (!FP_Send(
        FP_VERIFY_PACKET,
        sizeof(FP_VERIFY_PACKET),
        12,
        500))
        return 0;

    return fpBuffer[9] == 0x00;
}

static uint8_t FP_GetImage(void)
{
    if (!FP_Send(
        FP_GET_IMAGE_PACKET,
        sizeof(FP_GET_IMAGE_PACKET),
        12,
        300))
        return 0xFF;

    return fpBuffer[9];
}

static uint8_t FP_Image2Tz(uint8_t bufferNo)
{
    const uint8_t *packet =
        (bufferNo == 2U) ?
        FP_IMAGE2TZ2_PACKET :
        FP_IMAGE2TZ1_PACKET;

    if (!FP_Send(packet, 13, 12, 400))
        return 0xFF;

    return fpBuffer[9];
}

static uint8_t FP_CreateModel(void)
{
    if (!FP_Send(
        FP_CREATE_PACKET,
        sizeof(FP_CREATE_PACKET),
        12,
        500))
        return 0xFF;

    return fpBuffer[9];
}

static uint8_t FP_Search(uint16_t *id, uint16_t *score)
{
    if (!FP_Send(
        FP_SEARCH_PACKET,
        sizeof(FP_SEARCH_PACKET),
        16,
        800))
        return 0xFF;

    if (fpBuffer[9] != 0x00)
        return fpBuffer[9];

    *id =
        ((uint16_t)fpBuffer[10] << 8) |
        (uint16_t)fpBuffer[11];

    *score =
        ((uint16_t)fpBuffer[12] << 8) |
        (uint16_t)fpBuffer[13];

    return 0x00;
}

static uint8_t FP_Store(uint16_t id)
{
    uint8_t p[15];
    uint16_t sum;

    p[0]=0xEF; p[1]=0x01;
    p[2]=0xFF; p[3]=0xFF; p[4]=0xFF; p[5]=0xFF;
    p[6]=0x01;
    p[7]=0x00; p[8]=0x06;
    p[9]=0x06;
    p[10]=0x01;
    p[11]=(uint8_t)(id >> 8);
    p[12]=(uint8_t)(id & 0xFF);

    sum =
        (uint16_t)p[6] + (uint16_t)p[7] + (uint16_t)p[8] +
        (uint16_t)p[9] + (uint16_t)p[10] +
        (uint16_t)p[11] + (uint16_t)p[12];

    p[13]=(uint8_t)(sum >> 8);
    p[14]=(uint8_t)(sum & 0xFF);

    if (!FP_Send(p, sizeof(p), 12, 600))
        return 0xFF;

    return fpBuffer[9];
}

static uint8_t FP_Delete(uint16_t id)
{
    uint8_t p[16];
    uint16_t sum;

    p[0]=0xEF; p[1]=0x01;
    p[2]=0xFF; p[3]=0xFF; p[4]=0xFF; p[5]=0xFF;
    p[6]=0x01;
    p[7]=0x00; p[8]=0x07;
    p[9]=0x0C;
    p[10]=(uint8_t)(id >> 8);
    p[11]=(uint8_t)(id & 0xFF);
    p[12]=0x00;
    p[13]=0x01;

    sum =
        (uint16_t)p[6] + (uint16_t)p[7] + (uint16_t)p[8] +
        (uint16_t)p[9] + (uint16_t)p[10] +
        (uint16_t)p[11] + (uint16_t)p[12] + (uint16_t)p[13];

    p[14]=(uint8_t)(sum >> 8);
    p[15]=(uint8_t)(sum & 0xFF);

    if (!FP_Send(p, sizeof(p), 12, 600))
        return 0xFF;

    return fpBuffer[9];
}

/* ============================= MATCHING ============================== */

static void FP_SendMatch(uint16_t id, uint16_t score)
{
    MegaUART_Print("FP_MATCH,");
    MegaUART_SendUInt(id);
    MegaUART_Write(',');
    MegaUART_SendUInt(score);
    MegaUART_NewLine();
}

static void FP_ScanTask(void)
{
    uint8_t result;
    uint16_t id;
    uint16_t score;

    if (!scanEnabled || !fingerprintOnline)
        return;

    result = FP_GetImage();

    if (result == 0x02)
    {
        if (fingerPresent)
        {
            fingerPresent = 0;
            MegaUART_Print("FP_REMOVED");
            MegaUART_NewLine();
        }
        return;
    }

    if (result == 0xFF)
    {
        fpErrorTicks++;
        if (fpErrorTicks >= 10)
        {
            fpErrorTicks = 0;
            fingerprintOnline = 0;

            MegaUART_Print("FP_COMM_ERROR");
            MegaUART_NewLine();

            MegaUART_Print("AS608_ERROR");
            MegaUART_NewLine();
        }
        return;
    }

    if (result != 0x00 || fingerPresent)
        return;

    fingerPresent = 1;

    MegaUART_Print("FP_FINGER");
    MegaUART_NewLine();

    result = FP_Image2Tz(1);
    if (result != 0x00)
    {
        MegaUART_Print("FP_BAD_IMAGE");
        MegaUART_NewLine();
        return;
    }

    result = FP_Search(&id, &score);

    if (result == 0x00)
        FP_SendMatch(id, score);
    else if (result == 0x09)
    {
        MegaUART_Print("FP_NOT_FOUND");
        MegaUART_NewLine();
    }
    else
    {
        MegaUART_Print("FP_SEARCH_ERROR");
        MegaUART_NewLine();
    }
}

/* ============================= ENROLL / DELETE ============================== */

static uint8_t FP_WaitForFinger(uint16_t loops)
{
    uint16_t i;
    uint8_t r;

    for (i = 0; i < loops; i++)
    {
        r = FP_GetImage();
        if (r == 0x00) return 1;
        __delay_ms(120);
    }

    return 0;
}

static uint8_t FP_WaitForRemoval(uint16_t loops)
{
    uint16_t i;
    uint8_t r;

    for (i = 0; i < loops; i++)
    {
        r = FP_GetImage();
        if (r == 0x02) return 1;
        __delay_ms(120);
    }

    return 0;
}

static void FP_SendEnrollFail(uint8_t code)
{
    MegaUART_Print("ENROLL_FAIL,");
    MegaUART_SendUInt(code);
    MegaUART_NewLine();
}

static void FP_Enroll(uint16_t id)
{
    uint8_t r;

    scanEnabled = 0;
    fingerPresent = 0;

    MegaUART_Print("ENROLL_PLACE1,");
    MegaUART_SendUInt(id);
    MegaUART_NewLine();

    if (!FP_WaitForFinger(100))
    {
        FP_SendEnrollFail(1);
        scanEnabled = 1;
        return;
    }

    r = FP_Image2Tz(1);
    if (r != 0x00)
    {
        FP_SendEnrollFail(2);
        scanEnabled = 1;
        return;
    }

    MegaUART_Print("ENROLL_REMOVE");
    MegaUART_NewLine();

    if (!FP_WaitForRemoval(80))
    {
        FP_SendEnrollFail(3);
        scanEnabled = 1;
        return;
    }

    __delay_ms(400);

    MegaUART_Print("ENROLL_PLACE2");
    MegaUART_NewLine();

    if (!FP_WaitForFinger(100))
    {
        FP_SendEnrollFail(4);
        scanEnabled = 1;
        return;
    }

    r = FP_Image2Tz(2);
    if (r != 0x00)
    {
        FP_SendEnrollFail(5);
        scanEnabled = 1;
        return;
    }

    r = FP_CreateModel();
    if (r != 0x00)
    {
        FP_SendEnrollFail(6);
        scanEnabled = 1;
        return;
    }

    r = FP_Store(id);
    if (r != 0x00)
    {
        FP_SendEnrollFail(7);
        scanEnabled = 1;
        return;
    }

    MegaUART_Print("ENROLL_OK,");
    MegaUART_SendUInt(id);
    MegaUART_NewLine();

    Feedback_Enroll();
    (void)FP_WaitForRemoval(80);

    scanEnabled = 1;
    fingerPresent = 0;
}

static void FP_DeleteID(uint16_t id)
{
    uint8_t r;

    scanEnabled = 0;
    r = FP_Delete(id);

    if (r == 0x00)
    {
        MegaUART_Print("DELETE_OK,");
        MegaUART_SendUInt(id);
        MegaUART_NewLine();
        Feedback_Enroll();
    }
    else
    {
        MegaUART_Print("DELETE_FAIL,");
        MegaUART_SendUInt(r);
        MegaUART_NewLine();
    }

    scanEnabled = 1;
}

/* ============================= MEGA COMMANDS ============================== */

static uint8_t Parse2(const char *p)
{
    return (uint8_t)(((p[0]-'0') * 10) + (p[1]-'0'));
}

static uint16_t ParseUInt(const char *p)
{
    uint16_t value = 0;

    while (*p >= '0' && *p <= '9')
    {
        value = (uint16_t)(value * 10U + (uint16_t)(*p - '0'));
        p++;
    }

    return value;
}

static void ProcessMegaCommand(char *line)
{
    if (strcmp(line, "PING") == 0)
    {
        MegaUART_Print("PONG");
        MegaUART_NewLine();
        return;
    }

    if (strcmp(line, "GET_TIME") == 0)
    {
        RTC_SendToMega();
        return;
    }

    if (strncmp(line, "SET_DATE,", 9) == 0)
    {
        if (strlen(line) >= 19)
        {
            uint8_t year  = Parse2(line + 11);
            uint8_t month = Parse2(line + 14);
            uint8_t day   = Parse2(line + 17);

            RTC_SetDate(day, month, year);

            MegaUART_Print("ACK,SET_DATE");
            MegaUART_NewLine();
            RTC_SendToMega();
        }
        return;
    }

    if (strncmp(line, "SET_TIME,", 9) == 0)
    {
        if (strlen(line) >= 17)
        {
            uint8_t hour = Parse2(line + 9);
            uint8_t min  = Parse2(line + 12);
            uint8_t sec  = Parse2(line + 15);

            RTC_SetTime(hour, min, sec);

            MegaUART_Print("ACK,SET_TIME");
            MegaUART_NewLine();
            RTC_SendToMega();
        }
        return;
    }

    if (strncmp(line, "ENROLL,", 7) == 0)
    {
        FP_Enroll(ParseUInt(line + 7));
        return;
    }

    if (strncmp(line, "DELETE,", 7) == 0)
    {
        FP_DeleteID(ParseUInt(line + 7));
        return;
    }

    if (strcmp(line, "SCAN_ON") == 0)
    {
        scanEnabled = 1;
        MegaUART_Print("ACK,SCAN_ON");
        MegaUART_NewLine();
        return;
    }

    if (strcmp(line, "SCAN_OFF") == 0)
    {
        scanEnabled = 0;
        MegaUART_Print("ACK,SCAN_OFF");
        MegaUART_NewLine();
        return;
    }

    if (strcmp(line, "FEEDBACK,OK") == 0)
    {
        Feedback_OK();
        return;
    }

    if (strcmp(line, "FEEDBACK,DENY") == 0)
    {
        Feedback_Deny();
        return;
    }

    if (strcmp(line, "FEEDBACK,ENROLL") == 0)
    {
        Feedback_Enroll();
        return;
    }
}

static void MegaUART_Poll(void)
{
    uint8_t c;

    // Hardware UART receives the Mega command stream.
    // Mega sends characters with a deliberate small gap so this loop can
    // safely coexist with software-UART fingerprint transactions.
    while (MegaUART_ReadByte(&c))
    {
        if (c == '\r')
            continue;

        if (c == '\n')
        {
            megaRxLine[megaRxPos] = '\0';

            if (megaRxPos > 0)
                ProcessMegaCommand(megaRxLine);

            megaRxPos = 0;
            continue;
        }

        if (megaRxPos < MEGA_RX_LINE_SIZE - 1)
        {
            megaRxLine[megaRxPos++] = (char)c;
        }
        else
        {
            megaRxPos = 0;
        }
    }
}

/* ============================= BUTTONS ============================== */

static void SendButtonEvent(const char *event)
{
    MegaUART_Print(event);
    MegaUART_NewLine();

    scanPauseTicks = 60;
}

static void Buttons_Poll(void)
{
    static uint8_t lastAdd = 1;
    static uint8_t lastOk = 1;
    static uint8_t lastUp = 1;
    static uint8_t lastDown = 1;

    uint8_t add = BTN_ADD;
    uint8_t ok = BTN_OK;
    uint8_t up = BTN_UP;
    uint8_t down = BTN_DOWN;

    if (lastAdd && !add)
    {
        __delay_ms(20);
        if (!BTN_ADD) SendButtonEvent("BTN_ADD");
    }

    if (lastOk && !ok)
    {
        __delay_ms(20);
        if (!BTN_OK) SendButtonEvent("BTN_OK");
    }

    if (lastUp && !up)
    {
        __delay_ms(20);
        if (!BTN_UP) SendButtonEvent("BTN_UP");
    }

    if (lastDown && !down)
    {
        __delay_ms(20);
        if (!BTN_DOWN) SendButtonEvent("BTN_DOWN");
    }

    lastAdd = add;
    lastOk = ok;
    lastUp = up;
    lastDown = down;
}

/* ============================= MAIN ============================== */

void main(void)
{
    ADCON1 = 0x06;

    TRISBbits.TRISB0 = 1;
    TRISBbits.TRISB1 = 1;
    TRISBbits.TRISB2 = 1;
    TRISBbits.TRISB3 = 1;

    OPTION_REG |= 0x80;

    TRISDbits.TRISD0 = 0;
    TRISDbits.TRISD1 = 0;
    TRISCbits.TRISC2 = 0;

    Outputs_Off();

    MegaUART_Init();
    I2C_Init();
    FP_UART_Init();

    __delay_ms(1800);

    MegaUART_Print("PIC_READY");
    MegaUART_NewLine();

    if (FP_Verify())
    {
        fingerprintOnline = 1;

        MegaUART_Print("AS608_OK");
        MegaUART_NewLine();
    }
    else
    {
        fingerprintOnline = 0;

        MegaUART_Print("AS608_ERROR");
        MegaUART_NewLine();
    }

    RTC_SendToMega();

    while (1)
    {
        MegaUART_Poll();
        Buttons_Poll();

        if (scanPauseTicks > 0)
            scanPauseTicks--;

        rtcTicks++;
        if (rtcTicks >= 100)
        {
            rtcTicks = 0;
            RTC_SendToMega();
        }

        // Retry AS608 about every 5 seconds if it is offline.
        if (!fingerprintOnline)
        {
            fpRetryTicks++;

            if (fpRetryTicks >= 500U)
            {
                fpRetryTicks = 0;

                if (FP_Verify())
                {
                    fingerprintOnline = 1;
                    fpErrorTicks = 0;

                    MegaUART_Print("AS608_OK");
                    MegaUART_NewLine();
                }
            }
        }

        scanTicks++;
        if (scanTicks >= 35)
        {
            scanTicks = 0;

            if (scanPauseTicks == 0 &&
                fingerprintOnline)
            {
                FP_ScanTask();
            }
        }

        __delay_ms(10);
    }
}
