# Temperature and Humidity Monitor ESP-32
A basic temperature and humidity display device using the esp-32 and DHT-11, with WiFi capability.

This project uses the ESP-32 along with a DHT-11, WiFi and OLED screen to display the ambient temperature and humidity.
There are parts of the code here that are hard coded to look for a specific address to send the data to in JSON format using HTTP.

Sources / Other peoples stuff I used:
I used the esp-idf IDE for this project - https://github.com/espressif/esp-idf
This ssd1306 repo for the getting the OLED screen working and examples - https://github.com/nopnop2002/esp-idf-ssd1306
Low Level Learing videos on the ESP-32 helped to get started - https://youtu.be/_dRrarmQiAM?si=8bA9b7T5wJ8WLsD8
