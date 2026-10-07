#include <xc.h>
#include <stdint.h>
#include <string.h>

#define _XTAL_FREQ 10000000UL

#pragma config FOSC = HS
#pragma config WDTE = OFF
#pragma config PWRTE = ON
#pragma config BOREN = OFF
#pragma config LVP = OFF
#pragma config CPD = OFF
#pragma config WRT = OFF
#pragma config CP = OFF

/* =========================
   HARDWARE MAP
   =========================
   PIC RC6/TX pin 25 -> Mega 17 RX2
   PIC RC7/RX pin 26 <- Mega 16 TX2
   UART: 9600

   DS3231:
   RC3 pin 18 -> SCL
   RC4 pin 23 -> SDA

   Outputs:
   RD0 pin 19 -> WHITE LED
   RD1 pin 20 -> RED LED
   RC2 pin 17 -> BUZZER

   Switches (active LOW, 10k pull-up to +5V):
   RB0 pin 33 -> SW1 / BTN_ADD
   RB1 pin 34 -> SW2 / BTN_OK
   RB2 pin 35 -> SW3 / BTN_UP
   RB3 pin 36 -> SW4 / BTN_DOWN

   Mega decides page meaning:
   HOME: SW1 HOME, SW2 ENROLL, SW3 LOGS, SW4 USERS
   During enrollment: SW4 CANCEL
*/

#define WHITE_LED PORTDbits.RD0
#define RED_LED   PORTDbits.RD1
#define BUZZER    PORTCbits.RC2

#define BTN_ADD   PORTBbits.RB0
#define BTN_OK    PORTBbits.RB1
#define BTN_UP    PORTBbits.RB2
#define BTN_DOWN  PORTBbits.RB3

#define UART_RX_BUFFER_SIZE 64
#define UART_RX_BUFFER_MASK 63

volatile char uartRxBuffer[UART_RX_BUFFER_SIZE];
volatile uint8_t uartRxHead = 0;
volatile uint8_t uartRxTail = 0;

char commandBuffer[64];
uint8_t commandIndex = 0;

volatile uint8_t timerTick = 0;
volatile uint8_t rtcDivider = 0;
volatile uint8_t rtcSendFlag = 0;

static void white_led_on(void)  { WHITE_LED = 1; }
static void white_led_off(void) { WHITE_LED = 0; }
static void red_led_on(void)    { RED_LED = 1; }
static void red_led_off(void)   { RED_LED = 0; }
static void buzzer_on(void)     { BUZZER = 1; }
static void buzzer_off(void)    { BUZZER = 0; }

static void outputs_off(void)
{
    white_led_off();
    red_led_off();
    buzzer_off();
}

/* =========================
   UART
   ========================= */

static void uart_init(void)
{
    TRISCbits.TRISC6 = 0;
    TRISCbits.TRISC7 = 1;

    TXSTAbits.SYNC = 0;
    TXSTAbits.BRGH = 1;
    SPBRG = 64;              // 9600 baud @ 10 MHz

    RCSTAbits.SPEN = 1;
    TXSTAbits.TXEN = 1;
    RCSTAbits.CREN = 1;

    PIE1bits.RCIE = 1;
    INTCONbits.PEIE = 1;
}

static void uart_putc(char c)
{
    while (!PIR1bits.TXIF) {}
    TXREG = c;
}

static void uart_text(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_newline(void)
{
    uart_putc('\r');
    uart_putc('\n');
}

static void uart_ack(const char *msg)
{
    uart_text("ACK,");
    uart_text(msg);
    uart_newline();
}

static void uart_error(const char *msg)
{
    uart_text("ERR,");
    uart_text(msg);
    uart_newline();
}

/* =========================
   I2C / DS3231
   ========================= */

static void i2c_wait(void)
{
    while ((SSPCON2 & 0x1F) || SSPSTATbits.R_nW) {}
}

static void i2c_init(void)
{
    TRISCbits.TRISC3 = 1;
    TRISCbits.TRISC4 = 1;

    SSPCON = 0x28;
    SSPSTAT = 0x80;
    SSPADD = 24;             // 100 kHz @ 10 MHz
}

static void i2c_start(void)
{
    i2c_wait();
    SSPCON2bits.SEN = 1;
    while (SSPCON2bits.SEN) {}
}

static void i2c_restart(void)
{
    i2c_wait();
    SSPCON2bits.RSEN = 1;
    while (SSPCON2bits.RSEN) {}
}

static void i2c_stop(void)
{
    i2c_wait();
    SSPCON2bits.PEN = 1;
    while (SSPCON2bits.PEN) {}
}

static void i2c_write(uint8_t data)
{
    i2c_wait();
    SSPBUF = data;
    while (SSPSTATbits.BF) {}
    i2c_wait();
}

static uint8_t i2c_read(uint8_t sendAck)
{
    uint8_t data;

    i2c_wait();
    SSPCON2bits.RCEN = 1;
    while (!SSPSTATbits.BF) {}

    data = SSPBUF;

    i2c_wait();
    SSPCON2bits.ACKDT = sendAck ? 0 : 1;
    SSPCON2bits.ACKEN = 1;
    while (SSPCON2bits.ACKEN) {}

    return data;
}

static uint8_t bcd_to_dec(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

static uint8_t dec_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static void rtc_read(
    uint8_t *year,
    uint8_t *month,
    uint8_t *day,
    uint8_t *hour,
    uint8_t *minute,
    uint8_t *second)
{
    i2c_start();
    i2c_write(0xD0);
    i2c_write(0x00);

    i2c_restart();
    i2c_write(0xD1);

    *second = bcd_to_dec(i2c_read(1) & 0x7F);
    *minute = bcd_to_dec(i2c_read(1));
    *hour   = bcd_to_dec(i2c_read(1) & 0x3F);

    (void)i2c_read(1); // day of week

    *day   = bcd_to_dec(i2c_read(1));
    *month = bcd_to_dec(i2c_read(1) & 0x1F);
    *year  = bcd_to_dec(i2c_read(0));

    i2c_stop();
}

static void rtc_set_time(uint8_t hour, uint8_t minute, uint8_t second)
{
    i2c_start();
    i2c_write(0xD0);
    i2c_write(0x00);
    i2c_write(dec_to_bcd(second));
    i2c_write(dec_to_bcd(minute));
    i2c_write(dec_to_bcd(hour));
    i2c_stop();
}

static void rtc_set_date(uint8_t year, uint8_t month, uint8_t day)
{
    i2c_start();
    i2c_write(0xD0);
    i2c_write(0x04);
    i2c_write(dec_to_bcd(day));
    i2c_write(dec_to_bcd(month));
    i2c_write(dec_to_bcd(year));
    i2c_stop();
}

static void uart_two_digits(uint8_t value)
{
    uart_putc((char)('0' + value / 10U));
    uart_putc((char)('0' + value % 10U));
}

static void rtc_send(void)
{
    uint8_t year, month, day, hour, minute, second;

    rtc_read(&year, &month, &day, &hour, &minute, &second);

    uart_text("TIME,20");
    uart_two_digits(year);
    uart_putc('-');
    uart_two_digits(month);
    uart_putc('-');
    uart_two_digits(day);
    uart_putc(',');
    uart_two_digits(hour);
    uart_putc(':');
    uart_two_digits(minute);
    uart_putc(':');
    uart_two_digits(second);
    uart_newline();
}

/* =========================
   VALIDATION
   ========================= */

static uint8_t is_digit_char(char c)
{
    return (c >= '0' && c <= '9');
}

static uint8_t parse_two_digits_safe(const char *p, uint8_t *result)
{
    if (!is_digit_char(p[0]) || !is_digit_char(p[1])) return 0;

    *result = (uint8_t)(((p[0] - '0') * 10) + (p[1] - '0'));
    return 1;
}

static uint8_t valid_time(uint8_t hour, uint8_t minute, uint8_t second)
{
    return (hour <= 23 && minute <= 59 && second <= 59);
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    if (month == 2)
    {
        uint8_t leap = (uint8_t)(
            ((year % 4U == 0U) && (year % 100U != 0U)) ||
            (year % 400U == 0U));

        return leap ? 29 : 28;
    }

    if (month == 4 || month == 6 || month == 9 || month == 11)
        return 30;

    return 31;
}

static uint8_t valid_date(uint16_t year, uint8_t month, uint8_t day)
{
    if (year < 2000 || year > 2099) return 0;
    if (month < 1 || month > 12) return 0;
    if (day < 1 || day > days_in_month(year, month)) return 0;

    return 1;
}

/* =========================
   FEEDBACK
   ========================= */

static void feedback_ok(void)
{
    outputs_off();

    white_led_on();
    buzzer_on();
    __delay_ms(120);
    buzzer_off();
    __delay_ms(80);
    white_led_off();
    __delay_ms(100);

    white_led_on();
    buzzer_on();
    __delay_ms(100);
    buzzer_off();
    __delay_ms(80);
    white_led_off();

    outputs_off();
}

static void feedback_deny(void)
{
    outputs_off();

    red_led_on();
    buzzer_on();
    __delay_ms(320);
    red_led_off();
    buzzer_off();
    __delay_ms(120);

    for (uint8_t i = 0; i < 2; i++)
    {
        red_led_on();
        buzzer_on();
        __delay_ms(120);
        red_led_off();
        buzzer_off();
        __delay_ms(120);
    }

    outputs_off();
}

static void feedback_duplicate(void)
{
    outputs_off();

    white_led_on();
    buzzer_on();
    __delay_ms(80);
    white_led_off();
    buzzer_off();
    __delay_ms(70);

    red_led_on();
    buzzer_on();
    __delay_ms(80);
    red_led_off();
    buzzer_off();

    outputs_off();
}

static void feedback_saved(void)
{
    outputs_off();

    white_led_on();
    buzzer_on();
    __delay_ms(160);
    buzzer_off();
    __delay_ms(80);
    buzzer_on();
    __delay_ms(70);
    buzzer_off();
    __delay_ms(80);
    white_led_off();

    outputs_off();
}

/* =========================
   COMMAND PROCESSOR
   ========================= */

static void process_command(char *command)
{
    if (strcmp(command, "PING") == 0)
    {
        uart_ack("PONG");
        return;
    }

    if (strcmp(command, "GET_TIME") == 0)
    {
        rtc_send();
        return;
    }

    if (strncmp(command, "SET_DATE,", 9) == 0)
    {
        uint8_t yy, month, day;

        if (strlen(command) != 19 || command[13] != '-' || command[16] != '-')
        {
            uart_error("SET_DATE_FORMAT");
            return;
        }

        if (
            command[9] != '2' ||
            command[10] != '0' ||
            !parse_two_digits_safe(command + 11, &yy) ||
            !parse_two_digits_safe(command + 14, &month) ||
            !parse_two_digits_safe(command + 17, &day))
        {
            uart_error("INVALID_DATE");
            return;
        }

        if (!valid_date((uint16_t)(2000U + yy), month, day))
        {
            uart_error("INVALID_DATE");
            return;
        }

        rtc_set_date(yy, month, day);
        uart_ack("SET_DATE");
        rtc_send();
        return;
    }

    if (strncmp(command, "SET_TIME,", 9) == 0)
    {
        uint8_t hour, minute, second;

        if (strlen(command) != 17 || command[11] != ':' || command[14] != ':')
        {
            uart_error("SET_TIME_FORMAT");
            return;
        }

        if (
            !parse_two_digits_safe(command + 9, &hour) ||
            !parse_two_digits_safe(command + 12, &minute) ||
            !parse_two_digits_safe(command + 15, &second))
        {
            uart_error("INVALID_TIME");
            return;
        }

        if (!valid_time(hour, minute, second))
        {
            uart_error("INVALID_TIME");
            return;
        }

        rtc_set_time(hour, minute, second);
        uart_ack("SET_TIME");
        rtc_send();
        return;
    }

    if (strcmp(command, "FEEDBACK,OK") == 0)
    {
        uart_ack("FEEDBACK_OK");
        feedback_ok();
        return;
    }

    if (strcmp(command, "FEEDBACK,DENY") == 0)
    {
        uart_ack("FEEDBACK_DENY");
        feedback_deny();
        return;
    }

    if (strcmp(command, "FEEDBACK,DUP") == 0)
    {
        uart_ack("FEEDBACK_DUP");
        feedback_duplicate();
        return;
    }

    if (strcmp(command, "FEEDBACK,SAVED") == 0)
    {
        uart_ack("FEEDBACK_SAVED");
        feedback_saved();
        return;
    }

    if (strcmp(command, "TEST,WON") == 0)
    {
        white_led_on();
        uart_ack("WHITE_ON");
        return;
    }

    if (strcmp(command, "TEST,WOFF") == 0)
    {
        white_led_off();
        uart_ack("WHITE_OFF");
        return;
    }

    if (strcmp(command, "TEST,RON") == 0)
    {
        red_led_on();
        uart_ack("RED_ON");
        return;
    }

    if (strcmp(command, "TEST,ROFF") == 0)
    {
        red_led_off();
        uart_ack("RED_OFF");
        return;
    }

    if (strcmp(command, "TEST,BON") == 0)
    {
        buzzer_on();
        uart_ack("BUZZER_ON");
        return;
    }

    if (strcmp(command, "TEST,BOFF") == 0)
    {
        buzzer_off();
        uart_ack("BUZZER_OFF");
        return;
    }

    uart_error("UNKNOWN_COMMAND");
}

/* =========================
   UART POLL
   ========================= */

static void uart_poll(void)
{
    while (uartRxTail != uartRxHead)
    {
        char c = uartRxBuffer[uartRxTail];
        uartRxTail = (uint8_t)((uartRxTail + 1U) & UART_RX_BUFFER_MASK);

        if (c == '\r') continue;

        if (c == '\n')
        {
            if (commandIndex > 0)
            {
                commandBuffer[commandIndex] = '\0';
                process_command(commandBuffer);
                commandIndex = 0;
            }

            continue;
        }

        if (commandIndex < (sizeof(commandBuffer) - 1U))
        {
            commandBuffer[commandIndex++] = c;
        }
        else
        {
            commandIndex = 0;
        }
    }
}

/* =========================
   SWITCHES
   ========================= */

static void buttons_poll(void)
{
    static uint8_t oldAdd = 1;
    static uint8_t oldOk = 1;
    static uint8_t oldUp = 1;
    static uint8_t oldDown = 1;

    static uint8_t addTick = 0;
    static uint8_t okTick = 0;
    static uint8_t upTick = 0;
    static uint8_t downTick = 0;

    uint8_t tick = timerTick;
    uint8_t add = BTN_ADD;
    uint8_t ok = BTN_OK;
    uint8_t up = BTN_UP;
    uint8_t down = BTN_DOWN;

    if (oldAdd && !add && (uint8_t)(tick - addTick) >= 2U)
    {
        addTick = tick;
        uart_text("BTN_ADD");
        uart_newline();
    }

    if (oldOk && !ok && (uint8_t)(tick - okTick) >= 2U)
    {
        okTick = tick;
        uart_text("BTN_OK");
        uart_newline();
    }

    if (oldUp && !up && (uint8_t)(tick - upTick) >= 2U)
    {
        upTick = tick;
        uart_text("BTN_UP");
        uart_newline();
    }

    if (oldDown && !down && (uint8_t)(tick - downTick) >= 2U)
    {
        downTick = tick;
        uart_text("BTN_DOWN");
        uart_newline();
    }

    oldAdd = add;
    oldOk = ok;
    oldUp = up;
    oldDown = down;
}

/* =========================
   TIMER0
   ========================= */

static void timer0_init(void)
{
    OPTION_REG = 0x87;   // internal clock, prescaler 1:256
    TMR0 = 0;

    INTCONbits.T0IF = 0;
    INTCONbits.T0IE = 1;
    INTCONbits.GIE = 1;
}

/* =========================
   INTERRUPT
   ========================= */

void __interrupt() ISR(void)
{
    if (PIE1bits.RCIE && PIR1bits.RCIF)
    {
        if (RCSTAbits.OERR)
        {
            RCSTAbits.CREN = 0;
            RCSTAbits.CREN = 1;
        }

        while (PIR1bits.RCIF)
        {
            char c = RCREG;
            uint8_t next = (uint8_t)((uartRxHead + 1U) & UART_RX_BUFFER_MASK);

            if (next != uartRxTail)
            {
                uartRxBuffer[uartRxHead] = c;
                uartRxHead = next;
            }
        }
    }

    if (INTCONbits.T0IE && INTCONbits.T0IF)
    {
        INTCONbits.T0IF = 0;

        timerTick++;
        rtcDivider++;

        if (rtcDivider >= 38U)
        {
            rtcDivider = 0;
            rtcSendFlag = 1;
        }
    }
}

/* =========================
   I/O INIT
   ========================= */

static void io_init(void)
{
    ADCON1 = 0x06;

    // IMPORTANT: keep PORTD as normal GPIO, not Parallel Slave Port.
    TRISEbits.PSPMODE = 0;

    TRISBbits.TRISB0 = 1;
    TRISBbits.TRISB1 = 1;
    TRISBbits.TRISB2 = 1;
    TRISBbits.TRISB3 = 1;

    TRISDbits.TRISD0 = 0;
    TRISDbits.TRISD1 = 0;

    TRISCbits.TRISC2 = 0;

    outputs_off();
}

/* =========================
   MAIN
   ========================= */

void main(void)
{
    io_init();
    uart_init();
    i2c_init();
    timer0_init();

    __delay_ms(150);

    uart_ack("PIC_READY");
    rtc_send();

    while (1)
    {
        uart_poll();
        buttons_poll();

        if (rtcSendFlag)
        {
            rtcSendFlag = 0;
            rtc_send();
        }
    }
}
