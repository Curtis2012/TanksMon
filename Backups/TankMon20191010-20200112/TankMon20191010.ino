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
#include <BlynkSimpleEsp8266.h>
#include <NewPingESP8266.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <MQTT.h>


#ifndef STASSID
#define STASSID "AndroidCSC"
#define STAPSK  "sail2012"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;


//#define DEBUG_ESP_PORT Serial

#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif

#define BLYNK_PRINT Serial
#define NUMTANKS 2
#define MAXPINGDISTANCE 400
#define DEBUG 0
#define IMPERIAL 0
#define CVTFACTORGALLONS 0.26417F
#define CVTFACTORINCHES  0.39370F
#define TANKPINGDELAY 15000L
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

char auth[] = "989b37ebf16b4d7584dffbb8b8e9acfb";  // Blynk auth token
//char ssid[] = "Nexxt_582E74-2.4G";
//char pass[] = "12345678";

WidgetLED blynkUpdateLED(V8);
WidgetLED blynkErrorLED(V9);
WidgetLED blynkAlarmLED(V11);

BlynkTimer timer;
WidgetTerminal blynkTerminal(V10);

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

void handleAlarm(std::uint8_t alarmType, int tank)
{

  globalAlarmFlag = true;

  switch (alarmType) {
    case HIALARM :
      tanks[tank].alarmFlags = tanks[tank].alarmFlags | HIALARM;
      blynkTerminal.println();
      blynkTerminal.print("Tank ");
      blynkTerminal.print(tank + 1);
      blynkTerminal.println(" HI ALARM! ");
      blynkTerminal.flush();
      blynkAlarmLED.on();
      break;

    case LOALARM :
      tanks[tank].alarmFlags = tanks[tank].alarmFlags | LOALARM;
      blynkTerminal.println();
      blynkTerminal.print("Tank ");
      blynkTerminal.print(tank + 1);
      blynkTerminal.println(" LO ALARM! ");
      blynkTerminal.flush();
      blynkAlarmLED.on();
      break;

    case MAXDEPTH :
      tanks[tank].alarmFlags = tanks[tank].alarmFlags | MAXDEPTH;
      blynkTerminal.println();
      blynkTerminal.print("Tank ");
      blynkTerminal.print(tank + 1);
      blynkTerminal.println(" MAXDEPTH EXCEEDED ALARM! ");
      blynkTerminal.flush();
      blynkAlarmLED.on();
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

  if (!alarmCheck)
  {
    globalAlarmFlag = false;
    blynkAlarmLED.off();
    blynkTerminal.print("All alarms cleared");
    blynkTerminal.flush();
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
  pingTanks();
  blynkTankData();
}


void connectMQTT() {
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.print("\nconnecting...");
  while (!client.connect("soldier.cloudmqtt.com", "elgjnrbq", "OSJQq6imoXaU")) {
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
void setup()
{
  delay(3000);                                     // Initial delay to avoid Arduino upload problems

  Serial.begin(115200);
  
  //
  // OTA 
  //
  
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }


  //
  // MQTT Setup
  
  client.begin("soldier.cloudmqtt.com", net);
  client.onMessage(messageReceivedMQTT);
  connectMQTT();


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
  
  DEBUG_MSG("In setup \n");

  Blynk.begin(auth, ssid, password);

  timer.setInterval(TANKPINGDELAY, handleTimer);  // setup timer interrupt handler

  pinMode(LED_BUILTIN, OUTPUT);

  initTanks();
  pingTanks();                                    // initial ping just to get data for display

  blynkTerminal.clear();
  blynkTerminal.print(__DATE__);
  blynkTerminal.print(" ");
  blynkTerminal.print(__TIME__);
  blynkTerminal.println(" Tanks Mon V0.5");
  blynkTerminal.println();
  blynkTerminal.print("DEBUG = ");
  blynkTerminal.println(DEBUG);
  blynkTerminal.print("IMPERIAL = ");
  blynkTerminal.println(IMPERIAL);
  blynkTerminal.print("NUMTANKS = ");
  blynkTerminal.println(NUMTANKS);
  blynkTerminal.print("MAXPINGDISTANCE = ");
  blynkTerminal.println(MAXPINGDISTANCE);
  blynkTerminal.print("TANKPINGDELAY = ");
  blynkTerminal.println(TANKPINGDELAY);
  blynkTerminal.println();
  blynkTerminal.flush();

  displayTankData();

  blynkUpdateLED.off();
  blynkErrorLED.off();
  blynkAlarmLED.off();

}

void loop() {

  ArduinoOTA.handle();
  Blynk.run();
  timer.run();

}
