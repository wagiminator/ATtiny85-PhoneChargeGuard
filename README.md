# USB Phone Charge Guard
The ATtiny45/85 USB Phone Charge Guard controls and monitors the charging of phones and other devices. Voltage, current, power and energy are constantly measured via the INA219 and compared with the user limit values. It cuts off the power supply via a MOSFET when a condition selected by the user is reached. This allows the state of charge of the phone's Li-Ion battery to be controlled, thereby extending its service life. The user settings are saved in the EEPROM.

- Project Video (YouTube): https://youtu.be/9DHBoqHImcM
- Design Files (EasyEDA): https://easyeda.com/wagiminator/attiny85-phone-charge-guard

![pic6.jpg](https://raw.githubusercontent.com/wagiminator/ATtiny85-PhoneChargeGuard/master/documentation/PhoneChargeGuard_pic6.jpg)

# Hardware
## USB Connectors
The device is equipped with a USB-A plug for the input and a USB-A socket for the output, so that it can be plugged between the power supply and the phone (or other consumers). D+ and D- are passed through so that the phone can negotiate the charging protocol.

## Voltage and Current Measurement
An [INA219](https://www.ti.com/lit/ds/symlink/ina219.pdf) is used to measure voltage and current. The INA219 is a current shunt and power monitor with an I²C-compatible interface. The device monitors both shunt voltage drop and bus supply voltage, with programmable conversion times and filtering. A programmable calibration value, combined with an internal multiplier, enables direct readouts of current in amperes. The selected shunt resistance of 8mΩ enables both a very small influence on the circuit and a measurement with a resolution of 1mA. For an accurate measurement, a shunt resistor with a low tolerance (1% or better) should be selected.

## Voltage Regulator
The device is supplied with 3.3V via an [HT7333](http://www.angeladvance.com/HT73xx.pdf) LDO voltage regulator, which taps the input voltage of up to 12V from the USB bus power.

## MOSFET
The connection between the power supply and the phone is switched on and off via an [AS3621](https://datasheet.lcsc.com/szlcsc/1809211432_AnBon-AS3621_C232288.pdf) P-channel MOSFET. Despite its small size, it can switch up to 30V with 6A continuously. Its internal on-resistance is only about 20 milliohms.

## User Interface
The user interface utilizes five buttons and an [SSD1306 128x32 pixels OLED display](http://aliexpress.com/wholesale?SearchText=128+32+0.91+oled). An [ATtiny45/85](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-2586-AVR-8-bit-Microcontroller-ATtiny25-ATtiny45-ATtiny85_Datasheet.pdf) microcontroller handles the user interface, the control of the power connection and the calculation and display of the measured values.

![pic2.jpg](https://raw.githubusercontent.com/wagiminator/ATtiny85-PhoneChargeGuard/master/documentation/PhoneChargeGuard_pic2.jpg)
![pic3.jpg](https://raw.githubusercontent.com/wagiminator/ATtiny85-PhoneChargeGuard/master/documentation/PhoneChargeGuard_pic3.jpg)

# Software
## Basic Principle
The INA219 continuously measures current and voltage and transmits the values to the ATtiny via I²C. From this, the ATtiny calculates the other values and displays them on the OLED screen. It controls the connection between the power supply and the phone via the MOSFET depending on the settings chosen by the user. The user settings are saved in the EEPROM and automatically loaded the next time it is used.

## I²C OLED Implementation
The I²C protocol implementation is based on a crude bitbanging method. It was specifically designed for the limited resources of ATtiny10 and ATtiny13, but it works with some other AVRs (including the ATtiny45/85) as well. The functions for the OLED are adapted to the SSD1306 OLED module, but they can easily be modified to be used for other modules. In order to save resources, only the basic functionalities which are needed for this application are implemented. For a detailed information on the working principle of the I²C OLED implementation visit [TinyOLEDdemo](https://github.com/wagiminator/attiny13-tinyoleddemo).

## Accuracy of Time and Capacity Determination
The internal oscillator of the ATtiny is used to determine energy and capacity. The accuracy of the internal oscillator is +/-10% with the factory calibration. This can be improved to +/-2% or better by [manual calibration](https://github.com/wagiminator/ATtiny84-TinyCalibrator). The calibration value determined in this way can be set in the source code.

## Compiling and Uploading
Since there is no ICSP header on the board, you have to program the ATtiny either before soldering using an [SOP adapter](https://aliexpress.com/wholesale?SearchText=sop-8+150mil+adapter), or after soldering using an [EEPROM clip](https://aliexpress.com/wholesale?SearchText=sop8+eeprom+programming+clip). The [AVR Programmer Adapter](https://github.com/wagiminator/AVR-Programmer/tree/master/AVR_Programmer_Adapter) can help with this.

### If using the Arduino IDE
- Make sure you have installed [ATtinyCore](https://github.com/SpenceKonde/ATTinyCore).
- Go to **Tools -> Board -> ATtinyCore** and select **ATtiny25/45/85 (No bootloader)**.
- Go to **Tools** and choose the following board options:
  - **Chip:**           ATtiny45 or 85 (depending on your chip)
  - **Clock:**          1 MHz (internal)
  - **B.O.D.Level:**    B.O.D. enabled (2.7V)
  - Leave the rest at the default settings
- Connect your programmer to your PC and to the ATtiny.
- Go to **Tools -> Programmer** and select your ISP programmer (e.g. [USBasp](https://aliexpress.com/wholesale?SearchText=usbasp)).
- Go to **Tools -> Burn Bootloader** to burn the fuses.
- Open the PhonChargeGuard sketch and click **Upload**.

### If using the precompiled hex-file
- Make sure you have installed [avrdude](https://learn.adafruit.com/usbtinyisp/avrdude).
- Connect your programmer to your PC and to the ATtiny.
- Open a terminal.
- Navigate to the folder with the hex-file.
- Execute the following command (if necessary replace "usbasp" with the programmer you use):
  ```
  avrdude -c usbasp -p t85 -U lfuse:w:0x62:m -U hfuse:w:0xd5:m -U efuse:w:0xff:m -U flash:w:phonechargeguard.hex
  ```

### If using the makefile (Linux/Mac)
- Make sure you have installed [avr-gcc toolchain and avrdude](http://maxembedded.com/2015/06/setting-up-avr-gcc-toolchain-on-linux-and-mac-os-x/).
- Connect your programmer to your PC and to the ATtiny.
- Open a terminal.
- Navigate to the folder with the makefile and the Arduino sketch.
- Run `DEVICE=attiny85 PROGRMR=usbasp make install` to compile, burn the fuses and upload the firmware (change DEVICE and PROGRMR accordingly).

# Operating Instructions
1. Connect the device between a power supply and the phone (or other consumer).
2. Use the SELECT/INCREASE/DECREASE buttons to set the charging limit.
3. Use the START button to start the charging process.
4. Use the SELECT button during charging to switch between the displayed values.
5. The charging process stops automatically when the set limit value is reached.

![pic5.jpg](https://raw.githubusercontent.com/wagiminator/ATtiny85-PhoneChargeGuard/master/documentation/PhoneChargeGuard_pic5.jpg)

## Button Functions
|Button|Function|
|-|-|
|RESET|Reset all values|
|SELECT|Select limit type in pause mode / change displayed parameter in charging mode|
|INCREASE|Increase limit value|
|DECREASE|Decrease limit value|
|START|Start/pause charging|

## Selectable Charging Limits
|Limit Type|Function|
|-|-|
|mAh|Stop charging when electric charge reaches the selected value|
|mWh|Stop charging when provided energy reaches the selected value|
|mA|Stop charging when current falls below the selected value (this usually correlates with the state of charge of the battery)|
|min|Stop charging after the selected time in minutes|

## Characteristics
|Parameter|Value|
|-|-|
|Voltage|3V - 12V|
|Current|max 5A|
|Voltage Measurement Resolution|4mV|
|Current Measurement Resolution|1mA|

# References, Links and Notes
1. [ATtiny45/85 Datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-2586-AVR-8-bit-Microcontroller-ATtiny25-ATtiny45-ATtiny85_Datasheet.pdf)
2. [INA219 Datasheet](https://www.ti.com/lit/ds/symlink/ina219.pdf)
3. [SSD1306 Datasheet](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf)
4. [HT7333 Datasheet](http://www.angeladvance.com/HT73xx.pdf)
5. [AS3621 Datasheet](https://datasheet.lcsc.com/szlcsc/1809211432_AnBon-AS3621_C232288.pdf)
6. [128x32 OLED on Aliexpress](http://aliexpress.com/wholesale?SearchText=128+32+0.91+oled)

![pic4.jpg](https://raw.githubusercontent.com/wagiminator/ATtiny85-PhoneChargeGuard/master/documentation/PhoneChargeGuard_pic4.jpg)

# License
![license.png](https://i.creativecommons.org/l/by-sa/3.0/88x31.png)

This work is licensed under Creative Commons Attribution-ShareAlike 3.0 Unported License. 
(http://creativecommons.org/licenses/by-sa/3.0/)
