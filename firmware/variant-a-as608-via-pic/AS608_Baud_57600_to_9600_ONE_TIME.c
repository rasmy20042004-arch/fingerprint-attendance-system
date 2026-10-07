/*
 * ============================================================================
 * ONE-TIME AS608 BAUD MIGRATION: 57600 -> 9600
 * ============================================================================
 *
 * USE THIS ONLY BEFORE THE FINAL UART SWAP.
 *
 * OLD / CURRENT WORKING WIRING FOR THIS ONE-TIME PROGRAM:
 *
 * AS608 TX -> PIC RC7 / pin 26
 * AS608 RX <- PIC RC6 / pin 25
 *
 * PIC RD3 / pin 22 TX -> Mega RX2 pin 17
 * PIC RD2 / pin 21 RX <- Mega TX2 pin 16
 *
 * The program:
 *   1. Talks to AS608 at 57600.
 *   2. Sends SetSysPara to select 9600.
 *   3. Reinitializes PIC hardware UART at 9600.
 *   4. Verifies the AS608 at 9600.
 *
 * Only rewire after Serial Monitor reports:
 *      BAUD_CHANGE_OK
 */

#include <xc.h>
#include <stdint.h>

#define _XTAL_FREQ 10000000UL

#pragma config FOSC = HS
#pragma config WDTE = OFF
#pragma config PWRTE = ON
#pragma config BOREN = ON
#pragma config LVP = OFF
#pragma config CPD = OFF
#pragma config WRT = OFF
#pragma config CP = OFF

#define REPORT_TX PORTDbits.RD3

volatile uint8_t rxBuf[24];
volatile uint8_t rxCount = 0;

static void ReportWrite(uint8_t data)
{
    uint8_t i;

    REPORT_TX = 0;
    __delay_us(416);

    for (i = 0; i < 8; i++)
    {
        REPORT_TX = (data & 1U) ? 1 : 0;
        __delay_us(416);
        data >>= 1;
    }

    REPORT_TX = 1;
    __delay_us(416);
}

static void ReportPrint(const char *s)
{
    while (*s)
        ReportWrite((uint8_t)*s++);
}

static void SensorUART_Common(uint8_t spbrg, uint8_t brgh)
{
    RCSTAbits.SPEN = 0;

    TRISCbits.TRISC6 = 0;
    TRISCbits.TRISC7 = 1;

    TXSTAbits.SYNC = 0;
    TXSTAbits.BRGH = brgh;
    SPBRG = spbrg;

    RCSTAbits.SPEN = 1;
    TXSTAbits.TXEN = 1;
    RCSTAbits.CREN = 1;

    PIE1bits.RCIE = 1;
    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;

    __delay_ms(50);
}

static void SensorUART57600(void)
{
    SensorUART_Common(10, 1);
}

static void SensorUART9600(void)
{
    SensorUART_Common(64, 1);
}

static void SensorWrite(uint8_t b)
{
    while (!PIR1bits.TXIF);
    TXREG = b;
}

static void ClearRx(void)
{
    uint8_t i;

    PIE1bits.RCIE = 0;
    rxCount = 0;

    for (i = 0; i < sizeof(rxBuf); i++)
        rxBuf[i] = 0;

    while (PIR1bits.RCIF)
    {
        volatile uint8_t d = RCREG;
        (void)d;
    }

    if (RCSTAbits.OERR)
    {
        RCSTAbits.CREN = 0;
        RCSTAbits.CREN = 1;
    }

    PIE1bits.RCIE = 1;
}

void __interrupt() ISR(void)
{
    uint8_t d;

    if (PIE1bits.RCIE && PIR1bits.RCIF)
    {
        if (RCSTAbits.OERR)
        {
            RCSTAbits.CREN = 0;
            RCSTAbits.CREN = 1;
        }

        while (PIR1bits.RCIF)
        {
            d = RCREG;

            if (rxCount < sizeof(rxBuf))
                rxBuf[rxCount++] = d;
        }
    }
}

static uint8_t WaitAck(uint16_t timeoutMs)
{
    uint16_t t = 0;

    while (rxCount < 12U && t < timeoutMs)
    {
        __delay_ms(1);
        t++;
    }

    if (rxCount < 10U)
        return 0;

    if (rxBuf[0] != 0xEF ||
        rxBuf[1] != 0x01 ||
        rxBuf[6] != 0x07)
        return 0;

    return (rxBuf[9] == 0x00);
}

static uint8_t VerifySensor(void)
{
    uint8_t i;

    const uint8_t verify[] =
    {
        0xEF,0x01,0xFF,0xFF,0xFF,0xFF,
        0x01,0x00,0x07,0x13,
        0x00,0x00,0x00,0x00,
        0x00,0x1B
    };

    ClearRx();
    __delay_ms(100);

    for (i = 0; i < sizeof(verify); i++)
        SensorWrite(verify[i]);

    return WaitAck(1200);
}

void main(void)
{
    uint8_t i;

    /*
     * SetSysPara:
     * instruction 0x0E
     * parameter number 0x04 = baud-rate control
     * value 0x01 = 9600 baud
     */
    const uint8_t set9600[] =
    {
        0xEF,0x01,0xFF,0xFF,0xFF,0xFF,
        0x01,0x00,0x05,0x0E,0x04,0x01,
        0x00,0x19
    };

    ADCON1 = 0x06;
    TRISEbits.PSPMODE = 0;

    TRISDbits.TRISD3 = 0;
    REPORT_TX = 1;

    SensorUART57600();

    __delay_ms(1800);

    ReportPrint("\r\nAS608 BAUD MIGRATION 57600 -> 9600\r\n");
    ReportPrint("Sending baud-change command...\r\n");

    ClearRx();
    __delay_ms(100);

    for (i = 0; i < sizeof(set9600); i++)
        SensorWrite(set9600[i]);

    /*
     * We do not depend only on the ACK because some modules change
     * baud immediately.  The final decision is made by a real verify
     * command at 9600.
     */
    __delay_ms(500);

    SensorUART9600();

    ReportPrint("Testing sensor at 9600...\r\n");

    if (VerifySensor())
    {
        ReportPrint("BAUD_CHANGE_OK\r\n");
        ReportPrint("AS608 VERIFIED AT 9600\r\n");
        ReportPrint("POWER CYCLE, THEN USE FINAL WIRING/CODE\r\n");
    }
    else
    {
        ReportPrint("9600 VERIFY FAILED\r\n");
        ReportPrint("Checking whether sensor is still 57600...\r\n");

        SensorUART57600();

        if (VerifySensor())
        {
            ReportPrint("BAUD_CHANGE_FAILED_STILL_57600\r\n");
            ReportPrint("KEEP OLD WIRING AND RETRY\r\n");
        }
        else
        {
            ReportPrint("SENSOR_NOT_VERIFIED_AT_9600_OR_57600\r\n");
            ReportPrint("CHECK SENSOR POWER/WIRING BEFORE REWIRING\r\n");
        }
    }

    while (1)
        __delay_ms(1000);
}
