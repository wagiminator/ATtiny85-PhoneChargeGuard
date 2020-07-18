// USB Phone Charge Guard - Basic
//
// The ATtiny85 USB Phone Charge Guard controls and monitors the charging of phones
// and other devices. Voltage, current, power and energy are constantly measured via
// the INA219 and compared with the user limit values. It cuts off the power supply
// via a MOSFET when a condition selected by the user is reached. The user settings
// are saved in the EEPROM.
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
// 
//
//                           +-\/-+
// RESET ----- A0 (D5) PB5  1|    |8  Vcc
// BUTTONS --- A3 (D3) PB3  2|    |7  PB2 (D2) A1 --- OLED/INA (SCK)
// ENABLE ---- A2 (D4) PB4  3|    |6  PB1 (D1) 
//                     GND  4|    |5  PB0 (D0) ------ OLED/INA (SDA)
//                           +----+
//
// Controller:  ATtiny85
// Core:        ATTinyCore (https://github.com/SpenceKonde/ATTinyCore)
// Clockspeed:  8 MHz internal
// Millis:      enabled
//
// 2020 by Stefan Wagner (https://easyeda.com/wagiminator)
// License: http://creativecommons.org/licenses/by-sa/3.0/


//Libraries
#include <TinyI2CMaster.h>      // https://github.com/technoblogy/tiny-i2c
#include <Tiny4kOLED.h>         // https://github.com/datacute/Tiny4kOLED
#include <avr/pgmspace.h>       // for using data in program space
#include <EEPROM.h>             // for storing user settings into EEPROM

// Pin definitions
#define BUTTONS     A3
#define ENABLE      4

// INA219 register values
#define INA_ADDR    0b01000000          // I2C address of INA219
#define INA_CONFIG  0b0000011001100111  // INA config register according to datasheet
#define INA_CALIB   5120                // INA calibration register according to R_SHUNT
#define CONFIG_REG  0x00                // INA configuration register address
#define CALIB_REG   0x05                // INA calibration register address
#define SHUNT_REG   0x01                // INA shunt voltage register address
#define VOLTAGE_REG 0x02                // INA bus voltage register address
#define POWER_REG   0x03                // INA power register address
#define CURRENT_REG 0x04                // INA current register address

// Button definitions
#define NO_KEY      0
#define KEY_SELECT  1
#define KEY_INC     2
#define KEY_DEC     3
#define KEY_START   4

// EEPROM identifier
#define EEPROM_IDENT   0xA76C   // to identify if EEPROM was written by this program

// Button ADC thresholds
uint16_t THRESHOLD[] = {896, 726, 597, 256, 0};

// Variables (voltage in mV, current in mA, power in mW)
uint16_t  voltage, current, power, minutes;
uint32_t  startmillis, lastmillis, nowmillis, limitmillis, interval, seconds;
uint32_t  chargetime = 0;
uint32_t  capacity = 0, energy = 0;
uint8_t   primescreen = 0;
uint16_t  rawbutton;
uint8_t   button;
uint8_t   lastbutton  = 0;
uint8_t   limit = 0;
uint8_t   showvalue = 0;
bool      charging = false;

// Limits                  mAh    mWh    mA  min
uint16_t  limit_val[] = { 3000, 15000,  200, 180};  // current value
uint16_t  limit_min[] = {  500,  1000,  100,  10};  // minimum value
uint16_t  limit_max[] = {10000, 50000, 3000, 600};  // maximum value
uint16_t  limit_inc[] = {  100,   500,  100,  10};  // inc/dec step
const char *limit_s[] = {"mAh", "mWh", "mA","min"}; // strings


void setup() {
  // setup pins
  DDRB =  bit (ENABLE);                 // set output pins
  PORTB = 0;                            // no pullups, ENABLE = low;

  // read user settings from EEPROM
  getEEPROM();
  
  // start I2C
  TinyI2C.init();

  // start INA219
  initINA();

  // start OLED
  oled.begin();
  oled.clear();
  oled.on();
  oled.switchRenderFrame();

  // init some variables
  startmillis = millis();
  lastmillis  = startmillis;
}

void loop() {
  // read voltage, current and power from INA219
  updateINA();

  // calculate power in mW
  power = (uint32_t)voltage * current / 1000;

  // calculate capacity in uAh and energy in uWh
  nowmillis   = millis();
  interval    = nowmillis - lastmillis;     // calculate time interval
  lastmillis  = nowmillis;
  capacity += interval * current / 3600;    // calculate uAh
  energy   += interval * power   / 3600;    // calculate uWh
  if (charging) chargetime += interval;     // calculate charging time
  seconds     = chargetime / 1000;          // calculate total seconds
  minutes     = seconds / 60;               // calculate total minutes

  // check if charging limit is reached
  if (charging) {
    switch (limit) {
      case 0:   if ((capacity / 1000) >= limit_val[0]) charging = false; break;
      case 1:   if ((energy / 1000) >= limit_val[1]) charging = false; break;
      case 2:   if (current < limit_val[2]) {
                  if (nowmillis > limitmillis) charging = false;
                }
                else limitmillis = millis() + 5000;
                break;
      case 3:   if (minutes >= limit_val[3]) charging = false; break;
      default:  break;
    }
    if (!charging) PORTB = 0;
  }

  // check buttons
  rawbutton = analogRead(BUTTONS);
  button = 0;
  while (rawbutton < THRESHOLD[button]) button++;
  
  switch (button) {
    case 1:   if (button != lastbutton) {
                if (charging) showvalue++;
                else limit++;
                if (limit > 3) limit = 0;
                if (showvalue > 2) showvalue = 0;
              }
              break;
    case 2:   if (!charging) {
                if (limit_val[limit] < limit_max[limit]) limit_val[limit] += limit_inc[limit];
              }
              break;
    case 3:   if (!charging) {
                  if (limit_val[limit] > limit_min[limit]) limit_val[limit] -= limit_inc[limit];
              }
              break;
    case 4:   if (button != lastbutton) {
                charging = !charging;
                if (charging) PORTB = bit(ENABLE);
                else PORTB = 0;
                updateEEPROM();
                limitmillis = millis() + 5000;
              }
              break;
    default:  break;
  }

  lastbutton = button;

  // update OLED
  updateOLED();

  // a little delay
  delay(100);
}


// writes a register value to the INA219
void writeRegister(uint8_t reg, uint16_t value) {
  TinyI2C.start(INA_ADDR, 0);
  TinyI2C.write(reg);
  TinyI2C.write((value >> 8) & 0xff);
  TinyI2C.write(value & 0xff);
  TinyI2C.stop();
}

// reads a register from the INA219
uint16_t readRegister(uint8_t reg) {
  uint16_t result;
  TinyI2C.start(INA_ADDR, 0);
  TinyI2C.write(reg);
  TinyI2C.restart(INA_ADDR, 2);
  result = (uint16_t)(TinyI2C.read() << 8) | TinyI2C.read();
  TinyI2C.stop();
  return(result);
}

// writes inital configuration and calibration values to the INA
void initINA() {
  writeRegister(CONFIG_REG, INA_CONFIG);
  writeRegister(CALIB_REG,  INA_CALIB);
}

// read sensor values from INA219
void updateINA() {
  voltage = (readRegister(VOLTAGE_REG) >> 1) & 0xfffc;
  current = readRegister(CURRENT_REG);
  if (current > 32767) current = 0;
  if (current < 5) current = 0;
}

// updates OLED
void updateOLED() {
  oled.clear();
  oled.setFont(FONT6X8);
  oled.setCursor(0, 0);
  oled.print(F("LIMIT:"));
  oled.print(limit_val[limit]);
  oled.print(limit_s[limit]);

  oled.setCursor(92, 0);
  if (charging) oled.print(F("CHARGE"));
  else oled.print(F(" PAUSE"));
  oled.setFont(FONT8X16DIGITS);

  switch (showvalue) {
    case 0:   printValue(0, capacity / 1000);
              printValue(70, energy / 1000);
              oled.setFont(FONT6X8);
              oled.setCursor(40, 3);
              oled.print(F("mAh"));
              oled.setCursor(110, 3);
              oled.print(F("mWh"));
              break;
    case 1:   printValue(0, voltage);
              printValue(70, current);
              oled.setFont(FONT6X8);
              oled.setCursor(40, 3);
              oled.print(F("mV"));
              oled.setCursor(110, 3);
              oled.print(F("mA"));
              break;
    case 2:   printValue(30, minutes);
              oled.setFont(FONT6X8);
              oled.setCursor(70, 3);
              oled.print(F("min"));
              break;
    default:  break;
  }

  oled.switchFrame();
}

// prints 5-digit value right aligned
void printValue(uint8_t pos, uint16_t value) {
  uint32_t counter = value;
  if (counter == 0) counter = 1;
  while (counter < 10000) {
    pos += 8;
    counter *= 10;
  }
  oled.setCursor(pos, 2);
  oled.print(value);
}

// reads user settings from EEPROM; if EEPROM values are invalid, write defaults
void getEEPROM() {
  uint16_t identifier = (EEPROM.read(0) << 8) | EEPROM.read(1);
  if (identifier == EEPROM_IDENT) {
    for (uint8_t i=0; i<4; i++) {
      limit_val[i] = (EEPROM.read(2*i+2) << 8) | EEPROM.read(2*i+3);
    }
    limit     =  EEPROM.read(10);
    showvalue =  EEPROM.read(11);
  }
  else {
    EEPROM.update(0, EEPROM_IDENT >> 8); EEPROM.update(1, EEPROM_IDENT & 0xFF);
    updateEEPROM();
  }
}

// writes user settings to EEPROM using updade function to minimize write cycles
void updateEEPROM() {
  for (uint8_t i=0; i<4; i++) {
    EEPROM.update( 2*i+2, limit_val[i] >> 8);
    EEPROM.update( 2*i+3, limit_val[i] & 0xFF);
  }
  EEPROM.update(10, limit);
  EEPROM.update(11, showvalue);
}
