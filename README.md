# ESP8266 Discord Wake on Lan
This program will wake a device if a new message appears in a discord channel, after deleting said new message. <br>
You must create a dedicated discord channel for this.

### How to Use
Rename _secrets.h.rename_me_ to _secrets.h_ to and edit every variable which starts with ```SECRET_```.

## Disclaimer
You should not use this if you are scared of MITM attacks stealing your discord bot token.
I deliberately avoided dealing with certificates because they are invalidated too frequently.
