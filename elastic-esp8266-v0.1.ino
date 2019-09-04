/*
   Lee Drengenberg 2019-09-01

   4 main functions;
    1). Connect to local WiFi to get access to NTP servers and writing to Elasticsearch
    2). Read temperature sensors - I'm using one wire DS18B20
    3). Read NTP time service for our Elasticsearch timestamps
    4). Write to Elasticsearch over HTTPS

   Need to create a mapping in Elasticsearch to set timestamp as a date type.

   test curl command;
   curl -XPOST 'https://super:changeme@9b51cb6ccde0461e852103f074d33a14.us-central1.gcp.cloud.es.io:9243/leedr_temp/_doc' -H "Content-type: application/json" -d '{ "timestamp": "2019-08-27T10:20:30", "temp1": 37}'

*/


#include <DallasTemperature.h>
#include <OneWire.h>

#include <Arduino.h>
//#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
//#include <ESP8266WebServer.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Time.h>
#include <TimeLib.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

uint8_t thumbprint[20] = {};
String esThumbprint; // must be a 40 char string of hex chars

String esIndex = "leedr_temp";

// WiFi connection ----------------------------------------
String ssid;
String password;
String esHost;

ESP8266WiFiMulti WiFiMulti;

// NTP stuff -----------------------------------------------
WiFiUDP ntpUDP;
// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset
NTPClient timeClient(ntpUDP);
String timestamp;

// Temperature stuff ---------------------------------------
// Data wire is plugged into pin 2 on the Arduino
#define ONE_WIRE_BUS 2
// Setup a oneWire instance to communicate with any OneWire devices
// (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
float temp0;
float temp1;

int setupPin = 5; // this is actually D1 on my esp8266
const int led = 13;


const char* PARAM_INPUT_1 = "input1";
const char* PARAM_INPUT_2 = "input2";
const char* PARAM_INPUT_3 = "input3";
const char* PARAM_INPUT_4 = "input4";

AsyncWebServer server(80);
DNSServer dnsServer;

// WebServer stuff ------------------------------------------
//ESP8266WebServer server(80);

void handleRoot() {
  int sensorVal = digitalRead(setupPin);
  if (sensorVal == 0) {
    //    server.send(200, "text/html", "<h1>Setup your WiFi</h1>");
  } else {
    //    server.send(200, "text/html", "<h1>You are connected</h1><BR>temp0=" + String(temp0) + "<BR>temp1=" + String(temp1));
  }
}

// HTML web page to handle 3 input fields (input1, input2, input3)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  </head><body>
  <form action="/get">
    WiFi SID: <input type="text" name="input1">
  <br>
    WiFi Password: <input type="text" name="input2">
  <br>
    Elasticsearch URL with auth: <input type="text" name="input3">
  <br>
    Elasticsearch Thumbprint: <input type="text" name="input4">
    <input type="submit" value="Submit">
  </form>
</body></html>)rawliteral";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}


void clearEEPROM() {
  EEPROM.begin(512);
  // write a 0 to all 512 bytes of the EEPROM
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.end();
}
/*
 * writeEEPROMstring can take char[] or String types
 * 
 * 
 */
void writeEEPROMstring(String var) {
  static int addr;
  EEPROM.begin(512);
  int i = 0;
  while (var[i] != 0x00) {
    EEPROM.write(addr, var[i]);
    i++;
    addr ++;
  }
  
  EEPROM.write(addr, 0x00);
  Serial.println("Writing null byte at addr = " + String(addr));
  addr++; // Increment to start of next char[]
  EEPROM.commit();
}


/*
 * readEEPROMstring reads and returns 1 null terminated string 
 */
String readEEPROMstring() {
  static int addr;    
  EEPROM.begin(512);

  char temp[512];
//  Serial.println("Empty temp = " + temp);
  int i = 0;
  while (EEPROM.read(addr) != 0x00) {
    temp[i] = EEPROM.read(addr);
//    Serial.println(char temp[i]);
    addr += 1;
    i++;
  }
  temp[i] = 0x00;
  addr++;
  String newSid(temp);
    Serial.println("Reading " +  String(newSid));
  return newSid;
}



/** Store WLAN credentials to EEPROM */
void saveCredentials() {
  writeEEPROMstring(ssid);
  writeEEPROMstring(password);
  writeEEPROMstring(esHost);
  writeEEPROMstring(esThumbprint);
}

/** Load WLAN credentials from EEPROM */
void loadCredentials() {
  ssid = readEEPROMstring();
  password = readEEPROMstring();
  esHost = readEEPROMstring();
  esThumbprint = readEEPROMstring();
  Serial.println("Recovered credentials:");
  Serial.println("ssid=" + ssid);
//  Serial.println(strlen(password.c_str()) > 0 ? "********" : "<no password>");
  Serial.println("password=" + password);
  Serial.println("esHost=" + esHost);
  Serial.println("esThumbprint=" + esThumbprint);

  // convert esThumbprint from String to char(byte) array
  for (int k=0; k< esThumbprint.length(); k+=2) {
    char * pEnd;
    char tempString[3] = {};
    tempString[0] = esThumbprint[k];
    tempString[1] = esThumbprint[k+1];
    tempString[2] = '\0';
    //  Serial.println(tempString);
    thumbprint[k/2] = strtol(tempString, &pEnd, 16);
      Serial.printf("%x ", thumbprint[k/2] );
      Serial.println("");
  }

}

void setup() {

  Serial.begin(115200);
//  Serial.setDebugOutput(true);

  Serial.println();
  Serial.println();
  Serial.println();

  for (uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(1000);
  }

  pinMode(setupPin, INPUT_PULLUP);
  pinMode(13, OUTPUT);
  int sensorVal = digitalRead(setupPin);
  if (sensorVal == 0) {
    Serial.println("----------- Setup mode.  Connect to WiFi access point");
    WiFi.mode(WIFI_AP);
    //    WiFi.mode(WIFI_AP_STA);
    boolean result = WiFi.softAP("ESPsetup", "password");
    if (result == true)
    {
      for (int j = 0; j < 5; j++) {
        delay(2000);  
        Serial.println("WiFi.softAP Ready at " + WiFi.localIP() );
      }
      dnsServer.start(53, "*", WiFi.softAPIP());
      server.begin();
      delay(2000);
      Serial.println();
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());

      // Send web page with input fields to client
      server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send_P(200, "text/html", index_html);
      });

      // Send a GET request to <ESP_IP>/get?input1=<inputMessage>
      server.on("/get", HTTP_GET, [] (AsyncWebServerRequest * request) {
        String inputMessage;
        String inputParam;
        Serial.println(request->host().c_str());
        // GET input1 value on <ESP_IP>/get?input1=<inputMessage>
        if (request->hasParam(PARAM_INPUT_1)) {
          ssid = request->getParam(PARAM_INPUT_1)->value();
          Serial.println("input1 = " + ssid);
          inputParam = PARAM_INPUT_1;
        }
        // GET input2 value on <ESP_IP>/get?input2=<inputMessage>
        if (request->hasParam(PARAM_INPUT_2)) {
          password = request->getParam(PARAM_INPUT_2)->value();
          Serial.println("input2 = " + password);
          inputParam = PARAM_INPUT_2;
        }
        // GET input3 value on <ESP_IP>/get?input3=<inputMessage>
        if (request->hasParam(PARAM_INPUT_3)) {
          esHost = request->getParam(PARAM_INPUT_3)->value();
          Serial.println("input3 = " + esHost);
          inputParam = PARAM_INPUT_3;
        }
        // GET input4 value on <ESP_IP>/get?input4=<inputMessage>
        if (request->hasParam(PARAM_INPUT_4)) {
          esThumbprint = request->getParam(PARAM_INPUT_4)->value();
          Serial.println("input4 = " + esThumbprint);
          inputParam = PARAM_INPUT_4;
        }
        else {
          inputMessage = "No message sent";
          inputParam = "none";
        }
  

        saveCredentials();
        Serial.println("saved credentials");
        loadCredentials();
        Serial.println("ssid = " + ssid);

        request->send(200, "text/html", "HTTP GET request sent to your ESP on input field ("
                      + inputParam + ") with value: " + inputMessage +
                      "<br><a href=\"/\">Return to Home Page</a>");
      }); //end server.on("/get",
      server.onNotFound(notFound);
      server.begin();

    } else {
      Serial.println("WiFi.softAP(ESPsetup, password) Failed!");
    }
  } else {
    Serial.println("------------------ Normal mode. Connecting to WiFi");
    loadCredentials();
    Serial.println("ssid = " + ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("Connection Failed! Rebooting...");
      delay(5000);
      //      ESP.restart();
    }
    // Start up the library
    sensors.begin();
    timeClient.begin();


    server.begin();
    delay(2000);
    Serial.println();
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Send web page with input fields to client
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send_P(200, "text/html", "Put temperatures here?" );
    });

  }


  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

//  ArduinoOTA.onStart([]() {
//    Serial.println("Start");
//  });
//  ArduinoOTA.onEnd([]() {
//    Serial.println("\nEnd");
//  });
//  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
//    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
//  });
//  ArduinoOTA.onError([](ota_error_t error) {
//    Serial.printf("Error[%u]: ", error);
//    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
//    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
//    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
//    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
//    else if (error == OTA_END_ERROR) Serial.println("End Failed");
//  });
//  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //  server.on("/", handleRoot);
  //  server.begin();
  Serial.println("HTTP server started");

}

void loop() {
//  ArduinoOTA.handle();
  int sensorVal = digitalRead(setupPin);
  if (sensorVal == 0) {
    // start as access point and host pages to enter WiFi SID and Password
    //    server.handleClient();
    
  } else {
    //    server.handleClient();
//    Serial.println("+++++++ data -> Elasticsearch ");
    while (!timeClient.update()) {
      timeClient.forceUpdate();
    }
//    Serial.println(timeClient.getFormattedDate());
    if ((WiFiMulti.run() == WL_CONNECTED)) {
//      Serial.println("+++++++ WL_CONNECTED ");

      // Every 10 seconds on the 0.  If you have multiple boards writing to the same Elasticsearch
      // instance you could choose a different number (0-9) for each to spread out the writes.
      if (timeClient.getSeconds() % 10 == 0) {
        timestamp = timeClient.getFormattedDate();

        sensors.requestTemperatures(); // Send the command to get temperatures
        temp0 = sensors.getTempFByIndex(0);
        temp1 = sensors.getTempFByIndex(1);

        std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
//        client->setFingerprint(fingerprint);
client->setFingerprint(thumbprint);

        
        HTTPClient https;

        // Serial.print("[HTTPS] begin...\n");
        if (https.begin(*client, esHost + "/" + esIndex + "/_doc")) {

          https.addHeader("Content-Type", "application/json");
          String message = "{ \"timestamp\": \"" + timestamp + "\", \"temp0\": " + String(temp0) + ", \"temp1\": " + String(temp1) + "}";
          int httpCode = https.POST( message );

          // httpCode will be negative on error
          if (httpCode > 0) {
            // HTTP header has been send and Server response header has been handled
            Serial.println("[HTTPS] POST " + message + " response: " + String(httpCode));
          } else {
            Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
          }

          https.end();
        } else {
          Serial.println("======================= ERROR here");
          delay(3000);
        }
      }
    } // if ((WiFiMulti.run() == WL_CONNECTED)) {
  }
} // end loop()
