
/*
  Tanks_Mon

  2019-09-15 C.Collins Initial Arduino version supporrting up to 4 tanks
  2019-10-10 Ported to NodeMCU board and converted to Blynk standalone via WiFi
  2019-10-11 Added alarm functionality
  2019-10-13 Added OTA
  2019-10-22 Changed architecture to sensor node/manager node architecture where sensor node only deals with sensors and all other external interfaces and functions are handled on manager node.
			 Manager node can manage multiple sensor nodes. This allows multiple senor nodes to report to one manager node making number of sensors/tanks easily expandable.
			 This also supports sensor nodes being physically distant from each other and the manager node. Used MQTT protocol with JSON payload.
  2020-03-30 Begain process of restructuring code to use common library (Tankmon.h), fixing bugs, improving implementation, resolving OTA issues...
  2020-04-15 Converted to not use blynk/simpletimer library due to problems creating timers
  2020-04-18 Moved to Visual Studio for IDE, incorporated into Tanks Mon solution (but...had problems w Visual Micro stability...)
  2020-10-10 C. Collins. Added PumpMon integration/low water shutoff logic, commands via MQTT
  2020-11-06 C. Collins, moved to Github
  2020-11-08 C. Collins, moved to Visual Studio/Visual Micro
  2020-11-11 C. Collins, added OTA support


  TODO:

  - enchance code to handle multiple sensor nodes
	- use node name to differentiate, display tanks for each node on separage tab in app
  - clean up displayTankData and blynkTankData to be better structured/integrated
  - add command support to terminal
  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.
  - modify blynkTankData to remove hard code tank numbers
  - splash screen not displaying on blynk console
  - clean up outputs to use sprintf
  - move all blynkTerminal output to one function and implement semaphore to prevent conflicting output, also better isolates for future non-blynk user interface
  - hard coded VPIN numbers and resulting code can be inproved by mapping vector of VPIN numbers
  - simpler to move all alarm handling to manager node rather than syncing changes from user app, to manager node, to sensor node
  - setup enum class for Blynk V pins...or just move off of Blynk.

*/

#include <cscNetServices.h>
#include <Tanksmon.h>
#include <BlynkSimpleEsp8266.h>

#define IOLED 2
#define BLYNK_PRINT Serial
#define LEDFLASHTIME 3000L
#define PROPANETANKNUM 4
#define ALLTANKNODES -512
#define ALLPUMPNODES -1024

WidgetLED blynkUpdateLED(V8);
WidgetLED blynkErrorLED(V9);
WidgetLED blynkAlarmLED(V11);

WidgetTerminal blynkTerminal(V10);

bool ledOn = false;
bool blynkWidgetsUpdate = false;
bool displayTimerChanged = false;

unsigned long displayUpdateDelay = 15000L;
int selectedTank = 0;
long int chipID = 0;
auto displayUpdateTimer = timer_create_default();
auto LEDTimer = timer_create_default();
uintptr_t displayUpdateTaskID;

void sendPumpCmd(int t, char cmdin)
{

	StaticJsonDocument<100> cmdmsg;
	char cmdbuff[100] = "";                // use separate buffer from msgbuff to avoid command from getting stepped on in global msgbuff
	String cmd = " ";                     // kludge for converting char to "string" for use by json

	digitalWrite(LED_BUILTIN, LOW);                           // Turn the LED on (Note that LOW is the voltage level

	cmd[0] = cmdin;                        // kludge: json does not support char data type, so convert to "string"

	cmdmsg["node"] = chipID;
	cmdmsg["msgtype"] = "C";                      // command messge
	cmdmsg["cmd"] = cmd;                          // stop command
	cmdmsg["targetnode"] = tanks[t].pumpNode;            // address pump node
	cmdmsg["pumpnum"] = tanks[t].pumpNumber;          // pump number to stop


	serializeJson(cmdmsg, cmdbuff);

	if (debug)
	{
		Serial.print("in sendPumpCmd(), ");
		Serial.print("\ncmdbuff=(");
		Serial.print(cmdbuff);
		Serial.println(")");
		serializeJson(cmdmsg, Serial);
		displayTankData();
	}

	mqttClient.publish(mqttTopicCtrl, cmdbuff);
	digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
	if (debug) Serial.println("leaving sendPumpCmd()");
}

void handleAlarm(int tank)
{
	String notification = "";

	globalAlarmFlag = true;

	if (tanks[tank].alarmFlags & HIALARM)
	{
		blynkTimestamp();
		notification = "HI ALARM! on Tank " + String(tank + 1);
		blynkTerminal.println();
		blynkTerminal.println(notification);
		blynkTerminal.flush();
		blynkAlarmLED.on();
		Blynk.notify(notification);
	}

	if (tanks[tank].alarmFlags & LOALARM)
	{
		blynkTimestamp();
		notification = "LO ALARM! on Tank " + String(tank + 1);
		blynkTerminal.println();
		blynkTerminal.println(notification);
		blynkTerminal.flush();
		blynkAlarmLED.on();
		Blynk.notify(notification);
		if (debug)
		{
			Serial.print("Sending stop command for tank = ");
			Serial.println(tank);
		}
		if ((tanks[tank].tankType[0] != 'P') && (!tanks[tank].ignore)) sendPumpCmd(tank, 'S');  // send stop command
	}

	/*
	  if (tanks[tank].alarmFlags & MAXDEPTH)
	  {
		blynkTimestamp();
		notification = "MAX DEPTH ALARM! on Tank " + String(tank + 1) + ", depth = " + String(tanks[tank].depth);
		blynkTerminal.println();
		blynkTerminal.println(notification);
		blynkTerminal.flush();
		blynkAlarmLED.on();
		Blynk.notify(notification);
	  }
	*/

}

void outputMsg(char* msg, bool serialout, bool blynkout)
{
	// output msg to both com and blynk with timestamp header, convert all output to use this routine
	// always outputs to serial, optionally output to blynk
	// functions which are called by outputMsg and generate output should not use outputMsg to avoid recursive calls

	digitalWrite(IOLED, LOW);

	if (serialout)
	{
		printTimestamp();
		Serial.println(msg);
	}

	if (blynkout)
	{
		blynkTimestamp();
		blynkTerminal.println(msg);
		blynkTerminal.flush();
	}

	digitalWrite(IOLED, HIGH);
}

void blynkTimestamp()
{
	blynkTerminal.println();
	blynkTerminal.print(nodeName);
	blynkTerminal.print(" ");
	blynkTerminal.print(timestampString());
	blynkTerminal.println();
}

void printTimestamp()
{
	Serial.println();
	Serial.print(nodeName);
	Serial.print(" ");
	Serial.print(timestampString());
	Serial.println();
}

void clearAlarm(std::uint8_t alarmType, int tank)
{
	int a = -1;
	std::uint8_t alarmCheck = 0b00000000;

	blynkTimestamp();
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

		if (alarmType == LOALARM)
		{
			sendPumpCmd(tank, 'R');  // send resume command
		}
	}
	else
	{
		blynkTerminal.println();
		blynkTerminal.print("Error mapping alarm type to error name in clearAlarm, alarmtype = ");
		blynkTerminal.println(alarmType);
		blynkErrorLED.on();
	}

	for (int t = 0; t <= numtanks - 1; t++)
	{
		alarmCheck = alarmCheck | tanks[t].alarmFlags;
	}

	if (alarmCheck == 0)
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

	blynkUpdateLED.on();

	if (imperial)
	{
		cvtG = CVTFACTORGALLONS;
		cvtI = CVTFACTORINCHES;
	}

	if (useAvg)
	{
		Blynk.virtualWrite(V0, (tanks[0].liquidDepthAvg * cvtI));
		Blynk.virtualWrite(V1, (tanks[1].liquidDepthAvg * cvtI));
		Blynk.virtualWrite(V4, (tanks[0].liquidVolumeAvg * cvtG));
		Blynk.virtualWrite(V5, (tanks[1].liquidVolumeAvg * cvtG));
	}
	else
	{
		Blynk.virtualWrite(V0, (tanks[0].liquidDepth * cvtI));
		Blynk.virtualWrite(V1, (tanks[1].liquidDepth * cvtI));
		Blynk.virtualWrite(V4, (tanks[0].liquidVolume * cvtG));
		Blynk.virtualWrite(V5, (tanks[1].liquidVolume * cvtG));
	}

	Blynk.virtualWrite(V13, (tanks[PROPANETANKNUM].percentFull));

	blynkUpdateLED.off();
	digitalWrite(IOLED, HIGH);
}

void displayTankData()
{
	int  msgloc = 0;

	if (debug) Serial.println("in displayTankData()");
	for (int t = 0; t <= numtanks - 1; t++)
	{
		msgloc = 0;
		msgloc += snprintf(msgbuff, MSGBUFFSIZE, "\nTank %u \nTank Depth %1.2f \nTank liters/cm depth %1.2f  ", (t + 1), tanks[t].depth, tanks[t].vCM);
		msgloc += snprintf(&msgbuff[msgloc], MSGBUFFSIZE - msgloc, "\nActual liquid depth %1.2f \nAverage liquid depth %1.2f", tanks[t].liquidDepth, tanks[t].liquidDepthAvg);
		msgloc += snprintf(&msgbuff[msgloc], MSGBUFFSIZE - msgloc, "\nActual liquid volume %1.2f \nAverage liquid volume %1.2f ", tanks[t].liquidVolume, tanks[t].liquidVolumeAvg);
		msgloc += snprintf(&msgbuff[msgloc], MSGBUFFSIZE - msgloc, "\nLo alarm level %1.2f \nHi alarm level %1.2f", tanks[t].loAlarm, tanks[t].hiAlarm);
		outputMsg(msgbuff, false, true);
	}
}

bool handleDisplayTimer(void*)
{

	blynkTankData();
	checkTimeOut();
	return (true);
}

bool handleLEDTimer(void*)
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

void copyJSONmsgtoStruct()
{
	int t = tankmsg["t"];

	tanks[t].depth = tankmsg["d"];
	tanks[t].vCM = tankmsg["vCM"];
	tanks[t].liquidDepth = tankmsg["lD"];
	tanks[t].liquidDepthAvg = tankmsg["lDAvg"];
	tanks[t].liquidVolume = tankmsg["lV"];
	tanks[t].liquidVolumeAvg = tankmsg["lVAvg"];
	tanks[t].loAlarm = tankmsg["loA"];
	tanks[t].hiAlarm = tankmsg["hiA"];
	tanks[t].alarmFlags_prev = tanks[t].alarmFlags;
	tanks[t].alarmFlags = tankmsg["aF"];

}

void checkTimeOut()
{
	unsigned long timeDelta = 0;
	int t = 0;

	for (t = 0; t < numtanks; t++)
	{

		timeDelta = millis() - tanks[t].lastMsgTime;
		Serial.println();
		msgn = snprintf(msgbuff, MSGBUFFSIZE, "\nTank %i: timeDelta = %1d, timeOut = %ld, lastMsgTime = %ld", t, timeDelta, tanks[t].timeOut, tanks[t].lastMsgTime);
		outputMsg(msgbuff, true, true);
		if (timeDelta > tanks[t].timeOut)
		{
			Serial.print("Timeout on tank ");
			Serial.print(t);
		}
	}
}

void handleCommandMsg()
{
	const char* cmd = "";
	const char* msgtype = "";
	long int targetNode = 0;
	int pumpNum = 0;

	msgtype = tankmsg["msgtype"];
	if (msgtype[0] == 'C')                             // confirm is command msg
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
				break; // should never get here, but just in case
			}

			case 'Q':  // query
			{
				Serial.println("Query command received, responding...");
				displayTankData();
				blynkTankData();
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

void updatePumpDisplay()
{
	Serial.println("in updatePumpDisplay()");
}

void handleMQTTmsg(char* topic, byte* payload, unsigned int length) {

	int t = -1;
	float percentFullTest = 0;
	const char* msgtype = "";
	const char* tanktype = "";

	if (debug)
	{
		msgn = snprintf(msgbuff, MSGBUFFSIZE, "\nMessage arrived, topic [%s]", topic);
		outputMsg(msgbuff, true, true);
		for (int i = 0; i < length; i++)
		{
			Serial.print((char)payload[i]);
		}
		Serial.println();
	}


	deserializeJson(tankmsg, payload, MAXJSONSIZE);

	msgtype = tankmsg["msgtype"];
	Serial.print("\nmsgtype="); Serial.println(msgtype);
	switch (msgtype[0])
	{

	case 'C': // command msg
	{
		handleCommandMsg();
	}
	case 'P': // pump msg
	{
		updatePumpDisplay();
		break;
	}
	case 'T': // tank msg
	{
		t = tankmsg["t"];
		tanktype = tankmsg["tT"];
		Serial.print("\ntanktype="); Serial.println(tanktype);
		if (tanktype[0] == 'P')
		{
			Serial.println("\nPropane tank msg received.");
			percentFullTest = tankmsg["pF"];
			if ((percentFullTest > -1) && (percentFullTest < 101))
			{
				tanks[PROPANETANKNUM].percentFull = percentFullTest;
				Serial.print("\nPropane tank percentage full = ");
				Serial.println(tanks[PROPANETANKNUM].percentFull);
			}
		}
		else
		{
			Serial.println("Non-propane tank msg received");
			copyJSONmsgtoStruct();
		}

		if (tanks[t].alarmFlags != 0)
		{
			handleAlarm(t);
		}
		else if (tanks[t].alarmFlags_prev != 0) clearAlarm(CLEARALARMS, t);

		if (!globalAlarmFlag && blynkAlarmLED.getValue()) blynkAlarmLED.off(); // clear alarm LED if left on from previous device selection in Blynk app
		tanks[t].lastMsgTime = millis();
		break;
	}

	default:
	{
		Serial.println("Invalid message type received");
	}
	}
}

void splashScreen()
{
	blynkTerminal.clear();
	msgn = snprintf(msgbuff, MSGBUFFSIZE, "\n\nTank Monitor Manager Node started \nBuild: Beta Test %s %s\nSite: %s \nNode: %s", __DATE__, __TIME__, sitename, nodeName);
	outputMsg(msgbuff, false, true);
}

BLYNK_WRITE(V15)                      // Debug on/off
{
	if (!blynkWidgetsUpdate)
	{
		switch (param.asInt())
		{
		case 1: {
			debug = false;
			outputMsg("Debug off", false, true);

			break;
		}
		case 2: {
			debug = true;
			outputMsg("Debug on", false, true);
			break;
		}
		}
	}
}

void setBlynkWidgets(bool global, bool tankOnly)
{
	int dbg = 0;

	// update global parameters

	blynkWidgetsUpdate = true;

	blynkUpdateLED.off();
	blynkErrorLED.off();
	blynkAlarmLED.off();

	dbg = ((debug) ? (2) : (1));
	Blynk.virtualWrite(V15, dbg);
	blynkWidgetsUpdate = false;

}

void tickTimers()
{
	displayUpdateTimer.tick();
	LEDTimer.tick();
}

void initTanks()
{
	int i = 0;
	for (i = 0; i < numtanks; i++)
	{
		tanks[i].lastMsgTime = millis(); // init all to same start time
		if (tanks[i].tankType[0] == 'P') tanks[i].timeOut = 10800 * 1e6; // Set timeout to 3 hours since PropaneTankMon may not sample for hours
	}
}

void setup()
{
	delay(3000);                                     // Initial delay to provide intervention window
	debug = true;
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, LOW);            // keep LED on until start up complete
	pinMode(IOLED, OUTPUT);
	digitalWrite(IOLED, HIGH);

	Serial.begin(9600);

	Serial.println("\n\nTanks Mon Manager Node starting...");
	chipID = ESP.getChipId();
	msgn = sprintf(nodeName, "MNTM-ESP8266-%X", chipID);

	if (!loadConfig())
	{
		Serial.println("Failed to load config, halting");
		while (true);
	};

	initTanks();

	connectWiFi();
	Blynk.config(blynkAuth);
	Blynk.connect();
	setupNTP(timeZone);
	setupMdns(nodeName);
	startOTA(); // must be invoked AFTER mDNS setup
	hostEntry = findService("_mqtt", "_tcp");
	setupMQTT(MDNS.IP(hostEntry), MDNS.port(hostEntry), true, mqttTopicData, handleMQTTmsg);
	if (!subscribeMQTT(mqttTopicCtrl)) Serial.print("Subscribe to MQTT control topic failed!");

	setBlynkWidgets(true, true);

	splashScreen();

	digitalWrite(LED_BUILTIN, HIGH);
	displayUpdateTaskID = displayUpdateTimer.every(displayUpdateDelay, handleDisplayTimer);
	LEDTimer.every(LEDFLASHTIME, handleLEDTimer);
}

void loop() {

	handleOTA();
	Blynk.run();

	if (!mqttClient.connected())
	{
		connectMQTT(true, mqttTopicData, MDNS.IP(hostEntry));
		subscribeMQTT(mqttTopicCtrl);
	}
	mqttClient.loop();
	tickTimers();
}
