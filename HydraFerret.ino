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


/*
 * This is the main entry point and initialisation for HydraFerret
 *   - Hydra: many arms reaching in to things
 *   - Ferret: Good at sniffing out stuff
 */

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

/*
 * These are the include files, they only need to be specified once for Arduino IDE
 */
#include "envGeneral.h"
#ifdef USE_BLUETOOTH
#include <BluetoothSerial.h>
#endif


/*
 * Global variables
 */
const  char devType[][DEVTYPESIZE] = {"counter", "adc", "bh1750", "bme280", "css811", "ds1820", "hdc1080", "ina2xx", "pfc8583", "veml6075", "output", "serial", "switch", "sdd1306"};
const  char devTypeID[sizeof(devType)/DEVTYPESIZE] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };   // Used for timer IDs, we want const pointers to these
const int numberOfTypes = sizeof(devType)/DEVTYPESIZE;
static TwoWire I2C_bus[2] = {TwoWire(0), TwoWire(1)};
static OneWire *one_bus[MAX_ONEWIRE];
static HardwareSerial serial_dev[2] = {HardwareSerial(1), HardwareSerial(2)};
static bool devRestartable[sizeof(devType)/DEVTYPESIZE];
static bool I2C_enabled[2] = { false, false };
static SemaphoreHandle_t wiresemaphore[2] = { NULL, NULL };
static char device_name[SENSOR_NAME_LEN];
static char wifi_ssid[4][33];
static char wifi_passwd[4][33];
static char wifimode = 0;
static char devTypeCount[sizeof(devType)/DEVTYPESIZE];
static SemaphoreHandle_t devTypeSem[sizeof(devType)/DEVTYPESIZE];
static TimerHandle_t   devTypeTimer[sizeof(devType)/DEVTYPESIZE];
static QueueHandle_t   devTypeQueue[sizeof(devType)/DEVTYPESIZE];
static double unit_altitude = 0.0;
static char ntp_server[64];
static uint8_t debugLevel = 0;
// Pointers to device data arrays allocated using malloc
static void *devData[sizeof(devType)/DEVTYPESIZE];
// Warning colours
static const char xymonColour[][7] = { "green", "yellow", "red", "purple", "blue", "clear" };
// alert labels
static const char alertLabel[][9] = {"warning", "critical", "extreme" };
// list of metric and their count of sensors
// enum testType {TEMP, HUMID, PRES, LUX, UV, CO2, TVOC, DIST, COUN, VOLT, AMP, WATT};
static const char metricName[][16] = { "temperature", "humidity", "pressure", "lux", "uv", "co2", "tvoc", "distance", "count", "volts", "amps", "watts"};
static int metricCount[WATT + 1];
static WiFiServer telnetServer(23);
static WiFiClient telnetServerClients[MAX_TELNET_CLIENTS];
static bool configHasChanged = false;
static bool telnetRunning = false;
static bool showMemory = false;
static bool showOutput = false;
static bool showLogic  = false;
static bool isBigEndian= false;
static bool spiffsAvailable = false;
static bool ap_is_started = false;
// static bool disable_brownout = false;
static uint16_t networkUserCount = 0;
// Output control
static struct output_s outputCtrl [MAX_OUTPUT];
static char mt_identity_state = ID_FFLASH;
const char outputDescriptor[][6] = {"relay", "pwm", "servo", "var"};
  // Serial devices
const char acceptProto[][4]     = {"7n1",      "7n2",      "7e1",      "7e2",      "7o1",      "7o2",      "8n1",      "8n2",      "8e1",      "8e2",      "8o1",       "8o2"};
const uint32_t implementProto[]  = {SERIAL_7N1, SERIAL_7N2, SERIAL_7E1, SERIAL_7E2, SERIAL_7O1, SERIAL_7O2, SERIAL_8N1, SERIAL_8N2, SERIAL_8E1, SERIAL_8E2, SERIAL_8O1, SERIAL_8O2};
// ADC devices
const static adc_attenuation_t adcAttenuation[] = {ADC_0db, ADC_2_5db, ADC_6db, ADC_11db};
const static float adcFsd[] = {0.8, 1.1, 1.35, 2.6};  //FSD of ADC in volts for each of the above modes
// Current sense monitors, put popular ina219 last as it does not have device ID register
const char     inaName[][8]      = { "ina226"   , "ina3221"    , "ina219"};     // Current sense devices
const uint8_t  inaInputCnt[]     = { 1          ,  3           , 1       };     // Count of inputs per chip
const uint8_t  inaShuntShift[]   = { 0          ,  3           , 0       };     // Right shift shunt voltage
const uint8_t  inaBusShift[]     = { 0          ,  3           , 3       };     // Right shift bus voltage register
const uint8_t  inaSignature[][2] = {{0x22, 0x60}, {0x32, 0x20} , {0x39, 0x9f}}; // signature used to identify device type
const uint8_t  inaSigAddr[]      = { 0xff       ,  0xff        , 0x00    };     // address of device signature
const uint8_t  inaMaxAddr[]      = { 0x4f       ,  0x44        , 0x4f    };     // top end of device address range
const uint16_t inaMaxShuntBits[] = { 32767      ,  4095        , 32767   };     // Max absolute value of shunt register
const float    inaShuntLSB[]     = { 0.00250    ,  0.040       , 0.010   };     // each bit represents this number of uV over shunt
const float    inaBusLSB[]       = { 0.00125    ,  0.008       , 0.004   };     // each bit represents this number of mV on supply bus
// Used for writing files, only expect one write at a time from multiterm console device
static fs::File writeFile;
static bool writingFile = false;
// Used for ANSI console
const  char ansiName[][10]   = { "normal",  "command", "time",      "error",    "bold" };
static char ansiString[][16] = { "0;37;40", "0;32;40", "0;33;44", "0;31;40", "0;33;40" };
static bool ansiTerm = false;


void setup() {
  int sdapin, sclpin, cpuSpeed;
  int pinNumber;
  char msgBuffer[80];
  char tBuffer[16];
  uint32_t default_interval;
  union {
    uint8_t chkChar[2];
    int16_t chkInt;
  } bigEndTest;

 /*
   * Basic initialisation required to get system going
   */
  Serial.begin (115200);  // initialise first to maximise ability to show diagnostic messages
  sprintf (msgBuffer, "\r\n\n%s starting in 5 seconds.\n", PROJECT_NAME);
  Serial.println (msgBuffer);
  bigEndTest.chkInt = 1;
  if (bigEndTest.chkChar[1] == 1) isBigEndian = true;
  delay (5000);  // delay in case frequent reboot on boot, to slow pace
  //                - If fast reboot => probably an electrical fault
  //                - If slow reboot => software or configuration issue
  Serial.println ("initialise access to non-volatile memory (nvram)");
  nvs_init();
  nvs_get_string ("device_name", device_name, PROJECT_NAME, sizeof(device_name));
  device_name[16] = '\0';
  Serial.print ("Device name set to ");
  Serial.println (device_name);
  // Also change CPU speed before starting wireless comms
  cpuSpeed = nvs_get_int ("cpuspeed", 0);
  if (cpuSpeed > 0) {
    #ifndef ENABLE_LOW_FREQ
    if (cpuSpeed < 80) cpuSpeed = 80; 
    #endif
    setCpuFrequencyMhz (cpuSpeed);
  }
  xTaskCreate(idFlashCycle,  "idFlashCycle",  2048, NULL, 4, NULL);
  for (uint8_t n=0; n<sizeof(devType)/DEVTYPESIZE; n++) devRestartable[n] = true;
  debugLevel = nvs_get_int ("debug", 0);
  for (uint8_t n=0; n<5; n++) {
    sprintf (msgBuffer, "ansi_%s", ansiName[n]);
    nvs_get_string (msgBuffer, tBuffer, ansiString[n], sizeof(tBuffer));
    strcpy (ansiString[n], tBuffer);
  }
  Serial.println ("Initialise command line access");
  multiterm_init();  // depends on NVS available to load config
  if (nvs_get_int ("memorystats",   0) > 0) showMemory = true;
  if (nvs_get_int ("displayOutput", 0) > 0) showOutput = true;
  /*
   * Print out some info about the device
   */
  util_print_restart_cause();
  util_show_system_id();
  xTaskCreate(serialConsole, "serialConsole", 8192, NULL, 4, NULL);
  if (ansiTerm) displayAnsi (4);
  consolewriteln ("Console Ready");
  consolewriteln ("Press enter to get prompt");
  if (ansiTerm) displayAnsi (1);
  /*
   * Run the various tasks we require
   * Priorities:
   *   0 - Idle task
   *   4 - Console
   *   8 - Monitor loop, and data transmission
   *  12 - data collection tasks
   */
  consolewrite ("Start up delay before searching for devices: ");
  if (nvs_get_int("startupDelay", 1) == 1) {
    consolewriteln ("60 secs");
    delay (60000);
  }
  else {
    consolewriteln ("0 secs");
  }

  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
    Serial.println("SPIFFS Mount Failed: will attempt to format and mount.");
    spiffsAvailable = false;
    delay (1000);
  }
  else spiffsAvailable = true;
  OTAcertExists(SPIFFS);
  /*
   * Create full set of device handler semaphores, timers and queues - leave timers in a stopped state
   */
  for (uint8_t n=0; n<MAX_OUTPUT; n++) outputCtrl[n].outputPin = 99;
  for (uint8_t n=0; n<(sizeof(metricCount)/sizeof(int)); n++) metricCount[n] = 0;  
  for (uint8_t n=0; n<numberOfTypes; n++) {
    devTypeCount[n]= 0;
    devTypeSem[n]   = xSemaphoreCreateMutex();
    devData[n] = NULL;
    devTypeQueue[n] = xQueueCreate (1, sizeof(uint8_t));
    xQueueSend (devTypeQueue[n], &devTypeID[n], 0);
    devTypeTimer[n] = xTimerCreate ((char*) devType[n], pdMS_TO_TICKS(1000), pdTRUE, (void*) &devTypeID[n], generalTimerHandler);
    xTimerStop(devTypeTimer[n], 1000);
  }
   /*
   * Initialise I2C busses
   */
  consolewriteln ("i2c bus initialisation:");
  for (int n=0; n<2; n++) {
    pinNumber = 0;
    #ifdef SDA
    if (n==0) pinNumber = SDA
    #endif
    sprintf (msgBuffer, "sda_%d", n);
    sdapin = nvs_get_int (msgBuffer, pinNumber);
    pinNumber = 0;
    #ifdef SCL
    if (n==0) pinNumber = SCL
    #endif
    sprintf (msgBuffer, "scl_%d", n);
    sclpin = nvs_get_int (msgBuffer, pinNumber);
    if (sdapin != sclpin) {
      sprintf (msgBuffer, "i2c_speed_%d", n);
      I2C_bus[n].begin(sdapin, sclpin, nvs_get_int(msgBuffer, 0));
      wiresemaphore[n] = xSemaphoreCreateMutex();
      I2C_enabled[n] = true;
      sprintf (msgBuffer, " * i2c bus-%d enabled, pins: SDA=%d, SCL=%d", n, sdapin, sclpin);
      consolewriteln (msgBuffer);
    }
    else {
      sprintf (msgBuffer, " * i2c bus-%d disabled", n);
      consolewriteln (msgBuffer);
    }
  }
  /*
   * Display wifi multi settings
   */
  if (nvs_get_int ("wifi_ap", 1) == 1) {
    networkUserCount++;
    net_start_ap();
  }
  wifimode = nvs_get_int ("wifimode", 0);
  consolewriteln ("Test WiFi connectivity");
  networkUserCount++;
  if (!net_connect()) consolewriteln (" * Failed to connect to network"); 
  else {
    if (nvs_get_int ("otaonboot", 0) > 0) {
      consolewriteln ("Checking for over the air (ota) update.");
      OTAcheck4update();
    }
  }
  networkUserCount--;
  net_disconnect();
  xTaskCreate(monitorCycle,  "monitorCycle",  8192, NULL, 8, NULL);
  consolewriteln ((const char*) "\r\nProcesses started, system ready.\r\nType \"help\" to show commands or \"config\" to show configuration.");
  // while (true) sleep (1000);
}

void loop() {
  // Empty. Things are done in Tasks. The loop just needs to stop
  vTaskDelete( NULL );
}


static void generalTimerHandler (TimerHandle_t xTimer)
{
uint8_t *tchar;

tchar = ((uint8_t*) pvTimerGetTimerID(xTimer));
// xQueueSend (QHandle, Items_to_send, ticks_to_wait)
xQueueSend (devTypeQueue[tchar[0]], tchar, 0);
}
