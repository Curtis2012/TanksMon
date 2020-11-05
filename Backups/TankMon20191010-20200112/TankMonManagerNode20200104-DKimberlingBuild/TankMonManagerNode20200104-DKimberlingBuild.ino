/*
  Tanks_Mon

  2019-09-15 C.Collins Initial Arduino version supporrting up to 4 tanks
  2019-10-10 Ported to NodeMCU board and converted to Blynk standalone via WiFi
  2019-10-11 Added alarm functionality
  2019-10-13 Added OTA
  2019-10-22 Changed architecture to sensor node/manager node architecture where sensor node only deals with sensors and all other external interfaces and functions are handled on manager node.
             Manager node can manage multiple sensor nodes. This allows multiple senor nodes to report to one manager node making number of sensors/tanks easily expandable.
             This also supports sensor nodes being physically distant from each other and the manager node. Used MQTT protocol with JSON payload.


  TODO:

  - implement clearAlarms logic
  - move functions/declarations common to both sensor node and manager node  to library
  - add date/timestamp to terminal logs
  - clean up displayTankData and blynkTankData to be better structured/integrated
  - add command support to terminal
  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.

*/

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#define SSID "COWIFI157673918/0"
#define PWD  "WiFi-90944166"
#define ALTSSID "COWIFI157673918/0"
#define ALTPWD "WiFi-90944166"

#define WIFIRETRYCNT 5
#define WIFIRETRYDELAY 10000
#define WIFIREBOOT 1
#define WIFITRYALT 1
#define MQTTRETRYCNT 5
#define MQTTRETRYDELAY 5000
#define SITE "DEK/HOMEPA"

const char* ssid = SSID;
const char* password = PWD;
const char* mqtt_server = "soldier.cloudmqtt.com";
const int   mqtt_port = 12045;
const char* mqtt_uid = "afdizojo";
const char* mqtt_pwd = "hfi3smlfeSqV";
String nodename = "MNTM-ESP8266-";

#define MAXPAYLOADSIZE 256
#define MAXJSONSIZE 256
#define TOPIC "dek/homepa/tankmon"

//#define DEBUG_ESP_PORT Serial

#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif

#define BLYNK_PRINT Serial
#define NUMTANKS 1
#define DEBUG 0
#define IMPERIAL 0
#define CVTFACTORGALLONS 0.26417F
#define CVTFACTORINCHES  0.39370F
#define LOALARMFACTOR 0.30F
#define HIALARMFACTOR 0.90F
#define DISPLAYUPDATETIME 15000L

// Alarm related bit masks & structs

#define CLEARALARMS   0b11111111
#define HIALARM       0b00000001
#define LOALARM       0b00000010
#define MAXDEPTH      0b00000100
#define NUMALARMS 4

struct alarm {
  std::uint8_t alarmType;
  char alarmName[10];
};

alarm alarms[NUMALARMS] = {
  {HIALARM, "HI"},
  {LOALARM, "LO"},
  {MAXDEPTH, "MAXDEPTH"},
  {CLEARALARMS, "CLEARALL"}
};

bool globalAlarmFlag = false;

char auth[] = "989b37ebf16b4d7584dffbb8b8e9acfb";  // Blynk auth token
WidgetLED blynkUpdateLED(V8);
WidgetLED blynkErrorLED(V9);
WidgetLED blynkAlarmLED(V11);
BlynkTimer timer;
WidgetTerminal blynkTerminal(V10);

WiFiClient net;
PubSubClient client(net);

class tank {
  public:
    float depth = 0;
    float vCM = 0;        // volume in liters per cm of ht
    float maxVolume = 0;
    float liquidDepth = 0;
    float liquidVolume = 0;
    float loAlarm = 0;
    float hiAlarm = 0;
    std::uint8_t alarmFlags = 0b00000000;

    tank()
    {

    }

    tank(float depth, float vCM)
    {
      this->depth = depth;
      this->vCM = vCM;
    }

    tank(float depth, float vCM, float loAlarm, float hiAlarm)
    {
      this->depth = depth;
      this->vCM = vCM;
      this->loAlarm = loAlarm;
      this->hiAlarm  = hiAlarm;
    }
};

tank tanks[NUMTANKS] = {
  tank(97, 10.8),
  tank(97, 10.8)

};

StaticJsonDocument<MAXJSONSIZE> tankmsg;

/*
  {
    "t" : 0,
    "d" : 0,
    "vCM" : 0,
    "mV" : 0,
    "lD" : 0,
    "lVolume" : 0,
    "loA" : 0,
    "hiA" : 0,
    "aF" : 0;  // pass as byte value representing binary flags
  }
*/

char payload[MAXPAYLOADSIZE] = "";

void initTanks()
{
  DEBUG_MSG("In initTanks \n");

  for (int t = 0; t <= NUMTANKS - 1; t++)      // set max volume and alarm level on all tanks
  {
    tanks[t].maxVolume = tanks[t].depth * tanks[t].vCM;
    tanks[t].loAlarm = tanks[t].depth * LOALARMFACTOR;
    tanks[t].hiAlarm = tanks[t].depth * HIALARMFACTOR;
  }

  // clear unused tank widgets, hard coded for now...

  //  Blynk.virtualWrite(V0,0);
  //  Blynk.virtualWrite(V1,0);
  Blynk.virtualWrite(V2, 0);
  Blynk.virtualWrite(V3, 0);

  //  Blynk.setProperty(V1, "color", "#000000");
  //  Blynk.setProperty(V0, "color", "#000000");
  Blynk.setProperty(V2, "color", "#000000");
  Blynk.setProperty(V3, "color", "#000000");

  //  Blynk.setProperty(V1, "label", " ");
  //  Blynk.setProperty(V0, "label", " ");
  Blynk.setProperty(V2, "label", " ");
  Blynk.setProperty(V3, "label", " ");
}

void handleAlarm(int tank)
{
  String notification = "";

  globalAlarmFlag = true;

  if (tanks[tank].alarmFlags & HIALARM)
  {
    notification = "HI ALARM! on Tank " + String(tank+1);
    blynkTerminal.println();
    blynkTerminal.println(notification);
    blynkTerminal.flush();
    blynkAlarmLED.on();
    Blynk.notify(notification);
  }

  if (tanks[tank].alarmFlags & LOALARM)
  {
    notification = "LO ALARM! on Tank " + String(tank+1);
    blynkTerminal.println();
    blynkTerminal.println(notification);
    blynkTerminal.flush();
    blynkAlarmLED.on();
    Blynk.notify(notification);
  }

  if ( tanks[tank].alarmFlags & MAXDEPTH)
  {
    notification = "MAX DEPTH ALARM! on Tank " + String(tank+1);
    blynkTerminal.println();
    blynkTerminal.println(notification);
    blynkTerminal.flush();
    blynkAlarmLED.on();
    Blynk.notify(notification);
  }
}

int mapAlarm(std::uint8_t alarmType)
{
  switch (alarmType) {
    case HIALARM :
      return (0);

    case LOALARM :
      return (1);

    case MAXDEPTH :
      return (2);
      
    case CLEARALARMS :
      return (3);
      
    default: return (-1);

  }
}

void clearAlarm(std::uint8_t alarmType, int tank)
{
  int a = -1;
  std::uint8_t alarmCheck = 0b00000000;

  a = mapAlarm(alarmType);
  if (a >= 0)
  {
    tanks[tank].alarmFlags = tanks[tank].alarmFlags & (~alarmType); // clear specific alarm flag
    blynkTerminal.println();
    blynkTerminal.print("Alarm cleared: ");
    blynkTerminal.print(alarms[a].alarmName);
    blynkTerminal.print(", tank ");
    blynkTerminal.print(tank + 1);
    blynkTerminal.println();
    blynkTerminal.flush();
  }
  else
  {
    blynkTerminal.println();
    blynkTerminal.print("Error mapping alarm type to error name in clearAlarm");
    blynkErrorLED.on();
  }
  
  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    alarmCheck = alarmCheck | tanks[t].alarmFlags;
  }

  if ((alarmCheck = 0) && globalAlarmFlag)
  {
    globalAlarmFlag = false;
    blynkAlarmLED.off();
    blynkTerminal.print("All alarms cleared");
    blynkTerminal.flush();
  }
}

void blynkTankData()
{
  float cvtG = 1.0, cvtI = 1.0;

  DEBUG_MSG("In blynkTankData \n");

  blynkUpdateLED.on();

  if (IMPERIAL) {
    cvtG = CVTFACTORGALLONS;
    cvtI = CVTFACTORINCHES;
  }

  Blynk.virtualWrite(V0, (tanks[0].liquidDepth * cvtI));
  Blynk.virtualWrite(V1, (tanks[1].liquidDepth * cvtI));
  Blynk.virtualWrite(V4, (tanks[0].liquidVolume * cvtG));
  Blynk.virtualWrite(V5, (tanks[1].liquidVolume * cvtG));

  if (DEBUG)
  {
    Serial.println("\nIn blynkTankData");
    displayTankData();
  }
  blynkUpdateLED.off();
}

void displayTankData()
{
  DEBUG_MSG("In displayTankData \n");
  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    blynkTerminal.println();
    blynkTerminal.print("Tank ");
    blynkTerminal.print(t + 1);
    blynkTerminal.println();
    blynkTerminal.print("Tank Depth ");
    blynkTerminal.println(tanks[t].depth);
    blynkTerminal.print("Tank liters/cm depth ");
    blynkTerminal.println(tanks[t].vCM);
    blynkTerminal.print("Max Volume ");
    blynkTerminal.println(tanks[t].maxVolume);
    blynkTerminal.print("Actual liquid depth ");
    blynkTerminal.println(tanks[t].liquidDepth);
    blynkTerminal.print("Actual liquid volume ");
    blynkTerminal.println(tanks[t].liquidVolume);
    blynkTerminal.print("Lo alarm level ");
    blynkTerminal.println(tanks[t].loAlarm);
    blynkTerminal.print("Hi alarm level ");
    blynkTerminal.println(tanks[t].hiAlarm);

    blynkTerminal.flush();
  }
}

void handleTimer()
{
  DEBUG_MSG("In handleTimer \n");
  digitalWrite(LED_BUILTIN, LOW);
  blynkTankData();
  digitalWrite(LED_BUILTIN, HIGH);
}

void connectWiFi()
{
  int i = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\nWiFi connecting to ");
  Serial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(WIFIRETRYDELAY);
    WiFi.begin(ssid, password);
    i++;
    if (i > WIFIRETRYCNT)
    {
      i = 0;
      if (WIFITRYALT && (ssid != ALTSSID))
      {
        ssid = ALTSSID;
        password = ALTPWD;
        Serial.print("\nWiFi connecting to alt SSID: ");
        Serial.print(ssid);
      }
      else if (WIFIREBOOT)
      {
        Serial.print("\nRebooting...");
        ESP.restart();
      }
    }
  }
  Serial.print("\nWiFi connected to ");
  Serial.print(SSID);
}
void setupMQTT()
{
  //
  // MQTT Setup

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  connectMQTT();

}

void copyJSONmsgtoStruct()
{
  int t = tankmsg["t"];

  tanks[t].depth = tankmsg["d"];
  tanks[t].vCM =   tankmsg["vCM"];
  tanks[t].maxVolume = tankmsg["mV"];
  tanks[t].liquidDepth = tankmsg["lD"];
  tanks[t].liquidVolume = tankmsg["lV"];
  tanks[t].loAlarm = tankmsg["loA"];
  tanks[t].hiAlarm = tankmsg["hiA"];
  tanks[t].alarmFlags = tankmsg["aF"];
}


void callback(char* topic, byte* payload, unsigned int length) {
  int t = -1;

  if (DEBUG)
  {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
  }

  deserializeJson(tankmsg, payload, MAXJSONSIZE);
  copyJSONmsgtoStruct();

  t = tankmsg["t"];
  if (tanks[t].alarmFlags != 0) 
  {
    handleAlarm(t);
  } 
  
  if (DEBUG)
  {
    Serial.print("\nDump tankmsg: ");
    serializeJson(tankmsg, Serial);
  }
}

void connectMQTT() {

  int i = 0;

  Serial.print("\nConnecting to MQTT Server: ");
  Serial.print(mqtt_server);
  while (!client.connected())
  {
    if (client.connect(nodename.c_str(), mqtt_uid, mqtt_pwd)) {
      Serial.println();
      Serial.print(nodename);
      Serial.println(" connected");
      client.publish("hello", nodename.c_str());                        // Once connected, publish an announcement...
      client.subscribe(TOPIC);
      i = 0;
    }
    else
    {
      if (i++ > MQTTRETRYCNT)
      {
        Serial.println("Rebooting... ");
        delay(MQTTRETRYDELAY);
        ESP.restart();
      }
      Serial.print("failed, rc=");
      Serial.print(client.state());
      // Wait 5 seconds before retrying
      delay(MQTTRETRYDELAY);
      Serial.println("Retrying MQTT connect... ");
    }
  }
}

void setupOTA ()
{

  //
  // OTA Setup
  //
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");


  ArduinoOTA.setHostname(nodename.c_str());

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}

void splashScreen()
{
  String splashMsg = "\nTank Monitor Manager Node \nBuild: Beta Test ";

  splashMsg += __DATE__;
  splashMsg +=" ";
  splashMsg += __TIME__;
  splashMsg +="\nSite: ";
  splashMsg += SITE;
  splashMsg += "\nNodename: ";
  splashMsg += nodename;
  splashMsg += "\nDEBUG = ";
  splashMsg += DEBUG;
  splashMsg += "\nNUMTANKS = ";
  splashMsg += NUMTANKS;
  
  Serial.println(splashMsg);
  blynkTerminal.clear();
  blynkTerminal.println(splashMsg);
  blynkTerminal.flush();
}

void setup()
{
  DEBUG_MSG("In setup \n");

  delay(3000);                                     // Initial delay to avoid Arduino upload problems
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  nodename += String(ESP.getChipId(), HEX);
  setupOTA();
  connectWiFi();
  setupMQTT();
  Blynk.begin(auth, ssid, password);
  splashScreen();

  timer.setInterval(DISPLAYUPDATETIME, handleTimer);  // setup timer interrupt handler

  initTanks();
  displayTankData();

  blynkUpdateLED.off();
  blynkErrorLED.off();
  blynkAlarmLED.off();
}

void loop() {

  ArduinoOTA.handle();
  Blynk.run();
  timer.run();
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();
}
