/*
   Tank_Mon

   2019-02-09 C.Collins Initial simple version to monitor 2 tanks. Plan to add more features later.

to start blynk server: ...scripts folder: sudo ./blynk-ser.sh -c /dev/ttyACM0 -b 9600 -s blynk-cloud.com

*/

#include <BlynkSimpleStream.h>
#include <NewPing.h>
#define DEBUG 0
#define BLYNK 1

char auth[] = "989b37ebf16b4d7584dffbb8b8e9acfb";
const int numTanks = 2;
const int maxPingDistance = 300;
const int pingPeriod = 60000;

byte sonarPins[2][numTanks] = {{5, 4}, {7, 6}};  // Ping/echo pin pairs. Ping pins in row 1. Echo pins row 2
int sensorOffsets[numTanks] = {20, 20}; // offset of sensor from max fill in cm

class tank {
  public:
    float radius = 0;
    float depth = 0;
    float maxVolume = 0;
    float waterDepth = 0;
    float waterVolume = 0;
};

tank *tanks;

class sonarClass : public NewPing {
  public:
    sonarClass(int sonarPin, int echoPin, int maxDistance): NewPing(sonarPin, echoPin, maxDistance)
    {

    };
    byte pingPin = 0;
    byte echoPin = 0;
    float offset = 0;
};

sonarClass sonars[numTanks] = {
  sonarClass(sonarPins[1][1], sonarPins[2][1], maxPingDistance),
  sonarClass(sonarPins[1][2], sonarPins[2][2], maxPingDistance)
};


void initTanks()
{
  if (DEBUG)
  {
    Serial.println("In initTanks");
    Serial.flush();
  }
  tanks = new tank[numTanks];
  for (int t = 0; t < numTanks; t++)
  {
    tanks[t].radius = 25;     // Todo: get actual radius in cm
    tanks[t].depth = 105;       // My actual tank depth in cm
    tanks[t].maxVolume = PI * ((tanks[t].radius * tanks[t].radius) * tanks[t].depth);
    tanks[t].waterDepth = 0;     // Zero until first ping
    tanks[t].waterVolume = 0;    // Zero until first ping
  }
}

void initSonars()
{
  if (DEBUG)
  {
    Serial.println("In initSensors");
    Serial.flush();
  }
  for (int t = 0; t < numTanks; t++)
  {
    sonars[t].pingPin = sonarPins[t][0];
    sonars[t].echoPin = sonarPins[t][1];
    sonars[t].offset = sensorOffsets[t];
  }
}

void pingTanks()
{
  if (DEBUG)
  {
    Serial.println("In pingTanks");
    Serial.flush();
  }
  for (int t = 0; t < numTanks; t++)
  {
    //   int d = sonars[t].convert_cm(sonars[t].ping_median()) - sonars[t].offset;
    int d = random(sonars[t].offset,105) - sonars[t].offset;
   

    if ((tanks[t].depth - d) >=  0)
    {
      tanks[t].waterDepth = d;
      tanks[t].waterVolume = (PI * (tanks[t].radius * tanks[t].radius) * (tanks[t].waterDepth)) / 1000;   // Compute volume in liters
    }
  }
  if (DEBUG) printTankData();
}

void blynkTankData()
{
    Blynk.virtualWrite(V0,tanks[0].waterDepth);
    Blynk.virtualWrite(V1, tanks[1].waterDepth);
    Blynk.virtualWrite(V2, tanks[0].waterVolume);
    Blynk.virtualWrite(V3, tanks[1].waterVolume);
}
void printTankData()
{
  for (int t = 0; t < numTanks; t++)
  {
    Serial.print("Tank ");
    Serial.print(t);
    Serial.println();
    Serial.print("Tank Radius ");
    Serial.println(tanks[t].radius);
    Serial.print("Tank Depth ");
    Serial.println(tanks[t].depth);
    Serial.print("Max Water Volume ");
    Serial.println(tanks[t].maxVolume);
    Serial.print("Actual Water Depth ");
    Serial.println(tanks[t].waterDepth);
    Serial.print("Actual Water Volume ");
    Serial.println(tanks[t].waterVolume);
    Serial.println();
    Serial.print("Sonar Pins (ping, echo) ");
    Serial.print(sonars[t].pingPin);
    Serial.print(", ");
    Serial.println(sonars[t].echoPin);
    Serial.print("Sonar Offset ");
    Serial.println(sonars[t].offset);
    Serial.flush();
  }
}

void setup()
{
  delay(5000);         // Always do initial delay to avoid Arduino upload problems
  Serial.begin(9600);
  if (BLYNK) Blynk.begin(Serial, auth);
  if (DEBUG)
  {
    Serial.println("Tanks Mon V0.5");
    Serial.flush();
  }
  initTanks();

  initSonars();
  pingTanks();         // do initial ping
  // sonars[0].timer_ms(pingPeriod, pingTanks); // not working...debug...delay for now

  if (DEBUG) printTankData();

}

void loop() {

  if (BLYNK) Blynk.run();
  delay(3000);
  pingTanks();
  if (BLYNK) blynkTankData();
  

}
