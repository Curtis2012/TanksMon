/*
  Tank_Mon

  2019-09-15 C.Collins Initial version supporrting up to 4 tanks
  

  Requires blynk server and app:

  to start blynk server on Raspian:

  sudo /home/pi/Arduino/libraries/Blynk/scripts/blynk-ser.sh -c /dev/ttyACM0 -b 9600 -s blynk-cloud.com


  TODO:

  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.
  - add hi/low tank alarms
  


*/


#include <BlynkSimpleStream.h>
#define NUMTANKS 4
#define MAXPINGDISTANCE 400
#define DEBUG 0
#define IMPERIAL 0
#define CVTFACTORGALLONS 0.26417F
#define CVTFACTORINCHES  0.39370F
#define TANKPINGDELAY 15000L

char auth[] = "989b37ebf16b4d7584dffbb8b8e9acfb";  // Blynk auth token
WidgetLED blynkUpdateLED(V8);
WidgetLED blynkErrorLED(V9);
BlynkTimer timer;
WidgetTerminal blynkTerminal(V10);

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

tank tanks[NUMTANKS] = {
  tank(105, 0.5),
  tank(105, 0.5),
  tank(105, 0.5),
  tank(105, 0.5)
  
};

class sonarClass : public NewPing {
  public:
    sonarClass(int trigPin, int echoPin, int maxDistance): NewPing(trigPin, echoPin, maxDistance)
    {

    };
};

sonarClass sonars[NUMTANKS] = {
  sonarClass(2, 3, MAXPINGDISTANCE),
  sonarClass(4, 5, MAXPINGDISTANCE),
  sonarClass(6, 7, MAXPINGDISTANCE),
  sonarClass(8, 9, MAXPINGDISTANCE)
};

void initTanks()
{
  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    tanks[t].maxVolume = tanks[t].depth * tanks[t].vCM;
  }
}

void pingTanks()
{
  int d = 0;
  
  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    if (t < 2)                       // test code to fake 4 tanks
    {
    d = sonars[t].convert_cm(sonars[t].ping_median());
    }
    else
    {
      d = tanks[t-1].liquidDepth * 0.75;
    }

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
  float cvtG = 1.0, cvtI = 1.0;

  if (IMPERIAL) {
    cvtG = CVTFACTORGALLONS;
    cvtI = CVTFACTORINCHES;
  }
  
  Blynk.virtualWrite(V0, (tanks[0].liquidDepth*cvtI));
  Blynk.virtualWrite(V1, (tanks[1].liquidDepth*cvtI));
  Blynk.virtualWrite(V2, (tanks[2].liquidDepth*cvtI));  // test code to fake 4 tanks
  Blynk.virtualWrite(V3, (tanks[3].liquidDepth*cvtI));  // test code to fake 4 tanks
  Blynk.virtualWrite(V4, (tanks[0].liquidVolume*cvtG));
  Blynk.virtualWrite(V5, (tanks[1].liquidVolume*cvtG));
  Blynk.virtualWrite(V6, (tanks[2].liquidVolume*cvtG)); // test code to fake 4 tanks
  Blynk.virtualWrite(V7, (tanks[3].liquidVolume*cvtG)); // test code to fake 4 tanks
  
  blynkUpdateLED.on();
  blynkUpdateLED.off();
}

void displayTankData()
{

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

  initTanks();
  pingTanks();                                    // initial ping juset to get data for display

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
}

void loop() {

  Blynk.run();
  timer.run();

}
