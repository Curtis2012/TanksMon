
/*
  Tank Monitor Sensor Node

  2019-09-15 C.Collins Initial Arduino version supporrting up to 4 tanks
  2019-10-10 Ported to NodeMCU board and converted to Blynk standalone via WiFi
  2019-10-11 Added alarm functionality
  2019-10-13 Added OTA
  2019-10-17 Changed architecture to sensor controller/manager node architecture where sensor controller only deals with sensors and all other external interfaces and functions are handled on manager node. Manager node can manage multiple sensor nodes.
             This allows multiple tank senor nodes to report to one manager node making number of sensors/tanks easily expandable. This also supports sensor nodes being physically distant from each other and the manager node. Used JSON over MQTT.
             Also makes it easy to add future functionality such as pump monitor/control functions with pump related sensors being implemented as another sensor node.

  2020-03-28 Begin clean up code including elimnation of redundant code and moving shared code to libraries.
  2020-10-10 C. Collins. Added commands via MQTT
  2020-10-23 C. Collins. Converted to dynamic memory allocation for tanks/sensors

  TODO:

  - discard outliers pings
  - review alarm logic across sensor & manager nodes and clean up
  - add configuration persistence via flash file/string
  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.
  - add com output to browser
  - convert multiple prints to sprintf


*/

//#define NUMTANKS 4  // ...leave in for sonars until converted to dynamic allocations

#include <cscNetServices.h>
#include <Tanksmon.h>

#define debug false
#define LEDFLASHTIME 3000
#define MSGBUFFSIZE 40

bool ledOn = false;
int chipID = 0;

/* class sonarClass : public NewPingESP8266 {
  public:
    sonarClass(int trigPin, int echoPin, int maxDistance): NewPingESP8266(trigPin, echoPin, maxDistance)
    {

    };
};
*/

/* sonarClass sonars[NUMTANKS] = {
  sonarClass(14, 12, MAXPINGDISTANCE),
  sonarClass(13, 15, MAXPINGDISTANCE),
  sonarClass(5, 4, MAXPINGDISTANCE),
  sonarClass(0, 2, MAXPINGDISTANCE)
};
*/


auto timer = timer_create_default();

void printTimestamp()
{
  Serial.print(timeClient.getFormattedTime());
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
    Serial.println();
    printTimestamp();
    Serial.print("Error mapping alarm type to error name in clearAlarm");
  }

  for (int t = 0; t <= numtanks - 1; t++)
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
  float d = 0;

  digitalWrite(LED_BUILTIN, LOW);

  for (int t = 0; t <= numtanks - 1; t++)
  {
    if (!tanks[t].ignore)
    {
      d = tanks[t].sonar->convert_cm(tanks[t].sonar->ping_median());
      delay(500); // wait a moment between pings
      if (debug)
      {
        msgn  = snprintf(msgbuff, MSGBUFFSIZE, "\ntank = %i, pinged d= %f, trigPin = %i, echoPin = %i",t, d, tanks[t].sonarTrigPin, tanks[t].sonarEchoPin);
        Serial.println(msgbuff);
      }

      tanks[t].pingCount++;

      if (d > tanks[t].depth)
      {
        d = tanks[t].depth;
        handleAlarm(MAXDEPTH, t);
      }
      else if ((tanks[t].alarmFlags & MAXDEPTH) && (d != tanks[t].depth)) clearAlarm(MAXDEPTH, t);

      tanks[t].liquidDepth = tanks[t].depth - (d - tanks[t].sonarOffset);
      tanks[t].liquidDepthSum += tanks[t].liquidDepth;

      if (debug)
      {
        msgn = snprintf(msgbuff, MSGBUFFSIZE, "\ntank %i liquidDepth set to = %f", t, tanks[t].liquidDepth);
        Serial.println(msgbuff);
      }

      tanks[t].liquidVolume = tanks[t].liquidDepth * tanks[t].vCM;

      if (tanks[t].liquidDepth > tanks[t].hiAlarm)
      {
        handleAlarm(HIALARM, t);
      }
      else if (tanks[t].alarmFlags & HIALARM) clearAlarm(HIALARM, t);

      if (tanks[t].liquidDepth < tanks[t].loAlarm)
      {
        handleAlarm(LOALARM, t);
      }
      else if (tanks[t].alarmFlags & LOALARM) clearAlarm(LOALARM, t);

    }
  }

  if (debug) displayTankData();

  digitalWrite(LED_BUILTIN, HIGH);
}


void sendTankData()
{
  digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level

  for (int t = 0; t <= numtanks - 1; t++)
  {
    if (!tanks[t].ignore)
    {
      tanks[t].liquidDepthAvg = tanks[t].liquidDepthSum / tanks[t].pingCount;
      tanks[t].liquidVolumeAvg = tanks[t].liquidDepthAvg * tanks[t].vCM;

      tankmsg["node"] = chipID;
      tankmsg["msgtype"] = "T";   // tank msg
      tankmsg["t"] = t + startingTankNum;
      tankmsg["tT"] = tanks[t].tankType;
      tankmsg["d"] = tanks[t].depth;
      tankmsg["vCM"] = tanks[t].vCM;
      tankmsg["lD"] = tanks[t].liquidDepth;
      tankmsg["lDAvg"] = tanks[t].liquidDepthAvg;
      tankmsg["lV"] = tanks[t].liquidVolume;
      tankmsg["lVAvg"] = tanks[t].liquidVolumeAvg;
      tankmsg["loA"] = tanks[t].loAlarm;
      tankmsg["hiA"] = tanks[t].hiAlarm;
      tankmsg["aF"] = tanks[t].alarmFlags;

      serializeJson(tankmsg, msgbuff);

      if (debug)
      {
        Serial.print("\nmsgbuff=(");
        Serial.print(msgbuff);
        Serial.println(")");
        serializeJson(tankmsg, Serial);
        displayTankData();
      }
      tanks[t].liquidDepthSum = 0;
      tanks[t].pingCount = 0;

      mqttClient.publish(mqttTopicData, msgbuff);
      digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
    }
  }
}

bool handlePingTimer(void *)
{
  pingTanks();
  return true;
}

bool handleSendDataTimer(void *)
{
  sendTankData();
  return true;
}

bool handleLEDTimer(void *)
{
  if (ledOn)
  {
    digitalWrite(LED_BUILTIN, HIGH);
  }
  else
  {
    digitalWrite(LED_BUILTIN, LOW);
  }

  ledOn = !ledOn;
  return (true);
}

void displayTankData()
{
  if (debug)
  {
    Serial.println("\nIn displayTankData");
  }

  for (int t = 0; t <= numtanks - 1; t++)
  {

    Serial.println();
    printTimestamp();
    Serial.print(" Tank ");
    Serial.print(t + 1);
    Serial.println();
    Serial.print("Tank Depth ");
    Serial.println(tanks[t].depth);
    Serial.print("Tank liters/cm depth ");
    Serial.println(tanks[t].vCM);
    Serial.print("Max Volume ");
    Serial.print("Actual liquid depth ");
    Serial.println(tanks[t].liquidDepth);
    Serial.print("Average liquid depth ");
    Serial.println(tanks[t].liquidDepthAvg);
    Serial.print("Actual liquid volume ");
    Serial.println(tanks[t].liquidVolume);
    Serial.print("Average liquid volume ");
    Serial.println(tanks[t].liquidVolumeAvg);
    Serial.print("Lo alarm level ");
    Serial.println(tanks[t].loAlarm);
    Serial.print("Hi alarm level ");
    Serial.println(tanks[t].hiAlarm);
    Serial.print("Ping count ");
    Serial.println(tanks[t].pingCount);
  }
}

void acceptConfigFile()
{
  Serial.println("in acceptConfigFile()");
}

void handleMQTTmsg(char* mqtt_topic, byte * payload, unsigned int length)
{
  const char* cmd = "";
  const char* msgtype = "";
  long int targetNode = -1;
  int pumpNum = 0;

  if (debug)
  {
    msgn = snprintf(msgbuff, MSGBUFFSIZE, "\nMessage arrived, topic [ % s]", mqtt_topic);
    outputMsg(msgbuff);
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
  }

  deserializeJson(tankmsg, payload, MAXJSONSIZE);

  msgtype = tankmsg["msgtype"];
  if (msgtype[0] == 'C')                             // if command msg
  {
    targetNode = tankmsg["targetnode"];

    if ((targetNode == chipID) || (targetNode == ALLNODES))
    {
      cmd = tankmsg["cmd"];
      switch (cmd[0]) {
        case 'B':  // reboot
          {
            Serial.println("Reboot command received, rebooting...");
            ESP.restart();
          }
          
        case 'C':  // accept new config file
          {
            Serial.println("Accept config file command received, responding...");
            acceptConfigFile();
            break;
          }
                    
        case 'Q':  // query
          {
            Serial.println("Query command received, responding...");
            sendTankData();
            break;
          }

        default:
          {
            Serial.println("Invalid command message received, ignored");
          }
      }
    }
    else Serial.println("Node in cmd != chipID or ALLNODES, command ignored");
  }
  else Serial.println("Invalid message type received");
}

void displayStartHeader(int i)
{

  if (i == 1)
  {
    Serial.println();
    Serial.println("Tank Monitor Sensor Node");
    Serial.print("Build: ");
    Serial.print(__DATE__);
    Serial.print(" ");
    Serial.print(__TIME__);
    Serial.println();
  }

  if (i == 2)
  {
    msgn = snprintf(msgbuff, MSGBUFFLEN, "\nNodename: %s \nMQTT mqttTopic: %s \ndebug = %i \nnumtanks = %i ", nodeName, mqttTopic, debug, numtanks);
    Serial.println(msgbuff);
    displayTankData();
    Serial.println();
    printTimestamp();
    msgn = snprintf(msgbuff, MSGBUFFLEN, "  %s Started", nodeName);
    Serial.println(msgbuff);
    Serial.println();
    Serial.flush();
  }
}

void initSonars()
{
  for (int i = 0; i < numtanks; i++)
  {
    tanks[i].sonar = new NewPingESP8266(tanks[i].sonarTrigPin, tanks[i].sonarEchoPin);
  }
}

void setup()
{
  delay(5000);                                     // Initial delay to allow intervention
  Serial.begin(9600);
  if (!loadConfig())
  {
    Serial.println("Failed to load config");
  };
  displayStartHeader(1);
  chipID = ESP.getChipId();
  msgn = sprintf(nodeName, "SNTM-W-ESP8266-%X", chipID);  // SNTM-W (Sensor Node Tanks Mon - Water)
  pinMode(LED_BUILTIN, OUTPUT);
  connectWiFi();
  while (!setupNTP(timeZone)) {
    delay(5000);
  };
  while (!setupMdns(nodeName)) {
    delay(5000);
  };
  hostEntry = -1;
  while (hostEntry == -1) {
    hostEntry = findService("_mqtt", "_tcp");
  };
  while (!setupMQTT(MDNS.IP(hostEntry), MDNS.port(hostEntry), true, mqttTopicCtrl, handleMQTTmsg)) {
    delay(5000);
  };

  initSonars();

  timer.every(tankpingdelay, handlePingTimer);
  timer.every(SENDDATADELAY, handleSendDataTimer);
  timer.every(LEDFLASHTIME, handleLEDTimer);

  pinMode(LED_BUILTIN, OUTPUT);

  pingTanks();                                    // initial ping just to get data for display
  displayStartHeader(2);
}

void loop() {

  timer.tick();
  timeClient.update();

  if (!mqttClient.connected()) connectMQTT(true, mqttTopicCtrl, MDNS.IP(hostEntry));
  mqttClient.loop();
}
