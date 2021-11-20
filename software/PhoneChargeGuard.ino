// ===================================================================================
// Project:   USB Phone Charge Guard based on ATtiny45/85
// Version:   v1.2
// Year:      2020 - 2021
// Author:    Stefan Wagner
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// The ATtiny45/85 USB Phone Charge Guard controls and monitors the charging of
// phones and other devices. Voltage, current, power and energy are constantly
// measured via the INA219 and compared with the user limit values. It cuts off
// the power supply via a MOSFET when a condition selected by the user is reached.
// This allows the state of charge of the phone's Li-ion battery to be controlled,
// thereby extending its service life. The user settings are saved in the EEPROM.
//
// References:
// -----------
// The I²C OLED implementation is based on TinyOLEDdemo
// https://github.com/wagiminator/ATtiny13-TinyOLEDdemo
//
// Wiring:
// -------
//                            +-\/-+
//   RESET --- RST ADC0 PB5  1|°   |8  Vcc
// BUTTONS ------- ADC3 PB3  2|    |7  PB2 ADC1 -------- OLED/INA SCK
//  ENABLE ------- ADC2 PB4  3|    |6  PB1 AIN1 OC0B --- 
//                      GND  4|    |5  PB0 AIN0 OC0A --- OLED/INA SDA
//                            +----+
//
// Compilation Settings:
// ---------------------
// Core:    ATtinyCore (https://github.com/SpenceKonde/ATTinyCore)
// Board:   ATtiny25/45/85 (No bootloader)
// Chip:    ATtiny45 or ATtiny85 (depending on your chip)
// Clock:   1 MHz (internal)
// B.O.D.:  2.7V
//
// Leave the rest on default settings. Don't forget to "Burn bootloader"!
// No Arduino core functions or libraries are used. Use the makefile if 
// you want to compile without Arduino IDE.
//
// Note: The internal oscillator may need to be calibrated for precise
//       energy and capacity calculation.
//
// Fuse settings: -U lfuse:w:0x62:m -U hfuse:w:0xd5:m -U efuse:w:0xff:m 
//
// Operating Instructions:
// -----------------------
// Connect the device between a power supply and the phone (or other consumer).
// Use the SELECT/INCREASE/DECREASE buttons to set the charging limit.
// Use the START button to start the charging process.
// Use the SELECT button during charging to switch between the displayed values.
// The charging process stops automatically when the set limit value is reached.
//
// Buttons:
// RESET:     Reset all values
// SELECT:    Select limit type in pause mode/
//            change displayed parameter in charging mode
// INCREASE:  Increase limit value
// DECREASE:  Decrease limit value
// START:     Start/pause charging
//
// Limit types:
// mAh:       Stop charging when electric charge reaches the selected value
// mWh:       Stop charging when provided energy reaches the selected value
// mA:        Stop charging when current falls below the selected value
//            (this usually correlates with the state of charge of the battery)
// min:       Stop charging after the selected time in minutes


// ===================================================================================
// Libraries and Definitions
// ===================================================================================

// Oscillator calibration value (uncomment and set if necessary)
// #define OSCCAL_VAL  0x48

// Libraries
#include <avr/io.h>             // for GPIO
#include <avr/eeprom.h>         // for storing data in EEPROM
#include <avr/pgmspace.h>       // for data stored in program memory
#include <avr/interrupt.h>      // for interrupts
#include <util/delay.h>         // for delays

// Pin definitions
#define PIN_SDA     PB0         // I2C serial data pin, connected to OLED/INA219
#define PIN_SCL     PB2         // I2C serial clock pin, connected to OLED/INA219
#define PIN_KEYS    PB3         // pin connected to buttons array
#define PIN_ENABLE  PB4         // pin connected to MOSFET for power enable

// EEPROM identifier
#define EEPROM_IDENT   0xA76D   // to identify if EEPROM was written by this program

// Global variables
uint8_t   limit = 0;            // limit type selection flag
uint8_t   showValue = 0;        // displayed values selection flag

// Button enumeration
enum{ NO_KEY, KEY_SELECT, KEY_INC, KEY_DEC, KEY_START };

// Limits                               mAh    mWh    mA  min
uint16_t       limit_val[]         = { 3000, 15000,  200, 180};  // current value
const uint16_t limit_min[] PROGMEM = {  500,  1000,  100,  10};  // minimum value
const uint16_t limit_max[] PROGMEM = {10000, 50000, 3000, 590};  // maximum value
const uint16_t limit_inc[] PROGMEM = {  100,   500,  100,  10};  // inc/dec step

// Pin manipulation macros
#define pinInput(x)   DDRB  &= ~(1<<(x))    // set pin to INPUT
#define pinOutput(x)  DDRB  |=  (1<<(x))    // set pin to OUTPUT
#define pinLow(x)     PORTB &= ~(1<<(x))    // set pin to LOW
#define pinHigh(x)    PORTB |=  (1<<(x))    // set pin to HIGH
#define pinDisable(x) DIDR0 |=  (1<<(x))    // disable digital input buffer
#define pinRead(x)    (PINB &   (1<<(x)))   // READ pin
#define pinADC(x)     ((x)==2?1:((x)==3?3:((x)==4?2:0))) // convert pin to ADC port

// ===================================================================================
// I2C Master Implementation
// ===================================================================================

// I2C macros
#define I2C_SDA_HIGH()  pinInput(PIN_SDA)       // release SDA   -> pulled HIGH by resistor
#define I2C_SDA_LOW()   pinOutput(PIN_SDA)      // SDA as output -> pulled LOW  by MCU
#define I2C_SCL_HIGH()  pinInput(PIN_SCL)       // release SCL   -> pulled HIGH by resistor
#define I2C_SCL_LOW()   pinOutput(PIN_SCL)      // SCL as output -> pulled LOW  by MCU
#define I2C_SDA_READ()  pinRead(PIN_SDA)        // read SDA line
#define I2C_CLOCKOUT()  I2C_SCL_HIGH();I2C_SCL_LOW()  // clock out

// I2C transmit one data byte to the slave, ignore ACK bit, no clock stretching allowed
void I2C_write(uint8_t data) {
  for(uint8_t i=8; i; i--, data<<=1) {          // transmit 8 bits, MSB first
    (data&0x80)?I2C_SDA_HIGH():I2C_SDA_LOW();   // SDA depending on bit
    I2C_CLOCKOUT();                             // clock out -> slave reads the bit
  }
  I2C_SDA_HIGH();                               // release SDA for ACK bit of slave
  I2C_CLOCKOUT();                               // 9th clock pulse is for the ignored ACK bit
}

// I2C start transmission
void I2C_start(uint8_t addr) {
  I2C_SDA_LOW();                                // start condition: SDA goes LOW first
  I2C_SCL_LOW();                                // start condition: SCL goes LOW second
  I2C_write(addr);                              // send slave address
}

// I2C restart transmission
void I2C_restart(uint8_t addr) {
  I2C_SDA_HIGH();                               // prepare SDA for HIGH to LOW transition
  I2C_SCL_HIGH();                               // restart condition: clock HIGH
  I2C_start(addr);                              // start again
}

// I2C stop transmission
void I2C_stop(void) {
  I2C_SDA_LOW();                                // prepare SDA for LOW to HIGH transition
  I2C_SCL_HIGH();                               // stop condition: SCL goes HIGH first
  I2C_SDA_HIGH();                               // stop condition: SDA goes HIGH second
}

// I2C receive one data byte from the slave (ack=0 for last byte, ack>0 if more bytes to follow)
uint8_t I2C_read(uint8_t ack) {
  uint8_t data = 0;                             // variable for the received byte
  I2C_SDA_HIGH();                               // release SDA -> will be toggled by slave
  for(uint8_t i=8; i; i--) {                    // receive 8 bits
    data <<= 1;                                 // bits shifted in right (MSB first)
    I2C_SCL_HIGH();                             // clock HIGH
    if(I2C_SDA_READ()) data |= 1;               // read bit
    I2C_SCL_LOW();                              // clock LOW -> slave prepares next bit
  }
  if(ack) I2C_SDA_LOW();                        // pull SDA LOW to acknowledge (ACK)
  I2C_CLOCKOUT();                               // clock out -> slave reads ACK bit
  return data;                                  // return the received byte
}

// ===================================================================================
// OLED Implementation
// ===================================================================================

// OLED definitions
#define OLED_ADDR       0x78    // OLED write address
#define OLED_CMD_MODE   0x00    // set command mode
#define OLED_DAT_MODE   0x40    // set data mode
#define OLED_INIT_LEN   11      // 9: no screen flip, 11: screen flip

// OLED init settings
const uint8_t OLED_INIT_CMD[] PROGMEM = {
  0xA8, 0x1F,                   // set multiplex for 128x32
  0x20, 0x01,                   // set vertical memory addressing mode
  0xDA, 0x02,                   // set COM pins hardware configuration to sequential
  0x8D, 0x14,                   // enable charge pump
  0xAF,                         // switch on OLED
  0xA1, 0xC8                    // flip the screen
};

// OLED 5x8 font (adapted from Neven Boyanov and Stephen Denne)
const uint8_t OLED_FONT_SMALL[] PROGMEM = {
  0x3E, 0x51, 0x49, 0x45, 0x3E, //  0 0
  0x00, 0x42, 0x7F, 0x40, 0x00, //  1 1
  0x42, 0x61, 0x51, 0x49, 0x46, //  2 2
  0x21, 0x41, 0x45, 0x4B, 0x31, //  3 3
  0x18, 0x14, 0x12, 0x7F, 0x10, //  4 4
  0x27, 0x45, 0x45, 0x45, 0x39, //  5 5
  0x3C, 0x4A, 0x49, 0x49, 0x30, //  6 6
  0x01, 0x71, 0x09, 0x05, 0x03, //  7 7
  0x36, 0x49, 0x49, 0x49, 0x36, //  8 8
  0x06, 0x49, 0x49, 0x29, 0x1E, //  9 9
  0x7C, 0x12, 0x11, 0x12, 0x7C, // 10 A
  0x1F, 0x20, 0x40, 0x20, 0x1F, // 11 V
  0x3F, 0x40, 0x38, 0x40, 0x3F, // 12 W
  0x7F, 0x08, 0x04, 0x04, 0x78, // 13 h
  0x7C, 0x04, 0x18, 0x04, 0x78, // 14 m
  0x00, 0x36, 0x36, 0x00, 0x00, // 15 :
  0x00, 0x00, 0x00, 0x00, 0x00, // 16 SPACE
  0x3E, 0x41, 0x41, 0x41, 0x22, // 17 C
  0x7F, 0x49, 0x49, 0x49, 0x41, // 18 E
  0x3E, 0x41, 0x49, 0x49, 0x7A, // 19 G
  0x7F, 0x08, 0x08, 0x08, 0x7F, // 20 H
  0x00, 0x41, 0x7F, 0x41, 0x00, // 21 I
  0x7F, 0x40, 0x40, 0x40, 0x40, // 22 L
  0x7F, 0x02, 0x0C, 0x02, 0x7F, // 23 M
  0x7F, 0x09, 0x09, 0x09, 0x06, // 24 P
  0x7F, 0x09, 0x19, 0x29, 0x46, // 25 R
  0x46, 0x49, 0x49, 0x49, 0x31, // 26 S
  0x01, 0x01, 0x7F, 0x01, 0x01, // 27 T
  0x3F, 0x40, 0x40, 0x40, 0x3F, // 28 U
  0x00, 0x44, 0x7D, 0x40, 0x00, // 29 i
  0x7C, 0x08, 0x04, 0x04, 0x78  // 30 n
};

// OLED 6x16 font
const uint8_t OLED_FONT_BIG[] PROGMEM = {
  0x7C, 0x1F, 0x02, 0x20, 0x02, 0x20, 0x02, 0x20, 0x02, 0x20, 0x7C, 0x1F, //  0 0
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0x1F, //  1 1
  0x00, 0x1F, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x7C, 0x00, //  2 2
  0x00, 0x00, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x7C, 0x1F, //  3 3
  0x7C, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x7C, 0x1F, //  4 4
  0x7C, 0x00, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x00, 0x1F, //  5 5
  0x7C, 0x1F, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x00, 0x1F, //  6 6
  0x7C, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x7C, 0x1F, //  7 7
  0x7C, 0x1F, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x7C, 0x1F, //  8 8
  0x7C, 0x00, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x82, 0x20, 0x7C, 0x1F, //  9 9
  0x00, 0x00, 0xF0, 0x3F, 0x8C, 0x00, 0x82, 0x00, 0x8C, 0x00, 0xF0, 0x3F, // 10 A
  0x00, 0x00, 0xFE, 0x07, 0x00, 0x18, 0x00, 0x20, 0x00, 0x18, 0xFE, 0x07, // 11 V
  0x00, 0x00, 0xFE, 0x1F, 0x00, 0x20, 0x00, 0x1F, 0x00, 0x20, 0xFE, 0x1F, // 12 W
  0x00, 0x00, 0xFE, 0x3F, 0x00, 0x01, 0x80, 0x00, 0x80, 0x00, 0x00, 0x3F, // 13 h
  0x00, 0x00, 0x80, 0x3F, 0x80, 0x00, 0x80, 0x3F, 0x80, 0x00, 0x00, 0x3F, // 14 m
  0x00, 0x00, 0x00, 0x00, 0x30, 0x06, 0x30, 0x06, 0x00, 0x00, 0x00, 0x00, // 15 :
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // 16 SPACE
};

// Character definitions
#define COLON   15
#define SPACE   16

// OLED BCD conversion array
const uint16_t DIVIDER[] PROGMEM = {10000, 1000, 100, 10, 1};

// OLED current page
uint8_t OLED_page;

// OLED init function
void OLED_init(void) {
  I2C_start(OLED_ADDR);                         // start transmission to OLED
  I2C_write(OLED_CMD_MODE);                     // set command mode
  for(uint8_t i=0; i<OLED_INIT_LEN; i++) 
    I2C_write(pgm_read_byte(&OLED_INIT_CMD[i]));// send the command bytes
  I2C_stop();                                   // stop transmission
}

// OLED set the cursor
void OLED_setCursor(uint8_t xpos, uint8_t ypos) {
  I2C_start(OLED_ADDR);                         // start transmission to OLED
  I2C_write(OLED_CMD_MODE);                     // set command mode
  I2C_write(0x22);                              // command for min/max page
  I2C_write(ypos);                              // min: ypos
  (ypos > 1) ? I2C_write(3) : I2C_write(ypos);  // max: depending
  I2C_write(xpos & 0x0F);                       // set low nibble of start column
  I2C_write(0x10 | (xpos >> 4));                // set high nibble of start column
  I2C_write(0xB0 | (ypos));                     // set start page
  I2C_stop();                                   // stop transmission
  OLED_page = ypos;
}

// OLED clear screen
void OLED_clearScreen(void) {
  uint8_t i;                                    // count variable
  OLED_setCursor(0, 0);                         // set cursor at line 0
  I2C_start(OLED_ADDR);                         // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                     // set data mode
  for(i = 128; i; i--) I2C_write(0x00);         // clear line
  I2C_stop();                                   // stop transmission

  OLED_setCursor(0, 1);                         // set cursor at line 1
  I2C_start(OLED_ADDR);                         // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                     // set data mode
  for(i = 128; i; i--) I2C_write(0x02);         // draw line
  I2C_stop();                                   // stop transmission

  OLED_setCursor(0, 2);                         // set cursor at lower half
  I2C_start(OLED_ADDR);                         // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                     // set data mode
  do {I2C_write(0x00);} while(--i);             // clear lower half
  I2C_stop();                                   // stop transmission
}

// OLED plot a character
void OLED_plotChar(uint8_t ch) {
  if(OLED_page) {                               // big character ?
    ch = (ch << 3) + (ch << 2);                 // calculate position of character in font array
    I2C_write(0x00); I2C_write(0x00);           // print spacing between characters
    for(uint8_t i=12; i; i--)                   // 12 bytes per character
      I2C_write(pgm_read_byte(&OLED_FONT_BIG[ch++])); // print character
    I2C_write(0x00); I2C_write(0x00);           // print spacing between characters
  }
  else {                                        // small character ?
    ch += (ch << 2);                            // calculate position of character in font array
    I2C_write(0x00);                            // print spacing between characters
    for(uint8_t i=5; i; i--)                    // 5 bytes per character
      I2C_write(pgm_read_byte(&OLED_FONT_SMALL[ch++])); // print character
  }
}

// OLED print a character
void OLED_printChar(uint8_t ch) {
  I2C_start(OLED_ADDR);                         // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                     // set data mode
  OLED_plotChar(ch);                            // plot the character
  I2C_stop();                                   // stop transmission
}

// OLED print a string from program memory; terminator: 255
void OLED_printPrg(const uint8_t* p) {
  I2C_start(OLED_ADDR);                         // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                     // set data mode
  uint8_t ch = pgm_read_byte(p);                // read first character from program memory
  while(ch < 255) {                             // repeat until string terminator
    OLED_plotChar(ch);                          // plot character on OLED
    ch = pgm_read_byte(++p);                    // read next character
  }
  I2C_stop();                                   // stop transmission
}

// OLED print 16-bit value as 5-digit decimal (BCD conversion by substraction method)
void OLED_printDec16(uint16_t value) {
  uint8_t leadflag = 0;                         // flag for leading spaces
  I2C_start(OLED_ADDR);                         // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                     // set data mode
  for(uint8_t digit = 0; digit < 5; digit++) {  // 5 digits
    uint8_t digitval = 0;                       // start with digit value 0
    uint16_t divider = pgm_read_word(&DIVIDER[digit]); // read current divider
    while(value >= divider) {                   // if current divider fits into the value
      leadflag = 1;                             // end of leading spaces
      digitval++;                               // increase digit value
      value -= divider;                         // decrease value by divider
    }
    if(leadflag || (digit == 4)) OLED_plotChar(digitval); // print the digit
    else OLED_plotChar(SPACE);                  // or print leading space
  }
  I2C_stop();                                   // stop transmission
}

// OLED print 8-bit value as 2-digit decimal (BCD conversion by substraction method)
void OLED_printDec8(uint8_t value) {
  I2C_start(OLED_ADDR);                         // start transmission to OLED
  I2C_write(OLED_DAT_MODE);                     // set data mode
  uint8_t digitval = 0;                         // start with digit value 0
  while(value >= 10) {                          // if current divider fits into the value
    digitval++;                                 // increase digit value
    value -= 10;                                // decrease value by divider
  }
  OLED_plotChar(digitval);                      // print first digit
  OLED_plotChar(value);                         // print second digit
  I2C_stop();                                   // stop transmission
}

// ===================================================================================
// INA219 Implementation
// ===================================================================================

// INA219 register values
#define INA_ADDR        0x80                    // I2C write address of INA219
#define INA_CONFIG      0b0000011111111111      // INA config register according to datasheet
#define INA_CALIB       5120                    // INA calibration register according to R_SHUNT
#define INA_REG_CONFIG  0x00                    // INA configuration register address
#define INA_REG_CALIB   0x05                    // INA calibration register address
#define INA_REG_SHUNT   0x01                    // INA shunt voltage register address
#define INA_REG_VOLTAGE 0x02                    // INA bus voltage register address
#define INA_REG_POWER   0x03                    // INA power register address
#define INA_REG_CURRENT 0x04                    // INA current register address

// INA219 write a register value
void INA_write(uint8_t reg, uint16_t value) {
  I2C_start(INA_ADDR);                          // start transmission to INA219
  I2C_write(reg);                               // write register address
  I2C_write(value >> 8);                        // write register content high byte
  I2C_write(value);                             // write register content low  byte
  I2C_stop();                                   // stop transmission
}

// INA219 read a register
uint16_t INA_read(uint8_t reg) {
  uint16_t result;                              // result variable
  I2C_start(INA_ADDR);                          // start transmission to INA219
  I2C_write(reg);                               // write register address
  I2C_restart(INA_ADDR | 0x01);                 // restart for reading
  result = (uint16_t)(I2C_read(1) << 8) | I2C_read(0);  // read register content
  I2C_stop();                                   // stop transmission
  return result;                                // return result
}

// INA219 write inital configuration and calibration values
void INA_init(void) {
  INA_write(INA_REG_CONFIG, INA_CONFIG);        // write INA219 configuration
  INA_write(INA_REG_CALIB,  INA_CALIB);         // write INA219 calibration
}

// INA219 read voltage
uint16_t INA_readVoltage(void) {
  return((INA_read(INA_REG_VOLTAGE) >> 1) & 0xFFFC);
}

// INA219 read sensor values
uint16_t INA_readCurrent(void) {
  uint16_t result =  INA_read(INA_REG_CURRENT); // read current from INA
  if(result > 32767) result = 0;                // ignore negative currents
  if(result < 5)     result = 0;                // ignore tiny currents
  return result;                                // return result
}

// ===================================================================================
// Millis Counter Implementation for Timer0
// ===================================================================================

volatile uint32_t MIL_counter = 0;              // millis counter variable

// Init millis counter
void MIL_init(void) {
  OCR0A  = 124;                                 // TOP: 124 = 1000kHz / (8 * 1kHz) - 1
  TCCR0A = (1<<WGM01);                          // timer0 CTC mode
  TCCR0B = (1<<CS01);                           // start timer0 with prescaler 8
  TIMSK  = (1<<OCIE0A);                         // enable output compare match interrupt
}

// Read millis counter
uint32_t MIL_read(void) {
  cli();                                        // disable interrupt for atomic read
  uint32_t result = MIL_counter;                // read millis counter
  sei();                                        // enable interrupts
  return result;                                // return millis counter value
}

// Timer0 compare match A interrupt service routine (every millisecond)
ISR(TIM0_COMPA_vect) {
  MIL_counter++;                                // increase millis counter
}

// ===================================================================================
// EEPROM Implementation
// ===================================================================================

// EEPROM write user settings
void EEPROM_update(void) {
  eeprom_update_block((const void*)&limit_val, (void*)2, 8);
  eeprom_update_byte ((uint8_t*)10, limit);
  eeprom_update_byte ((uint8_t*)11, showValue);
}

// EEPROM read user settings; if EEPROM values are invalid, write defaults
void EEPROM_get(void) {
  uint16_t identifier = eeprom_read_word((const uint16_t*)0);
  if(identifier == EEPROM_IDENT) {
    eeprom_read_block((void*)&limit_val, (const void*)2, 8);
    limit     =  eeprom_read_byte((const uint8_t*)10);
    showValue =  eeprom_read_byte((const uint8_t*)11);
  }
  else {
    eeprom_update_word((uint16_t*)0, EEPROM_IDENT);
    EEPROM_update();
  }
}

// ===================================================================================
// ADC Implementation for Buttons
// ===================================================================================

// Button ADC thresholds
const uint16_t THRESHOLDS[] PROGMEM = {896, 726, 597, 256, 0};

// ADC init
void ADC_init(void) {
  ADCSRA = (1<<ADEN)                            // enable ADC
         | (1<<ADPS1) | (1<<ADPS0);             // set ADC prescaler 8
  ADMUX  = pinADC(PIN_KEYS);                    // set port of buttons against Vcc
}

// ADC read button and return button number
uint8_t readButton(void) {
  ADCSRA |= (1<<ADSC);                          // start sampling
  while(ADCSRA & (1<<ADSC));                    // wait until sampling complete
  uint16_t raw = ADC;                           // read sampling result
  uint8_t  button = 0;                          // get button number ...
  while(raw < pgm_read_word(&THRESHOLDS[button])) button++;
  return button;                                // return button number
}

// ===================================================================================
// Main Function
// ===================================================================================

// Some "strings"
const uint8_t limitStr[]  PROGMEM = { 22, 21, 23, 21, 27, 15, 255 };  // "LIMIT:"
const uint8_t chargeStr[] PROGMEM = { 17, 20, 10, 25, 19, 18, 255 };  // "CHARGE"
const uint8_t pauseStr[]  PROGMEM = { 16, 24, 10, 28, 26, 18, 255 };  // " PAUSE"
const uint8_t voltStr[]   PROGMEM = { 14, 11, 16, 16, 255 };          // "mV  "
const uint8_t currStr[]   PROGMEM = { 14, 10, 255 };                  // "mA"
const uint8_t limit_s[]   PROGMEM = { 14, 10, 13, 255,                // "mAh"
                                      14, 12, 13, 255,                // "mWh"
                                      14, 10, 16, 255,                // "mA "
                                      14, 29, 30, 255 };              // "min"

// Main function
int main(void) {
  // Local variables
  uint16_t  voltage, current, power;            // voltage in mV, current in mA, power in mW
  uint32_t  lastmillis, nowmillis, interval;    // for timing calculation in millis
  uint32_t  limitmillis;                        // timer limit
  uint32_t  chargetime = 0;                     // total charge time in millis
  uint16_t  seconds, minutes;                   // total duration in seconds and minutes
  uint32_t  capacity = 0, energy = 0;           // counter for capacity in uAh and energy in uWh
  uint8_t   lastbutton  = 0;                    // button flag (0: button pressed)
  uint8_t   button;                             // button number
  uint8_t   isCharging = 0;                     // charging flag (0: not charging)

  // Set oscillator calibration value
  #ifdef OSCCAL_VAL
    OSCCAL = OSCCAL_VAL;                        // set the value if defined above
  #endif

  // Setup
  pinOutput(PIN_ENABLE);                        // power enable pin as output
  pinDisable(PIN_KEYS);                         // disable digital input buffer
  MIL_init();                                   // init millis counter
  ADC_init();                                   // init ADC
  INA_init();                                   // init INA219
  OLED_init();                                  // init OLED
  OLED_clearScreen();                           // clear screen
  EEPROM_get();                                 // read user settings from EEPROM
  lastmillis = MIL_read();                      // read millis counter

  // Loop
  while(1) {
    // Read sensor values
    voltage = INA_readVoltage();                // read voltage in mV from INA219
    current = INA_readCurrent();                // read current in mA from INA219  

    // Calculate timings
    nowmillis   = MIL_read();                   // read millis counter
    interval    = nowmillis - lastmillis;       // calculate recent time interval
    lastmillis  = nowmillis;                    // reset lastmillis
    if(isCharging) chargetime += interval;      // calculate charging time
    seconds     = chargetime / 1000;            // calculate total seconds
    if(seconds > 35999) seconds = 35999;        // limit seconds timer
    minutes     = seconds / 60;                 // calculate total minutes

    // Calculate power, capacity and energy
    power = (uint32_t)voltage * current / 1000; // calculate power    in mW
    capacity += interval * current / 3600;      // calculate capacity in uAh
    energy   += interval * power   / 3600;      // calculate energy   in uWh

    // Check if charging limit is reached
    if(isCharging) {
      switch(limit) {
        case 0:   if((capacity / 1000) >= limit_val[0]) isCharging = 0; break;
        case 1:   if((energy   / 1000) >= limit_val[1]) isCharging = 0; break;
        case 2:   if(current < limit_val[2]) {
                    if(nowmillis > limitmillis) isCharging = 0;
                  }
                  else limitmillis = nowmillis + 5000;
                  break;
        case 3:   if(minutes >= limit_val[3]) isCharging = 0; break;
        default:  break;
      }
      if(!isCharging) pinLow(PIN_ENABLE);
    }

    // Check button and take proper action
    button = readButton();
    switch(button) {
      case KEY_SELECT:  if(button != lastbutton) {
                          if(isCharging) showValue++;
                          else limit++;
                          if(limit > 3) limit = 0;
                          if(showValue > 2) showValue = 0;
                        }
                        break;
      case KEY_INC:     if(!isCharging) {
                          if(limit_val[limit] <  pgm_read_word(&limit_max[limit]))
                             limit_val[limit] += pgm_read_word(&limit_inc[limit]);
                        }
                        break;
      case KEY_DEC:     if(!isCharging) {
                          if(limit_val[limit]  > pgm_read_word(&limit_min[limit]))
                             limit_val[limit] -= pgm_read_word(&limit_inc[limit]);
                        }
                        break;
      case KEY_START:   if(button != lastbutton) {
                          isCharging = !isCharging;
                          isCharging ? pinHigh(PIN_ENABLE) : pinLow(PIN_ENABLE);
                          EEPROM_update();
                          limitmillis = nowmillis + 5000;
                        }
                        break;
      default:          break;
    }
    lastbutton = button;

    // Update OLED
    OLED_setCursor(0, 0);
    OLED_printPrg(limitStr);
    OLED_printDec16(limit_val[limit]);
    OLED_printPrg(limit_s + (limit << 2));

    OLED_setCursor(92, 0);
    if(isCharging) OLED_printPrg(chargeStr);
    else           OLED_printPrg(pauseStr);

    OLED_setCursor(0, 2);
    if(showValue) {
      OLED_printChar(minutes / 60); OLED_printChar(COLON);
      OLED_printDec8(minutes % 60); OLED_printChar(COLON);
      OLED_printDec8(seconds % 60); OLED_printChar(SPACE);
    }

    switch(showValue) {
      case 0:   OLED_printDec16(voltage);
                OLED_printPrg(voltStr);
                OLED_printDec16(current);
                OLED_printPrg(currStr);
                break;
      case 1:   OLED_printDec16(capacity / 1000);
                OLED_printPrg(limit_s);
                break;
      case 2:   OLED_printDec16(energy / 1000);
                OLED_printPrg(limit_s + 4);
                break;
      default:  break;
    }

    // A little delay
    _delay_ms(125);
  }
}
