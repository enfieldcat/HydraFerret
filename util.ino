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


int util_str2int(char *string)
{
  char *tptr;
  long result;
  result = strtol (string, &tptr, 10);

  return ((int) result);
}


double util_str2double (char *string)
{
  return (strtod (string, NULL));
}


float util_str2float (char *string)
{
  return ((float)(strtod (string, NULL)));
}


// Convert degrees C to F
float util_ctof (float celcius)
{
  return ((celcius * (9.00/5.00)) + 32);
}

// Convert degrees F to C
float util_ftoc (float fahrenheit)
{
  return ((fahrenheit - 32) / 1.8);
}

// Convert radians to degrees
float util_rtod (float radian)
{
  return (radian * 57.295779513);
}

// Convert degrees to radians
float util_dtor (float degree)
{
  return (degree * 0.01745329252);
}

// Return device type number
uint8_t util_get_dev_type (char *devName)
{
  uint8_t retval = 255;

  for (uint8_t n=0; n<numberOfTypes && retval==255; n++) {
    if (strcmp(devType[n], devName) == 0) retval = n;
  }
  return (retval);
}

// Return signed int16_t from high and low bytes
int16_t util_transInt (uint8_t high, uint8_t low)
{
  union {
    uint8_t chkChar[2];
    int16_t chkInt;
  } result;
  if (isBigEndian) {
    result.chkChar[0] = high;
    result.chkChar[1] = low;
  }
  else {
    result.chkChar[0] = low;
    result.chkChar[1] = high;
  }
  return (result.chkInt);
}

void util_i2c_scan()
{
  byte error;
  char buffer[6];
  
  // if (verbosity>3) consolewriteln ("i2cscan()");
  for (int n=0; n<2; n++) {
    sprintf (buffer, "%d", n);
    if (ansiTerm) displayAnsi(4);
    consolewrite   ("\n     --- i2c bus-");
    consolewrite   (buffer);
    consolewriteln (" --------------------------------");
    if (I2C_enabled[n]) {
      consolewriteln ("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
      consolewrite   ("00:         ");
      if (ansiTerm) displayAnsi(0);
      // hold the wire semaphore for the duration of the scan
      for (int addr=3; addr< 0x78; addr++) {
        if (xSemaphoreTake(wiresemaphore[n], 30000) == pdTRUE) {
          I2C_bus[n].beginTransmission(addr);
          delay(10);
          error = I2C_bus[n].endTransmission();
          xSemaphoreGive(wiresemaphore[n]);
        } else error = 170;
        if ((addr%16) == 0) {
          consolewriteln ("");
          sprintf(buffer, "%02X:", addr);
          if (ansiTerm) displayAnsi(4);
          consolewrite (buffer);
          if (ansiTerm) displayAnsi(0);
        }
        if (error == 0) {
          sprintf(buffer, " %02X", addr);
          consolewrite(buffer);
        } else {
          if (error==170) consolewrite("ERR");
          else consolewrite(" --");
        }
      }
      consolewriteln ("");
      delay(10);
    }
  else consolewriteln ("bus is disabled");
  }
}

// return true if a device responds at an address
bool util_i2c_probe (int bus, int addr)
{
  byte error;

  if (!I2C_enabled[bus]) return false;
  if (xSemaphoreTake(wiresemaphore[bus], 30000) == pdTRUE) {
    I2C_bus[bus].beginTransmission(addr);
    error = I2C_bus[bus].endTransmission();
    xSemaphoreGive(wiresemaphore[bus]);
  }
  else error = 1;
  if (error==0) return (true);
  return (false);
}

/*
* Print the reason by which ESP32 has woken from sleep
*/
void util_print_restart_cause()
{
  esp_sleep_wakeup_cause_t wakeup_cause;
  wakeup_cause = esp_sleep_get_wakeup_cause();

  switch(wakeup_cause)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : consolewriteln ("Restart caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : consolewriteln ("Restart caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : consolewriteln ("Restart caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : consolewriteln ("Restart caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : consolewriteln ("Restart caused by ULP program"); break;
    // default : consolewriteln ("Restart was not caused by deep sleep"); break;
  }
}

/*
 * Go to sleep now
 */
void util_start_deep_sleep(int sleep_mins)
{
  if (esp_sleep_enable_timer_wakeup((uint64_t)sleep_mins * (uint64_t) (60 * uS_TO_S_FACTOR)) == ESP_OK) {
    consolewriteln ("Going to sleep now");
    delay(1000);
    Serial.flush();
#ifdef USE_BLUETOOTH
    SerialBT.flush();
#endif
    esp_deep_sleep_start();
  }
  else consolewriteln ("Failed to set deep sleep timer");
}

/*
 * Routine to check if a string contains an integer.
 */
bool util_str_isa_int (char *inString)
{
  bool retval = true;
  int  howlong = strlen(inString);
  for (int n=0; n<howlong; n++) {
    if ((inString[n]>='0' && inString[n]<='9') || (n==0 && (inString[n]=='+' || inString[n]=='-'))) {
      // it looks integer!
    }
    else retval = false;
  }
  return (retval);
}

bool util_str_isa_double (char *inString)
{
  bool retval = true;
  int  howlong = strlen(inString);
  char msgBuffer[40];
  for (int n=0; n<howlong; n++) {
    if ((inString[n]>='0' && inString[n]<='9') || inString[n]=='.' || (n==0 && (inString[n]=='+' || inString[n]=='-'))) {
      // it looks like a float!
    }
    else {
      retval = false;
      sprintf (msgBuffer, "\"%c\" is not part of float", inString[n]);
      consolewriteln (msgBuffer);
    }
  }
  return (retval);
}

/*
 * Check if string contains a time value
 */
bool util_str_isa_time (char *inString)
{
  bool retval = true;
  int howlong = strlen(inString);
  char myBuffer[6];

  if (howlong != 5 && (inString[2]!=':' || inString[2]!='h')) {
    consolewriteln ("possible string length error or missing \":\" between hour and minute");
    retval = false;
  }
  else {
    strncpy(myBuffer, inString, 6);
    myBuffer[2] = '\0';
    if (util_str_isa_int(myBuffer) && util_str_isa_int(&myBuffer[3])) {
      howlong = util_str2int(myBuffer);
      if (howlong<0 || howlong>23) {
        consolewriteln ("hours should be between 00 and 23");
        retval = false;    // hours between 00 and 23
      }
      else {
        howlong = util_str2int(&myBuffer[3]);
        if (howlong<0 || howlong>59) {
          consolewriteln ("minutes should be between 00 and 59");
          retval = false;  // minutes between 00 and 59       
        }
      }
    }
    else {
      if (!util_str_isa_int(myBuffer)) {
        consolewrite ("non-integer hour: ");
        consolewriteln (myBuffer);
      }
      if (!util_str_isa_int(&myBuffer[3])) {
        consolewrite ("non-integer minutes: ");
        consolewriteln (&myBuffer[3]);
      }
      retval = false;
    }
  }
  return (retval);
}

/*
 * Print time string
 */
char* util_gettime()
{
  const char monthname[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const char downame[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static char timestring[40];
  struct tm timeinfo;
  if (strcmp (ntp_server, "none") != 0 && getLocalTime(&timeinfo)){
  //    strcpy (timestring, "could not get localtime value"); 
  //  }
  //  else {
      sprintf (timestring, "%s %02d-%s-%d %02d:%02d:%02d", downame[timeinfo.tm_wday],
             timeinfo.tm_mday, monthname[timeinfo.tm_mon], timeinfo.tm_year+1900,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec); 
  // }
  }
  else {
    float uptime = esp_timer_get_time() / (uS_TO_S_FACTOR * 60.0);
    uint32_t hours = floor (uptime / 60.0);
    uint32_t days  = hours / 24;
    uint8_t  mins  = fmod  (uptime,  60.0);
    hours = hours - (days * 24);
    sprintf (timestring, "%d days, %d hours, %d mins", days, hours, mins);
  }
  return (timestring);
}

char* util_getDate()
{
  const char monthname[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  static char timestring[20];
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    strcpy (timestring, "No localtime value"); 
  }
  else {
    sprintf (timestring, "%02d-%s-%02d", timeinfo.tm_mday, monthname[timeinfo.tm_mon], timeinfo.tm_year-100);
  }
  return (timestring);
}

char* util_getMinute()
{
  static char timestring[6];
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    strcpy (timestring, "Error"); 
  }
  else {
    sprintf (timestring, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min); 
  }
  return (timestring);
}


/*
 * Show system infomation
 */
void util_show_system_id()
{
  char msgBuffer[100];
  uint64_t chipid;
  // uint32_t uptime;
  int64_t uptime;
  int days, hours, mins, secs;

  uptime = esp_timer_get_time() / uS_TO_S_FACTOR;
  // uptime = ESP.getCycleCount() / 100;
  days = uptime / (60*60*24);
  uptime = uptime - (days * (60*60*24));
  hours = uptime / (60*60);
  uptime = uptime - (hours *(60*60));
  mins = uptime / 60;
  secs = uptime - (mins * 60);
  sprintf (msgBuffer, " * Uptime: %d days, %02d:%02d:%02d", days, hours, mins, secs);
  consolewriteln (msgBuffer);
  sprintf (msgBuffer, " * Processor chip revision %d, Speed %dMHz, Xtal Freq: %dMHz", ESP.getChipRevision(), ESP.getCpuFreqMHz(), getXtalFrequencyMhz());
  consolewriteln (msgBuffer);
  chipid = ESP.getEfuseMac();  //The chip ID is essentially its MAC address(length: 6 bytes).
  sprintf (msgBuffer, " * ESP32 Chip ID / MAC address: %02X:%02X:%02X:%02X:%02X:%02X",(uint8_t)chipid, (uint8_t)(chipid>>8), (uint8_t)(chipid>>16), (uint8_t)(chipid>>24), (uint8_t)(chipid>>32), (uint8_t)(chipid>>40));
  consolewriteln (msgBuffer);
  sprintf (msgBuffer, " * Free Non Volatile Storage (NVS) entries: %d", nvs_get_freeEntries());
  consolewriteln (msgBuffer);
  sprintf (msgBuffer, " * Free memory: %d bytes, Min free: %d bytes from %d byte heap", ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getHeapSize());
  consolewriteln(msgBuffer);
  sprintf (msgBuffer, " * Flash Size: %d, Code size: %d,Code space: %d", ESP.getFlashChipSize(), ESP.getSketchSize(), ESP.getFreeSketchSpace());
  consolewriteln(msgBuffer);
  sprintf (msgBuffer, " * %s v%s, Build date: %s %s", PROJECT_NAME, VERSION, __DATE__, __TIME__);
  consolewriteln (msgBuffer);
  sprintf (msgBuffer, " * SDK: %s, FreeRTOS: %s", ESP.getSdkVersion(), tskKERNEL_VERSION_NUMBER);
  consolewriteln (msgBuffer);
}

/*
 * Dump a block of memory to console
 */
void util_dump (char* memblk, int memsize)
{
  char message[78];
  char postfix[17];
  char tstring[4];
  int memoffset;
  int rowoffset;
  if (ansiTerm) displayAnsi(4);
  sprintf (message, "--- Address 0x%08x, Size %d bytes ", (uint32_t) memblk, memsize);
  for (rowoffset=strlen(message); rowoffset<77; rowoffset++) strcat (message, "-");
  consolewriteln (message);
  for (memoffset=0; memoffset<memsize; ) {
    if (ansiTerm) displayAnsi(4);
    sprintf (message, "0x%04x ", memoffset);
    consolewrite (message);
    if (ansiTerm) displayAnsi(0);
    message[0] = '\0';
    for (rowoffset=0; rowoffset<16 && memoffset<memsize; rowoffset++) {
      if (memblk[memoffset]>=' ' && memblk[memoffset]<='z') postfix[rowoffset] = memblk[memoffset];
      else postfix[rowoffset] = '.';
      if (rowoffset==8) strcat (message, " -");
      sprintf (tstring, " %02x", memblk[memoffset]);
      strcat  (message, tstring);
      memoffset++;
    }
    postfix[rowoffset] = '\0';
    if (rowoffset<=8) strcat (message, "  ");
    for (; rowoffset<16; rowoffset++) strcat (message, "   ");
    consolewrite   (message);
    consolewrite   ("   ");
    consolewriteln (postfix);
  }
}

/*
 * Homespun double to string function with dp decimal points
 */
char* util_dtos (double value, int dp)
{
  static char retval[32];
  //double mult;
  //int ws;
  //int64_t intPart;
  char format[15];

  if (dp<=0) sprintf(format, "%%1.0lf");
  else sprintf (format, "%%%d.%dlf", (dp+2), dp);
  sprintf (retval, format, value);
  return (retval);
}

/*
 * Homespun float to string function with dp decimal points
 */
char* util_ftos (float value, int dp)
{
  static char retval[32];
  //float mult;
  //int ws;
  //int64_t intPart;
  char format[15];

  if (dp<=0) sprintf(format, "%%1.0lf");
  else sprintf (format, "%%%d.%dlf", (dp+2), dp);
  sprintf (retval, format, value);
  return (retval);
}


/*
 * Write a command to i2c device
 */
int8_t util_i2c_command (uint8_t bus, uint8_t deviceAddr, uint8_t i2cCommand)
{
  uint8_t retVal = 0;
  if (xSemaphoreTake(wiresemaphore[bus], 30000) == pdTRUE) {
    I2C_bus[bus].beginTransmission(deviceAddr);
    I2C_bus[bus].write(i2cCommand);
    I2C_bus[bus].endTransmission();
    xSemaphoreGive(wiresemaphore[bus]);
  } else retVal = -1;
  return (retVal);  
}

/*
 * Write a byte to I2C
 */
int8_t util_i2c_write (uint8_t bus, uint8_t deviceAddr, uint8_t i2cRegister, uint8_t value)
{
  uint8_t retVal = 0;
  if (xSemaphoreTake(wiresemaphore[bus], 30000) == pdTRUE) {
    I2C_bus[bus].beginTransmission(deviceAddr);
    I2C_bus[bus].write(i2cRegister);
    I2C_bus[bus].write(value);
    I2C_bus[bus].endTransmission();
    xSemaphoreGive(wiresemaphore[bus]);
  } else retVal = -1;
  return (retVal);  
}

int8_t util_i2c_write (uint8_t bus, uint8_t deviceAddr, int count, uint8_t* value)
{
  uint8_t retVal = 0;
  if (xSemaphoreTake(wiresemaphore[bus], 30000) == pdTRUE) {
    I2C_bus[bus].beginTransmission(deviceAddr);
    for (retVal=0; retVal<count; retVal++) I2C_bus[bus].write(value[retVal]);
    I2C_bus[bus].endTransmission();
    xSemaphoreGive(wiresemaphore[bus]);
  } else retVal = -1;
  return (retVal);  
}

/*
 * Read one byte from I2C bus
 */
uint8_t util_i2c_read (uint8_t bus, uint8_t deviceAddr, uint8_t i2cRegister) {
  uint8_t retVal;
  if (xSemaphoreTake(wiresemaphore[bus], 30000) == pdTRUE) {
    I2C_bus[bus].beginTransmission(deviceAddr);
    I2C_bus[bus].write(i2cRegister);
    I2C_bus[bus].endTransmission();
    // delay(5);  // arbitarary allowance for clock stretching
    I2C_bus[bus].requestFrom(deviceAddr, (uint8_t) 1);
    if (I2C_bus[bus].available()) {
      retVal = I2C_bus[bus].read();
    }
    //while (I2C_bus[bus].available()) {
    //  I2C_bus[bus].read();
    //}
    xSemaphoreGive(wiresemaphore[bus]);
  } else retVal = 0;
  return (retVal);  
}

uint8_t util_i2c_read (uint8_t bus, uint8_t deviceAddr) {
  uint8_t retVal;
  if (xSemaphoreTake(wiresemaphore[bus], 30000) == pdTRUE) {
    /* I2C_bus[bus].beginTransmission(deviceAddr);
    I2C_bus[bus].endTransmission(); */
    I2C_bus[bus].requestFrom(deviceAddr, (uint8_t) 1);
    if (I2C_bus[bus].available()) {
      retVal = I2C_bus[bus].read();
    }
    //while (I2C_bus[bus].available()) {
    //  I2C_bus[bus].read();
    //}
    xSemaphoreGive(wiresemaphore[bus]);
  } else retVal = 0;
  return (retVal);  
}

/*
 * Read multiple bytes from I2C bus
 */
int util_i2c_read (uint8_t bus, uint8_t deviceAddr, uint8_t i2cRegister, uint8_t count, uint8_t* results)
{
  int retCount = 0;

  if (xSemaphoreTake(wiresemaphore[bus], 30000) == pdTRUE) {
    I2C_bus[bus].beginTransmission(deviceAddr);
    I2C_bus[bus].write(i2cRegister);
    I2C_bus[bus].endTransmission();
    //delay(5);  // arbitarary allowance for clock stretching
    I2C_bus[bus].requestFrom(deviceAddr, count);
    while (retCount < count && I2C_bus[bus].available()) {
      results[retCount++] = I2C_bus[bus].read();
    }
    while (I2C_bus[bus].available()) {
      I2C_bus[bus].read();
    }
    xSemaphoreGive(wiresemaphore[bus]);
  } else retCount = -1;
  return (retCount);
}

int util_i2c_read (uint8_t bus, uint8_t deviceAddr, uint8_t count, uint8_t* results)
{
  int retCount = 0;

  if (xSemaphoreTake(wiresemaphore[bus], 30000) == pdTRUE) {
    /* I2C_bus[bus].beginTransmission(deviceAddr);
    I2C_bus[bus].endTransmission();
    xSemaphoreGive(wiresemaphore[bus]); */
    I2C_bus[bus].requestFrom(deviceAddr, count);
    while (retCount < count && I2C_bus[bus].available()) {
      results[retCount++] = I2C_bus[bus].read();
    }
    while (I2C_bus[bus].available()) {
      I2C_bus[bus].read();
    }
    xSemaphoreGive(wiresemaphore[bus]);
  } else retCount = -1;
  return (retCount);
}

/*
 * Simple dewpoint calculation
 * Using Magnus formula from http://www.ti.com/lit/an/snaa216/snaa216.pdf
 */
float util_dewpoint (float temp, float humidity)
{
  // The constants should cover a range -45C to +60C
  const float beta = 17.62;
  const float lambda = 243.12;
  float daktar;
  float retval;

  daktar = (log (humidity/100.0)) + ((beta*temp)/(lambda+temp));
  return ((lambda * daktar) / (beta - daktar));
  // return (temp - ((100.00 - humidity)/5.00)); 
}

/*
 * Simple pressure adj for altitude
 */
float util_compensatePressure (float actualPressure, float altitude)
{
  if (altitude==0.00) return (actualPressure);
  return (actualPressure / (pow((1-(altitude/44330.00)),5.255)));
}

// https://keisan.casio.com/exec/system/1224575267
float util_compensatePressure (float actualPressure, float altitude, float temperature)
{
  float altAdj = altitude * 0.0065;
  return (actualPressure * pow((1 - (altAdj / (273.15 + temperature + altAdj))), -5.257));
}

/*
 * Return altitude give sea-level (QNH), measured pressure and temperature
 * https://keisan.casio.com/exec/system/1224585971
 */
float util_calcAltitude (float actualPressure, float seaLevelPressure, float temperature)
{
  float tempAdj = temperature + 273.15;
  float presAdj = (pow((seaLevelPressure / actualPressure), (1/5.257))) -1;
  return ((tempAdj * presAdj) / 0.0065);
}

/*
 * Calculate the speed of sound based on temperature
 * See: https://en.wikipedia.org/wiki/Speed_of_sound
 */
float util_speedOfSound (float temperature)
{
  return (331.3 + (0.606 * temperature));
}

// Compared to 0% humidity at 20C, at 100% speed is expected to be about 1.5m/s faster.
float util_speedOfSound (float temperature, float humidity)
{
  return (331.3 + (0.606 * temperature) + (humidity * 0.015));  
}

/*
 * get the NVS string indexName and break it into an rpnLogic_s structure pointed to by logicPtr
 * for use by outputs, alerts and derived values
 */
void util_getLogic (char* indexName, struct rpnLogic_s **logicPtr)
{
  char logicBuffer[BUFFSIZE];
  char spaceCount = 1;
  char logicLength = 0;
  int  n, i;
  struct rpnLogic_s *myStruct;
  char *termOffset;
 
  // load this outputs control logic
  nvs_get_string(indexName, logicBuffer, "", sizeof(logicBuffer));
  logicLength = strlen(logicBuffer);
  // Count the number of terms it contains
  for (n=0; n<logicLength; n++) if (logicBuffer[n]==' ' || logicBuffer[n]=='\t') spaceCount++;
  // Allocate memory to hold logic + pointer to each term + count of terms
  n = (2 * sizeof(uint16_t)) + (spaceCount * sizeof(char*)) + logicLength + 1;
//  *logicPtr = (struct rpnLogic_s*) malloc(n);
//  termOffset = (char*) (*logicPtr + (2*sizeof(uint16_t)) + (spaceCount * sizeof(char*)));
  myStruct = (struct rpnLogic_s*) malloc(n);
  *logicPtr = myStruct;
  termOffset = (char*) ((char *) myStruct + (2*sizeof(uint16_t)) + (spaceCount * sizeof(char*)));
  // Populate pointer table, split terms into \0 terminated string array and put logic in buffer
  myStruct->count = spaceCount;
  myStruct->size  = n;   // this is the size we requested from malloc - the volume of data to be dumped by dump
  myStruct->term[0] = termOffset;
  for (n=0, i=1; n<logicLength && i<spaceCount; n++) if (logicBuffer[n]==' ' || logicBuffer[n]=='\t') {
    logicBuffer[n] = '\0';
    myStruct->term[i++] = termOffset + sizeof(char) + n;
  }
  for (n=0; n<=logicLength; n++) {
    termOffset[n] = logicBuffer[n];
  }
}

/*
 * Append rpnlogic text to xydata
 */
void util_getLogicText (struct rpnLogic_s *logicPtr, char *xydata)
{
  uint8_t numTerms = logicPtr->count;

  for (uint8_t loopCnt=0; loopCnt<numTerms; loopCnt++) {
    strcat (xydata, " ");
    strcat (xydata, logicPtr->term[loopCnt]);
  }
}

void util_getLogicTextXymon (struct rpnLogic_s *logicPtr, char *xydata, uint8_t state, char *deviceName)
{
  char msgBuffer[40];
  uint8_t logicNum;

  if (showLogic && state != GREEN) {
    switch (state) {
      case YELLOW: logicNum = 0; break;
      case RED:    logicNum = 1; break;
      case PURPLE: logicNum = 2; break;
      default:     logicNum = 0; break;
    }
    sprintf (msgBuffer, " &%s %-16s", xymonColour[state], deviceName);
    strcat (xydata, msgBuffer);
    util_getLogicText (logicPtr, xydata);
    strcat (xydata, "\n");
  }
}

// Make up a http agent name
const char* util_getAgent()
{
  static char agent[40] = {""};
  
  if (agent[0] == '\0') {
    strcat (agent, PROJECT_NAME);
    strcat (agent, " - ");
    strcat (agent, VERSION);
    strcat (agent, " (Argon)");
    }
  return (agent);
}

// directory listing of SPiffs filesystem
// cf: https://github.com/espressif/arduino-esp32/blob/master/libraries/SPIFFS/examples/SPIFFS_Test/SPIFFS_Test.ino
void util_listDir(fs::FS &fs, const char * dirname, uint8_t levels){
  char msgBuffer[80];
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if(!root){
    consolewriteln ("- failed to open directory");
    return;
  }
  if(!root.isDirectory()){
    consolewriteln(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while(file){
    for (uint8_t space=0; space<levels; space++) consolewrite ("  ");
    if(file.isDirectory()){
      consolewrite  ("  DIR : ");
      consolewriteln((char*)file.name());
      util_listDir(fs, file.name(), levels +1);
    } 
    else {
      consolewrite  ("  FILE: ");
      consolewrite  ((char*)file.name());
      sprintf (msgBuffer, "%d", (uint)file.size());
      consolewrite  ("\tSIZE: ");
      consolewriteln(msgBuffer);
    }
    file = root.openNextFile();
  }
  sprintf (msgBuffer, "%d bytes used of %d available (%d%% used)", SPIFFS.usedBytes(), SPIFFS.totalBytes(), (SPIFFS.usedBytes()*100)/SPIFFS.totalBytes());
  consolewriteln (msgBuffer);
}

void util_deleteFile(fs::FS &fs, const char * path){
  if (!fs.exists(path)) {
    consolewriteln ("  - File does not exist");
    return;
  }
  consolewrite   ("Deleting file: ");
  consolewriteln ((char*) path);
  if(fs.remove(path)){
    consolewriteln("  - file deleted");
  } else {
    consolewriteln("  - delete failed");
  }
}

char* util_loadFile(fs::FS &fs, const char* path)
{
  util_loadFile (fs, path, NULL);
}


char* util_loadFile(fs::FS &fs, const char* path, int* sizeOfFile)
{
  char *retval = NULL;
  int fileSize = 0;
  int ptr = 0;

  if (sizeOfFile!=NULL) *sizeOfFile = 0;
  if (!fs.exists(path)) {
    consolewriteln ("  - File does not exist");
    return (retval);
  }
  File file = fs.open(path);
  if(!file){
    consolewriteln("  - failed to open file for reading");
    return (retval);
  }
  if(file.isDirectory()){
    consolewriteln("  - Cannot open directory for reading");
    return (retval);
  }
  fileSize = file.size() + 1;
  retval = (char*) malloc (fileSize);
  if (retval != NULL) {
    while(file.available() && ptr<fileSize){
      retval[ptr++] = file.read();
    }
    file.close();
    retval[fileSize-1] = '\0';  // Ensure data ends with a null terminator character
  }
  if (retval!=NULL && sizeOfFile!=NULL) *sizeOfFile = fileSize;

  return (retval);
}


void util_readFile(fs::FS &fs, const char * path) {
  uint8_t inChar;

  consolewrite   ("Reading file: ");
  consolewriteln ((char*) path);

  if (!fs.exists(path)) {
    consolewriteln ("  - File does not exist");
    return;
  }
  File file = fs.open(path);
  if(!file){
    consolewriteln("  - failed to open file for reading");
    return;
  }
  if(file.isDirectory()){
    consolewriteln("  - Cannot open directory for reading");
    return;
  }

  consolewriteln ("--- read from file -----------------------");
  consolewriteln ("");
  while(file.available()){
    inChar = (uint8_t) file.read();
    if (inChar == '\n') consolewrite('\r');
    consolewrite(inChar);
  }
  file.close();
  consolewriteln ("");
  consolewriteln ("--- end of file --------------------------");
}

void util_writeFile (fs::FS &fs, const char * path)
{
  consolewrite   ("Writing file: ");
  consolewrite   ((char*) path);
  consolewriteln ("  -  Use \".\" on a line of its own to stop writing.");

  if (fs.exists(path)) fs.remove(path);
  writeFile = fs.open(path, FILE_WRITE);
  if(!writeFile){
    Serial.println("  - failed to open file for writing");
  }
  else writingFile = true;
}

void util_closeWriteFile()
{
  writeFile.close();
}

void util_appendWriteFile (char* content)
{
  writeFile.print(content);
}

void util_format_spiffs()
{
  SPIFFS.format();
}

/*
 * Create timers for discovered devices
 */
void util_deviceTimerCreate(uint8_t n)
{
  static uint32_t default_interval;
  char msgBuffer[SENSOR_NAME_LEN];
  
  if (n==0) default_interval = 300000;  // counter should only read for the full period
  else {
    sprintf (msgBuffer, "defaultPoll_%d", n);
    default_interval = nvs_get_int (msgBuffer, default_interval) * 1000;
  }
  xTimerStart (devTypeTimer[n], pdMS_TO_TICKS(default_interval));
}


static void util_generalTimerHandler (TimerHandle_t xTimer)
{
uint8_t *tchar;

tchar = ((uint8_t*) pvTimerGetTimerID(xTimer));
// xQueueSend (QHandle, Items_to_send, ticks_to_wait)
xQueueSend (devTypeQueue[tchar[0]], tchar, 0);
}


/*
 * Start devices with delays betwen each start
 */
void util_start_devices()
{
  the_sdd1306.begin();
  delay (INIT_DELAY);
  the_wire.begin();
  delay (INIT_DELAY);
  theCounter.begin();
  delay (INIT_DELAY);
  the_serial.begin();
  delay (INIT_DELAY);
  the_adc.begin();
  delay (INIT_DELAY);
  the_switch.begin();
  delay (INIT_DELAY);
  the_bme280.begin();
  delay (INIT_DELAY);
  the_hdc1080.begin();
  delay (INIT_DELAY);
  the_veml6075.begin();
  delay (INIT_DELAY);
  the_bh1750.begin();
  delay (INIT_DELAY);
  the_css811.begin();
  delay (INIT_DELAY);
  the_ina2xx.begin();
  delay (INIT_DELAY);
  the_output.begin();
}
