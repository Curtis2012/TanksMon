/*
  Tank_Mon

  2019-09-15 C.Collins Initial version supporrting up to 4 tanks
  

  Requires blynk server and app:

  to start blynk server on Raspian:

  cd ~/Arduino/libraries/Blynk/scripts
  sudo ./blynk-ser.sh -c /dev/ttyACM0 -b 9600 -s blynk-cloud.com


  TODO:

  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.


*/


#include <BlynkSimpleStream.h>
#include <NewPing.h>
#define DEBUG 0

#define TANKPINGDELAY 15000L

char auth[] = "989b37ebf16b4d7584dffbb8b8e9acfb";  // Blynk auth token
WidgetLED blynkUpdateLED(V8);
WidgetLED blynkErrorLED(V9);
BlynkTimer timer;
WidgetTerminal blynkTerminal(V10);

const int numTanks = 2;
const int maxPingDistance = 400;

class tank {
  public:
    float depth = 0;
    float vCM = 0;        // volume in liters per cm
    float maxVolume = 0;
    float liquidDepth = 0;
    float liquidVolume = 0;

    tank()
    {

    }

    tank(float depth, float vCM)
    {
      this->depth = depth;
      this->vCM = vCM;
    }

};

tank tanks[numTanks] = {
  tank(105, 0.5),
  tank(105, 0.5)
};

class sonarClass : public NewPing {
  public:
    sonarClass(int trigPin, int echoPin, int maxDistance): NewPing(trigPin, echoPin, maxDistance)
    {

    };
};

sonarClass sonars[numTanks] = {
  sonarClass(2, 3, maxPingDistance),
  sonarClass(4, 5, maxPingDistance)
};

void initTanks()
{
  for (int t = 0; t <= numTanks - 1; t++)
  {
    tanks[t].maxVolume = tanks[t].depth * tanks[t].vCM;
  }
}

void pingTanks()
{

  for (int t = 0; t <= numTanks - 1; t++)
  {
    int d = sonars[t].convert_cm(sonars[t].ping_median());

    if (d > tanks[t].depth)
    {
      d = tanks[t].depth;
    }

    tanks[t].liquidDepth = d;
    tanks[t].liquidVolume = tanks[t].liquidDepth * tanks[t].vCM;

  }
  if (DEBUG) displayTankData();
}

void blynkTankData()
{
  Blynk.virtualWrite(V0, tanks[0].liquidDepth);
  Blynk.virtualWrite(V1, tanks[1].liquidDepth);
  Blynk.virtualWrite(V2, 0);
  Blynk.virtualWrite(V3, 0);
  Blynk.virtualWrite(V4, tanks[0].liquidVolume);
  Blynk.virtualWrite(V5, tanks[1].liquidVolume);
  Blynk.virtualWrite(V6, 0);
  Blynk.virtualWrite(V7, 0);
  blynkUpdateLED.on();
  blynkUpdateLED.off();
}

void displayTankData()
{

  for (int t = 0; t <= numTanks - 1; t++)
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
    blynkTerminal.flush();
  }
}

void handleTimer()
{
  pingTanks();
  blynkTankData();
}

void setup()
{
  delay(3000);                                     // Initial delay to avoid Arduino upload problems

  Serial.begin(9600);
  Blynk.begin(Serial, auth);

  timer.setInterval(TANKPINGDELAY, handleTimer);  // setup timer interrupt handler

  blynkTerminal.clear();
  blynkTerminal.print(__DATE__);
  blynkTerminal.print(" ");
  blynkTerminal.print(__TIME__);
  blynkTerminal.println(" Tanks Mon V0.5");
  blynkTerminal.println();
  blynkTerminal.flush();
  blynkUpdateLED.off();
  blynkErrorLED.off();

  initTanks();
  pingTanks();                                   // initial ping juset to get data for display
  displayTankData();
}

void loop() {

  Blynk.run();
  timer.run();

}
