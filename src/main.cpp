#include <Arduino.h>

// Relays: 
//  1 - Water pump (WATER_LOAD_PIN)
//  2 - Main pump
//  3 - Drain pump
//  4 - Dispenser
//  H - Heater
#define WATER_LOAD_PIN 6
#define MAIN_PUMP_PIN 7
#define DRAIN_PIN 4
#define SOAP_PIN 3
#define HEATER_PIN 8

#define WATER_DISABLED_PIN 5
#define TEMP_SENSOR A5

#define LED_PIN 12
#define SPEAKER_PIN 11
#define SWITCH_PIN 10

// Error codes
#define GENERIC_ISSUE 1
#define DRAIN_ISSUE 2
#define FAILED_LOAD_ISSUE 3
#define FAILED_TOP_UP_ISSUE 4
#define FAILED_REACH_TEMP 5

// Message codes
#define WELCOME_MSG 2
#define LOAD_MSG 3
#define DRAIN_MSG 4

// Times
#define DRAIN_TIME 22000
#define LOAD_TIMEOUT 200000
#define HEATER_TIMEOUT 600000

// Modes
 #define RELAY_MODULE_OFF HIGH
 #define RELAY_MODULE_ON LOW
 #define LED_OFF HIGH
 #define LED_ON LOW

// Shutdown everything that might be on, optionally delaying changes to avoid power spikes.
void reset(int stabiliseTime = 0) {
  // Make sure heater is the 1st one to be switched off, as it requires water movement to cold down.
  digitalWrite(HEATER_PIN, LOW);
  delay(stabiliseTime);
  
  // Shutdown anything that might be on.
  digitalWrite(WATER_LOAD_PIN, RELAY_MODULE_OFF);
  delay(stabiliseTime);
  digitalWrite(DRAIN_PIN, RELAY_MODULE_OFF);
  delay(stabiliseTime);
  digitalWrite(SOAP_PIN, RELAY_MODULE_OFF);
  delay(stabiliseTime);
  digitalWrite(LED_PIN, LED_OFF);
  delay(stabiliseTime);
  
  // Main pump is the last one to be switched off, main pump keeps the water level down, a reset might be followed by a drain process.
  digitalWrite(MAIN_PUMP_PIN, RELAY_MODULE_OFF);
  delay(stabiliseTime);
}

// Do beeps.
void beep(int many, int length = 150, int delayLength = 50) {
  for (int i = 0; i < many; i++) {
    tone(SPEAKER_PIN, 1000, length);
    delay(length + delayLength);
  }
}

// Report an issue with beeps.
void beepError(int issue) {
  beep(10, 50, 50);
  delay(100);
  beep(issue, 500, 300);
}

// Report a message with beeps.
void beepMessage(int message) {
  beep(2, 350, 220);
  beep(message);
}

// Halt everything and report an issue forever.
void crash(int issue) {
  reset(500);
  while (1) {
    beepError(issue);
    delay(2000);
  }
}

// Check if minimum water level has been reached.
bool isLoaded() {
  // test if is loaded for 10 milliseconds
  for (int i = 0; i < 10; i++) {
    if (digitalRead(WATER_DISABLED_PIN)) { // WATER_DISABLED_PIN pin is high when there is no water.
      return false; // if is not loaded at any time, return
    }
    delay(1);
  }
  return true;  // we had the same result for 10 milliseconds, it's fair to say we have water.
}

// Check if main switch is pressed.
bool switchPressed() {
  return !digitalRead(SWITCH_PIN);
}

// Drain water by activating the drain pump for the defined DRAIN_TIME.
void drain() {
  reset(1000); // a working main pump keeps the water level down, reset() will turn the main pump off last so we can start the drain process any flooding.
  digitalWrite(DRAIN_PIN, RELAY_MODULE_ON);

  beepMessage(DRAIN_MSG);
  delay(DRAIN_TIME);

  digitalWrite(DRAIN_PIN, RELAY_MODULE_OFF);
  
  // Water still available?, something is not ok, crash.
  if (isLoaded()) {
    crash(DRAIN_ISSUE);
  }
}

// Load water and start main pump when base level is reached.
// Continue loading for 1 loading time (double level).
// Start main pump and continue loading for 2/3 loading time.
// Continue loading until base level is recovered (and a little bit more).
void load() {
  reset(200); // make sure everything is off
  beepMessage(LOAD_MSG);
  
  // Start loading process.
  unsigned long int loadStarts = millis();
  digitalWrite(WATER_LOAD_PIN, RELAY_MODULE_ON);
  
  // Wait until water reaches base level or timeout.
  while(!isLoaded() && millis() - loadStarts < LOAD_TIMEOUT) {
    delay(10);
  }
  
  // Timed out but there is no water?, crash with failed to load error.
  // Mind that isLoaded() can be unstable and return a false negative
  // is only a failure if we have a negative AND a timeout.
  if (!isLoaded() && millis() - loadStarts >= LOAD_TIMEOUT) {
    crash(FAILED_LOAD_ISSUE);
  }
  
  // Calculate a base level loadTime.
  //  loadTime is the time the water took to reach the base and minimum level, detected by isLoaded().
  // The maximum water capacity is around 3 times the base level.
  unsigned long int loadTime = millis() - loadStarts;
  
  // With loadTime defined, we can now double the current water level.
  loadStarts = millis();
  while (millis() - loadStarts < loadTime) {
    beep(1, 80);
    delay(1000);
  }
  
  // With double the base level, is ok to initiate water movement by starting the main pump.
  digitalWrite(MAIN_PUMP_PIN, RELAY_MODULE_ON);

  // Main pump will move the water up the pipes causing a drop in level, isLoaded() will be unstable and can't be trusted. 
  // To ensure we get closer to 3 times the base level, we continue the load not checking isLoaded() for a 2/3 of loadTime.
  loadStarts = millis();
  while (millis() - loadStarts < (loadTime / 1.5)) {  // run for 2/3 the load time
    beep(2, 50);
    delay(800);
  }
  
  // At this point we are pretty sure we have enough water (2.66 loadTime).
  // We can now safely stop the loading process as soon isLoaded() reports true.
  // A fixed timeout of 1.5 loadTime is in place in case water level is not reached.
  loadStarts = millis();
  while (!isLoaded() && millis() - loadStarts < (loadTime * 1.5)) { // don't allow it to run more than one and a half extra load time
    beep(1, 50);
    delay(400);
  }
  
  // We should have plenty of water by now.
  // isLoaded() indicates the 12v line is available. Outside software, is used to switch the heater relay On.
  // Without a stable isLoaded() we would not be able to use the heater, crash.
  if (!isLoaded()) {
    crash(FAILED_TOP_UP_ISSUE);
  }
  
  // Loading done.
  digitalWrite(WATER_LOAD_PIN, RELAY_MODULE_OFF);
  delay(1000); // stabilise
}

// Load water and run main pump up to given time.
// Release soap and enable heater if required.
void cycle(unsigned long int  washTime, bool soap = false, long int temperature = 0) {
  int Vo;
  load();
  
  if (soap) {
    digitalWrite(SOAP_PIN, RELAY_MODULE_ON);
    delay(200);
    digitalWrite(SOAP_PIN, RELAY_MODULE_OFF);
    delay(1000); // stabilise
  }
  
  // Enable heater at start of the cycle if temp below required.
  // We don't turn it ON again when temperature goes down.
  Vo = analogRead(TEMP_SENSOR);
  if (temperature > 0 && Vo < temperature) {
    digitalWrite(HEATER_PIN, HIGH);
    delay(1000); // stabilise
  }
  
  unsigned long int cycleStarts = millis();
  unsigned long int washStarts = millis();
  while((washTime * 60 * 1000) >  millis() - washStarts) {
    // Turn off the heater once desired temperature is reached.
    if (Vo > temperature) {
      digitalWrite(HEATER_PIN, LOW);
    } else {
      Vo = analogRead(TEMP_SENSOR);

      if (Vo < temperature && (millis() - cycleStarts) > HEATER_TIMEOUT) {
        crash(FAILED_REACH_TEMP);
      } 

      washStarts = millis(); // reset start time until temperature is reached
      beep(1); // indicate is heating
    }
    
    
    delay(1000);
    digitalWrite(LED_PIN, LED_ON);
    delay(1000);
    digitalWrite(LED_PIN, LED_OFF);
  }

  drain();
}

// Set pins modes and startup checks.
void setup() {
  pinMode(WATER_DISABLED_PIN, INPUT);     
  pinMode(LED_PIN, OUTPUT);     
  pinMode(WATER_LOAD_PIN, OUTPUT);     
  pinMode(DRAIN_PIN, OUTPUT);     
  pinMode(MAIN_PUMP_PIN, OUTPUT);     
  pinMode(HEATER_PIN, OUTPUT);    
  pinMode(TEMP_SENSOR, INPUT);  
  pinMode(SOAP_PIN, OUTPUT);     
  pinMode(SPEAKER_PIN, OUTPUT);     
  pinMode(SWITCH_PIN, INPUT_PULLUP);     
  
  reset(); // Make sure everything is off.

  // The main switch should not be pressed at startup.
  if (switchPressed()) {
    crash(GENERIC_ISSUE);
  }
  
  // We should have no water at startup.
  if (isLoaded()) {
    // error and try to drain
    beepError(DRAIN_ISSUE);
    drain();
  }

  // Welcome beeps
  beepMessage(WELCOME_MSG);
}

void loop() {
  // wait for user action
  while (!switchPressed()) {
    delay(100);
  }
  beep(3); // action detected
  
  // if the switch still pressed after 2 seconds, is alternative program
  delay(2000);
  if (switchPressed())  {
    // rinse program
    beep(5, 80);
    cycle(5, false, 0);
  } else {
    // regular wash program
    cycle(3, false, 950); // temperature is defined by the reading of a thermistor, no fancy centigrades conversion here
    cycle(15, true, 950);
    cycle(3, false, 950);
    cycle(3, false, 0);
  }
  
  // done
  digitalWrite(LED_PIN, LED_ON);
  while (true) {
    beep(20, 50);
    delay(100);
  }
}
