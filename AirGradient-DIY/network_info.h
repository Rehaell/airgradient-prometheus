#ifndef NETWORK_INFO_
#define NETWORK_INFO_H

// WiFi and IP connection info.
const char* ssid = "your-very-own-ssid";
const char* password = "your-very-own-ssid-password";
const int port = 9926;

// Uncomment the line below to configure a static IP address.
// #define staticip
#ifdef staticip
IPAddress static_ip(192, 168, 0, 0);
IPAddress gateway(192, 168, 0, 0);
IPAddress subnet(255, 255, 255, 0);
#endif

#endif