#include <avr/wdt.h>

uint8_t resetSource __attribute__ ((section(".noinit")));

void resetFlagsInit(void) __attribute__ ((naked))
__attribute__ ((used))
__attribute__ ((section (".init0")));

void resetFlagsInit(void) {
  __asm__ __volatile__ ("sts %0, r2\n" : "=m" (resetSource) :);
}


uint16_t i = 0;

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(57600);
  Serial.println("Started");
  reset_source();
  //wdt_enable(WDTO_8S);
}

// the loop function runs over and over again forever
void loop() {
  //wdt_reset();
  Serial.println(i++);
  Serial.println(millis());
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(1000);                       // wait for a second
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(1000);                       // wait for a second
}

void reset_source(void)
{
  // Check source of reset
  if (resetSource & 1) {              // PowerOn Reset  
    Serial.println(F("PowerOn Reset"));
  }
  else if (resetSource & 2) {         // External Reset
    Serial.println(F("External Reset"));
  }
  else if (resetSource & 4) {         // Brown-Out Reset
    Serial.println(F("BrownOut Reset"));
  }
  else if (resetSource & 8) {         // Watchdog Reset
    Serial.println(F("WatchDog Reset"));
  }
  else if (resetSource & 0x10) {      // Jtag Reset
    Serial.println(F("JTAG Reset"));
  }
  else {                              // Unknown Reset
    Serial.println(F("Unknown Reset"));
  }
}
