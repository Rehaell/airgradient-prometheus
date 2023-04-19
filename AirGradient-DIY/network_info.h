#ifndef NETWORK_INFO_
#define NETWORK_INFO_H

// WiFi and IP connection info.
const char* ssid = "It-Hertz-When-IP";
const char* password = "ruipipasmadalena2013";
const int port = 9926;

// NTP server info.
const char* ntpServer = "your-very-own-custom-ntp-server"; //custom NTP server

// Uncomment the line below to configure a static IP address.
// #define staticip
#ifdef staticip
IPAddress static_ip(192, 168, 0, 0);
IPAddress gateway(192, 168, 0, 0);
IPAddress subnet(255, 255, 255, 0);
#endif

#endif