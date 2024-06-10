# ESP8266 Discord Wake on Lan
This program will wake a device if a new message appears in a discord channel, after deleting said new message. <br>
You must create a dedicated discord channel for this. Your discord bot will need the ```Manage Messages``` and ```Read Messages/View Channels``` permissions set under Default Install Settings.

### Building
Rename _secrets.h.rename_me_ to _secrets.h_ to and edit every variable which starts with ```SECRET_```. <br>
Build and flash using [Arduino IDE](https://www.arduino.cc/en/software).
Make sure to install the required library [WakeOnLan](https://github.com/a7md0/WakeOnLan) first.

## Disclaimer
You should not use this if you are scared of MITM attacks stealing your discord bot token.
I deliberately avoided dealing with certificates because they are invalidated too frequently.
