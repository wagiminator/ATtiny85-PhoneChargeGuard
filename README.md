# USB Phone Charge Guard
The ATtiny85 USB Phone Charge Guard controls and monitors the charging of phones and other devices. Voltage, current, power and energy are constantly measured via the INA219 and compared with the user limit values. It cuts off the power supply via a MOSFET when a condition selected by the user is reached. The user settings are saved in the EEPROM.

|Button|Function|
|-|-|
|RESET|Reset all values|
|SELECT|Select limit type in pause mode / change displayed parameter in charging mode|
|INCREASE|Increase limit value|
|DECREASE|Decrease limit value|
|START|Start/pause charging|

|Limit Type|Function|
|-|-|
|mAh|Stop charging when electric charge reaches the selected value|
|mWh|Stop charging when provided energy reaches the selected value|
|mA|Stop charging when current falls below the selected value (this usually correlates with the state of charge of the battery)|
|min|Stop charging after the selected time in minutes|

![IMG_20200718_184849_x.jpg](https://image.easyeda.com/pullimage/bpP2bVaRxFUDLPbcNhybCVts6gUZ4GgktVu9Ewga.jpeg)
![IMG_20200718_152748_x.jpg](https://image.easyeda.com/pullimage/BE2gF9fozvO5gDDAsaNqzVgwoUroLCRWibQ0LX0X.jpeg)
![IMG_20200718_152855_x.jpg](https://image.easyeda.com/pullimage/3QvPnmSNou87xKf8SOlAa34D73HGcBACELj2p5Mx.jpeg)

