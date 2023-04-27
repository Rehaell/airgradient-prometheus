/**
 * This sketch connects an AirGradient DIY sensor to a WiFi network, and runs a
 * tiny HTTP server to serve air quality metrics to Prometheus.
 * 
 * This will be an upgrade to the existing code base and a complete re-write.
 * The plan is to add the ability to adjust the time offset automatically as well as 
 * access a web page allowing the user to adjust the sensors offset.
 * 
 **/
#include <AirGradient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include "SSD1306Wire.h"

#include <network_info.h>

// Config ----------------------------------------------------------------------

// set to 'F' to switch display from Celcius to Fahrenheit
char temp_display = 'C';

// Hardware options for AirGradient DIY sensor.
const bool hasPM = true;
const bool hasCO2 = true;
const bool hasSHT = true;

// The frequency of measurement updates in milliseconds.
const int updateFrequency = 5000; //60000 = one minute
// The frequency of weather updates in milliseconds.
const int updateWeatherFrequency = 5000 ; //1800000 = 30 minutes

// For housekeeping.
long lastUpdate = 0;
long lastWeatherUpdate = 0;
int counter = 0;

//offsets for time, temperature, and humidity
int timeOffset = 0;
int tempOffset = 0;
int humOffset = 0;
String offsetUpdateString = "";

// Config End ------------------------------------------------------------------

AirGradient ag = AirGradient();

SSD1306Wire display(0x3c, SDA, SCL);

ESP8266WebServer server(port);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, timeOffset);




void setup() {
    //setup the serial port
    Serial.begin(115200);

    // Init Display.
    display.init();
    display.flipScreenVertically();
    showTextRectangle("Init", String(ESP.getChipId(),HEX),true);

     // Enable enabled sensors.
    if (hasPM) ag.PMS_Init();
    if (hasCO2) ag.CO2_Init();
    if (hasSHT) ag.TMP_RH_Init(0x44);

    // Set static IP address if configured.
    #ifdef staticip
    WiFi.config(static_ip,gateway,subnet);
    #endif

    // Set WiFi mode to client (without this it may try to act as an AP).
    WiFi.mode(WIFI_STA);    

    // Configure Hostname
    if ((deviceId != NULL) && (deviceId[0] == '\0')) {
        Serial.printf("No Device ID is Defined, Defaulting to board defaults");
    }
    else {
        wifi_station_set_hostname(deviceId);
        WiFi.setHostname(deviceId);
    }

    // Setup and wait for WiFi.
    WiFi.begin(ssid, password);
    Serial.println("");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        showTextRectangle("Trying to", "connect...", true);
        Serial.print(".");
    }

    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Hostname: ");
    Serial.println(WiFi.hostname());

    server.on("/", handleRoot);
    server.on("/metrics", handleRoot);
    server.on("/offsets", handleOffsets);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("HTTP server started at ip " + WiFi.localIP().toString() + ":" + String(port));

    timeClient.begin();

    getOffsetsFromRemoteFile(jsonURL);
}

void loop() {
    long t = millis();

    server.handleClient();

    timeClient.update();
    
    //check the offsets every hour
    if (t % 3600000 == 0) {
        Serial.println("Checking offsets");
        getOffsetsFromRemoteFile(jsonURL);
    } else 
        Serial.println("Not time to check offsets yet");
    
    //updateWeather(t);

    updateScreen(t);

}

String GenerateMetrics() {
  String message = "";
  String idString = "{id=\"" + String(deviceId) + "\",mac=\"" + WiFi.macAddress().c_str() + "\"}";

  if (hasPM) {
    int stat = ag.getPM2_Raw();

    message += "# HELP pm02 Particulate Matter PM2.5 value\n";
    message += "# TYPE pm02 gauge\n";
    message += "pm02";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  if (hasCO2) {
    int stat = ag.getCO2_Raw();

    message += "# HELP rco2 CO2 value, in ppm\n";
    message += "# TYPE rco2 gauge\n";
    message += "rco2";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  if (hasSHT) {
    TMP_RH stat = ag.periodicFetchData();

    message += "# HELP atmp Temperature, in degrees Celsius\n";
    message += "# TYPE atmp gauge\n";
    message += "atmp";
    message += idString;
    message += String(stat.t+tempOffset);
    message += "\n";

    message += "# HELP rhum Relative humidity, in percent\n";
    message += "# TYPE rhum gauge\n";
    message += "rhum";
    message += idString;
    message += String(stat.rh+humOffset);
    message += "\n";
  }
  return message;
}

void handleRoot() {
    server.send(200, "text/plain", GenerateMetrics() );
}

void handleOffsets() {
    String message = "";
    message += "Last updated on: ";
    message += offsetUpdateString;
    message += "\n";
    message += "Time Offset: ";
    message += String(timeOffset);
    message += "\n";
    message += "Temperature Offset: ";
    message += String(tempOffset);
    message += "\n";
    message += "Humidity Offset: ";
    message += String(humOffset);
    message += "\n";
    server.send(200, "text/plain", message);
}

void handleNotFound(){
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/html", message);
}

void updateWeather(long now) {
    HTTPClient http;
    http.begin("https://wttr.in/" + weatherLocation);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Weather: " + payload);
    } else {
        Serial.println("Error getting weather");
        Serial.println(httpCode);
    }
}

void showTextRectangle(String ln1, String ln2, boolean small) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  if (small) {
    display.setFont(ArialMT_Plain_16);
  } else {
    display.setFont(ArialMT_Plain_24);
  }
  display.drawString(32, 16, ln1);
  display.drawString(32, 36, ln2);
  display.display();
}

void updateScreen(long now) {
  if ((now - lastUpdate) > updateFrequency) {
    // Take a measurement at a fixed interval.
    switch (counter) {
      case 0:
        if (hasPM) {
          int stat = ag.getPM2_Raw();
          showTextRectangle("PM2",String(stat),true);
          Serial.println("PM2 " + String(stat));
        }
        break;
      case 1:
        if (hasCO2) {
          int stat = ag.getCO2_Raw();
          showTextRectangle("CO2", String(stat), true);
          Serial.println("CO2 " + String(stat));
        }
        break;
      case 2:
        if (hasSHT) {
          TMP_RH stat = ag.periodicFetchData();
          if (temp_display == 'F' || temp_display == 'f') {
            showTextRectangle("TMP", String((stat.t * 9 / 5) + 32 + tempOffset, 1) + "F", true);
            Serial.println("TMP " + String((stat.t * 9 / 5) + 32 + tempOffset, 1) + "F");

          } else {
            showTextRectangle("TMP", String(stat.t + tempOffset, 1) + "C", true);
            Serial.println("TMP " + String(stat.t + tempOffset, 1) + "C");
          }
        }
        break;
      case 3:
        if (hasSHT) {
          TMP_RH stat = ag.periodicFetchData();
          showTextRectangle("HUM", String(stat.rh + humOffset) + "%", true);
          Serial.println("HUM " + String(stat.rh + humOffset) + "%");
        }
        break;
      case 4:
        ///show weather
        break;
      case 5:
        //display clock
        showTextRectangle("CLOCK", timeClient.getFormattedTime(), true);
        Serial.println("CLOCK " + timeClient.getFormattedTime());
        delay(2000); //waits 2 seconds before going back to displaying the measurements
        break;
    }
    counter++;
    if (counter > 5) counter = 0;
    lastUpdate = millis();
  }

  //if time between 19:00 and 8:00 turn off display backlight
  if (timeClient.getHours() >= 19 || timeClient.getHours() <= 8) {
    display.setContrast(10, 5, 0);
  } else { //turns it back on
    display.setContrast(100, 241, 64);
  }
}

void getOffsetsFromRemoteFile(String file) {

    HTTPClient http;
    http.begin(file);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        Serial.print("HTTP GET successful, parsing offsets from: ");
        Serial.println(file);
        
        String payload = http.getString();
        //parse the json
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        JsonObject obj = doc.as<JsonObject>();
        //get the offsets
        timeOffset = obj["timeOffset"];
        tempOffset = obj["tempOffset"];
        humOffset = obj["humOffset"];
        timeClient.setTimeOffset(timeOffset);
        //print the offsets
        Serial.println("Time Offset: " + String(timeOffset));
        Serial.println("Temperature Offset: " + String(tempOffset));
        Serial.println("Humidity Offset: " + String(humOffset));
        //update the offset update string
        offsetUpdateString = timeClient.getFormattedTime();
    } else {
        //return to default offsets
        timeOffset = 0;
        tempOffset = 0;
        humOffset = 0;
        Serial.println("Error getting offsets from remote file");
    }
}