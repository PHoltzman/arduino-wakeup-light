# arduino-wakeup-light
This is an arduino-based wakeup light implementation and a companion Android app. This was my first attempt at a wakeup light and it works, but the socket connection is flakier than I would like and I ran up against the memory limits of the Arduino Pro Mini. My newer implementation uses LED pixel strips and a Raspberry Pi and is available here: https://github.com/PHoltzman/sunrise-wakeup-light

## Arduino
The wakeup light consists of an Arduino Pro Mini, an ESP8266-01 wi-fi module, and a generic LED par can stage light. Communication to the device is via a wireless socket connection. Communication to the light fixture is over DMX.

## Android App
