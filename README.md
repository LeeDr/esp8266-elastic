# esp8266-elastic
Example code for writing temperature data to Elastic Cloud service

I've called this first version **elastic-esp8266-v0.1**

There are 2 "modes" of operation;

"setup mode" which is entered by jumpering D1 on the esp8266 to ground.  In this mode, the esp8266 starts up in Access Point (AP) mode with a SID of "ESPsetup" and password of "password".  After a few seconds you can use a wireless device to connect to the ESPsetup access point over WiFi.  (TODO: make the on board LED blink some code at this point to indicate it's ready in setup mode).  Then open your browser to 192.168.4.1 (this is the default address on the AP).  You should get a page with 4 fields to enter data into;

WiFi SID (this is your local Access Point name which will give the esp8266 access to the internet)
WiFi Password
Elasticsearch host (fully qualified with a username and password (assuming Cloud where security is enabled) which can write to the index we're using.
Elasticsearch Thumbprint (40 character hex string.  To get this, connect to your Elasticsearch node and click on the lock in the Chrome address bar, click on Certificate, Details, and Thumbprint.

Save the full Elasticsearch URL and Thumbprint somewhere you have access after you switch your device from your normal WiFi connect to this AP so you can paste them into the fields (you wouldn't want to have to type them).

Click **Submit**.  This stores those variables in the esp8266 EEPROM.

Remove the jumper from ground to D1 and press the rst (reset) button on the esp8266 (or power off and back on).

Now your in "normal mode".  The esp8266 will use the saved WiFi credentials to connect.  Now it can use NTP to get the current time so that it can write timestamps with the data to Elasticsearch.

Add pictures and screenshots of Kibana data here.
