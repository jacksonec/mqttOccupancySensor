#include <ESP8266WiFi.h>        // Include the Wi-Fi library
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include <EEPROM.h>
#include <ESP8266WebServer.h>

#define Version "1.51"
#define Product "BFE Occupancy Sensor"
#define HTMLElements 8

const int intLightPin(A0);
const int intSoundPin(D7);
const int intMotionPin(D5);
const int intSwitchPin(D6);

//Setup switch pin
const int intSetupSwitch = D2; //if on, we're in setup, if off, we're running
//Generate unique ID (BFE-<macAddress>)
String strUniqueID = "BFE-" + String(WiFi.macAddress());

//Server instance needs to be global or else I'll kill myself keeping track
ESP8266WebServer server(80);
WiFiClient espClient;
String strTopicSound = strUniqueID + "/sound";
char* charTopicSound = (char*) strTopicSound.c_str();
String strTopicMotion = strUniqueID + "/motion";
char* charTopicMotion = (char*) strTopicMotion.c_str();
String strTopicLight = strUniqueID + "/light";
char* charTopicLight = (char*) strTopicLight.c_str();
String strTopicOccupancy = strUniqueID + "/occupancy";
char* charTopicOccupancy = (char*) strTopicOccupancy.c_str();
bool bMqtt = false;
long lngLastPub = millis();

struct objRom{
   char charIPAddress[15];
   char charPassword[63];
   char charSSID[32];
   int intMqttPort;
   int intListenTime;
   int intResetTime;
   float fltSoundSensitivity;
   float fltMotionSensitivity;
   int intSoundMotionBoth;
};

struct objHtml{
  String strFieldName;
  String strFieldDescription;
  String strFieldValue;
};

struct objSensor{
  long lngStartTime = 0;
  long lngEndTime;
  long lngResetTime;
  int intRawSensor = 0;
  int intThreshold = 0;
};

objRom objRomData;
objHtml objHtmlData[HTMLElements];
objSensor objSound;
objSensor objMotion;
objSensor objOccupancy;
Adafruit_MQTT_Client *mqtt;

void setup(){
  bool bWifi = false;
  //Serial monitor on
  Serial.begin(115200);
  pinMode(intSwitchPin, INPUT);
  pinMode(intSoundPin, INPUT);
  pinMode(intMotionPin, INPUT);
  pinMode(intLightPin, INPUT);
  loadEEPROM();

  mqtt = new Adafruit_MQTT_Client(&espClient, objRomData.charIPAddress, objRomData.intMqttPort, "", "", "");

  //Connect to Wifi
  bWifi = connectWifi(objRomData.charSSID, objRomData.charPassword);
  delay(2500);
  if (!bWifi){
    Serial.println("WiFi Connect Failed. Starting AP Mode.");

    String strTempSSID = strUniqueID + "_Setup";
    const char* ssidAP = strTempSSID.c_str();  // BFE-<mac>_Setup

    IPAddress local_ip(192,168,1,1);
    IPAddress gateway(192,168,1,1);
    IPAddress subnet(255,255,255,0);

    Serial.println("Starting Access Point");
    WiFi.softAP(ssidAP);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    delay(100);
  }
  if (digitalRead(intSwitchPin)){

    Serial.println("Starting Web Server.");
      
    //connection event
    server.on("/", handle_OnConnect);
    //form submit
    server.on("/action_page", handleForm);
    //invalid URL
    server.onNotFound(handle_NotFound);
    
    Serial.println("HTTP server started");

    server.begin();
  }

  Serial.print("Connecting to MQTT... ");
  bMqtt = connectMQTT();
  long lngNow = millis();
  long lngListen = objRomData.intListenTime * 1000;
  int intDelay = 50;
  objSound.lngStartTime = lngNow;
  objMotion.lngStartTime = lngNow;
  objOccupancy.lngStartTime = lngNow;
  objSound.lngEndTime = objSound.lngStartTime + lngListen;
  objMotion.lngEndTime = objMotion.lngStartTime + lngListen;
  objOccupancy.lngEndTime = objOccupancy.lngStartTime + lngListen;
  objSound.intThreshold = getThreshold(intDelay, objRomData.intListenTime,objRomData.fltSoundSensitivity);
  objMotion.intThreshold = getThreshold(intDelay, objRomData.intListenTime,objRomData.fltMotionSensitivity);
}

float getThreshold(int aintDelay, int aintSeconds, float afltSensitivity){
  float fltThreshold1 = (aintSeconds*1000)/aintDelay;
  float fltPct = 1 - afltSensitivity;
  int fltThreshold = fltThreshold1*fltPct;
  return fltThreshold;
}

void loop(){
  if (digitalRead(intSwitchPin)){
    //Serial.println("Server mode");
    server.handleClient();
  }
  else{
    Adafruit_MQTT_Publish pubNoise(mqtt, charTopicSound);
    Adafruit_MQTT_Publish pubMotion(mqtt, charTopicMotion);
    Adafruit_MQTT_Publish pubLight(mqtt, charTopicLight);
    Adafruit_MQTT_Publish pubOccupancy(mqtt, charTopicOccupancy);


    long lngNow = millis();
    //if sensor start is before now
    Serial.print("Now: ");
    Serial.println(lngNow);
    Serial.print("Start: ");
    Serial.println(objOccupancy.lngStartTime);
    Serial.print("End: ");
    Serial.println(objOccupancy.lngEndTime);

    if (lngNow < objSound.lngEndTime && lngNow > objSound.lngStartTime){
      objSound.intRawSensor = objSound.intRawSensor + digitalRead(intSoundPin);
      Serial.print("Sound Read: ");
      Serial.print(objSound.intRawSensor);
      Serial.print("/");
      Serial.println(objSound.intThreshold);
      if (objSound.intRawSensor > objSound.intThreshold){
        objSound.lngStartTime = objRomData.intResetTime * 1000 + millis();
        objSound.lngEndTime = objRomData.intListenTime * 1000 + objSound.lngStartTime;
        pubNoise.publish(1);
        objSound.intRawSensor = 0;
      }
    }
    else{
      if (lngNow > objSound.lngEndTime){
        pubNoise.publish(0);
        objSound.intRawSensor = 0;
        objSound.lngEndTime = objRomData.intListenTime * 1000 + lngNow;
      }
    }    

    if (lngNow < objMotion.lngEndTime && lngNow > objMotion.lngStartTime){
      objMotion.intRawSensor = objMotion.intRawSensor + digitalRead(intMotionPin);
      Serial.print("Motion Read: ");
      Serial.print(objMotion.intRawSensor);
      Serial.print("/");
      Serial.println(objMotion.intThreshold);
      if (objMotion.intRawSensor > objMotion.intThreshold){
        objMotion.lngStartTime = objRomData.intResetTime * 1000 + millis();
        objMotion.lngEndTime = objRomData.intListenTime * 1000 + objMotion.lngStartTime;
        pubMotion.publish(1);
        objMotion.intRawSensor = 0;
      }
    }
    else{
      if (lngNow > objMotion.lngEndTime){
        pubMotion.publish(0);
        objMotion.intRawSensor = 0;
        objMotion.lngEndTime = objRomData.intListenTime * 1000 + lngNow;
      }
    }

    bool bCriteria = false;
    switch(objRomData.intSoundMotionBoth){
      case 0:
        bCriteria = objSound.lngStartTime > lngNow;
        break;
      case 1:
        bCriteria = objMotion.lngStartTime > lngNow;
        break;
      default:
        bCriteria = objSound.lngStartTime > lngNow && objMotion.lngStartTime > lngNow;
    }


    if (bCriteria){
      objOccupancy.lngStartTime = lngNow;
      pubOccupancy.publish(1);
      objOccupancy.lngEndTime = lngNow + objRomData.intResetTime * 1000;
    }

    if(objOccupancy.lngEndTime < lngNow){
      pubOccupancy.publish(0);
    }
    
    if (lngLastPub + 60000 < lngNow){
      pubLight.publish(analogRead(intLightPin));
      lngLastPub = lngNow;
    }
    

    delay(50);

  }
}

void loadEEPROM(){
  EEPROM.begin(512);
  EEPROM.get(0, objRomData);
}

int getLight(int aintPin){
  int intReturn = analogRead(aintPin);
  Serial.println(intReturn);
  return intReturn;
}

bool connectWifi(char* acharSSID, char* acharPass)
{
  delay(10);
  //WiFi.begin(acharSSID, acharPass);             // Connect to the network
  Serial.print("Connecting to ");
  Serial.print(acharSSID); Serial.println(" ...");

  int intCounter = 0;
  bool bReturn = false;
  WiFi.begin(acharSSID, acharPass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    intCounter++;
    if (intCounter >= 10){
      break;
    }
  }
  if (WiFi.status() == WL_CONNECTED){
    Serial.println();
    Serial.println('\n');
    Serial.println("Connection established!");  
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP());         // Send the IP address of the ESP8266 to the computer
    bReturn = true;
  }
  else{
    bReturn = false;
  }
  return bReturn;
}

bool handle_OnConnect() {
  //root URI request
  Serial.println("Took client connection to default page.");
  server.send(200, "text/html", SendHTML()); 
  return true;
}

void handle_NotFound(){
  //Bar URI request
  Serial.println("Bad URL requested by client.");
  server.send(404, "text/plain", "Not found");
}

void buildHtmlData(){
  loadEEPROM();
  
  //SSID
  objHtmlData[0].strFieldDescription = "WiFi SSID:";
  objHtmlData[0].strFieldValue = objRomData.charSSID;
  objHtmlData[0].strFieldName = "clientSSID";

  //Wifi Password
  objHtmlData[1].strFieldDescription = "WiFi Password:";
  objHtmlData[1].strFieldValue = objRomData.charPassword;
  objHtmlData[1].strFieldName = "clientPass";

  //MQTT IP Address
  objHtmlData[2].strFieldDescription = "MQTT IP Address:";
  objHtmlData[2].strFieldValue = objRomData.charIPAddress;
  objHtmlData[2].strFieldName = "mqttIP";

  //MQTT Port
  objHtmlData[3].strFieldDescription = "MQTT IP Port:";
  objHtmlData[3].strFieldValue = String(objRomData.intMqttPort);
  objHtmlData[3].strFieldName = "mqttPort";

  //Listening Time
  objHtmlData[4].strFieldDescription = "Listening Time:";
  objHtmlData[4].strFieldValue = String(objRomData.intListenTime);
  objHtmlData[4].strFieldName = "listenTime";

  //Motion sensitivity
  objHtmlData[5].strFieldDescription = "Motion Sensitivity:";
  objHtmlData[5].strFieldValue = String(objRomData.fltMotionSensitivity*100);
  objHtmlData[5].strFieldName = "motionSensitivityPct";

  //Sound Sensitivity
  objHtmlData[6].strFieldDescription = "Sound Sensitivity:";
  objHtmlData[6].strFieldValue = String(objRomData.fltSoundSensitivity*100);
  objHtmlData[6].strFieldName = "soundSensitivityPct";

  //Reset Time
  objHtmlData[7].strFieldDescription = "Reset Time:";
  objHtmlData[7].strFieldValue = String(objRomData.intResetTime);
  objHtmlData[7].strFieldName = "resetTime";
}

String SendHTML(){
  buildHtmlData();

  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>Device Settings</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<h1>Brute Force Engineering</h1>\n";
  ptr +="<h2>" + String(Product) + " " + String(Version) + "</h2>\n";
  ptr +="<h3>Using Access Point(AP) Mode<br>\n";
  ptr += strUniqueID;

  ptr +="</h3><form action=\"/action_page\">";
  for (int intCounter = 0; intCounter < HTMLElements; intCounter++){
    ptr += objHtmlData[intCounter].strFieldDescription + "<br>";
    ptr += "<input type=\"text\" name=\"" + objHtmlData[intCounter].strFieldName + "\" value=\"" + objHtmlData[intCounter].strFieldValue + "\">";
    ptr += "<br>";
  }

  String strSound = "";
  String strMotion = "";
  String strBoth = "";

  switch(objRomData.intSoundMotionBoth){
    case 0:
      strSound = "checked";
      break;
    case 1:
      strMotion = "checked";
      break;
    default:
      strBoth = "checked";
  }

  ptr += "Occupancy based on:<br>";
  ptr += "<input type=\"radio\" name=\"SoundMotionBoth\" value=\"2\" id=\"both\" " + strBoth + ">";
  ptr += "<label for=\"both\">Both</label><br>";
  ptr += "<input type=\"radio\" name=\"SoundMotionBoth\" value=\"0\" id=\"sound\" " + strSound + ">";
  ptr += "<label for=\"sound\">Sound</label><br>";
  ptr += "<input type=\"radio\" name=\"SoundMotionBoth\" value=\"1\" id=\"motion\" " + strMotion + ">";
  ptr += "<label for=\"motion\">Motion</label><br>";

  ptr +="  <br>";
  ptr +="  <input type=\"submit\" value=\"Submit\">";
  ptr +="  <br><br>";


  //fix this later
  ptr +="Sub to:<br> ";
  ptr += strUniqueID + "/sound<br>";
  ptr += strUniqueID + "/light<br>";
  ptr += strUniqueID + "/occupancy<br>";
  ptr += strUniqueID + "/motion<br>";

  ptr +="</form>";
  ptr +="</body>";
  ptr +="</html>";

  return ptr;
}

void handleForm() {
  float fltTemp;
  //ssid
  server.arg("clientSSID").toCharArray(objRomData.charSSID, 32);
  //password
  String strPass = server.arg("clientPass");
  strPass.toCharArray(objRomData.charPassword, 63);

  Serial.println(server.arg("SoundMotionBoth"));
  objRomData.intSoundMotionBoth =  server.arg("SoundMotionBoth").toInt();

  //mqtt IP
  server.arg("mqttIP").toCharArray(objRomData.charIPAddress, 15);

  //mqtt port
  objRomData.intMqttPort = server.arg("mqttPort").toInt();

  //listen time
  objRomData.intListenTime = server.arg("listenTime").toInt();
  
  //motion sensitivity
  fltTemp = server.arg("motionSensitivityPct").toFloat()/100;
  objRomData.fltMotionSensitivity = fltTemp;

  //sound sensitivity
  fltTemp = server.arg("soundSensitivityPct").toFloat()/100;
  objRomData.fltSoundSensitivity = fltTemp;

  //reset time
  objRomData.intResetTime = server.arg("resetTime").toInt();
  
  EEPROM.begin(512);
  EEPROM.put(0, objRomData);

  delay(500);
  if (EEPROM.commit()) {
    Serial.println("EEPROM successfully committed");
  } else {
    Serial.println("ERROR! EEPROM commit failed");
  }
  delay(500);
  handle_OnConnect();
}

bool connectMQTT(){
    int8_t ret;
    bool bReturn = false;

    // Stop if already connected.
    if (mqtt->connected()) {
      return true;
    }

    Serial.print("Connecting to MQTT... ");

    uint8_t retries = 3;
    while ((ret = mqtt->connect()) != 0) { // connect will return 0 for connected
        Serial.println(mqtt->connectErrorString(ret));
        Serial.println("Retrying MQTT connection in 5 seconds...");
        mqtt->disconnect();
        delay(5000);  // wait 5 seconds
        retries--;
        if (retries == 0) {
          // basically die and wait for WDT to reset me
          break;
        }
    }
    if (ret == 0){
      Serial.println("MQTT Connected!");
      bReturn = true;
    }
    else{
      Serial.println("MQTT Connection Failed!");
      bReturn = false;
    }
  return bReturn;
}