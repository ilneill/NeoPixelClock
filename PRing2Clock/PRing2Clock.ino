/*!
 * Abstract 24hr clock using 2x 12 LED NeoPixel Rings.
 * LHS ring: Hours, red(AM)/green(PM). Blue, seconds.
 * RHS ring: Minutes, red.             Blue, tenths.
 *
 * Time replicated to a TM1637 based 6-digit 7-segment LED display module.
 *
 * ToDo:
 * DS1307 RTC used to retain the time while off.
 * AT24C32 EEPROM to track DS1307 accuracy/drift?
 * DS18B20 Temperature Sensor too... with an LEDC68 7-segment LED display module.
 *
 * Written for the Arduino Uno/Nano/Mega.
 * (c) Ian Neill 2025
 * GPL(v3) Licence
 *
 * **********************
 * * PRing2Clock Sketch *
 * **********************
 */

#include <TimerOne.h>                                     // 16-bit hardware interrupt timer library.
#include <digitalWriteFast.h>                             // Fast digital I/O library.
#include <Adafruit_NeoPixel.h>                            // An easy to use NeoPixel library.
//#include <RTClib.h>                                     // A library for the DS1307 RTC.
//#include 24c32 lib                                      // An AT24C32 EEPROM library.
//#include ds18b20 lib                                    // DS18B20 temperature sensor library.
#include "easiTM1637.h"                                   // My TM1637 based 7-segment LED display library.
//#include "easiTM1651.h"                                 // My TM1651 based 7-segment LED display library.

#define ON              HIGH
#define OFF             LOW

#define B20PIN          2                                 // DS18B20 data pin.
#define NEOPXPIN        3                                 // NeoPixel data pin.
#define CLKPIN37        4                                 // TM1637 clock pin.
#define DIOPIN37        5                                 // TM1637 data I/O pin.
#define CLKPIN51        6                                 // TM1651 clock pin.
#define DIOPIN51        7                                 // TM1651 data I/O pin.
#define UNUSED08        8                                 // Unused I/O pin.
#define UNUSED09        9                                 // Unused I/O pin.
#define UNUSED10        10                                // Unused I/O pin.
#define UNUSED11        11                                // Unused I/O pin.
#define UNUSED12        12                                // Unused I/O pin.
#define LEDPIN          13                                // The builtin LED.

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS       24
// How bright do we want the NeoPixel LED colours?
#define REDBRIGHT       25
#define GRNBRIGHT       25
#define BLUBRIGHT       20
// The number of digits in the TM1637 based LED display.
#define NUMDGTS37       6

// Instantiate a NeoPixel ring object.
Adafruit_NeoPixel myPixels(NUMPIXELS, NEOPXPIN, NEO_GRB + NEO_KHZ800);
// Instantiate a TM1637 display object.
TM1637 myDisplay37(CLKPIN37, DIOPIN37);

// Time (in milliseconds) to pause between pixels in the infinity symbol loop.
#define INFINITYDELAY   50
// Infinity physical pixel display order.
#define INFINITYPIXELS  26
const byte ledOrderInfinity[] = {3, 4, 5, 6, 7, 8, 9, 15, 14, 13 ,12, 23, 22, 21, 20, 19, 18, 17, 16, 15, 9, 10, 11, 0, 1, 2};

// Translate the logical pixels to the physical pixels.
const byte ledMappingL2PP[]   = {6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5};
// What logical pixels to use for the tenths.
const byte ledMappingTenths[] = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11};
// Current values of each LED for each pixel in both rings.
byte redLEDValueR1[]          = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
byte grnLEDValueR1[]          = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
byte bluLEDValueR1[]          = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
byte redLEDValueR2[]          = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
byte grnLEDValueR2[]          = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
byte bluLEDValueR2[]          = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// A table to describe the TM1637 7-segment display module physical to logical digit numbering.
byte tm37DigitMap[NUMDGTS37]  = {2, 1, 0, 5, 4, 3};

// These are updated by the ISR.
volatile byte hours   = 0;
volatile byte minutes = 0;
volatile byte seconds = 0;
volatile byte tenths  = 0;

void setup() {
  // The builtin LED is used just to indicate that the ISR is running.
  pinModeFast(LEDPIN, OUTPUT);
  digitalWriteFast(LEDPIN, OFF);
  // Initialize the NeoPixel ring object.
  myPixels.begin();
  myPixels.clear();                                       // Set all pixel colors to OFF.
  // Initialise the TM1637 display object.
  myDisplay37.begin(tm37DigitMap, NUMDGTS37);
  myDisplay37.displayClear();                             // Set all digit segments and dps OFF.

  // Infinity symbol ring test.
  infinitySymbol();
  myPixels.clear();
  // TM1637 display test - all segments and decimal points ON, then restored.
  myDisplay37.displayTest(true);
  delay(1000);
  myDisplay37.displayTest(false);

  // Set up the 16-bit hardware timer interrupt.
  Timer1.initialize(100000);                              // Interrupt timer value in microseconds.
  Timer1.attachInterrupt(myCounter);                      // The ISR to run every 0.1 seconds.
}

void loop() {
  byte gapCounter, pixelCounter;
  // Previous values - so that the updaters run only if they have changed.
  static byte lastHour   = 23;
  static byte lastMinute = 59;
  static byte lastSecond = 59;
  static byte lastTenth  = 9;
  // Update the hours.
  if (lastHour != hours) {
    if(hours < 12) {
      redLEDValueR1[hours] = REDBRIGHT;                   // AM = red LEDs used.
    }
    else {
      grnLEDValueR1[hours - 12] = GRNBRIGHT;              // PM = green LEDs used.
    }
    if(lastHour < 12) {
      redLEDValueR1[lastHour] = 0;
    }
    else {
      grnLEDValueR1[lastHour - 12] = 0;
    }
    myDisplay37.displayInt8(0, hours);
    lastHour = hours;                                     // Update the hours for the next time round the loop.
  }
  // Update the minutes.
  if(lastMinute != minutes) {
    gapCounter = minutes % 5;
    redLEDValueR2[((minutes / 5) + 1) % 12] = gapCounter;
    if(lastMinute / 5 != minutes / 5) {
      redLEDValueR2[minutes / 5] = REDBRIGHT;
      redLEDValueR2[lastMinute / 5] = 0;
    }
    myDisplay37.displayInt8(2, minutes);
    lastMinute = minutes;                                 // Update the minutes for the next time round the loop.
  }
  // Update the seconds.
  if(lastSecond != seconds) {
    gapCounter = seconds % 5;
    bluLEDValueR1[((seconds / 5) + 1) % 12] = gapCounter;
    if(lastSecond / 5 != seconds / 5) {
      bluLEDValueR1[seconds / 5] = BLUBRIGHT;
      bluLEDValueR1[lastSecond / 5] = 0;
    }
    myDisplay37.displayInt8(4, seconds);
    lastSecond = seconds;                                 // Update the seconds for the next time round the loop.
  }
  // Update the tenths, the pixels on the rings and the 7-segment display.
  if(lastTenth != tenths) {
    bluLEDValueR2[ledMappingTenths[tenths]] = BLUBRIGHT;
    bluLEDValueR2[ledMappingTenths[lastTenth]] = 0;
    // Update the the pixels.
    for(pixelCounter = 0; pixelCounter < 12; pixelCounter++) {
      myPixels.setPixelColor(ledMappingL2PP[pixelCounter], myPixels.Color(redLEDValueR1[pixelCounter], grnLEDValueR1[pixelCounter], bluLEDValueR1[pixelCounter]));
      myPixels.setPixelColor(ledMappingL2PP[pixelCounter] + 12, myPixels.Color(redLEDValueR2[pixelCounter], grnLEDValueR2[pixelCounter], bluLEDValueR2[pixelCounter]));
    }
    // Send the updated pixel colours to the hardware.
    myPixels.show();
    // Turn the TM1637 dps (digits 1 and 3) ON/OFF.
    if(tenths == 0) {
      myDisplay37.displayDP(1, ON);
      myDisplay37.displayDP(3, ON);
    }
    if(tenths == 5) {
      myDisplay37.displayDP(1, OFF);
      myDisplay37.displayDP(3, OFF);
    }
    // Update the tenths for the next time round the loop.
    lastTenth = tenths;
  }
}

// Draw an Infinity symbol, working through 8 colours in each ring.
void infinitySymbol(void) {
  byte red, green, blue, pixel;
  for(red = 0; red < 2; red++) {
    for(green = 0; green < 2; green++) {
      for(blue = 0; blue < 2; blue++){
        if(red != 0 || green != 0 || blue != 0) {
          for(pixel = 0; pixel < INFINITYPIXELS; pixel++) {
            // Update the the pixels.
            myPixels.setPixelColor(ledOrderInfinity[pixel], myPixels.Color(red * REDBRIGHT, green * GRNBRIGHT, blue * BLUBRIGHT));
            // Send the updated pixel colours to the hardware.
            myPixels.show();
            // Pause before next pass through loop.
            if(pixel == 9 || pixel == 15) {
              delay(INFINITYDELAY / 2);                   // Crossover LEDs timed as a pair to be the same as other LEDs.
            }
            else {
              delay(INFINITYDELAY);
            }
          }
        }
      }
    }
  }
}

// ISR: Tenths, second, minutes and hours counter, and LED toggle.
void myCounter(void) {
  tenths++;
  if(tenths > 9) {
    tenths = 0;
  }
  if(tenths == 0) {
    seconds++;
    if(seconds > 59) {
      seconds = 0;
    }
    if(seconds == 0) {
      minutes++;
      if(minutes > 59) {
        minutes = 0;
      }
      if(minutes == 0) {
        hours++;
        if(hours > 23) {
          hours = 0;
        }
      }
    }
  }
  digitalToggleFast(LEDPIN);
}

// EOF