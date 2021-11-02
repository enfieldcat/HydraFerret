/*
MIT License

Copyright (c) [2021] [Enfield Cat]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#define IP_FORWARD 1

// Include of other files

#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <esp32-hal-cpu.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <string.h>
#include <HardwareSerial.h>
#include <time.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <Wire.h>
#include <OneWire.h>
#include <Adafruit_VEML6075.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


/*
 * Version is:
 *   Major release - major review of working or principle of operation, change of hardware architecture
 *   Minor release - new functionality
 *   Patch release - bug fixes
 *   Use build date as build number
 */
#define PROJECT_NAME "HydraFerret"
#define VERSION "21.10"

// Name of Console log file
#define CONSOLELOG "/console.log"

// Name of default certificate file
#define CERTFILE "/rootCACertificate"

/* You only need to format SPIFFS the first time you run a
   test or else use the SPIFFS plugin to create a partition
   https://github.com/me-no-dev/arduino-esp32fs-plugin */
#define FORMAT_SPIFFS_IF_FAILED true

// Maximum one wire busses to support
#define MAX_ONEWIRE 4

// The interval in miliseconds between initiating each device type
// The idea is to provide some separation between sensors accessing the I2C
// busses to avoid contention, so it should not be a simple multiple where
// overlap is likely to happen.
#define INIT_DELAY 38

// Default monitoring interval
// NB some devices such as CO2 measurement should not run less frequently
//    than once per second
#define DEFAULT_INTERVAL 3

// Divisor for converting uSeconds to Seconds
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

// Default IP address to use for Access Point
#define AP_IP_ADDRESS "192.168.200.65"

// Number of concurrent telnet clients
#define MAX_TELNET_CLIENTS 5

// Uncomment next definition to enable Bluetooth support / Comment out to disable
// NB: this will increase the amount of memory used by HydraFerret!
//#define USE_BLUETOOTH 1

// Max size of LIFO stack used by RPN calculator
#define LIFO_SIZE 10

// Maximum Xymon data packet size
#define MAX_XYMON_DATA 4098

// Console command history size
#define COMMAND_HISTORY 1024

// Maximum number of interrupt counters to enable
#define MAX_COUNTER 8

// Std xymon cycle is 300 seconds (5 minutes)
#define MONITOR_INTERVAL 300000

// How long can device type names be?
#define DEVTYPESIZE 11

// How long can sensor device labels be
#define SENSOR_NAME_LEN 17

// Maximum number of outputs
#define MAX_OUTPUT 10

// Maximum number of switches
#define MAX_SWITCH 10

// Size of input buffer
#define BUFFSIZE 128

// Maximum number of tokens expected on any input line
#define MAXPARAMS 32

// Maximum number of NMEA fields to process
#define NMEAMAXFIELD 24

// Maximum number of satellites tracked
#define NMEA_MAX_SATS 16

// Default resistance in milli-ohms for volatge measurements
#define DEFAULT_RESISTOR 100

// Screen definitions
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// enum for testType
enum testType {TEMP, HUMID, PRES, LUX, UV, CO2, TVOC, DIST, COUN, VOLT, AMP, WATT};

// enum for warning levels
enum warningLevel {GREEN=0, YELLOW=1, RED=2, PURPLE=3, BLUE=4, CLEAR=5};

// enum for identity
enum id_state { ID_OFF, ID_ON, ID_FLASH, ID_FFLASH, ID_SFLASH };


// Timzone descriptors
struct timezone_s {
  char shortname[11];
  char description[50];
};

// structure to hold interrupt counter data
struct int_counter_s {
  struct rpnLogic_s *alert[3];
  struct rpnLogic_s *xfrmLogic;
  SemaphoreHandle_t mux;
  float offsetval;
  float multiplier;
  float transform;
  uint32_t accumulator;
  uint32_t current;
  uint32_t previous;
  uint8_t state;
  char uniquename[17];
  char uom[17];
  char xfrmName[SENSOR_NAME_LEN];
  uint8_t pin;
  bool pending;
};

struct adc_s {
  struct rpnLogic_s *alert[3];
  struct rpnLogic_s *xfrmLogic;
  float offsetval;
  float multiplier;
  float transform;
  uint32_t accumulator;
  uint16_t average;
  uint16_t lastVal;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint8_t state;
  uint8_t attenuation;
  char uniquename[17];
  char uom[17];
  char xfrmName[SENSOR_NAME_LEN];
  uint8_t pin;
  bool pending;
};

// structure for holding bme280 temperature, humidity and pressure data
struct bme280_s {
  Adafruit_BME280 *bme;
  struct rpnLogic_s *alert[9];
  struct rpnLogic_s *xfrmLogic;
  float temp_accum,  temp_average,  temp_last;
  float humid_accum, humid_average, humid_last;
  float pres_accum,  pres_average,  pres_last,  uncompensatedPres;
  float altitude;
  float transform;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint8_t model;
  uint8_t state[3];
  char uniquename[SENSOR_NAME_LEN];
  char dewpointName[SENSOR_NAME_LEN];
  char xfrmName[SENSOR_NAME_LEN];
  uint8_t bus;
  uint8_t addr;
  bool isvalid;
};

// Structure for holding veml6075 UVI data
struct veml6075_s {
  Adafruit_VEML6075 *veml;
  struct rpnLogic_s *alert[3];
  struct rpnLogic_s *xfrmLogic;
  float uvi_accum, uvi_average, uvi_last;
  float uva_accum, uva_average, uva_last;
  float uvb_accum, uvb_average, uvb_last;
  float transform;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint8_t state;
  char uniquename[SENSOR_NAME_LEN];
  char xfrmName[SENSOR_NAME_LEN];
  uint8_t bus;
  uint8_t addr;
  bool isvalid;
};

// Structure for holding bh1750 light data
struct bh1750_s {
  struct rpnLogic_s *alert[3];
  struct rpnLogic_s *xfrmLogic;
  float lux_accum, lux_average, lux_last, opacity;
  float transform;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint8_t state;
  char uniquename[SENSOR_NAME_LEN];
  char xfrmName[SENSOR_NAME_LEN];
  uint8_t gear;
  uint8_t bus;
  uint8_t addr;
  bool isvalid;
};

// Structure for holding hdc1080 Temp / humidity data
struct hdc1080_s {
  struct rpnLogic_s *alert[6];
  struct rpnLogic_s *xfrmLogic;
  float temp_accum,  temp_average,  temp_last;
  float humid_accum, humid_average, humid_last;
  float transform;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint8_t state[2];
  char uniquename[SENSOR_NAME_LEN];
  char dewpointName[SENSOR_NAME_LEN];
  char xfrmName[SENSOR_NAME_LEN];
  uint8_t bus;
  uint8_t addr;
  bool isvalid;
};

// Structure for holding pfc8583 counter / distance
struct pfc8583_s {
  struct rpnLogic_s *alert[3];
  struct rpnLogic_s *xfrmLogic;
  float dist_average, dist_last, transform;
  float count_accum,  count_average, count_last;
  uint32_t ref_frequency;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint8_t compensationDevType;
  uint8_t compensationDevNr;
  uint8_t triggerPin;
  uint8_t state;
  char uniquename[SENSOR_NAME_LEN];
  char xfrmName[SENSOR_NAME_LEN];
  uint8_t bus;
  uint8_t addr;
  bool isvalid;
};

// Structure for holding css811 co2 / tvoc
struct css811_s {
  struct rpnLogic_s *alert[6];
  struct rpnLogic_s *xfrmLogic;
  float co2_accum,  co2_average,  co2_last;
  float tvoc_accum, tvoc_average, tvoc_last;
  float transform;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint8_t compensationDevType;
  uint8_t compensationDevNr;
  uint8_t lastMode;
  uint8_t errorCode;
  uint8_t state[2];
  char uniquename[SENSOR_NAME_LEN];
  char xfrmName[SENSOR_NAME_LEN];
  uint8_t bus;
  uint8_t addr;
  bool isvalid;
};

// Structure for ina2xx
struct ina2xx_s {
  struct rpnLogic_s *alert[9];
  struct rpnLogic_s *xfrmLogic;
  float volt_accum, volt_average,   volt_last;
  float shunt_accum, shunt_average, shunt_last;
  float amps_accum, amps_average,   amps_last;
  float watt_accum, watt_average,   watt_last;
  float transform;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint16_t resistor;    
  uint8_t channel;
  uint8_t state[3];
  char uniquename[SENSOR_NAME_LEN];
  char xfrmName[SENSOR_NAME_LEN];
  uint8_t modelNr;
  uint8_t bus;
  uint8_t addr;
  bool isvalid;
};

// Structure for Dallas temperature devices
struct dallasTemp_s {
  float temp_accum, temp_average, temp_last;
  float transform;
  struct rpnLogic_s *alert[3];
  struct rpnLogic_s *xfrmLogic;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint8_t address[8];
  uint8_t state;
  uint8_t bus;
  uint8_t index;
  char uniquename[SENSOR_NAME_LEN];
  char xfrmName[SENSOR_NAME_LEN];
  bool isvalid;
  bool isTypeS;
};

struct ds_address_t {
  uint8_t address[8];
  uint8_t bus;
};

// Display structures
struct sdd1306_s {
  uint8_t bus;
  uint8_t addr;
  uint8_t width;
  uint8_t height;
  bool isvalid;
  Adafruit_SSD1306 *ssd1306;
};

// serial device structure
struct zebSerial_s {
  struct rpnLogic_s *alert[3];   // rpn logic pointers
  struct rpnLogic_s *xfrmLogic;
  float transform;
  uint8_t *dataBuffer;           // buffer holding data
  uint32_t baud;                 // interface speed 
  uint16_t headPtr;              // start of data
  uint16_t tailPtr;              // end of data 
  uint16_t bufferSize;           // buffer size in bytes
  uint8_t state;                 // green, yellow or red indication
  uint8_t rx;                    // receive pin
  uint8_t tx;                    // transmit pin
  char uniquename[SENSOR_NAME_LEN];
  char devType[SENSOR_NAME_LEN]; // Type of serial device
  char xfrmName[SENSOR_NAME_LEN];
  bool isvalid;                  // valid / invalid indicator
};

struct nmea_s {
  float lati;
  float longi;
  float alt;
  float heading;
  float knots;
  float kph;
  float horzPrecision;
  uint16_t gsv[NMEA_MAX_SATS][4];
  uint8_t gsvCount;
  uint8_t msgState;
  uint8_t satCount;
  char msgBuffer[120];
  char checkBuffer[3];
  char altMeasure[2];
  char talker[3];
  char message[32];
  char dateTime[20];
  bool hasLati;
  bool hasLongi;
  bool hasAlt;
  bool hasHeading;
  bool hasKnots;
  bool hasKph;
  bool hasDateTime;
  bool hasSatCount;
  bool hasGsv;
  bool hasHorzPrecision;
};

struct pms5003_s {
  uint32_t sumData[14];
  uint16_t avgData[14];
  uint16_t lastData[14];
  uint16_t count;
  uint16_t avgOver;
  uint16_t checksum;
  int16_t  byteCount;
  uint8_t  msgBuffer[32];
};

struct winsen_s {
  uint32_t sumData;
  uint16_t avgData;
  uint16_t lastData[2];
  uint16_t count;
  uint16_t avgOver;
  uint8_t  msgBuffer[9];
  uint8_t  devType;
  uint8_t  unit;
  uint8_t  byteCount;
};

struct switch_s {
  struct   rpnLogic_s *alert[3];   // rpn logic pointers
  float    average;
  uint16_t readingCount;
  uint16_t averagedOver;
  uint16_t accumulator;
  uint8_t  pinNumber;
  uint8_t  lastVal;
  uint8_t  state;                 // green, yellow or red indication
  char     uniquename[SENSOR_NAME_LEN];
};


// Structures used for output and logic control
struct output_s {
  struct  rpnLogic_s *outputLogic;
  struct  rpnLogic_s *alert[3];
  float   result = 0.0;
  float   defaultVal = 0.0;
  uint8_t outputPin;
  uint8_t outputType;      // eg relay or pwm
  uint8_t initialised;
  uint8_t state;
  char    uniquename[SENSOR_NAME_LEN];
};

struct rpnLogic_s {
  uint8_t count;
  uint16_t size;
  char *term[3];
};


/*
 * Add prototypes for calls where required
 */
// static void generalTimerHandler (TimerHandle_t xTimer);
