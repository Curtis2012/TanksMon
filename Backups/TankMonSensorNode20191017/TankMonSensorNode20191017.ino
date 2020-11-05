/*
  Tanks_Mon

  2019-09-15 C.Collins Initial Arduino version supporrting up to 4 tanks
  2019-10-10 Ported to NodeMCU board and converted to Blynk standalone via WiFi
  2019-10-11 Added alarm functionality
  2019-10-13 Added OTA
  2019-10-17 Idea: change architecture to sensor controller/manager node architecture where sensor controller only deals with sensors and all other external interfaces and functions are handled on manager node. Manager node can manager multiple sensor nodes.
                   This would allow multiple tank senor nodes to report to one manager node making number of sensors/tanks easily expandable. This also supports sensor nodes being physically distant from each other and the manager node. Use MQTT protocol.
                   Would make it easy to include pump monitor/control functions in the future with pump related sensors being implemented as another sensor node.

  TODO:

  - convert to sensor node/manager node architecture using MQTT
  - add date/timestamp to terminal logs
  - add command support to terminal
  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.

*/

#include <ESP8266WiFi.h>
#include <NewPingESP8266.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "timer.h"
#include <MQTT.h>
#include <ArduinoJson.h>

#ifndef STASSID
#define STASSID "AndroidCSC"
#define STAPSK  "sail2012"
#endif

//char ssid[] = "Nexxt_582E74-2.4G";
//char pass[] = "12345678";

const char* ssid = STASSID;
const char* password = STAPSK;


//#define DEBUG_ESP_PORT Serial

#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif

#define MAXPAYLOADSIZE 500


#define NUMTANKS 2
#define MAXPINGDISTANCE 400
#define DEBUG 1
#define IMPERIAL 0
#define CVTFACTORGALLONS 0.26417F
#define CVTFACTORINCHES  0.39370F
#define TANKPINGDELAY 5000
#define LOALARMFACTOR 0.30F
#define HIALARMFACTOR 0.90F

// Alarm related bit masks & structs

#define CLEARALARMS   0b00000000
#define HIALARM       0b00000001
#define LOALARM       0b00000010
#define MAXDEPTH      0b00000100
#define NUMALARMS 3

struct alarm {
  std::uint8_t alarmType;
  char alarmName[10];
};

alarm alarms[NUMALARMS] = {
  {HIALARM, "HI"},
  {LOALARM, "LO"},
  {MAXDEPTH, "MAXDEPTH"}
};

bool globalAlarmFlag = false;

WiFiClient net;
MQTTClient client;

class tank {
  public:
    float depth = 0;
    float vCM = 0;        // volume in liters per cm
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
  tank(105, 0.5),
  tank(105, 0.5)

};

// JSON Tank Definition

StaticJsonDocument<MAXPAYLOADSIZE> tankmsg;
/*
  {
    "tankID" : 0,
    "depth" : 0,
    "vCM" : 0,
    "maxVolume" : 0,
    "liquidDepth" : 0,
    "liquidVolume" : 0,
    "loAlarm" : 0,
    "hiAlarm" : 0,
    "alarmFlags" : 0;  // pass as byte value representing binary flags
  }
*/


class sonarClass : public NewPingESP8266 {
  public:
    sonarClass(int trigPin, int echoPin, int maxDistance): NewPingESP8266(trigPin, echoPin, maxDistance)
    {

    };
};

sonarClass sonars[NUMTANKS] = {
  sonarClass(D5, D6, MAXPINGDISTANCE),
  sonarClass(D7, D8, MAXPINGDISTANCE)
};

auto timer = timer_create_default();

void initTanks()
{
  DEBUG_MSG("In initTanks \n");

  for (int t = 0; t <= NUMTANKS - 1; t++)      // set max volume and alarm level on all tanks
  {
    tanks[t].maxVolume = tanks[t].depth * tanks[t].vCM;
    tanks[t].loAlarm = tanks[t].depth * LOALARMFACTOR;
    tanks[t].hiAlarm = tanks[t].depth * HIALARMFACTOR;
  }
  tankmsg["tankdepth"] = tanks[0].depth;
  tankmsg["maxVolume"] = tanks[0].maxVolume;
}

void handleAlarm(std::uint8_t alarmType, int tank)
{

  globalAlarmFlag = true;

  switch (alarmType) {
    case HIALARM :
      tanks[tank].alarmFlags = tanks[tank].alarmFlags | HIALARM;
      break;

    case LOALARM :
      tanks[tank].alarmFlags = tanks[tank].alarmFlags | LOALARM;
      break;

    case MAXDEPTH :
      tanks[tank].alarmFlags = tanks[tank].alarmFlags | MAXDEPTH;
      break;
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

    default: return (-1);

  }
}

void clearAlarm(std::uint8_t alarmType, int tank)
{
  int a = -1;
  std::uint8_t alarmCheck = CLEARALARMS;

  a = mapAlarm(alarmType);
  if (a >= 0)
  {
    tanks[tank].alarmFlags = tanks[tank].alarmFlags & (~alarmType); // clear specific alarm flag
  }
  else
  {

  }
  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    alarmCheck = alarmCheck | tanks[t].alarmFlags;
  }

  if (!alarmCheck)
  {
    globalAlarmFlag = false;
  }
}


void pingTanks()
{
  int d = 0;

  DEBUG_MSG("In blynkTankData \n");

  digitalWrite(LED_BUILTIN, LOW);

  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    d = sonars[t].convert_cm(sonars[t].ping_median());

    if (d > tanks[t].depth)
    {
      d = tanks[t].depth;
      handleAlarm(MAXDEPTH, t);
    }
    else if ((tanks[t].alarmFlags & MAXDEPTH) && (d != tanks[t].depth)) clearAlarm(MAXDEPTH, t);

    tanks[t].liquidDepth = d;
    tanks[t].liquidVolume = tanks[t].liquidDepth * tanks[t].vCM;

    if (d > tanks[t].hiAlarm)
    {
      handleAlarm(HIALARM, t);
    }
    else if (tanks[t].alarmFlags & HIALARM) clearAlarm(HIALARM, t);

    if (d < tanks[t].loAlarm)
    {
      handleAlarm(LOALARM, t);
    }
    else if (tanks[t].alarmFlags & LOALARM) clearAlarm(LOALARM, t);

  }

  if (DEBUG) displayTankData();

  digitalWrite(LED_BUILTIN, HIGH);
}


void sendTankData()
{
  DEBUG_MSG("In displayTankData \n");

  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    tankmsg["tanknum"] = t;
    tankmsg["depth"] = tanks[t].depth;
    tankmsg["vCM"] = tanks[t].vCM;
    tankmsg["maxVolume"] = tanks[t].maxVolume;
    tankmsg["liquidDepth"] = tanks[t].liquidDepth;
    tankmsg["liquidVolume"] = tanks[t].liquidVolume;
    tankmsg["loAlarm"] = tanks[t].loAlarm;
    tankmsg["hiAlarm"] = tanks[t].hiAlarm;
    tankmsg["alarmFlags"] = tanks[t].alarmFlags;

    if (DEBUG)
    {
      Serial.println();
      serializeJson(tankmsg, Serial);
      Serial.println();
    }

  }
}

bool handleTimer(void *)
//void handleTimer()
{
  DEBUG_MSG("In handleTimer \n");
  pingTanks();
  //displayTankData();
  sendTankData();
}

void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("WiFi connecting to ");
  Serial.print(STASSID);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("WiFi connected to ");
  Serial.print(STASSID);
}

void connectMQTT() {

  Serial.print("\nconnecting...");
  while (!client.connect("soldier.cloudmqtt.com", "afdizojo", "hfi3smlfeSqV")) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nconnected!");

  client.subscribe("/hello");
  // client.unsubscribe("/hello");

}

void messageReceivedMQTT(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
}


void displayTankData()
{
  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    Serial.println();
    Serial.println("Tank ");
    Serial.print(t + 1);
    Serial.print("Tank Depth ");
    Serial.println(tanks[t].depth);
    Serial.print("Tank liters/cm depth ");
    Serial.println(tanks[t].vCM);
    Serial.print("Max Volume ");
    Serial.println(tanks[t].maxVolume);
    Serial.print("Actual liquid depth ");
    Serial.println(tanks[t].liquidDepth);
    Serial.print("Actual liquid volume ");
    Serial.println(tanks[t].liquidVolume);
    Serial.print("Lo alarm level ");
    Serial.println(tanks[t].loAlarm);
    Serial.print("Hi alarm level ");
    Serial.println(tanks[t].hiAlarm);

    Serial.flush();
  }

}

void setupMQTT()
{
  //
  // MQTT Setup

  client.begin("soldier.cloudmqtt.com", 12045, net);
  client.onMessage(messageReceivedMQTT);
  connectMQTT();

}

void setupJSON()
{

  //
  // JSON setup
  //
  /*
    {
      "tanknum" : 0,
      "depth" : 0,
      "vCM" : 0,
      "maxVolume" : 0,
      "liquidDepth" : 0,
      "liquidVolume" : 0,
      "loAlarm" : 0,
      "hiAlarm" : 0,
      "alarmFlags" : 0;  // pass as byte value representing binary flags
    }
  */

  // populate tankmsg

  tankmsg["tanknum"] = 0;
  tankmsg["depth"] = 0;
  tankmsg["vCM"] = 0;
  tankmsg["maxVolume"] = 0;
  tankmsg["liquidDepth"] = 0;
  tankmsg["liquidVolume"] = 0;
  tankmsg["loAlarm"] = 0;
  tankmsg["hiAlarm"] = 0;
  tankmsg["alarmFlags"] = 0;
}

void setup()
{
  DEBUG_MSG("In setup \n");
  delay(3000);                                     // Initial delay to avoid Arduino upload problems
  Serial.begin(115200);
  connectWiFi();
  setupMQTT();
  setupJSON();

  //
  // OTA Setup
  //

  /*Serial.println("Booting");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  */

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

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
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  timer.every(5000, handleTimer);

  pinMode(LED_BUILTIN, OUTPUT);

  initTanks();
  pingTanks();                                    // initial ping just to get data for display

  Serial.println();
  Serial.println(" Tanks Mon");
  Serial.println("Buuild:__DATE__ __TIME__");
  Serial.println();
  Serial.print("DEBUG = ");
  Serial.println(DEBUG);
  Serial.print("IMPERIAL = ");
  Serial.println(IMPERIAL);
  Serial.print("NUMTANKS = ");
  Serial.println(NUMTANKS);
  Serial.print("MAXPINGDISTANCE = ");
  Serial.println(MAXPINGDISTANCE);
  Serial.print("TANKPINGDELAY = ");
  Serial.println(TANKPINGDELAY);
  Serial.println();
  Serial.flush();

  displayTankData();

}


void loop() {

  ArduinoOTA.handle();
  timer.tick();
  client.loop();
  if (!client.connected()) {
    connectMQTT();
  }

}
