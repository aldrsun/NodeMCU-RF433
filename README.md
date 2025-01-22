# NodeMCU-RF433
Firmware for NodeMCU enabling RF signal transmission and reception, with UDP communication to a server.

## Setup Instructions

Before building and uploading the firmware, you need to create a `constants.h` file in the project directory. This file should define the following constants:

```cpp
#define SSID "YourWiFiSSID"
#define PASSWORD "YourWiFiPassword"
#define UDP_ADDRESS "YourUDPServerAddress"
#define UDP_PORT YourUDPServerPort
