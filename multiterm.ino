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
 * Console related stuff goes here
 * 
 * Global variable prefix: mt_ --> MultiTerminal
 */
#ifndef LED_BUILTIN
#define LED_BUILTIN 99
#endif

#include <nvs_flash.h>

static char* mt_cmdHistory = NULL;  // ring buffer for command history
static int mt_cmdHistoryStart = 0;  // First (oldest) byte in command history
static int mt_cmdHistoryEnd = 0;    // Next (newest) byte in command history
static int mt_cmdHistoryReq = 0 ;   // requested command relative to latest in history. 0 = none requested, 1 = previous, 2 = one prior, etc
#ifdef USE_BLUETOOTH
static char mt_bt_pin[10];
static BluetoothSerial SerialBT;
#endif
static char mt_password[17];
static char mt_identity_pin = LED_BUILTIN;
static char mt_identity_mode = 0;
static SemaphoreHandle_t consoleSemaphore;
static bool mt_bt_password_rqd = false;
static bool mt_bt_enabled = false;
static bool runMultiTerm = true;
static bool consolelog = false;
static uint8_t termID = 250;
const static struct timezone_s mytz_table[35] = {
    {"UTC",        "UTC0"},
    {"UK",         "GMT0BST,M3.5.0/01,M10.5.0/02"},
    {"EU",         "WEST-1WEDT-2,M3.5.0/02:00:00,M10.5.0/03:00:00"},
    {"MOSCOW",     "MSK-3"},
    {"ESTONIA",    "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"GAZA",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"IL",         "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"IRAN",       "<+0330>-3:30<+0430>,J79/24,J263/24"},
    {"INDIA",      "UTC-05:30"},
    {"KATHMANDU",  "<+0545>-5:45"},
    {"JAKARTA",    "WIB-7"},
    {"KOREA",      "KST-9"},
    {"JAPAN",      "JST-9"},
    {"ANTIGUA",    "AST4"},
    {"CUBA",       "CST5CDT,M3.2.0/0,M11.1.0/1"},
    {"EASTERN",    "EST+5EDT,M3.2.0/2,M11.1.9/2"},
    {"CENTRAL",    "CST6CDT,M3.2.0,M11.1.0"},
    {"MOUNTAIN",   "MST7MDT,M3.2.0,M11.1.0"},
    {"PACIFIC",    "PST8PDT,M3.2.0,M11.1.0"},
    {"ALASKA",     "AKST9AKDT,M3.2.0,M11.1.0"},
    {"WESTAUST",   "AWST-8"},
    {"SOUTHAUST",  "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"NORTHTERR",  "ACST-9:30"},
    {"QUEENSLAND", "AEST-10"},
    {"NSW",        "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"NZ",         "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"FIJI",       "<+12>-12<+13>,M11.2.0,M1.2.3/99"},
    {"EASTER",     "<-06>6<-05>,M9.1.6/22,M4.1.6/22"},
    {"APIA",       "<+13>-13<+14>,M9.5.0/3,M4.1.0/4"},
    {"NORFOLK",    "<+11>-11<+12>,M10.1.0,M4.1.0/3"},
    {"WESTAFRICA", "WAT-01:00"},
    {"CENAFRICA",  "CAT-02:00"},
    {"EASTAFRICA", "EAT-03:00"},
    {"KE",         "EAT-03:00"},
    {"ZA",         "ZA-02:00"}
};
// cf: https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h

void multiterm_init()
{
  char msgBuffer[80];

  if (mt_cmdHistory == NULL) {
    mt_cmdHistory = (char*) malloc (COMMAND_HISTORY);
    }
  if (mt_identity_pin == -1) mt_identity_pin = 99;
  consoleSemaphore = xSemaphoreCreateMutex();
  nvs_get_string ("mt_password", mt_password, "MySecret", sizeof(mt_password));
  if (nvs_get_int ("ansiTerm", 0) == 0) ansiTerm = false;
  else ansiTerm = true;
#ifdef USE_BLUETOOTH
  nvs_get_string ("mt_bt_pin", mt_bt_pin, "none", sizeof(mt_bt_pin));
  if (strcmp (mt_bt_pin, "disabled") == 0) { 
    mt_bt_enabled = false;
    }
  else {
    if (Serial.availableForWrite() > 0) {
      sprintf (msgBuffer, "BlueTooth device %s", device_name);
      consolewriteln (msgBuffer);
    }
    SerialBT.register_callback(bluetooth_callback);
    if (strcmp(mt_bt_pin, "ssp") == 0) {
      consolewriteln ("Enabling Bluetooth SSP");
      SerialBT.enableSSP();
    }
    if (SerialBT.begin(device_name)) mt_bt_enabled = true;
    if (strcmp (mt_bt_pin, "none") != 0 && strcmp (mt_bt_pin, "ssp") != 0) {
      if (SerialBT.setPin(mt_bt_pin)) {
        sprintf (msgBuffer, "Bluetooth pin is %s", mt_bt_pin);
        consolewriteln (msgBuffer);
      }
      else consolewriteln ("Failed to set bluetooth pin.");
    }
  }
  if (!mt_bt_enabled) consolewriteln ("Bluetooth serial connectivity is disabled.");
#endif
}

void consolewriteNoSem (uint8_t outChar)
{
  if (Serial.availableForWrite() > 0) {
    Serial.write (outChar);
  }
#ifdef USE_BLUETOOTH
  if (mt_bt_enabled && !mt_bt_password_rqd) SerialBT.write (outChar);
#endif
  if (telnetRunning) {
    for(uint8_t i = 0; i < MAX_TELNET_CLIENTS; i++){
      if (telnetServerClients[i] && telnetServerClients[i].connected()){
        telnetServerClients[i].write(outChar);
      }
    }
  }
}

void consolewrite (uint8_t outChar)
{
  if (xSemaphoreTake(consoleSemaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
    if (Serial.availableForWrite() > 0) {
      Serial.write (outChar);
    }
#ifdef USE_BLUETOOTH
    if (mt_bt_enabled && !mt_bt_password_rqd) SerialBT.write (outChar);
#endif
    xSemaphoreGive (consoleSemaphore);
    if (telnetRunning) {
      for(uint8_t i = 0; i < MAX_TELNET_CLIENTS; i++){
        if (telnetServerClients[i] && telnetServerClients[i].connected()){
          telnetServerClients[i].write(outChar);
        }
      }
    }
  }  
}

void consolewrite (char *outString)
{
  char i;
  
  if (xSemaphoreTake(consoleSemaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
    if (Serial.availableForWrite() > 0) {
      Serial.print (outString);
      //yield();
      delay(2);
    }
#ifdef USE_BLUETOOTH
    if (mt_bt_enabled && !mt_bt_password_rqd) SerialBT.print (outString);
#endif
    xSemaphoreGive (consoleSemaphore);
    if (telnetRunning) {
      for(i = 0; i < MAX_TELNET_CLIENTS; i++){
        if (telnetServerClients[i] && telnetServerClients[i].connected()){
          telnetServerClients[i].write(outString);
        }
      }
    }
  }
}


void consolewriteln (char *outString)
{
  char i;
  
  if (xSemaphoreTake(consoleSemaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
    if (Serial.availableForWrite() > 0) {
      Serial.println (outString);
      //yield();
      delay(2);
    }
#ifdef USE_BLUETOOTH
    if (mt_bt_enabled && !mt_bt_password_rqd) SerialBT.println (outString);
#endif
    xSemaphoreGive (consoleSemaphore);
    if (telnetRunning) {
      for(i = 0; i < MAX_TELNET_CLIENTS; i++){
        if (telnetServerClients[i] && telnetServerClients[i].connected()){
          telnetServerClients[i].write(outString);
          telnetServerClients[i].write("\r\n");
        }
      }
    }
  }
}


void displayAnsi (int index)
{
  uint8_t lim;

  if (xSemaphoreTake(consoleSemaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
    consolewriteNoSem ((uint8_t) 27);
    consolewriteNoSem ((uint8_t) '[');
    lim=strlen((char*) ansiString[index]);
    for (uint8_t n=0; n<lim; n ++)
      consolewriteNoSem ((uint8_t) ansiString[index][n]);
    consolewriteNoSem ((uint8_t) 'm');
    xSemaphoreGive (consoleSemaphore);
  }
}


void saveCursorPosition()
{
  // const uint8_t command[] = { 27, '[', 's', '\0' };
  const uint8_t command[] = { 27, '7', '\0' };
  consolewrite ((char*) command); 
}


void restoreCursorPosition()
{
  // const uint8_t command[] = { 27, '[', 'u', '\0' };
  const uint8_t command[] = { 27, '8', '\0' };
  consolewrite ((char*) command); 
}


#ifdef USE_BLUETOOTH
void bluetooth_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    Serial.print("Bluetooth client connected with address: ");
    for (int i = 0; i < 6; i++) {
      Serial.printf("%02X", param->srv_open.rem_bda[i]);
      if (i < 5) {
        Serial.print(":");
      }
      else {
        Serial.println("");
      }
    }
    mt_bt_password_rqd = true;
  }
  else if (event == ESP_SPP_CLOSE_EVT) {
    Serial.println("Bluetooth client disconnected.");
  }
}
#endif


/*
 * Pull a prior command from the command history stack
 */
char* getCmdHistory (int8_t direction, bool reset)
{
  static int16_t depth = 0; 
  int16_t curptr;
  uint16_t backCount;
  uint16_t n = 0;
  static char histBuffer[BUFFSIZE];
  char msgBuffer[5];

  // reset
  if (reset) {
    depth = 0;
    histBuffer[0] = '\0';
    return (histBuffer);
  }
  // no history
  if (mt_cmdHistoryStart == mt_cmdHistoryEnd) {
    histBuffer[0] = '\0';
  }
  else {
    // work out direction
    if (direction>0) {
      depth++;
      }
    else {
      depth--;
      if (depth <  0) depth = 0;
      if (depth == 0) return (NULL);
      }
    // find start of command
    curptr = mt_cmdHistoryEnd-2;
    if (curptr<0) curptr = COMMAND_HISTORY + curptr;
    for (backCount=0; curptr!=mt_cmdHistoryStart && backCount!=depth; curptr--) {
      if (curptr<0) curptr = COMMAND_HISTORY -1;
      if (mt_cmdHistory[curptr] == '\0') backCount++;
      }
    if (curptr != mt_cmdHistoryStart) curptr = curptr + 2;
    if (mt_cmdHistory[curptr] == '\0') {
      curptr++;
      if (curptr==COMMAND_HISTORY) curptr = 0;
      }
    if (depth > backCount) depth = backCount;
    // copy it to the buffer
    for (n=0; mt_cmdHistory[curptr]!='\0'; curptr++, n++) {
      if (curptr == COMMAND_HISTORY) curptr = 0;
      histBuffer[n] = mt_cmdHistory[curptr];
    }
    histBuffer[n] = '\0';
  }
  return (histBuffer);
}


/*
 * serial console - the serial console and bt devices are treated equally
 */
void serialConsole(void *pvParameters)
// This is the console task.
{
  (void) pvParameters;
#ifdef USE_BLUETOOTH
  const char remConnection[] = {"\r\nBluetooth/Telnet connected, password required!\r\n* password> "};
#else
  const char remConnection[] = {"\r\nTelnet connected, password required!\r\n* password> "};
#endif
  int64_t uptime;
  char *oldCommand;
  int inpPtr = 0;
  int endPtr = 0;
  char telnetEnabled = 0;
  char inChar, cmdChar, lastChar;
  char lifetime;
  char i;
  char msgBuffer[50];
  char cmdBuffer[BUFFSIZE];
  char onHoldBuffer[BUFFSIZE];
  uint8_t escapeMode = 0;
  uint8_t escapeCount = 0;
  uint8_t histDepth = 0;
  static char telnetOffer[] = { 255, 251, 1, 255, 251, 3}; // will echo, will suppress go-ahead, default other settings to won't / don't
  static char erase2eol[] = { 27, '[', '0', 'K' };
  bool in_pw_prompt = false;

  lastChar = 255;
  mt_identity_state = (char) nvs_get_int ("id_state", ID_FFLASH);
  mt_identity_pin   = (char) nvs_get_int ("id_pin", LED_BUILTIN);
  mt_identity_mode  = (char) nvs_get_int ("id_mode", 0);
  lifetime          = (char) nvs_get_int ("multiTermDuration", 0);
  telnetEnabled     = (char) nvs_get_int ("telnetEnabled", 0);
  if (mt_identity_pin != 99) {
    pinMode(mt_identity_pin, OUTPUT);
    mt_set_identify_output();
  }
  while ( runMultiTerm ) {
    // Check for termination of the terminal
    if (lifetime > 0) {
      uptime = esp_timer_get_time() / (uS_TO_S_FACTOR * 60);
      if (uptime > lifetime) {
        if (ansiTerm) displayAnsi (3);
        consolewriteln ("Console service terminating");
        if (ansiTerm) displayAnsi (0);
        runMultiTerm = false;
      }
    }
    // check we need start telnet service
    if ((!telnetRunning) && WiFi.status() == WL_CONNECTED && wifimode == 0 && telnetEnabled == 0) {
      telnetServer.begin();
      telnetServer.setNoDelay(true);
      telnetRunning = true;  
      if (ansiTerm) displayAnsi (3);
      consolewriteln ("Telnet server started");
      if (ansiTerm) displayAnsi (0);
    }
    //
    //check if there are any new telnet clients
    if (telnetRunning && WiFi.status() == WL_CONNECTED && telnetServer.hasClient()){
      for(i = 0; i < MAX_TELNET_CLIENTS; i++){
        //find free/disconnected spot
        if (!telnetServerClients[i] || !telnetServerClients[i].connected()){
          if(telnetServerClients[i]) telnetServerClients[i].stop();
          telnetServerClients[i] = telnetServer.available();
          if (!telnetServerClients[i]) {
            if (ansiTerm) displayAnsi (3);
            consolewriteln ("Telnet available broken");
            if (ansiTerm) displayAnsi (0);
            }
          else {
            for (char j=0; j<sizeof(telnetOffer); j++) telnetServerClients[i].write((char)telnetOffer[j]);
          }
          if (writingFile) {
            writingFile = false;
            util_closeWriteFile();
            consolewriteln ("");
            if (ansiTerm) displayAnsi (3);
            consolewriteln ("Closing file open for writing prematurely, new remote session connected.");
          }
          if (ansiTerm) displayAnsi (3);
          sprintf (msgBuffer, "New telnet client (%d): %s", (int) i, net_ip2str((uint32_t) telnetServerClients[i].remoteIP()));
          if (ansiTerm) displayAnsi (0);
          consolewriteln (msgBuffer);
          mt_bt_password_rqd = true;
          break;
        }
      }
      if (i >= MAX_TELNET_CLIENTS) {
        //no free/disconnected spot so reject
        telnetServer.available().stop();
        if (ansiTerm) displayAnsi (3);
        consolewriteln ("No more telnet connections available");
        if (ansiTerm) displayAnsi (0);
      }
    }
    //
    inChar = 255;
    // If new remote connection start password prompt
    if (mt_bt_password_rqd && strcmp(mt_password, "none")==0) mt_bt_password_rqd = false;
    if (mt_bt_password_rqd && !in_pw_prompt) {
      in_pw_prompt = true;
      inpPtr = 0;    // trash anything in the command buffer - we need a password first!
      cmdBuffer[0] = '\0';
#ifdef USE_BLUETOOTH
      SerialBT.print ((char*)remConnection);
#endif
      Serial.print ((char*)remConnection);
      if (telnetRunning && WiFi.status() == WL_CONNECTED) {
        for(i = 0; i < MAX_TELNET_CLIENTS; i++){
          if (telnetServerClients[i] && telnetServerClients[i].connected()){
            telnetServerClients[i].write((char*)remConnection);
          }
        }
      }
    }
    // Read next character from any source
#ifdef USE_BLUETOOTH
    if (mt_bt_enabled && SerialBT.available()) {
      inChar = SerialBT.read();
      termID = 255;
    }
    else
#endif
    if (Serial.available()) {
      inChar = Serial.read();
      termID = 250;
    }
    else if (telnetRunning && WiFi.status() == WL_CONNECTED) {
      //
      //check telnet clients for data
      for(i = 0; i < MAX_TELNET_CLIENTS; i++){
        if (telnetServerClients[i] && telnetServerClients[i].connected()){
          if(telnetServerClients[i].available()){
            //get data from the telnet client - nb if multiple clients have data they will overwrite
            inChar = telnetServerClients[i].read();
            termID = i;
            // read options & ignore
            if (inChar == 0xFF) {
              delay(10);
              if(telnetServerClients[i].available()) telnetServerClients[i].read();
              if(telnetServerClients[i].available()) telnetServerClients[i].read();
            }
          }
        }
        else {
          if (telnetServerClients[i]) {
            telnetServerClients[i].stop();
          }
        }
      }
    }
    // check if we terminate telnet service
    if (telnetRunning) {
      if (WiFi.status() != WL_CONNECTED) {
        for(i = 0; i < MAX_TELNET_CLIENTS; i++){
          if (telnetServerClients[i]) {
            telnetServerClients[i].stop();
          }
        }
        telnetRunning = false;
        telnetServer.close();
        telnetServer.stop();
        telnetServer.end();
        if (ansiTerm) displayAnsi (3);
        consolewriteln ("Telnet stopped");
        if (ansiTerm) displayAnsi (0);
      }
      else for(i = 0; i < MAX_TELNET_CLIENTS; i++){
        if (telnetServerClients[i] && ! telnetServerClients[i].connected()){
          telnetServerClients[i].stop();
        }
      }
    }
    if (inChar < 255) {
      //
      // process escape chars before displaying anything
      //
      if (inChar == 27) escapeMode = 1;
      else if (inChar == 0x9b) escapeMode = 2; // CSI code = same as ESC [
      else if (escapeMode > 0  && inChar == 27 ) escapeMode = 4;
      else if (escapeMode == 1 && inChar == '[') escapeMode = 2;
      else if (escapeMode == 2 && inChar >= '0' && inChar <= '9') {
        escapeMode = 3;
        escapeCount = inChar - '0';
        }
      else if (escapeMode == 3 || escapeMode == 2 || (escapeMode == 1 && (inChar=='h' || inChar=='j' || inChar=='k' || inChar=='l'))) {
        if (escapeMode ==2) escapeCount = 1;
        escapeMode = 4;
        switch (inChar) {
          case 'A':   // back 1 command
          case 'k':
            if (histDepth == 0) strcpy (onHoldBuffer, cmdBuffer);
            histDepth++;
            oldCommand = getCmdHistory (1, false);
            strcpy (cmdBuffer, oldCommand);
            for (uint16_t n=0; n< inpPtr; n++) consolewrite((uint8_t) 8);
            consolewrite (cmdBuffer);
            consolewrite (erase2eol);
            inpPtr = strlen (cmdBuffer);
            endPtr = inpPtr;
            break;
          case 'B':  // forward 1 command
          case 'j':
            if (histDepth > 0) {
              histDepth--;
              oldCommand = getCmdHistory (-1, false);
              if (oldCommand == NULL || histDepth < 0) histDepth = 0;
              if (histDepth == 0) strcpy (cmdBuffer, onHoldBuffer);
              else strcpy (cmdBuffer, oldCommand);
              for (uint16_t n=0; n< inpPtr; n++) consolewrite((uint8_t) 8);
              consolewrite (cmdBuffer);
              consolewrite (erase2eol);
              inpPtr = strlen (cmdBuffer);
              endPtr = inpPtr;
              }
            break;
          case 'C':   // cursor right
          case 'l':
            if (inpPtr<endPtr) {
              consolewrite((uint8_t) cmdBuffer[inpPtr]);
              inpPtr++;
              }
            break;
          case 'D':   // cursor left
          case 'h':
            if (inpPtr>0) {
              consolewrite((uint8_t) 8);
              inpPtr--;
              }
            break;
          case 'H':   // home
            for (uint16_t n = 0; n<inpPtr; n++) consolewrite((uint8_t) 8);
            inpPtr = 0;
            break;
          case 'F':   // end
             for (;inpPtr < endPtr; inpPtr++) consolewrite((uint8_t) cmdBuffer[inpPtr]);
             break;
          default:
            break;
          }
        }
      else escapeMode = 0;
      //
      // Write to terminal
      //
      if (inChar==127) inChar = 8;
      else if (inChar == '\r') inChar = '\n';
      else if (inChar == '\t') inChar = ' ';
      // write read characters
      if (escapeMode==0 && inChar!='\n' && inChar!=8) {
#ifdef USE_BLUETOOTH
        if (mt_bt_enabled) {
          if (mt_bt_password_rqd) SerialBT.write('*');
          else SerialBT.write(inChar);
        }
#endif
        if (Serial.availableForWrite() > 0) {
          if (mt_bt_password_rqd) Serial.write('*');
          else Serial.write(inChar);
        }
        if (telnetRunning && WiFi.status() == WL_CONNECTED) {
          for(i = 0; i < MAX_TELNET_CLIENTS; i++){
            if (telnetServerClients[i] && telnetServerClients[i].connected()){
              if (mt_bt_password_rqd) telnetServerClients[i].write('*');
              else telnetServerClients[i].write(inChar);
            }
          }
        }
      }
      //
      // Process the char
      //
      if (escapeMode == 0 && !(lastChar=='\n' && inChar=='\n')) switch (inChar) {
        case 8:
          if (inpPtr > 0) {
            inpPtr--;
            endPtr--;
            for (int n=inpPtr; n<endPtr; n++) cmdBuffer[n] = cmdBuffer[n+1];
            // cmdBuffer[endPtr] = '\0';
            consolewrite ((uint8_t) 8);
            saveCursorPosition();
            for (int n=inpPtr; n<endPtr; n++) consolewrite(cmdBuffer[n]);
            consolewrite (' ');
            restoreCursorPosition();
          }
          break;
        case '\n':
          consolewriteln ("");
          in_pw_prompt = false;
          cmdBuffer[endPtr] = '\0'; // Terminate string
          getCmdHistory (0, true);
          if (writingFile) {
            if (strcmp (cmdBuffer, ".") == 0) {
              writingFile = false;
              util_closeWriteFile();
            }
            else {
              util_appendWriteFile (cmdBuffer);
            }
          }
          else if (mt_bt_password_rqd) {
            if (strcmp (cmdBuffer, mt_password) == 0) {
              mt_bt_password_rqd = false;
            }
          }
          else {
            if (ansiTerm) displayAnsi (0);
            mt_parse_command (cmdBuffer); // Process it
            }
          inpPtr = 0;
          endPtr = 0;
          if (!writingFile) {
            if (configHasChanged) consolewriteln ((const char*) "Config has changed since start, \"restart\" required to become effective.");
            if (ansiTerm) {
              displayAnsi    (2);
              consolewrite   (util_gettime());
              displayAnsi    (0);
              consolewriteln ("");
              }
            consolewrite (device_name);
          }
          consolewrite ("> ");
          if (ansiTerm && !writingFile) {
            displayAnsi  (1);
            consolewrite ("");
            }
          break;
        default:
          if (endPtr < (BUFFSIZE - 1)) {
            for (int n=endPtr; n>inpPtr; n--) cmdBuffer[n]=cmdBuffer[n-1];
            cmdBuffer[inpPtr++] = inChar;
            endPtr++;
            if (endPtr != inpPtr) {
              saveCursorPosition();
              for (int n=inpPtr; n<endPtr; n++) consolewrite(cmdBuffer[n]);
              restoreCursorPosition();
              }
            }
          break;
      }
      if (lastChar == inChar) lastChar = '\0';
      else lastChar = inChar;
    }
    // A delay here forces a task yield for better multi tasking
    //yield();
    delay(5);
  }
  if (telnetRunning) {
    telnetRunning = false;
    telnetServer.close();
    telnetServer.stop();
    telnetServer.end();
  }
  if (ansiTerm) displayAnsi (3);
  consolewriteln ("All console services stopping");
  if (ansiTerm) displayAnsi (0);
  vTaskDelete( NULL );
}


void mt_append_cmdHistory (char* command)
{
  static bool wrapped = false;              // is end expected to be < start
  uint16_t cmdLimit = strlen(command) + 1;  // include the null terminator
  for (uint16_t inPtr=0; inPtr<cmdLimit; inPtr++) {
    mt_cmdHistory[mt_cmdHistoryEnd++] = command[inPtr];
    if (mt_cmdHistoryEnd >= COMMAND_HISTORY) {
      mt_cmdHistoryEnd = 0;
      wrapped = true;
      }
    if (mt_cmdHistoryEnd == mt_cmdHistoryStart) {
      while (mt_cmdHistory[mt_cmdHistoryStart] != '\0' || ((wrapped && mt_cmdHistoryStart <= mt_cmdHistoryEnd) || ((!wrapped) && mt_cmdHistoryStart >= mt_cmdHistoryEnd))) {
        mt_cmdHistoryStart++;
        if (mt_cmdHistoryStart == COMMAND_HISTORY) {
          mt_cmdHistoryStart = 0;
          wrapped = false;
        }
      }
    }
  }
}


void mt_parse_command (char *cmdBuffer)
{
  static char lastCmd[BUFFSIZE] = {'\0'};
  char *param[MAXPARAMS];
  char tempMessage[80];
  int n = 0;
  int nparam = 0;
  bool inSpace = true;

  if (cmdBuffer[0]=='\0') return; // ignore empty strings
  if (strcmp (cmdBuffer, lastCmd) != 0) {       // do not log successions of repeated commands
    if (strncmp ("repeat", cmdBuffer, 6)!=0) {  // do not log the repeat command itself.
      strcpy (lastCmd, cmdBuffer);
      mt_append_cmdHistory (cmdBuffer);
      /*
       * Log data if required
       */
      if (consolelog) {
        fs::File logFile = SPIFFS.open(CONSOLELOG, FILE_APPEND);
        logFile.println (cmdBuffer);
        logFile.close();
      }
    }
  }
  /*
   * tokenise the cmdBuffer in situ
   */ 
  for (n=0, nparam=0; n<BUFFSIZE && cmdBuffer[n]!= '\0' && nparam<MAXPARAMS; n++) {
    if (cmdBuffer[n]<=' ') {
      cmdBuffer[n] = '\0';
      inSpace = true;
    }
    else if (inSpace) {
      inSpace = false;
      param[nparam++] = &cmdBuffer[n];
    }
  }
  for (n=nparam; n<MAXPARAMS; n++) param[n] = NULL;
  if (nparam == 0) return;
  else if (nparam<4 && strcmp (param[0], "repeat")==0) {
    // repeat previous command param[1] times with param[2] seconds in between
    // NB does not accept CTRL-C 
    uint16_t repeat = 1;
    uint16_t interval = 1;
    char repCmd[BUFFSIZE];
    if (nparam>=2 && util_str_isa_int(param[1])) repeat   = util_str2int (param[1]);
    if (nparam>=3 && util_str_isa_int(param[2])) interval = util_str2int (param[2]);
    for (uint16_t repeatCount=0; repeatCount<repeat; repeatCount++) {
      if (repeatCount>0) for (uint16_t intervalCount=0; intervalCount<interval; intervalCount++) delay(1000);
      strcpy (repCmd, lastCmd); // make a copy to be tokenised by next parse
      if (ansiTerm) {           // Timestamp output if colourised output is enabled.
        displayAnsi    (2);
        consolewrite   (util_gettime());
        displayAnsi    (0);
        consolewriteln ("");
        }
      mt_parse_command (repCmd);
      }
    }
  /*
   * Process known commands
   */
  else if ((nparam==3 || nparam==1 || nparam == 6) && strcmp (param[0], "adc") == 0) mt_adc      (nparam, param);
  else if (nparam<=3 && strcmp (param[0], "altitude") == 0)                          mt_altitude (nparam, param);
  else if (nparam<=3 && strcmp (param[0], "ansi") == 0)                              mt_ansi     (nparam, param);
#ifdef USE_BLUETOOTH
  else if (nparam<=2 && strcmp (param[0], "bluetooth") == 0)                         mt_bluetooth(nparam, param);
#endif
  else if (nparam==1 && strcmp (param[0], "clear") == 0)                             mt_clear    (nparam, param);
  else if ((nparam==1 || nparam==3) && strcmp (param[0], "cert") == 0)               mt_cert     (nparam, param);
  else if (nparam==1 && strcmp (param[0], "config") == 0)                            mt_config   (nparam, param);
  else if ((nparam==1 || nparam==3 || nparam==7) && strcmp (param[0], "counter") == 0) mt_counter(nparam, param);
  else if (nparam<=2 && strcmp (param[0], "combine") == 0)                           mt_combineName (nparam, param);
  else if (nparam<=4 && strcmp (param[0], "constant") == 0)                          mt_constant (nparam, param);
  else if (nparam<=2 && strcmp (param[0], "cpuspeed") == 0)                          mt_set_cpuspeed (nparam, param);
  else if ((nparam==1 || nparam>4) && (strcmp (param[0], "warning") == 0 || strcmp (param[0], "critical") == 0 || strcmp (param[0], "extreme") == 0)) {
    mt_set_alert (nparam, param);
  }
  else if (nparam==2 && strcmp (param[0], "del")  == 0)      util_deleteFile (SPIFFS, param[1]);
  else if ((nparam<=2 || nparam==4) && strcmp (param[0], "devicename") == 0) mt_set_devicename(nparam, param);
  else if ((nparam==1 || nparam==4) && strcmp (param[0], "dewpoint") == 0)   mt_set_dewpoint(nparam, param);
  else if (nparam==1 && strcmp (param[0], "dir")  == 0)      util_listDir (SPIFFS, "/", 0);
  else if (nparam==2 && strcmp (param[0], "dump") == 0)      mt_dump          (nparam, param);
  else if (nparam<=2 && (strcmp (param[0], "enable") == 0 || strcmp (param[0], "disable") == 0)) mt_enable (nparam, param);
  else if (nparam==2 && strcmp (param[0], "erase")==0 && strcmp (param[1], "config")== 0) mt_erase_config();
  else if (nparam<=2 && strcmp (param[0], "help") == 0) {
    if (nparam==2) mt_help (param[1]);
    else mt_help (NULL);
  }
  else if ((nparam==1 || nparam==4 || nparam==5) && strcmp (param[0], "i2c") == 0) mt_set_i2c (nparam, param);
  else if (nparam<=3 && strcmp (param[0], "hibernate") == 0) mt_hibernate     (nparam, param);
  else if (nparam==1 && strcmp (param[0], "history")   == 0) mt_history       (nparam, param);
  else if (nparam<=3 && strcmp (param[0], "identify")  == 0) mt_identify      (nparam, param);
  else if ((nparam==1 || nparam==3) && strcmp (param[0], "interval") == 0) mt_dev_interval (nparam, param);
  else if (nparam==1 && strcmp (param[0], "inventory") == 0) mt_inventory     (nparam, param);
  else if (nparam<=2 && (strcmp (param[0], "list") == 0 || strcmp (param[0], "show") == 0)) mt_list (nparam, param);
  else if (nparam<=2 &&  strcmp (param[0], "ntp") == 0)      mt_ntp           (nparam, param);
  else if (nparam==2 && (strcmp (param[0], "nvsstr") == 0 ||strcmp (param[0], "nvsint") == 0 || strcmp (param[0], "nvsfloat") == 0)) mt_get_nvs (nparam, param);
  else if ((nparam==1 || nparam==3 || nparam==4) && strcmp (param[0], "onewire") == 0) mt_set_dallas (nparam, param);
  else if ((nparam==1 || nparam==4) && strcmp (param[0], "opacity") == 0) mt_set_opacity (nparam, param);
  else if (nparam<=2 && strcmp (param[0], "ota") == 0)       mt_ota           (nparam, param);
  else if (nparam!=2 && strcmp (param[0], "output") == 0)    mt_output        (nparam, param);
  else if (nparam<=2 && strcmp (param[0], "password") == 0)  mt_set_password  (nparam, param);
  else if (nparam<=2 && strcmp (param[0], "quit") == 0)      mt_quit          (nparam, param);
  else if (nparam<=2 && strcmp (param[0], "qnh") == 0)       mt_set_qnh       (nparam, param);
  else if (nparam==2 && strcmp (param[0], "read")  == 0)     util_readFile    (SPIFFS, param[1]);
  else if ((nparam==1 || nparam == 4) && strcmp (param[0], "resistor") == 0)  mt_resistor (nparam, param);
  else if (nparam==1 && strcmp (param[0], "restart") == 0)   mt_sys_restart   ("command line request");
  else if (nparam>1  && strcmp (param[0], "rpn") == 0)       rpn_calc         (nparam, param);
  else if ((nparam==1 || nparam==3 || (nparam>=4 && nparam<=6)) && strcmp (param[0], "serial")==0) mt_serial (nparam, param);
  else if (nparam==1 && strcmp (param[0], "scan") == 0)      util_i2c_scan();
  else if (nparam<=2 && strcmp (param[0], "telnet") == 0)    mt_telnet        (nparam, param);
  else if (nparam<=2 && strcmp (param[0], "terminate") == 0) mt_set_terminate (nparam, param);
  else if (nparam<=2 && strcmp (param[0], "timezone") == 0)  mt_set_timezone  (nparam, param);
  else if (nparam<=4 && strcmp (param[0], "wifi") == 0)      mt_set_wifi      (nparam, param);
  else if (nparam<=2 && strcmp (param[0], "wifimode") == 0)  mt_set_wifimode  (nparam, param);
  else if (nparam<=2 && strcmp (param[0], "wifiscan") == 0)  mt_set_wifiscan  (nparam, param);
  else if (nparam==2 && strcmp (param[0], "write")  == 0)    util_writeFile   (SPIFFS, param[1]);
  else if (nparam<=2 && strcmp (param[0], "xysecret") == 0)  mt_set_xysecret  (nparam, param);
  else if (nparam<=3 && strcmp (param[0], "xyserver") == 0)  mt_set_xyserver  (nparam, param);
  else {
    /*
     * Error: commands not recognised
     */
    if (ansiTerm) displayAnsi (3);
    consolewrite ("Error processing \"");
    if (ansiTerm) displayAnsi (1);
    consolewrite (param[0]);
    for (n=1; n<nparam; n++) {
      consolewrite (" ");
      consolewrite (param[n]);
    }
    if (ansiTerm) displayAnsi (3);
    consolewriteln ("\" command");
    if (ansiTerm) displayAnsi (0);
    consolewrite   ("Type ");
    if (ansiTerm) displayAnsi (3);
    consolewrite   ("help");
    if (ansiTerm) displayAnsi (0);
    consolewriteln (" for more info");
    }
}

void mt_adc (int nparam, char **param)
{
  float mult, offsetval;
  uint8_t pin, attenuation;
  char msgBuffer[SENSOR_NAME_LEN];

  if (nparam == 1) the_adc.inventory();
  else {
    pin = util_str2int (param[1]);
    if (pin >=32 && pin<40) {
      if (nparam > 2 && strcmp(param[2], "disable") == 0) {
        sprintf (msgBuffer, "adc_%d", pin);
        nvs_put_string (msgBuffer, "disable");
      }
      else if (nparam > 3) {
        // adc [<pin> <unit-of-measure> [<attenuation> [<offset> [<multiplier>]]]]
        sprintf (msgBuffer, "adc_uom_%d", pin);
        nvs_put_string (msgBuffer, param[2]);
        sprintf (msgBuffer, "adc_atten_%d", pin);
        if (nparam > 3 && util_str_isa_int(param[3])) {
          attenuation = util_str2int (param[3]);
          if (attenuation < 4 && attenuation >= 0) {
            nvs_put_int (msgBuffer, attenuation);
          }
          else {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Attenuation should be 0-3, defaulting to 3");
            nvs_put_int (msgBuffer, 3);
          }
        }
        else nvs_put_int (msgBuffer, 3);
        sprintf (msgBuffer, "adc_offs_%d", pin);
        if (nparam > 4 && util_str_isa_double(param[4])) {
          offsetval = util_str2float (param[4]);
          nvs_put_float (msgBuffer, offsetval);
        }
        else nvs_put_float (msgBuffer, 0.0);
        sprintf (msgBuffer, "adc_mult_%d", pin);
        if (nparam > 5 && util_str_isa_double(param[5])) {
          mult = util_str2float (param[5]);
          nvs_put_float (msgBuffer, mult);
        }
        else nvs_put_float (msgBuffer, 1.0);
      }
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Valid adc pin range is 32-39");
    }
  }
}

void mt_altitude (int nparam, char **param)
{
  struct bme280_s *myData = NULL;
  float  default_alt = 0.00;
  char   myDevTypeID = 255;
  char   sensor[17];
  char   msg[40];

  for (myDevTypeID=0; myDevTypeID<numberOfTypes && strcmp(devType[myDevTypeID], "bme280")!=0; myDevTypeID++);
  if (myDevTypeID<numberOfTypes && devTypeCount[myDevTypeID]> 0) myData = (struct bme280_s*) (devData[myDevTypeID]);
  if (nparam==1) {
    if (devTypeCount[myDevTypeID] == 0) consolewriteln (" * No bme280 pressure sensors attached");
    for (int n=0; n<devTypeCount[myDevTypeID]; n++) {
      sprintf (msg, " * bmp280[%d] altitude = %sm", n, util_ftos (myData[n].altitude, 2));
      consolewrite (msg);
      sprintf (msg, " (%s ft)", util_ftos ((myData[n].altitude * 3.2808399), 2));
      consolewriteln (msg);
    }
  }
  else {
    if (util_str_isa_double(param[1])) {
      default_alt = util_str2float(param[1]);
      if ((nparam==3 && param[2], "ft") == 0) default_alt = default_alt / 3.2808399;
      else if ((nparam==3 && param[2], "m") == 0) consolewriteln ("warning: unit of measure should be either m or ft, defaulting to m");
      for (int n=0; n<devTypeCount[myDevTypeID]; n++) {
        myData[n].altitude = default_alt;
        sprintf (sensor, "bme280Alt_%d", n);
        nvs_put_float (sensor, default_alt);
      }
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Altitude should be numeric");
    }
  }
}


void mt_ansi (int nparam, char **param)
{
char msgBuffer[17];

if (nparam==1) {
  for (uint8_t n=0; n<5; n++) {
    displayAnsi(n);
    sprintf (msgBuffer, " %10s ", ansiName[n]);
    consolewrite (msgBuffer);
    displayAnsi(0);
    consolewrite ("  ");
    consolewriteln (ansiString[n]);
    }
  }
else if (nparam==2 && strcmp (param[1], "test")==0) {
  const uint8_t attrib_list[] = { 0, 1, 3, 4, 5, 7, 9 };
  const char attrib_name[][10] = {"normal", "Bold", "Italic", "underline", "Blink", "Inverse", "Strikeout" };
  char msgBuffer[32];
  consolewriteln ("Note: Some attributes might not be supported on all terminals");
  for (uint8_t attrib = 0; attrib < sizeof(attrib_list); attrib++) {
    sprintf (msgBuffer, "--> Attribute: %d (%s)", attrib_list[attrib], attrib_name[attrib]);
    consolewriteln (msgBuffer);
    consolewrite (" -;--;-- ");
    for (uint8_t fg=30; fg<38; fg++) {
      sprintf (msgBuffer, " %d;%d;-- ", attrib_list[attrib], fg);
      consolewrite (msgBuffer);
      }
    consolewriteln ("");
    for (uint8_t bg=40; bg<48; bg++) {
      sprintf (msgBuffer, " %d;--;%d ", attrib_list[attrib], bg);
      consolewrite (msgBuffer);
      for (uint8_t fg=30; fg<38; fg++) {
        sprintf (msgBuffer, "%c[%d;%d;%dm %d;%d;%d ", 27, attrib_list[attrib], fg, bg, attrib_list[attrib], fg, bg);
        consolewrite (msgBuffer);
        }
      sprintf (msgBuffer, "%c[0;37;40m", 27);
      consolewriteln (msgBuffer);
      }
    consolewriteln ("");
    }
  }
else if (nparam==3) {
  bool found = false;
  uint8_t n = 0;
  for (n=0; n<5 && !found; n++) if (strcmp(ansiName[n], param[1])==0) found=true;
  if (found) {
    if (strlen(param[2])>15) param[2][15] = '\0'; // truncate too long strings
    sprintf (msgBuffer, "ansi_%s", param[1]);
    nvs_put_string (msgBuffer, param[2]);
    strcpy (ansiString[n], param[2]);
    }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewrite (param[1]);
    consolewriteln (" does not describe a supported colour attribute");
    }
  }
}


#ifdef USE_BLUETOOTH
void mt_bluetooth (int nparam, char **param)
{
  char setval[sizeof(mt_bt_pin)];

  if (nparam==1) {
    nvs_get_string ("mt_bt_pin", setval, "none", sizeof(mt_bt_pin));
    if (util_str_isa_int(setval)) {
      consolewrite (" * Uses pin of ");
      consolewriteln (setval);
    }
    else if (strcmp(setval, "none") == 0) {
      consolewriteln (" * Enabled without pin");
    }
    else consolewriteln (" * Is disabled");
  }
  else {
    if (strcmp(param[1], "disable")==0 || strcmp(param[1], "none")==0 || (strlen(param[1])==4 && util_str_isa_int(param[1]))) {
      nvs_put_string ("mt_bt_pin", param[1]);
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Bluetooth parameter not understood");
    }
  }
}
#endif


void mt_set_password (int nparam, char **param)
{
  char setval[sizeof(mt_password)];
  
  if (nparam==1) {
    nvs_get_string ("mt_password", setval, "MySecret", sizeof(mt_password));
    consolewrite (" * ");
    consolewriteln (setval);
  }
  else {
    if (strlen(param[1])>= sizeof(mt_password)) param[1][sizeof(mt_password)-1] = '\0';
    nvs_put_string ("mt_password", param[1]);
  }
}


void mt_clear (int nparam, char **param)
{
  const uint8_t string[] = { 27, '[', '2', 'J', 27, '[', 'H', '\0' };
  if (ansiTerm) consolewrite ((char*) string);
}


void mt_cert (int nparam, char **param)
{
  const char certType[][6] = {"ota", "xymon"};
  char varName[17];
  char fileName[48];
  
  if (nparam==1) {
    for (uint8_t n=0; n<2; n++) {
      sprintf (varName, "%s_certFile",certType[n]);
      nvs_get_string (varName, fileName, CERTFILE, sizeof(fileName));
      consolewrite (" * ");
      consolewrite ((char*) certType[n]);
      consolewrite (" certificate file: ");
      consolewriteln (fileName);
    }
  }
  else if (nparam==3) {
    int found = 0;
    for (uint8_t n=0; n<2 && found==0; n++) {
      if (strcmp (param[1],certType[n]) == 0) found = 1;
    }
    if (found == 1) {
      sprintf (varName, "%s_certFile",param[1]);
      nvs_put_string (varName, param[2]);
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewrite ("invalid cert type: ");
      consolewriteln (param[1]);
    }
  }
}

/*
 * 
 */
void mt_compensate (int nparam, char **param)
{
  char compensateDevType[][7] = { "css811" };
  char msgBuffer[80];
  uint8_t n, i, typeNr;
  
  if (nparam==1) {
    for (n=0; n<1; n++) {
      typeNr = util_get_dev_type(compensateDevType[n]);
      for (i=0; i<devTypeCount[typeNr]; i++) {
        sprintf (msgBuffer, " * %s[%d] temp/humidity comp from ", compensateDevType[n], i);
        consolewrite (msgBuffer);
        if (n == 0) {
          if (((struct css811_s*) devData[typeNr])[i].compensationDevType >= numberOfTypes)
            strcmp (msgBuffer, "unknown device type (uncompensated)");
          else sprintf (msgBuffer, "%s[%d]", devType[((struct css811_s*) devData[typeNr])[i].compensationDevType],
                                                ((struct css811_s*) devData[typeNr])[i].compensationDevNr);
        }
        consolewriteln (msgBuffer);
      }
    }
  }
  else if (nparam==5) {
    if (util_str_isa_int(param[2]) && util_str_isa_int(param[4])) {
      n = util_str2int (param[2]);
      i = util_str2int (param[4]);
      if (strcmp (param[1], "css811") == 0) {
        if (strcmp (param[3], "hdc1080") == 0 || strcmp(param[3], "bme280") == 0) {
          // if (strcmp (param[1], "css811") == 0 && (struct css811_t*) devData[]
          sprintf (msgBuffer, "%s_c%d_type", param[1], n);
          nvs_put_string (msgBuffer, param[3]);
          sprintf (msgBuffer, "%s_c%d_nr", param[1], n);
          nvs_put_int (msgBuffer, i);
        }
        else {
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Compensation device should be bme280 or hdc1080");
        }
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("device to compensate should be css811");
      }
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("device index should be an integer value");
    }
  }
}

/*
 * Display configuration
 */
void mt_config (int nparam, char **param)
{
  consolewriteln ("Device Name:");
  mt_set_devicename (1, param);
  consolewriteln ("Terminal life time:");
  mt_set_terminate (1, param);
#ifdef USE_BLUETOOTH
  consolewriteln ("Bluetooth:");
  mt_bluetooth (1, param);
#endif
  consolewriteln ("Telnet:");
  mt_telnet (1, param);
  consolewriteln ("i2c busses:");
  mt_set_i2c (1, param);
  consolewriteln (" * Run \"scan\" to show discovered devices.");
  consolewriteln ("Hibernation");
  mt_hibernate (1, param);
  consolewriteln ("Identify:");
  mt_identify (1, param);
  consolewriteln ("Time Zone:");
  mt_set_timezone (1, param);
  mt_ntp (1, param);
  consolewriteln ("WiFi Networks:");
  mt_set_wifi (1, param);
  consolewriteln ("WiFi Mode:");
  mt_set_wifimode (1, param);
  consolewriteln ("WiFi Scan:");
  mt_set_wifiscan (1, param);
  consolewriteln ("WiFi Connection:");
  net_display_connection();
  consolewriteln ("xymon server:");
  mt_set_xyserver (1, param);
  consolewriteln("System Details");
  util_show_system_id();
}

void mt_constant(int nparam, char **param)
{
  char varName[17];
  char varLabel[21];
  uint8_t index;
  float value;
  
  if (nparam<=2) {
    uint8_t lower = 0;
    uint8_t upper =10;
 
    consolewriteln (" * Constants");
    if (nparam == 2 && util_str_isa_int(param[1])) {
      lower = util_str2int (param[1]);
      if (lower>=0 && lower<10) {
        upper = lower+1;
      }
      else lower = 0;
    }
    for (uint8_t n=lower; n<upper; n++) {
      sprintf (varName, "const_val_%d", n);
      value = nvs_get_float (varName, 0.00);
      if (value != 0.00) {
        sprintf (varName, "const_desc_%d", n);
        nvs_get_string (varName, varLabel, "-", sizeof(varLabel));
        sprintf (varName, "   %d.) ", n);
        consolewrite (varName);
        consolewrite (util_ftos (value, 6));
        consolewrite ("   ");
        consolewriteln (varLabel);
      }
    }
  }
  else {
    if (util_str_isa_int(param[1]) && util_str_isa_double(param[2])) {
      index = util_str2int (param[1]);
      if (index>=0 && index<10) {
        value = util_str2double(param[2]);
        sprintf (varName, "const_val_%d", index);
        nvs_put_float (varName, value);
        sprintf (varName, "const_desc_%d", index);
        if (nparam>3) {
          if (strlen(param[3]) > sizeof(varLabel)-1) param[3][sizeof(varLabel)-1] = '\0';
          nvs_put_string (varName, param[3]);
        }
        else nvs_put_string (varName, "-");
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Index should be between 0 and 9");
      }
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Index and constant should both be numeric");
    }
  }
}

void mt_inventory(int nparam, char **param)
{
  consolewriteln ("Test: Counter");
  mt_counter (1, param);
  the_adc.inventory();
  the_bh1750.inventory();
  the_bme280.inventory();
  the_css811.inventory();
  the_hdc1080.inventory();
  the_ina2xx.inventory();
  the_veml6075.inventory();
  the_serial.inventory();
  the_sdd1306.inventory();
}

void mt_set_cpuspeed(int nparam, char **param)
{
  char msgBuffer[80];
  int storedSpeed;
  int xtal;

  if (nparam==1) {
    storedSpeed = nvs_get_int ("cpuspeed", 0);
    sprintf (msgBuffer, " * CPU speed: %dMHz, Xtal freq: %dMHz", getCpuFrequencyMhz(), getXtalFrequencyMhz());
    consolewriteln (msgBuffer);
    if (storedSpeed==0) consolewriteln (" * Using default CPU speed");
    else if (storedSpeed!=getCpuFrequencyMhz()) {
      if (ansiTerm) displayAnsi(3);
      sprintf (msgBuffer, " * WARNING: configured CPU %dMHz speed mismatch with actual speed", storedSpeed);
      consolewriteln (msgBuffer);
    }
  }
  else if (util_str_isa_int (param[1])) {
    storedSpeed = util_str2int(param[1]);
    xtal = getXtalFrequencyMhz();
    if (storedSpeed==240 || storedSpeed==160 || storedSpeed==80 || storedSpeed==0
       #ifdef ENABLE_LOW_FREQ
       // * FreeRTOS appears to have issues if using speeds less than 80MHz
       || (xtal==40 &&(storedSpeed==40 || storedSpeed==20 || storedSpeed==10)) ||
       (xtal==26 &&(storedSpeed==26 || storedSpeed==13)) ||
       (xtal==24 &&(storedSpeed==24 || storedSpeed==12))
       #endif
       ) {
      nvs_put_int ("cpuspeed", storedSpeed);
      // setCpuFrequencyMhz (storedSpeed);
      // wait for reboot to reset speed, or we may have unwanted WiFi errors
    }
    else consolewriteln ("Invalid CPU speed specified");
  }
  else consolewriteln ("Use integer value to set CPU speed.");
}

void mt_counter (int nparam, char **param)
{
  char nameBuffer[80];
  int tally = 0;
  int cntr_pin;
  int cntr_index;
  char cntr_name[17];
  char cntr_uom[17];
  float cntr_offset = 0.00;
  float cntr_multiply = 0.00;

  if (nparam==1) {
    for (int n=0; n<MAX_COUNTER; n++) {
      sprintf (nameBuffer, "cntr_pin_%d", n);
      cntr_pin = nvs_get_int (nameBuffer, 99);
      if (cntr_pin < 99) {
        tally ++;
        sprintf (nameBuffer, "cntr_name_%d", n);
        nvs_get_string (nameBuffer, cntr_name, "unknown", sizeof(cntr_name));
        sprintf (nameBuffer, "cntr_uom_%d", n);
        nvs_get_string (nameBuffer, cntr_uom, "unknown", sizeof(cntr_uom));
        sprintf (nameBuffer, "cntr_offset_%d", n);
        cntr_offset = nvs_get_float (nameBuffer, 0.00);
        sprintf (nameBuffer, "cntr_mult_%d", n);
        cntr_multiply = nvs_get_float (nameBuffer, 1.00);
        // print double to string conversions serially to avoid occasional stack issues
        sprintf (nameBuffer, " * counter %d - pin: %d, id: %s, measures: %s, offset: ", n, cntr_pin, cntr_name, cntr_uom);
        consolewrite (nameBuffer);
        sprintf (nameBuffer, "%s, multiplier: ", util_ftos(cntr_offset, 3));
        consolewrite (nameBuffer);
        sprintf (nameBuffer, "%s", util_ftos(cntr_multiply, 3));
        consolewriteln (nameBuffer);
      }
    }
    if (tally == 0) consolewriteln (" * No counters defined");
  }
  else if (nparam==3 && util_str_isa_int(param[1]) && strcmp(param[2], "disable")==0) {
    cntr_index = util_str2int (param[1]);
    sprintf (nameBuffer, "cntr_pin_%d", cntr_index);
    nvs_put_int (nameBuffer, 99);    
  }
  else if (nparam==7 && util_str_isa_int(param[1]) && util_str_isa_int(param[2]) && util_str_isa_double(param[5]) && util_str_isa_double(param[6])) {
    cntr_index    = util_str2int (param[1]);
    cntr_pin      = util_str2int (param[2]);
    cntr_offset   = util_str2float (param[5]);
    cntr_multiply = util_str2float (param[6]);
    if (cntr_index<0 || cntr_index>=MAX_COUNTER) {
      if (ansiTerm) displayAnsi(3);
      sprintf (nameBuffer, "counter index must be between 0 and %d", MAX_COUNTER);
      consolewriteln (nameBuffer);
      return;
    }
    if (cntr_pin<0 || cntr_pin>39) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ((const char*) "counter input pin should be between 0 and 39");
      return;
    }
    if (strlen(param[3]) > 16) {
      consolewriteln ((const char*) "counter unique name restricted to 16 characters in length");
      param[3][16] = '\0';
    }
    if (strlen(param[4]) > 16) {
      consolewriteln ((const char*) "counter unit of measure name restricted to 16 characters in length");
      param[4][16] = '\0';
    }
    if (cntr_multiply == 0) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ((const char*) "counter multiplier must be non zero");
      return;
    }
    sprintf (nameBuffer, "cntr_pin_%d", cntr_index);
    nvs_put_int (nameBuffer, cntr_pin);
    sprintf (nameBuffer, "cntr_name_%d", cntr_index);
    nvs_put_string (nameBuffer, param[3]);
    sprintf (nameBuffer, "cntr_uom_%d", cntr_index);
    nvs_put_string (nameBuffer, param[4]);
    sprintf (nameBuffer, "cntr_offset_%d", cntr_index);
    nvs_put_float (nameBuffer, cntr_offset);
    sprintf (nameBuffer, "cntr_mult_%d", cntr_index);
    nvs_put_float (nameBuffer, cntr_multiply);
  }
  else if (nparam==7) {
    if (!util_str_isa_int(param[1])) consolewriteln ((const char*) "counter number should be an integer number");
    if (!util_str_isa_int(param[2])) consolewriteln ((const char*) "pin number should be an integer number");
    if (!util_str_isa_double(param[5])) consolewriteln ((const char*) "offset should be an floating point number");
    if (!util_str_isa_double(param[6])) consolewriteln ((const char*) "multiplier should be an floating point number");
  }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewrite ((const char*) "Unrecognised form of counter command");
    if (nparam >= 2 && !util_str_isa_int(param[1])) consolewriteln ((const char*) "counter number should be an integer number");
  }
}


/*
 * Set combine name to use when combining graphs
 */
void mt_combineName (int nparam, char **param)
{
  char combineName[17];

  if (nparam==1) {
    nvs_get_string ("combineName", combineName, "none", 17);
    consolewrite (" * Combine name is: ");
    consolewriteln (combineName);
  }
  else {
    if (strlen(param[1])>16) param[1][16]='\0';
    nvs_put_string ("combineName", param[1]);
  }
}

/*
 * Display configuration
 */
void mt_set_devicename (int nparam, char **param)
{
bool notfound = true;
int n, i;
int limit = (sizeof(devType))/DEVTYPESIZE;
char msgBuffer[20];

if (nparam==1) {
  consolewrite (" * ");
  consolewriteln (device_name);
}
else if (nparam == 2) {
  if (strlen(param[1]) > 16) param[1][16] = '\0';
  nvs_put_string ("device_name", param[1]);
  strcpy (device_name, param[1]);
  }
else if (nparam == 4) {
  for (n=0; notfound && n<limit; n++) if (strcmp (param[1], devType[n]) == 0) {
    notfound = false;
    }
  if (notfound) {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("Device type not found, use \"list devices\" to see supported types.");
    return;
    }
  if (util_str_isa_int(param[2])) {
    if (strlen(param[3]) > 16) param[3][16] = '\0';
    i = util_str2int(param[2]); // NB running through integer conversion should clear leading zeros
    sprintf (msgBuffer, "%s_%d", param[1], i);
    nvs_put_string (msgBuffer, param[3]);
    if (i<devTypeCount[n]) {
      if      (strcmp ("counter",   param[1]) == 0) strcpy(((struct int_counter_s*) devData)[n].uniquename, param[3]);
      else if (strcmp ("adc",       param[1]) == 0) strcpy(((struct adc_s*)         devData)[n].uniquename, param[3]);
      else if (strcmp ("bme280",    param[1]) == 0) strcpy(((struct bme280_s*)      devData)[n].uniquename, param[3]);
      else if (strcmp ("ds1820",    param[1]) == 0) strcpy(((struct dallasTemp_s*)  devData)[n].uniquename, param[3]);
      else if (strcmp ("hdc1080",   param[1]) == 0) strcpy(((struct hdc1080_s*)     devData)[n].uniquename, param[3]);
      else if (strcmp ("veml6075",  param[1]) == 0) strcpy(((struct veml6075_s*)    devData)[n].uniquename, param[3]);
      else if (strcmp ("bh1750",    param[1]) == 0) strcpy(((struct bh1750_s*)      devData)[n].uniquename, param[3]);
      else if (strcmp ("hdc1080",   param[1]) == 0) strcpy(((struct hdc1080_s*)     devData)[n].uniquename, param[3]);
      else if (strcmp ("ina2xx",    param[1]) == 0) strcpy(((struct ina2xx_s*)      devData)[n].uniquename, param[3]);
      else if (strcmp ("output",    param[1]) == 0) strcpy(((struct output_s*)      devData)[n].uniquename, param[3]);
      else if (strcmp ("serial",    param[1]) == 0) strcpy(((struct zebSerial_s*)   devData)[n].uniquename, param[3]);
      }
    else {
      if (strcmp ("ds1820", param[1]) != 0) {
        sprintf (msgBuffer, "%s id %d", param[1], i);
        if (ansiTerm) displayAnsi(4);
        consolewrite    (msgBuffer);
        consolewriteln (" device does not exist, but name stored anyway.");
        }
      else consolewriteln ("Name stored in NV RAM, reboot to apply");
      }
    }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("device number should be an integer value, first unit is always 0");
    }
  }
}


/*
 * Set dewpoint name for a device
 */
void mt_set_dewpoint (int nparam, char **param)
{
  char *theName;
  char msg[20];
  char suppTy;
  char dptypes[][8] = {"bme280", "hdc1080"};
  char selectedType;
  int index;
  
  if (nparam == 1) {
    for (suppTy=0; suppTy<2; suppTy++) {
      for (selectedType=0; selectedType<numberOfTypes && strcmp(dptypes[suppTy], devType[selectedType])!=0; selectedType++) ;
      if (selectedType<numberOfTypes && devTypeCount[selectedType]>0) {
        for (int n=0; n<devTypeCount[selectedType]; n++) {
          if (suppTy == 0) theName = ((struct bme280_s*)(devData[selectedType]))[n].dewpointName;
          else if (suppTy == 1) theName = ((struct hdc1080_s*)(devData[selectedType]))[n].dewpointName;
          else theName = "none";
          sprintf (msg, " * %s[%d] ", dptypes[suppTy], n);
          consolewrite   (msg);
          consolewriteln (theName);
        }
      }
    }
  }
  else if (strcmp (param[1], dptypes[0])==0 || strcmp (param[1],dptypes[1])==0) {
    if (util_str_isa_int(param[2])) {
      // save to nvram if possible
      if (strlen(param[3])>16) param[3][16]='\0';
      index = util_str2int(param[2]);
      sprintf (msg, "%sDP_%d", param[1], index);
      nvs_put_string(msg, param[3]);
      for (suppTy=0; suppTy<2; suppTy++) {
        for (selectedType=0; selectedType<numberOfTypes && strcmp(dptypes[suppTy], devType[selectedType])!=0; selectedType++) ;
        if (selectedType<numberOfTypes && devTypeCount[selectedType]>index) {
          if (suppTy == 0) strcpy (((struct bme280_s*)(devData[selectedType]))[index].dewpointName, param[3]);
          else if (suppTy == 1) strcpy (((struct hdc1080_s*)(devData[selectedType]))[index].dewpointName, param[3]);
        }
      }
    }
    else {
      consolewrite (param[1]);
      if (ansiTerm) displayAnsi(3);
      consolewriteln (" unit number should be an integer.");
    }
  }
  else {
    consolewrite (param[1]);
    if (ansiTerm) displayAnsi(3);
    consolewrite (" is not supported for dewpoint measurement.");
  }
}


/*
 * Set up serial comms
 */
void mt_serial (int nparam, char **param)
{
  const int32_t acceptSpeed[] = {300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600, 115200, 256000, 512000, 962100};
  const char acceptDevice[][SENSOR_NAME_LEN] = {"disable", "pms5003", "zh03b", "mh-z19c", "winsen", "ascii", "nmea"};
  char devNr, rx, tx, ok, errCnt;
  char proto[4];
  char msgBuffer[80];
  uint32_t baud;
  
  if (nparam == 1) {
    the_serial.inventory();
  }
  if (nparam > 2 && util_str_isa_int(param[1])) {
    devNr = util_str2int(param[1]);
    if (devNr != 0 && devNr != 1) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("device number must be either 0 or 1");
    }
    else {
      // set device type over serial link
      if (nparam == 3) {
        ok = 0;
        for (uint8_t cntr=0; cntr<(sizeof(acceptDevice)/SENSOR_NAME_LEN) && ok==0; cntr++) {
          if (strcmp (param[2], acceptDevice[cntr])==0) ok = 1;
        }
        if (ok==0) {
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("device must be one of:");
          for (uint8_t cntr=0; cntr<(sizeof(acceptDevice)/SENSOR_NAME_LEN); cntr++) {
            consolewrite (" * ");
            consolewriteln ((char*)acceptDevice[cntr]);
          }
        }
        else {
          sprintf (msgBuffer, "serialType_%d", devNr);
          nvs_put_string (msgBuffer, param[2]);
        }
      }
      else if (nparam>=4 && nparam<=6) {
        errCnt = 0;
        if (nparam<6) {
          strcpy (proto, "8n1");
          if (nparam<5) baud = 9600;
        }
        if (util_str_isa_int(param[2]) && util_str_isa_int(param[3])) {
          rx = util_str2int(param[2]);
          tx = util_str2int(param[3]);
          if (rx>=0 && rx<40) {
            if (tx>=0 && tx<33) {
              if (nparam>4) {
                if (util_str_isa_int(param[4])) {
                  baud = util_str2int(param[4]);
                  ok = 0;
                  for (uint8_t cntr=0; cntr<(sizeof(acceptSpeed)/sizeof(int32_t)) && ok==0; cntr++) {
                  if (baud == acceptSpeed[cntr]) ok = 1;
                  }
                  if (ok==0) {
                    errCnt++;
                    if (ansiTerm) displayAnsi(3);
                    consolewriteln ("device speed must be one of:");
                    for (uint8_t cntr=0; cntr<(sizeof(acceptSpeed)/sizeof(int32_t)); cntr++) {
                      sprintf (msgBuffer, " * %d", acceptSpeed[cntr]);
                      consolewriteln (msgBuffer);
                    }
                  }                
                }
                else {
                  errCnt++;
                  if (ansiTerm) displayAnsi(3);
                  consolewriteln ("Serial speed should be numeric");
                }
                if (nparam>5) {
                  ok = 0;
                  for (uint8_t cntr=0; cntr<(sizeof(acceptProto)/4) && ok==0; cntr++) {
                    if (strcmp (param[5], acceptProto[cntr])==0) ok = 1;
                  }
                  if (ok==0) {
                    errCnt++;
                    if (ansiTerm) displayAnsi(3);
                    consolewriteln ("device protocol must be one of:");
                    for (uint8_t cntr=0; cntr<(sizeof(acceptProto)/4); cntr++) {
                      consolewrite (" * ");
                      consolewriteln ((char*)acceptProto[cntr]);
                    }
                  }
                  else strcpy (proto, param[5]);
                }
              }
            }
            else {
              errCnt++;
              if (ansiTerm) displayAnsi(3);
              consolewriteln ("tx pin should be between 0 and 32");
            }
          }
          else {
            errCnt++;
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("rx pin should be between 0 and 39");
          }
        }
        else {
          errCnt++;
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("rx and tx pins should be numeric.");
        }
        if (tx == rx) {
          errCnt++;
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("rx and tx should be different pins.");
        }
        if (errCnt == 0) {
          consolewriteln ("updating configuration");
          sprintf (msgBuffer, "serial_rx_%d", devNr);
          nvs_put_int (msgBuffer, rx);
          sprintf (msgBuffer, "serial_tx_%d", devNr);
          nvs_put_int (msgBuffer, tx);
          sprintf (msgBuffer, "serial_sp_%d", devNr);
          nvs_put_int (msgBuffer, baud);
          sprintf (msgBuffer, "serial_pr_%d", devNr);
          nvs_put_string (msgBuffer, proto);
        }
      }
    }
  }
}


/*
 * Dump structures
 */
void mt_dump (int nparam, char **param)
{
  int tPtr, dPtr;
  struct rpnLogic_s *rpnPtr;

  // Call ourself if working recuresively
  if (strcmp(param[1], "all") == 0) {
    for (char n=0; n<numberOfTypes; n++) if (devTypeCount[n] > 0) {
      param[1] = (char*) devType[n];
      mt_dump (nparam, param);
    }
    return;
  }
  // look for the type specified
  if (param[1][0]!='/' && strcmp(param[1], "command")!=0) {
    for (tPtr=0; tPtr<numberOfTypes && strcmp(param[1], devType[tPtr])!=0; tPtr++);
    if (tPtr == numberOfTypes) {
      if (ansiTerm) displayAnsi(3);
      consolewrite ("Device type ");
      consolewrite (param[1]);
      consolewriteln (" not recognised.");
      return;
    }
    if (devTypeCount[tPtr] == 0) {
      if (ansiTerm) displayAnsi(3);
      consolewrite ("No ");
      consolewrite (param[1]);
      consolewriteln (" devices found on system");
      return;
    }
  }
  // Call util_dump with device specific parameters
  if (ansiTerm) displayAnsi(4);
  consolewrite ("Dumping ");
  consolewrite (param[1]);
  consolewriteln (" structures:");
  if (strcmp(param[1], "command") == 0) {
    char outBuffer[15];
    sprintf (outBuffer, "%d (0x%03x)", mt_cmdHistoryStart, mt_cmdHistoryStart);
    consolewrite ("Command buffer start: ");
    consolewrite (outBuffer);
    sprintf (outBuffer, "%d (0x%03x)", mt_cmdHistoryEnd, mt_cmdHistoryEnd);
    consolewrite (", end: ");
    consolewriteln (outBuffer);
    util_dump (mt_cmdHistory, COMMAND_HISTORY);
    }
  else if (param[1][0] == '/' ) { // Looks like a file dump
    int fileSize;
    char* fileData = NULL;
    fileData = util_loadFile (SPIFFS, param[1], &fileSize);
    if (fileData!=NULL && fileSize>0) util_dump (fileData, fileSize);
    else {
      if (ansiTerm) displayAnsi(3);
      consolewrite ("Cannot dump file: ");
      consolewriteln (param[1]);
    }
    if (fileData!=NULL) free (fileData);
  }
  else if (strcmp(param[1], "counter") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) util_dump ((char*) &(((struct int_counter_s*)  devData[tPtr])[dPtr]), sizeof(struct int_counter_s));
  }
  else if (strcmp(param[1], "adc") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) util_dump ((char*) &(((struct adc_s*)          devData[tPtr])[dPtr]), sizeof(struct adc_s));
  }
  else if (strcmp(param[1], "bh1750") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) util_dump ((char*) &(((struct bh1750_s*)       devData[tPtr])[dPtr]), sizeof(struct bh1750_s));
  }
  else if (strcmp(param[1], "bme280") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) util_dump ((char*) &(((struct bme280_s*)       devData[tPtr])[dPtr]), sizeof(struct bme280_s));
  }
  else if (strcmp(param[1], "ds1820") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) util_dump ((char*) &(((struct dallasTemp_s*)   devData[tPtr])[dPtr]), sizeof(struct dallasTemp_s));
  }
  else if (strcmp(param[1], "css811") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) util_dump ((char*) &(((struct css811_s*)       devData[tPtr])[dPtr]), sizeof(struct css811_s));
  }
  else if (strcmp(param[1], "hdc1080") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) util_dump ((char*) &(((struct hdc1080_s*)      devData[tPtr])[dPtr]), sizeof(struct hdc1080_s));
  }
  else if (strcmp(param[1], "sdd1306") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) util_dump ((char*) &(((struct sdd1306_s*)      devData[tPtr])[dPtr]), sizeof(struct sdd1306_s));
  }
  else if (strcmp(param[1], "ina2xx") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) util_dump ((char*) &(((struct ina2xx_s*)       devData[tPtr])[dPtr]), sizeof(struct ina2xx_s));
  }
  else if (strcmp(param[1], "veml6075") == 0) {
    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) {
      util_dump ((char*) &(((struct veml6075_s*) devData[tPtr])[dPtr]), sizeof(struct veml6075_s));
      for (uint8_t innerloop=0; innerloop<3; innerloop++) {
        rpnPtr =  ((struct veml6075_s) (((struct veml6075_s*) devData[tPtr])[dPtr])).alert[innerloop];
        if (rpnPtr != NULL) {
          consolewrite ((char*) xymonColour[innerloop+1]);
          consolewriteln (" alert logic:");
          util_dump ((char*) rpnPtr, rpnPtr->size);
        }
      }
    }
  }
  else if (strcmp(param[1], "output")   == 0) {
    struct output_s *serPtr;

    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) {
      serPtr = &((struct output_s*) devData[tPtr])[dPtr];
      if (serPtr->outputLogic != NULL) {
        util_dump ((char*) serPtr, sizeof(struct output_s));
        consolewriteln ("Logic portion:");
        rpnPtr =  serPtr->outputLogic;
        if (rpnPtr!=NULL) util_dump ((char*) rpnPtr, rpnPtr->size);
      }
    }
  }
  else if (strcmp(param[1], "serial") == 0) {
    struct zebSerial_s *serPtr;

    for (dPtr=0; dPtr<devTypeCount[tPtr]; dPtr++) {
      serPtr = &((struct zebSerial_s*) devData[tPtr])[dPtr];
      util_dump ((char*) serPtr, sizeof(struct zebSerial_s));
      if (serPtr->isvalid && serPtr->dataBuffer != NULL) {
        consolewriteln ("Data Buffer:");
        util_dump ((char*) serPtr->dataBuffer, serPtr->bufferSize);
      }
    }
  }
  consolewriteln ("");
}

/*
 * enable / disable features
 */
void mt_enable (int nparam, char **param)
{
  if (nparam==1) {
    consolewrite (" * ansi (ANSI colorized terminal) ");
    if (ansiTerm) consolewriteln ("enabled");
    else consolewriteln ("disabled");
    consolewrite (" * consolelog (console logging to file) ");
    if (consolelog) consolewriteln ("enabled");
    else consolewriteln ("disabled");
    consolewrite (" * memory (memory stats displayed in xymon) ");
    if (showMemory) consolewriteln ("enabled");
    else consolewriteln ("disabled");
    consolewrite (" * output (Output status displayed in xymon) ");
    if (showOutput) consolewriteln ("enabled");
    else consolewriteln ("disabled");
    consolewrite (" * otaonboot (OTA update check on boot) ");
    if (nvs_get_int ("otaonboot", 0) == 1) consolewriteln ("enabled");
    else consolewriteln ("disabled");
    consolewrite (" * startupdelay (60 second boot startup delay) ");
    if (nvs_get_int ("startupDelay", 0) == 1) consolewriteln ("enabled");
    else consolewriteln ("disabled");
    consolewrite (" * showlogic (Display alert logic on xymon console) ");
    if (nvs_get_int ("showlogic", 0) == 1) consolewriteln ("enabled");
    else consolewriteln ("disabled");
    consolewrite (" * telnet (telnet service) ");
    if (nvs_get_int ("telnetEnabled", 0) == 0) {
      if (wifimode == 0) consolewriteln ("enabled");
      else consolewriteln ("disabled in ondemand mode");
    }
    else consolewriteln ("disabled");
  }
  else {
    if (strcmp(param[1], "consolelog") == 0) {
      if (strcmp(param[0], "enable") == 0) {
        nvs_put_int ("consolelog", 1);
        consolelog = true;
      }
      else {
        nvs_put_int ("consolelog", 0);
        consolelog = false;
      }
    }
    else if (strcmp(param[1], "ansi") == 0) {
      if (strcmp(param[0], "enable") == 0) {
        nvs_put_int ("ansiTerm", 1);
        ansiTerm = true;
      }
      else {
        nvs_put_int ("ansiTerm", 0);
        ansiTerm = false;
      }
    }
    else if (strcmp(param[1], "memory") == 0) {
      if (strcmp(param[0], "enable") == 0) {
        nvs_put_int ("memorystats", 1);
        showMemory = true;
      }
      else {
        nvs_put_int ("memorystats", 0);
        showMemory = false;
      }
    }
    else if (strcmp(param[1], "output") == 0) {
      if (strcmp(param[0], "enable") == 0) {
        nvs_put_int ("displayOutput", 1);
        showOutput = true;
      }
      else {
        nvs_put_int ("displayOutput", 0);
        showOutput = false;
      }
    }
    else if (strcmp(param[1], "showlogic") == 0) {
      if (strcmp(param[0], "enable") == 0) nvs_put_int ("showlogic", 1);
      else nvs_put_int ("showlogic", 0);
    }
    else if (strcmp(param[1], "otaonboot") == 0) {
      if (strcmp(param[0], "enable") == 0) nvs_put_int ("otaonboot", 1);
      else nvs_put_int ("otaonboot", 0);
    }
    else if (strcmp (param[1], "startupdelay") == 0) {
      if (strcmp(param[0], "enable") == 0) nvs_put_int ("startupDelay", 1);
      else nvs_put_int ("startupDelay", 0);
    }
    else if (strcmp (param[1], "telnet") == 0) {
      if (strcmp(param[0], "enable") == 0) nvs_put_int ("telnetEnabled", 0);
      else nvs_put_int ("telnetEnabled", 1);
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewrite (param[1]);
      consolewriteln (" not recognised as an enable/disable setting");
    }
  }
}


/*
 * List command line history
 */
void mt_history (int nparam, char **param)
{
  char outBuffer[BUFFSIZE];
  uint16_t srcPtr = mt_cmdHistoryStart;
  uint16_t dstPtr = 0;

  while (srcPtr != mt_cmdHistoryEnd)
  {
    outBuffer[dstPtr] = mt_cmdHistory[srcPtr];
    if (outBuffer[dstPtr] == '\0') {
      dstPtr = 0;
      if (outBuffer[0] != '\0') consolewriteln (outBuffer);
    }
    else dstPtr++;
    if (++srcPtr == COMMAND_HISTORY) srcPtr = 0;
  }
}


/*
 * List things
 */
void mt_list (int nparam, char **param)
{
  int n, i;
  char msgBuffer[12];
  struct tm timeinfo;
  
  if (nparam==1) {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ((const char*) "Supply something to list, eg devices, tz, vars, dir, history");
  }
  else if (strcmp (param[1], "devices") == 0) {
    n = (sizeof(devType))/DEVTYPESIZE;
    for (i=0 ; i<n ; i++) {
      consolewrite (" * ");
      consolewriteln ((char*)devType[i]);
    }
  }
  else if (strcmp (param[1], "tz") == 0) {
    for (n=0; n<(sizeof(mytz_table)/sizeof(timezone_s)); n++) {
      consolewrite (" * ");
      sprintf (msgBuffer, "%10s ", (char*) mytz_table[n].shortname);
      consolewrite (msgBuffer);
      consolewrite (" --> ");
      consolewriteln ((char*) mytz_table[n].description);
    }
  }
  else if (strcmp (param[1], "dir") == 0) {
    util_listDir (SPIFFS, "/", 0);
    }
  else if (strcmp (param[1], "history") == 0) {
     mt_history (nparam, param);
    }
  else if (strcmp (param[1], "vars") == 0 ) {
    for (int n=0; n<numberOfTypes; n++) {
      if (devTypeCount[n]>0) {
        if      (strcmp (devType[n], "bh1750")   == 0) the_bh1750.printData();
        else if (strcmp (devType[n], "hdc1080")  == 0) the_hdc1080.printData();
        else if (strcmp (devType[n], "ds1820")   == 0) the_wire.printData();
        else if (strcmp (devType[n], "bme280")   == 0) the_bme280.printData();
        else if (strcmp (devType[n], "veml6075") == 0) the_veml6075.printData();
        else if (strcmp (devType[n], "css811")   == 0) the_css811.printData();
        else if (strcmp (devType[n], "ina2xx")   == 0) the_ina2xx.printData();
        else if (strcmp (devType[n], "counter")  == 0) theCounter.printData();
        else if (strcmp (devType[n], "output")   == 0) the_output.printData();
        else if (strcmp (devType[n], "serial")   == 0) the_serial.printData();
        else if (strcmp (devType[n], "adc")      == 0) the_adc.printData();
      }
    }
    consolewrite ("memory.free "); consolewrite (util_ftos (ESP.getFreeHeap(), 0)); consolewriteln (" bytes");
    consolewrite ("memory.minf "); consolewrite (util_ftos (ESP.getMinFreeHeap(), 0)); consolewriteln (" bytes");
    consolewrite ("memory.size "); consolewrite (util_ftos (ESP.getHeapSize(), 0)); consolewriteln (" bytes");
    consolewrite ("memory.time "); consolewrite (util_ftos (esp_timer_get_time() / (uS_TO_S_FACTOR * 60.0), 2)); consolewriteln (" mins");
    consolewrite ("memory.freq "); consolewrite (util_ftos (ESP.getCpuFreqMHz(), 0)); consolewriteln (" MHz");
    consolewrite ("memory.xtal "); consolewrite (util_ftos (getXtalFrequencyMhz(), 0)); consolewriteln (" MHz");
    // It may be some time thing....
    if (strcmp (ntp_server, "none") != 0 && getLocalTime(&timeinfo)){
      consolewrite ("memory.year "); consolewriteln (util_ftos (timeinfo.tm_year + 1900, 0));
      consolewrite ("memory.mont "); consolewriteln (util_ftos (timeinfo.tm_mon + 1, 0));
      consolewrite ("memory.dow  "); consolewriteln (util_ftos (timeinfo.tm_wday, 0));
      consolewrite ("memory.dom  "); consolewriteln (util_ftos (timeinfo.tm_mday, 0));
      consolewrite ("memory.doy  "); consolewriteln (util_ftos (timeinfo.tm_yday, 0));
      consolewrite ("memory.hour "); consolewriteln (util_ftos (timeinfo.tm_hour, 0));
      consolewrite ("memory.min  "); consolewriteln (util_ftos (timeinfo.tm_min,  0));
      consolewrite ("memory.mind "); consolewriteln (util_ftos (timeinfo.tm_min + (60 * timeinfo.tm_hour), 0));
      consolewrite ("memory.secs "); consolewriteln (util_ftos (timeinfo.tm_sec,  0));
    }
  }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ((const char*) "Supply something to list, eg devices, tz");
  }
}

void mt_resistor (int nparam, char **param)
{
  uint8_t devTypePtr = 255;
  int resistance;
  char sensorName[60];
  const char sensorTypes[1][7] = {"ina2xx"};
  bool typeOK = false;
  
  if (nparam == 1) {
    for (uint8_t n=0; n<1; n++) {
      devTypePtr = util_get_dev_type((char*)sensorTypes[n]);
      if (devTypePtr < 250) {
        for (uint8_t j=0; j<devTypeCount[devTypePtr]; j++) {
          sprintf (sensorName, "%sRes_%d", sensorTypes[n], j);
          resistance = nvs_get_int (sensorName, DEFAULT_RESISTOR);
          sprintf (sensorName, "* %s[%d].resi = %d milliohm", sensorTypes[n], j, resistance);
          consolewriteln (sensorName);
        }
      }
    }
  }
  else {
    for (uint8_t n=0; n<1; n++) if (strcmp(sensorTypes[n], param[1]) == 0) typeOK = true;
    if (typeOK) {
      if (util_str_isa_int(param[2])) {
        devTypePtr = util_str2int(param[2]);
        if (util_str_isa_int(param[3])) {
          resistance = util_str2int(param[3]);
          sprintf (sensorName, "%sRes_%d", param[1], devTypePtr);
          nvs_put_int (sensorName, resistance);          
        }
        else {
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Resistance in milliohms should be numeric.");
        }
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("device index must be numeric");
      }
    }
    else {
      if (ansiTerm) displayAnsi(3);
      sprintf (sensorName, "%s devices don't require resistance.", param[1]);
      consolewriteln (sensorName);
    }
  }
}

/*
 * Terminate multiterm
 */
void mt_set_terminate (int nparam, char **param)
{
  int lifetime;
  char msgBuffer[40];
  
  if (nparam == 1) {
    lifetime = nvs_get_int ("multiTermDuration", 0);
    if (lifetime==0) consolewriteln ((const char*) " * continous terminal operation");
    else {
      sprintf (msgBuffer, " * terminal life: %d minutes", lifetime);
    }
  }
  else {
    if (strcmp (param[1], "now") == 0) {
      consolewriteln ("Console service terminating");
      runMultiTerm = false;
    }
    else if (util_str_isa_int (param[1])) {
      lifetime = util_str2int(param[1]);
      if (lifetime >= 0 && lifetime <= 60) {
        nvs_put_int ("multiTermDuration", lifetime);
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ((const char *) "Terminal life time should be 60 minutes or less or zero for indefinite");
      }
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ((const char*) "terminate command not run");
    }
  }
}

/*
 * Over the air update controls
 */
void mt_ota (int nparam, char **param)
{

  if (nparam==1) {
    consolewriteln ((char *) OTAstatus());
  }
  else if (nparam==2) {
    if (strcmp(param[1], "update") == 0) OTAcheck4update();
    else if (strcmp(param[1], "revert") == 0) OTAcheck4rollback();
    else if (strncmp(param[1], "http://", 7) == 0 || strncmp(param[1], "https://", 8) == 0) {
      nvs_put_string ("ota_url", param[1]);
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewrite ("ota parameter not understood: ");
      consolewriteln (param[1]);
    }
  }
}


/* 
 *  Handle output configuration
 */
void mt_output (int nparam, char **param)
{
  uint8_t outDeviceType = util_get_dev_type("output");
  uint8_t indexNum = 0;
  uint8_t pinNum = 99;
  uint8_t outType = 99;
  char msgBuffer[BUFFSIZE];
  char nameBuffer[20];
  
  if (nparam == 1) {
    for (uint8_t n=0; n<MAX_OUTPUT; n++) {
      if (outputCtrl[n].outputPin<40) {
        sprintf (msgBuffer, "output %d, pin %d, type %s, name %s, default %s, condition: ", n, outputCtrl[n].outputPin, outputDescriptor[outputCtrl[n].outputType], outputCtrl[n].uniquename, util_dtos(outputCtrl[n].defaultVal, 3));
        consolewriteln (msgBuffer);
        sprintf (nameBuffer, "outputLogi_%d", n);
        nvs_get_string (nameBuffer, msgBuffer, BUFFSIZE);
        consolewrite ("       ");
        consolewriteln (msgBuffer);
      }
      else {
        if (ansiTerm) displayAnsi(3);
        sprintf (msgBuffer, "output %d disabled", n);
        consolewriteln (msgBuffer);
      }
    }
  }
  else if (nparam==3 && strcmp(param[2], "disable")==0 && util_str_isa_int(param[1])){
    indexNum = util_str2int(param[1]);
    if (indexNum<0 || indexNum>=MAX_OUTPUT) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Index is out of range");
      }
    else {
      if (outputCtrl[indexNum].outputPin<40) {
        if (outputCtrl[indexNum].outputLogic != NULL) {
          free (outputCtrl[indexNum].outputLogic);
          outputCtrl[indexNum].outputLogic = NULL;
        }
        outputCtrl[indexNum].outputPin = 99;
        sprintf (msgBuffer, "outputPin_%d", indexNum);
        nvs_put_int (msgBuffer, 99);
      }
    }
  }
  else if (nparam==4 && strcmp(param[2], "default")==0 && util_str_isa_int(param[1]) && util_str_isa_double(param[3])){
    indexNum = util_str2int(param[1]);
    if (indexNum<0 || indexNum>=MAX_OUTPUT) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Index is out of range");
      }
    else {
      sprintf (msgBuffer, "outputDef_%d", indexNum);
      nvs_put_float (msgBuffer, util_str2float(param[3]));
    }
  }
  else if (nparam>5 && util_str_isa_int(param[1]) && util_str_isa_int(param[2])) {
    indexNum = util_str2int(param[1]);
    pinNum   = util_str2int(param[2]);
    for (uint8_t n=0; n<4; n++) if (strcmp (param[3], outputDescriptor[n]) == 0) outType=n;
    if (outType == 99) {
      if (ansiTerm) displayAnsi(3);
      consolewrite   ("Output type ");
      consolewrite   (param[3]);
      consolewriteln (" not recognised");
    }
    else {
      if (indexNum<0 || indexNum>=MAX_OUTPUT) {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Index is out of range");
      }
      else {
        if (pinNum<40) {
          if (outputCtrl[indexNum].outputLogic != NULL) {
            free (outputCtrl[indexNum].outputLogic);
            outputCtrl[indexNum].outputLogic = NULL;
          }
          sprintf (msgBuffer, "outputPin_%d", indexNum);
          nvs_put_int (msgBuffer, pinNum);
          sprintf (msgBuffer, "outputType_%d", indexNum);
          nvs_put_int (msgBuffer, outType);
          sprintf (msgBuffer, "outputName_%d", indexNum);
          nvs_put_string (msgBuffer, param[4]);
          sprintf (nameBuffer, "outputLogi_%d", indexNum);
          strcpy  (msgBuffer, param[5]);
          for (uint8_t n=6; n<nparam; n++) {
            strcat (msgBuffer, " ");
            strcat (msgBuffer, param[n]);
          }
          nvs_put_string (nameBuffer, msgBuffer);
          strcpy (outputCtrl[indexNum].uniquename, param[4]);
          outputCtrl[indexNum].outputType = outType;
          outputCtrl[indexNum].outputPin  = pinNum;
          util_getLogic (nameBuffer, &outputCtrl[indexNum].outputLogic);
        }
        else {
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Invalid pin number");
        }
      }
    }
  }
}


void mt_quit (int nparam, char **param)
{
  uint8_t killID= 250;
  char msgbuffer[4];

  if (nparam==1 && termID<250) {
    killID = termID;
  }
  else if (nparam == 2) {
    if (util_str_isa_int(param[1])) {
      killID = util_str2int (param[1]);
    }
  }
  if (killID < MAX_TELNET_CLIENTS) {
    if(telnetServerClients[killID]) {
      sprintf (msgbuffer, "%d", killID);
      consolewrite ("Telnet session ");
      consolewrite (msgbuffer);
      consolewriteln (" quitting.");
      telnetServerClients[killID].stop();
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("No telnet clients quitting");
    }
  }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("No telnet clients quitting");
  }
}


/*
 * Hibernate the device for a period"
 */
void mt_hibernate (int nparam, char **param)
{
  int wait_time = 0;
  char msgBuffer[40];
  char startend[2][6];
  bool inerror = false;

  if (nparam==1) {
    nvs_get_string ("hib_start", startend[0], "00:00", 6);
    nvs_get_string ("hib_end",   startend[1], "00:00", 6);
    sprintf (msgBuffer, " * hibernate from %s to %s", startend[0], startend[1]);
    consolewriteln (msgBuffer);
    consolewriteln ("   same start and end time disables hibernation.");
    return;
  }
  if (nparam==2 || ( nparam==3 && (strcmp (param[2], "mins")==0 || strcmp (param[2], "hours")==0 || strcmp (param[2], "days")==0))) {
    if (!util_str_isa_int(param[1])) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("The number of mins/hours/days to hibernate should be an integer value");
      return;
    }
    wait_time = util_str2int(param[1]);
    if (nparam==3 && strcmp(param[2], "hours")==0) wait_time = wait_time * 60;
    else if (nparam==3 && strcmp(param[2], "days" )==0) wait_time = wait_time * 60 * 24;
    else if (nparam==3 && strcmp(param[2], "mins" )!=0) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("The hibernation period should be one of mins/hours/days");
      return;
    }
    if (wait_time < 1) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("hibernate time must be 1 or more minutes in duration.");
      return;
    }
    sprintf (msgBuffer, "hibernate minutes: %d", wait_time);
    consolewriteln (msgBuffer);
    if (wait_time > 0) util_start_deep_sleep (wait_time);
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln (" * Wait period must be 1 minute or longer");
    }
  }
  else if (nparam==3) {
    for (int n=1; n<3; n++) {
      if (!util_str_isa_time (param[n])) inerror = true;
    }
    if (inerror) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("hibernate start and end time should be in hh:mm format");
      consolewriteln ("eg: hibernate 18:15 05:45");
    }
    else {
      nvs_put_string ("hib_start", param[1]);
      nvs_put_string ("hib_end", param[2]);
    }
  }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("Hibernation needs start and end times OR immediate hibernation time");
  }
}


/*
 * Set interval device types
 */
void mt_dev_interval (int nparam, char **param)
{
  char n;
  char message[17];
  int  interval;

  // NB deviceType 0 - counters don't have a meaningful "interval" definition
  if (nparam == 1) {
    for (n=1; n<numberOfTypes; n++) if (devTypeCount[n] > 0) {
      sprintf (message, "defaultPoll_%d", (int) n);
      interval = nvs_get_int (message, DEFAULT_INTERVAL);
      consolewrite (" * ");
      consolewrite ((char*) devType[n]);
      sprintf (message, " %d", interval);
      consolewrite (message);
      consolewriteln (" seconds");
    }
  }
  else {
    for (n=1; n<numberOfTypes && strcmp(devType[n], param[1])!=0; n++);
    if (n==numberOfTypes) {
      consolewrite ("Unknown device type: ");
      consolewriteln (param[1]);
    }
    else {
      if (util_str_isa_int(param[2])) {
        interval = util_str2int (param[2]);
        if (interval > 0 && interval<=300) {
          sprintf (message, "defaultPoll_%d", (int) n);
          nvs_put_int (message, interval);
          consolewriteln ("Interval will be effective after next reboot");
          if (interval > 0 && (strcmp(param[1], "css811")==0 || strcmp (param[1], "serial")==0)) {
            if (ansiTerm) displayAnsi(4);
            consolewriteln ("WARNING: some devices such as CO2 measurement should not run less frequently");
            consolewriteln ("         than once per second to ensure good measurement.");
          }
        }
        else {
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Interval should be between 1 and 300 seconds");
        }
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Specify interval in seconds");
      }
    }
  }
}

/*
 * Set opacity factor for light sensors
 */
void mt_set_opacity (int nparam, char **param)
{
  int deviceNr = 0;
  float opacity = 1.2;
  
  if (nparam==1) {
    // Go through devices supporting opacity and list settings
    consolewriteln (" * Opacity settings:");
    the_bh1750.displayOpacity();
  }
  // Initially only bh1750 supports this
  else if (nparam==4 && strcmp (param[1],"bh1750")==0 && util_str_isa_int(param[2]) && util_str_isa_double(param[3])) {
    deviceNr = util_str2int(param[2]);
    opacity = util_str2float(param[3]);
    if (deviceNr<0 || deviceNr>3) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Device number must be between 0 and 3");
      return;
    }
    if (opacity<0.5 || opacity>1.5) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Opacity should be between 0.5 and 1.5");
      return;
    }
    the_bh1750.setOpacity (deviceNr, opacity);
  }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("Opacity command not understood");
  }
}


/*
 * Set altitude by air pressure
 */
void mt_set_qnh (int nparam, char **param)
{
  float qnh;
  struct bme280_s *myData;
  uint8_t devTypePtr = 255;
  char nameBuffer[50];

  // Only bme280 supports pressure at this stage
  devTypePtr = util_get_dev_type("bme280");
  if (devTypePtr == 255 || devTypeCount[devTypePtr] == 0) {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("No supported pressure measurement devices found.");
    return;
  }
  myData = (struct bme280_s*) devData[devTypePtr];
  if (nparam==1) {
    for (int n=0; n<devTypeCount[devTypePtr]; n++) {
      consolewrite (" * bme280.");
      sprintf (nameBuffer, "%d (%s), %s", n, myData[n].uniquename, util_ftos(myData[n].altitude, 1));
      consolewrite (nameBuffer);
      sprintf (nameBuffer, "m / %sft", util_ftos((myData[n].altitude * 3.280839895), 1));
      consolewriteln (nameBuffer);
    }
  }
  else if (nparam==2 && util_str_isa_double (param[1])) {
    qnh = util_str2float(param[1]);
    for (int n=0; n<devTypeCount[devTypePtr]; n++) {
      if (myData[n].averagedOver < 1) {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Insufficient data to set altitude, wait 5 minutes and try again");
      }
      else {
        sprintf (nameBuffer, "%sAlt_%d", devType[devTypePtr], n);
        myData[n].altitude = util_calcAltitude (myData[n].uncompensatedPres, qnh, myData[n].temp_average);
        nvs_put_float (nameBuffer, myData[n].altitude);
      }
    }
  }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("Specify the pressure adjusted to sea level as a parameter");
  }
}

/*
 * Set state of Identify pin
 */
void mt_write_id_state (char state)
{
  switch (state) {
    case ID_OFF: consolewriteln ("Off");
         break;
    case ID_ON: consolewriteln ("On");
         break;
    case ID_FLASH: consolewriteln ("Flash");
         break;
    case ID_FFLASH: consolewriteln ("Fast-Flash");
         break;
    case ID_SFLASH: consolewriteln ("Slow-Flash");
         break;
  }
}

void mt_set_identify_output()
{
  static char lastState = 0;
  char invertChk = 0;

  if (mt_identity_pin != 99) {
    if (mt_identity_state == ID_OFF) lastState = 0;       // Off required
    else if (mt_identity_state == ID_ON) lastState = 1;   // On required
    else lastState = abs(lastState - 1);                  // Flash
    if (mt_identity_mode == 1) invertChk = abs(lastState - 1);
    else invertChk = lastState;                           // Invert check
    if (invertChk==0) digitalWrite(mt_identity_pin, LOW); // set output
    else digitalWrite(mt_identity_pin, HIGH);
  }
}

void mt_identify (int nparam, char **param)
{
  char identity_state = ID_OFF;
  identity_state = (char) nvs_get_int ("id_state", ID_FFLASH);
  char msgBuffer[50];
  char id_mode[2][10] = {"normal", "inverted"};
  int val;

  if (nparam==1) {
    if (mt_identity_pin == 99) consolewriteln (" * ID pin is unconfigured.");
    else {
      sprintf (msgBuffer, " * Saved state: pin: %d, mode %s, state: ", mt_identity_pin, id_mode[mt_identity_mode]);
      consolewrite (msgBuffer);
      mt_write_id_state (identity_state);
    }
    consolewrite (" * Displayed state: ");
    mt_write_id_state (mt_identity_state);
  }
  else if (util_str_isa_int(param[1])) {
    val = util_str2int(param[1]);
    if (val>=0 && val<40) {
      nvs_put_int("id_pin", val);
      mt_identity_pin = val;
      pinMode(val, OUTPUT);
      mt_set_identify_output();
      if (nparam==3 && strcmp(param[2], "invert") == 0) {
        nvs_put_int ("id_mode", 1);
        mt_identity_mode = 1;
      }
      else {
        nvs_put_int ("id_mode", 0);
        mt_identity_mode = 0;        
      }
      mt_set_identify_output();
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewrite ("ID pin should be between 0 and 39");
    }
  }
  else if (strcmp(param[1], "on") == 0 || strcmp(param[1], "off") == 0 || strcmp(param[1], "flash") == 0 || strcmp(param[1], "fflash") == 0 || strcmp(param[1], "sflash") == 0) {
   if (strcmp(param[1], "off") == 0) {
    mt_identity_state = ID_OFF;
   }
   else if (strcmp(param[1], "on") == 0) {
    mt_identity_state = ID_ON;
   }
   else if (strcmp(param[1], "flash")  == 0) mt_identity_state = ID_FLASH;
   else if (strcmp(param[1], "fflash") == 0) mt_identity_state = ID_FFLASH;
   else mt_identity_state = ID_SFLASH;
   if (nparam==3 && strcmp (param[2], "persist") == 0) nvs_put_int ("id_state", mt_identity_state);
   mt_set_identify_output();
  }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("identify command not understood");
  }
}


/*
 * Telnet configuration
 */
void mt_telnet (int nparam, char **param)
{
  int telnetstate;
  char i;
  
  if (nparam == 1) {
    char msgBuffer[3];
    telnetstate = nvs_get_int("telnetEnabled", 0);
    consolewrite (" * telnet ");
    if (telnetstate == 0) {
      sprintf (msgBuffer, "%d", MAX_TELNET_CLIENTS);
      consolewrite ("enabled, max concurrent clients = ");
      consolewriteln (msgBuffer);
      if (telnetRunning) {
        for(i = 0; i < MAX_TELNET_CLIENTS; i++){
          if (telnetServerClients[i] && telnetServerClients[i].connected()){
            sprintf (msgBuffer, "%d", i);
            consolewrite (" * client: ");
            consolewrite (msgBuffer);
            consolewrite (", ");
            consolewriteln (net_ip2str ((uint32_t) telnetServerClients[i].remoteIP()));
          }
        }
      }
    }
    else consolewriteln ("disabled");
  }
  else if (strcmp (param[1], "disable") == 0 || strcmp (param[1], "enable") == 0) {
    if (strcmp (param[1], "enable") == 0) telnetstate = 0;
    else telnetstate =1;
    nvs_put_int ("telnetEnabled", telnetstate);
  }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("parameter should be enable or disable");
  }
}

/*
 * set up i2c pins
 */
void mt_set_i2c (int nparam, char **param)
{
  int n=0;  // bus Number
  int sdapin;
  int sclpin;
  int pinNumber;
  int i2c_speed;
  char msgBuffer[80];
  bool in_error = false;

  if (nparam==1) {
    for (n=0; n<2; n++) {
      pinNumber = 0;
      #ifdef SDA
      if (n==0) pinNumber = SDA;
      #endif
      sprintf (msgBuffer, "sda_%d", n);
      sdapin = nvs_get_int (msgBuffer, pinNumber);
      pinNumber = 0;
      #ifdef SCL
      if (n==0) pinNumber = SCL;
      #endif
      sprintf (msgBuffer, "scl_%d", n);
      sclpin = nvs_get_int (msgBuffer, pinNumber);
      if (sdapin != sclpin) {
        sprintf (msgBuffer, "i2c_speed_%d", n);
        i2c_speed = nvs_get_int(msgBuffer, 0);
        sprintf (msgBuffer, " * i2c bus-%d enabled, pin assignment: SDA=%d, SCL=%d, speed=%d", n, sdapin, sclpin, i2c_speed);
        consolewriteln (msgBuffer);
      }
      else {
        sprintf (msgBuffer, " * i2c bus-%d disabled", n);
        consolewriteln (msgBuffer);
      }
    }
  }
  else if (nparam > 3) {
    n = util_str2int (param[1]);
    if (n<0 || n>1 || !util_str_isa_int(param[1])) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln (" * i2c bus number should be either 0 or 1");
      in_error = true;
    }
    sdapin = util_str2int (param[2]);
    if (sdapin < 0 || sdapin > 39 || !util_str_isa_int(param[2])) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln (" * sda pin number is out of range");
      in_error = true;
    }
    sclpin = util_str2int (param[3]);
    if (sclpin < 0 || sclpin > 39 || !util_str_isa_int(param[3])) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln (" * scl pin number is out of range");
      in_error = true;
    }
    if (in_error) return;
    sprintf (msgBuffer, "sda_%d", n);
    nvs_put_int (msgBuffer, sdapin);
    sprintf (msgBuffer, "scl_%d", n);
    nvs_put_int (msgBuffer, sclpin);
    if (ansiTerm) displayAnsi(0);
    consolewriteln ("change effective after \"restart\"");
  }
  if (nparam == 5) {
    i2c_speed = util_str2int (param[4]);
    if (i2c_speed != 0 && i2c_speed != 10000 && i2c_speed != 100000 && i2c_speed != 400000 && util_str_isa_int(param[4])) {
      if (ansiTerm) displayAnsi(3);
      consolewriteln (" * i2c speed should be one of {0|10000|100000|400000}");
      consolewriteln ("   where 0 means chip default");
      return;
    }
    sprintf (msgBuffer, "i2c_speed_%d", n);
    nvs_put_int (msgBuffer, i2c_speed);
  }
}


/*
 * Timezone copnfiguration
 */
void mt_set_timezone (int nparam, char **param)
{
  // Ref: https://www.iana.org/time-zones
  // Ref: http://gnu.org/software/libc/manual/html_node/TZ-Variable.html
  char msgBuffer[80];
  int n;
  int limit = (sizeof(mytz_table)/sizeof(timezone_s));
  bool notfound = true;

  if (nparam == 1) {
    nvs_get_string ("timezone", msgBuffer, "WAT-01:00", sizeof(msgBuffer));  // Default East African Time
    consolewrite   (" * timezone = ");
    consolewriteln (msgBuffer);
    consolewrite   ("   ");
    consolewriteln (util_gettime());
  }
  else {
    for (n=0; n<strlen(param[1]); n++) param[1][n] = toupper(param[1][n]);
    if (strlen(param[1])>sizeof(msgBuffer)-1) param[1][sizeof(msgBuffer)-1] = '\0';
    for (n=0, notfound=true; notfound && n<limit; n++) {
      if (strcmp (param[1], (char*) mytz_table[n].shortname) == 0) {
        setenv("TZ", (char*) mytz_table[n].description, 1);
        nvs_put_string ("timezone", (char*) mytz_table[n].description);
        notfound = false;
      }
    }
    if (notfound) {
      setenv("TZ", param[1], 1);
      nvs_put_string ("timezone", param[1]);
    }
  }
}

void mt_ntp(int nparam, char **param)
{
  if (nparam==1) {
    consolewrite (" * NTP Server = ");
    nvs_get_string ("ntp_server", ntp_server, "pool.ntp.org", sizeof(ntp_server));
    consolewriteln (ntp_server);
  }
  else {
    if (strlen (param[1]) > sizeof(ntp_server)) param[1][sizeof(ntp_server)-1] = '\0';    // truncate if too long
    strcpy (ntp_server, param[1]);
    nvs_put_string ("ntp_server", param[1]);
  }
}

void mt_set_wifimode(int nparam, char **param)
{
  int wmode = 99;
  
  if (nparam==1) {
    consolewrite (" * wifi mode: ");
    wmode = nvs_get_int ("wifimode", 0);
    if (wmode == 0) consolewriteln ("always on");
    else consolewriteln ("on demand");
  }
  else {
    if (strcmp (param[1], "alwayson") == 0) wmode = 0;
    else if (strcmp (param[1], "ondemand") == 0) wmode = 1;
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln (" * wifimode should be alwayson or ondemand");
      return;
    }
    if (wmode==0 || wmode==1) nvs_put_int ("wifimode", wmode);
    wifimode = wmode;
  }
}


void mt_set_wifiscan(int nparam, char **param)
{
  int use_multiwifi = 99;
  
  if (nparam==1) {
    consolewrite (" * wifi scan: ");
    use_multiwifi = nvs_get_int ("use_multiwifi", 1);
    if (use_multiwifi == 0) consolewriteln ("disabled");
    else consolewriteln ("enabled");
  }
  else {
    if (strcmp (param[1], "disabled") == 0) use_multiwifi = 0;
    else if (strcmp (param[1], "enabled") == 0) use_multiwifi = 1;
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln (" * wifiscan should be enabled or disabled");
      return;
    }
    if (use_multiwifi==0 || use_multiwifi==1) nvs_put_int ("use_multiwifi", use_multiwifi);
  }
}

void mt_set_wifi (int nparam, char **param)
{
  int unit = 0;
  char msgBuffer[14];
  char outline[33];
  int startpt=0;
  int endpt = 3;

  if (nparam>1) unit = util_str2int(param[1]);
  /*
   * Print settings
   */
  if (nparam<3) {
    if (nparam==2) {
      startpt = unit;
      endpt   = unit;   
      }
    for (;startpt<=endpt; startpt++) {
      sprintf (outline, " * wifi-%d: ", startpt);
      consolewrite (outline);
      sprintf (msgBuffer, "wifi_ssid_%d", startpt);
      nvs_get_string (msgBuffer, outline, "none", sizeof(outline));
      consolewriteln (outline);      
      }
    return;
    }
  /*
   * Update settings
   */
  if (unit>=0 && unit<4) {
    sprintf (msgBuffer, "wifi_ssid_%d", unit);
    if (strlen(param[2])>32) param[2][32] = '\0';
    strcpy (wifi_ssid[unit], param[2]);
    nvs_put_string (msgBuffer, wifi_ssid[unit]);
    sprintf (msgBuffer, "wifi_passwd_%d", unit);
    if (nparam==4) {
      if (strlen(param[3])>32) param[3][32] = '\0'; 
      strcpy (wifi_passwd[unit], param[3]);
    }
    else {
      strcpy (wifi_passwd[unit], "none");
    }
    nvs_put_string (msgBuffer, wifi_passwd[unit]);
    consolewriteln ("change becomes effective on \"restart\"");
  }
  else {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("Error: ssid number must be between 0 and 3");
  }
}

void  mt_set_xysecret (int nparam, char **param)
{
  char xysecret[80];

  if (nparam==1) {
    nvs_get_string ("xysecret", xysecret, "none", sizeof(xysecret));
    consolewrite (" * ");
    consolewriteln (xysecret);
  }
  else {
    nvs_put_string ("xysecret", param[1]);
  }
}

void  mt_set_xyserver (int nparam, char **param)
{
  char xyserver[80];

  if (nparam==1) {
    nvs_get_string ("xyserver", xyserver, "<UNDEFINED>", sizeof(xyserver));
    consolewrite (" * ");
    consolewrite (xyserver);
    if (strncmp (xyserver, "http", 4) == 0) consolewriteln ("\n");
    else {
      sprintf (xyserver, ":%d", nvs_get_int ("xyport", 1984));
      consolewriteln (xyserver);
    }
  }
  else {
    nvs_put_string ("xyserver", param[1]);
    if (nparam==3 && util_str_isa_int(param[2])) {
      int xyport = util_str2int(param[2]);

      if (xyport>0 && xyport<65535) {
        nvs_put_int ("xyport", xyport);
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ((const char*) "Error: Invalid port number");
      }
    }
  }
}

void mt_sys_restart (char *reason)
{
  if (ansiTerm) displayAnsi(3);
  consolewrite   ("Restarting system: ");
  consolewriteln (reason);
  if (ansiTerm) displayAnsi(0);
  for (uint8_t killID=0; killID<MAX_TELNET_CLIENTS ; killID++) {
    if (telnetServerClients[killID]) {
      telnetServerClients[killID].stop();
    }
  }
  esp_restart();  
}


void mt_erase_config()
{
  if (ansiTerm) displayAnsi(3);
  consolewriteln ("Erasing config and rebooting system");
  consolewriteln ("Clearing non-volatile-storage (NVS) memory");
  nvs_flash_erase();
  consolewriteln ("Formatting file system");
  util_format_spiffs();
  consolewriteln ("Rebooting");
  esp_restart();
}

void mt_get_nvs (int nparam, char **param)
{
  char msgBuffer[BUFFSIZE];
  int  nvsint;
  float nvsfloat;

  if (strcmp (param[0], "nvsstr") == 0) nvs_get_string (param[1], msgBuffer, "", sizeof(msgBuffer));
  else if (strcmp (param[0], "nvsint") == 0) {
    nvsint = nvs_get_int (param[1], 0);
    sprintf (msgBuffer, "%d", nvsint);
  }
  else if (strcmp (param[0], "nvsfloat") == 0) {
    nvsfloat = nvs_get_float (param[1], 0.0);
    sprintf (msgBuffer, "%5.3", nvsfloat);
  }
  consolewrite (param[1]);
  consolewrite (" is ");
  consolewriteln (msgBuffer);
}

void mt_set_dallas (int nparam, char **param)
{
  char sensorName[SENSOR_NAME_LEN];
  uint8_t pin, index;

  if (nparam == 1) {
    for (index=0; index<MAX_ONEWIRE; index++) {
      sprintf (sensorName, "DallasPin%d", index);
      pin = nvs_get_int(sensorName, 99);
      if (pin < 36) {
        consolewrite ("OneWire Bus ");
        sprintf (sensorName, "%d, pin %d", index, pin);
        consolewriteln (sensorName);
      }
    }
    the_wire.inventory();
  }
  if (nparam == 3) {
    if (util_str_isa_int(param[1])) {
      index = util_str2int (param[1]);
      sprintf (sensorName, "DallasPin%d", index);
      if ((param[2], "disable") == 0) {
        nvs_put_int (sensorName, 99);
      }
      else {
        if (util_str_isa_int(param[2])) {
          pin = util_str2int (param[2]);
          if (pin < 36 && pin >= 0) {
            nvs_put_int (sensorName, pin);
          }
          else { if (ansiTerm) displayAnsi(3); consolewriteln ("OneWire pin number must < 36"); }
        }
        else { if (ansiTerm) displayAnsi(3); consolewriteln ("OneWire pin number must be numeric"); }
      }
    }
    else { if (ansiTerm) displayAnsi(3); consolewriteln ("OneWire bus number must be numeric"); }
  }
}

void mt_set_alert (int nparam, char **param)
{
  uint8_t alMode = 0;
  uint8_t devIndex = 0;
  uint8_t targetDevType = 255;
  uint8_t isOK = 0;
  char nvsName[SENSOR_NAME_LEN];
  char rpnBuffer[BUFFSIZE];
  char *subDesc, *tPtr;
  char subLimit = 0;

  if (strcmp(param[0], "critical") == 0) alMode = 1;
  else if (strcmp(param[0], "extreme") == 0) alMode = 2;
  if (nparam == 1) {
    uint8_t start=0;
    uint8_t endpt;
    for (uint8_t devx=0; devx<numberOfTypes; devx++) {
      subLimit = 0;
      start = 0;
      endpt = devTypeCount[devx];
      // if (devTypeCount[devx]>0) {
        if      (strcmp (devType[devx], "veml6075") == 0) { subLimit = the_veml6075.subtypeLen; subDesc = &(the_veml6075.subtypeList[0][0]); }
        else if (strcmp (devType[devx], "bh1750")   == 0) { subLimit = the_bh1750.subtypeLen;   subDesc = &(the_bh1750.subtypeList[0][0]);   }
        else if (strcmp (devType[devx], "bme280")   == 0) { subLimit = the_bme280.subtypeLen;   subDesc = &(the_bme280.subtypeList[0][0]);   }
        else if (strcmp (devType[devx], "css811")   == 0) { subLimit = the_css811.subtypeLen;   subDesc = &(the_css811.subtypeList[0][0]);   }
        else if (strcmp (devType[devx], "hdc1080")  == 0) { subLimit = the_hdc1080.subtypeLen;  subDesc = &(the_hdc1080.subtypeList[0][0]);  }
        else if (strcmp (devType[devx], "ina2xx")   == 0) { subLimit = the_ina2xx.subtypeLen;   subDesc = &(the_ina2xx.subtypeList[0][0]);   }
        else if (strcmp (devType[devx], "counter")  == 0) { subLimit = theCounter.subtypeLen;   subDesc = &(theCounter.subtypeList[0][0]);   }
        else if (strcmp (devType[devx], "output")   == 0) { subLimit = the_output.subtypeLen;   subDesc = &(the_output.subtypeList[0][0]);   }
        else if (strcmp (devType[devx], "ds1820")   == 0) { subLimit = the_wire.subtypeLen;     subDesc = &(the_wire.subtypeList[0][0]);     }
        else if (strcmp (devType[devx], "serial")   == 0) { subLimit = the_serial.subtypeLen;   subDesc = &(the_serial.subtypeList[0][0]);   }
        else if (strcmp (devType[devx], "adc")      == 0) { subLimit = the_adc.subtypeLen;      subDesc = &(the_adc.subtypeList[0][0]);      start = 32 ; endpt = 40; }
      // }
      // format of name is first four letters of device type + variable name + warning/crit/extreme index + device number,
      // eg for counter-5 warning: counvar_05
      //
      for (char limit=0; limit<subLimit; limit++) {
        for (uint8_t devNr=start; devNr<endpt; devNr++) {
          strcpy (nvsName, devType[devx]);
          nvsName[4] = '\0';
          tPtr = subDesc + (limit * 5);
          sprintf (rpnBuffer, "%s_%d%d", tPtr, alMode, devNr);
          strcat (nvsName, rpnBuffer);
          nvs_get_string (nvsName, rpnBuffer, "disable", BUFFSIZE);
          if (strcmp (rpnBuffer, "disable") != 0) {
            sprintf (nvsName, ".%d %s: ", devNr, tPtr);
            consolewrite (param[0]);
            consolewrite (" -> ");
            consolewrite ((char*) devType[devx]);
            consolewrite (nvsName);
            consolewriteln (rpnBuffer);
          }
        }
      }
    }
  }
  else {
    targetDevType = util_get_dev_type(param[1]);
    if (targetDevType < 250) {
      if (util_str_isa_int(param[2])) {
        devIndex = util_str2int (param[2]);
        // Allow ds1820 devices to be out of range
        if (devIndex >= 0 && (devIndex < devTypeCount[targetDevType] || strcmp (param[1], "ds1820") == 0 || (strcmp (param[1], "adc") == 0 && devIndex >=32 && devIndex < 40))) {
          isOK = 0;
          if      (strcmp (param[1], "veml6075") == 0) { subLimit = the_veml6075.subtypeLen; subDesc = &(the_veml6075.subtypeList[0][0]); }
          else if (strcmp (param[1], "bh1750")   == 0) { subLimit = the_bh1750.subtypeLen;   subDesc = &(the_bh1750.subtypeList[0][0]);   }
          else if (strcmp (param[1], "bme280")   == 0) { subLimit = the_bme280.subtypeLen;   subDesc = &(the_bme280.subtypeList[0][0]);   }
          else if (strcmp (param[1], "css811")   == 0) { subLimit = the_css811.subtypeLen;   subDesc = &(the_css811.subtypeList[0][0]);   }
          else if (strcmp (param[1], "hdc1080")  == 0) { subLimit = the_hdc1080.subtypeLen;  subDesc = &(the_hdc1080.subtypeList[0][0]);  }
          else if (strcmp (param[1], "ina2xx")   == 0) { subLimit = the_ina2xx.subtypeLen;   subDesc = &(the_ina2xx.subtypeList[0][0]);   }
          else if (strcmp (param[1], "counter")  == 0) { subLimit = theCounter.subtypeLen;   subDesc = &(theCounter.subtypeList[0][0]);   }
          else if (strcmp (param[1], "output")   == 0) { subLimit = the_output.subtypeLen;   subDesc = &(the_output.subtypeList[0][0]);   }
          else if (strcmp (param[1], "ds1820")   == 0) { subLimit = the_wire.subtypeLen;     subDesc = &(the_wire.subtypeList[0][0]);     }
          else if (strcmp (param[1], "serial")   == 0) { subLimit = the_serial.subtypeLen;   subDesc = &(the_serial.subtypeList[0][0]);   }
          else if (strcmp (param[1], "adc")      == 0) { subLimit = the_adc.subtypeLen;      subDesc = &(the_adc.subtypeList[0][0]);      }
          for (char limit=0; limit<subLimit && isOK==0; limit++) {
            tPtr = subDesc + (limit * 5);
            if (strcmp (param[3], tPtr) == 0) isOK = 1;
          }
          if (isOK > 0) {
            if (strlen(param[1]) > 4) param[1][4] = '\0';
            sprintf (nvsName, "%s%s_%d%d", param[1], param[3], alMode, devIndex);
            rpnBuffer[0] = '\0';
            for (uint8_t n=4; n<nparam; n++) {
              if (n>4) strcat (rpnBuffer, " ");
              strcat (rpnBuffer, param[n]);
            }
            nvs_put_string (nvsName, rpnBuffer);
          }
          else {
            if (ansiTerm) displayAnsi(3);
            consolewrite (param[3]);
            consolewrite (" is not a recognised parameter for ");
            consolewrite (param[1]);
            consolewrite (" (try:");
            for (char limit=0; limit<subLimit && isOK==0; limit++) {
              tPtr = subDesc + (limit * 5);
              consolewrite (" ");
              consolewrite (tPtr);
            }
            consolewriteln (")");
          }
        }
        else {
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Device index is out of range for existing devices");
        }
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Device index must be numeric");
      }
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewrite (param[1]);
      consolewriteln (" is not a supported device type");
    }
  }
}


void mt_help(char *query)
{
  if (query==NULL || strcmp(query, "adc") == 0) {
    consolewriteln ((const char*) "adc [<pin> disable]");
    consolewriteln ((const char*) "adc [<pin> <unit-of-measure> <attenuation> <offset> <multiplier>]");
    consolewriteln ((const char*) "    Configure adc (Analogue to Digital Conversion)");
    consolewriteln ((const char*) "    NB: pin number must be in range 32-39");
    consolewriteln ((const char*) "        attenuation range 0-3 sets FSD: 0=800mV, 1=1.1V, 2=1.35V, 3=2.6V");
    consolewriteln ((const char*) "        voltages beyond those indicated may produce non-linear results");
  }
  if (query==NULL || strcmp(query, "altitude") == 0) {
    consolewriteln ((const char*) "altitude [<altitude> [m|ft]]");
    consolewriteln ((const char*) "    Set altitude of unit for pressure compensation");
  }
  if (query==NULL || strcmp(query, "ansi") == 0) {
    consolewriteln ((const char*) "ansi [{bold|command|error|normal|time} <attribute-list>]");
    consolewriteln ((const char*) "ansi [test]");
    consolewriteln ((const char*) "    Set colour attributes when ANSI colourisation is enabled");
    consolewriteln ((const char*) "    Attribute-list is a semicolon separated list of the following:");
    consolewriteln ((const char*) "    Attributes: support may vary between terminal types");
    consolewriteln ((const char*) "        0 - reset       1 - bold      3 - italic     4 - underline");
    consolewriteln ((const char*) "        5 - blink       7 - reverse video            9 - strikeout");
    consolewriteln ((const char*) "    Colours: \"30 + number\" for text, \"40 + number\" for background");
    consolewriteln ((const char*) "        0 - black       1 - red       2 - green      3 - yellow");
    consolewriteln ((const char*) "        4 - purple      5 - purple    6 - cyan       7 - white");
    consolewriteln ((const char*) "    \"test\" gives a preview of the colour combinations described above");
  }
#ifdef USE_BLUETOOTH
  if (query==NULL || strcmp(query, "bluetooth") == 0) {
    consolewriteln ((const char*) "bluetooth [<pin>|none|disable]");
    consolewriteln ((const char*) "    Enable with 4 digit pin or no pin or disable Bluetooth serial console");
  }
#endif
  if (query==NULL || strcmp(query, "cert") == 0) {
    consolewriteln ((const char*) "cert [{ota|xymon} <fileName>]");
    consolewriteln ((const char*) "    Certificate file to use if using https for transfers");
  }
  if (query==NULL || strcmp(query, "clear") == 0) {
    consolewriteln ((const char*) "clear");
    consolewriteln ((const char*) "    Clear terminal contents");
  }
  if (query==NULL || strcmp(query, "combine") == 0) {
    consolewriteln ((const char*) "combine [<unitName>]");
    consolewriteln ((const char*) "    Combine graphs from this unit with <unitName>");
  }
  if (query==NULL || strcmp(query, "compensate") == 0) {
    consolewriteln ((const char*) "compensate [css811 <n> [hdc1080|bme280] <i>]");
    consolewriteln ((const char*) "    Apply temperature compensation to css811[n] from device[i]");
  }
  if (query==NULL || strcmp(query, "config") == 0) {
    consolewriteln ((const char*) "config");
    consolewriteln ((const char*) "    Display configuration");
  }
  if (query==NULL || strcmp(query, "constant") == 0) {
    consolewriteln ((const char*) "constant [<0-9> <value> [<label]]");
    consolewriteln ((const char*) "    define a constant against memory index and optionally label it.");
    consolewriteln ((const char*) "    recall using rpn calculator with\"k\" operator, eg: \"rpn adc.var 7 k *\"");
    consolewriteln ((const char*) "    The label may be up to 20 chars long, it is only displayed by \"constant\"");
  }
  if (query==NULL || strcmp(query, "counter") == 0) {
    consolewriteln ((const char*) "counter <0-7> disable");
    consolewriteln ((const char*) "counter <0-7> <0-39> <unique-name> <unit_of_measure> <offset> <multiplier>");
    consolewriteln ((const char*) "    configure counter <0-3> on pin <0-39> with a unique name/identifier with a");
    consolewriteln ((const char*) "    straight line conversion of <offset> and <multiplier> (default 0.0 and 1.0)");
  }
  if (query==NULL || strcmp(query, "critical") == 0 || strcmp(query, "extreme") == 0 || strcmp(query, "warning") == 0) {
    consolewriteln ((const char*) "critical|extreme|warning <devicetype> <0-n> <val> disable");
    consolewriteln ((const char*) "critical|extreme|warning <devicetype> <0-n> <val> <rpn-expression>");
    consolewriteln ((const char*) "    enable or disable alerting thresholds on sensors");
    consolewriteln ((const char*) "    Use \"inventory\" to list devicetype, val follows first 4 letters of test");
    consolewriteln ((const char*) "      adc:      adc");
    consolewriteln ((const char*) "      bh1750:   lux");
    consolewriteln ((const char*) "      bme280:   temp, humi, pres");
    consolewriteln ((const char*) "      counter:  var");
    consolewriteln ((const char*) "      css811:   co2, tvoc");
    consolewriteln ((const char*) "      ds1820:   temp");
    consolewriteln ((const char*) "      ina2xx:   amps, volt, watt");
    consolewriteln ((const char*) "      veml6075: uv");
    consolewriteln ((const char*) "      output:   outp");
    consolewriteln ((const char*) "      serial:   var");
  }
  if (query==NULL || strcmp(query, "cpuspeed") == 0) {
    consolewrite   ((const char*) "cpuspeed [240|160|80");
#ifdef ENABLE_LOW_FREQ
    // FreeRTOS / WiFi appears to have issues if using speeds of less than 80.
    switch ((int) getXtalFrequencyMhz()) {
      case 40: consolewrite ((const char*) "|40|20|10");
             break;
      case 26: consolewrite ((const char*) "|26|13");
             break;
      case 24: consolewrite ((const char*) "|24|12");
             break;
    }
#endif
    consolewriteln ((const char*) "|0]");
    consolewriteln ((const char*) "    Set CPU speed in MHz, try to use the lowest viable to save power consumption");
    consolewriteln ((const char*) "    Use zero to use factory default speed");
  }
  if (query==NULL || strcmp(query, "del") == 0) {
    consolewriteln ((const char*) "del <filename>");
    consolewriteln ((const char*) "    Delete a file");
  }
  if (query==NULL || strcmp(query, "devicename") == 0) {
    consolewriteln ((const char*) "devicename [<device-name>]");
    consolewriteln ((const char*) "    Set unique device name to identify this unit (16 char max length, avoid spaces)");
    consolewriteln ((const char*) "devicename <devicetype> <0-n> <device-name>");
    consolewriteln ((const char*) "    Name a sensor device, use \"inventory\" to list device types");
  }
  if (query==NULL || strcmp(query, "dewpoint") == 0) {
    consolewriteln ((const char*) "dewpoint [bme280|hdc1080] <0-n> <dewpointname|\"none\">");
    consolewriteln ((const char*) "    Name a dew point for a temperature sensor, or use none to disable");
  }
  if (query==NULL || strcmp(query, "dir") == 0) {
    consolewriteln ((const char*) "dir");
    consolewriteln ((const char*) "    Directory listing of file system");
  }
  if (query==NULL || strcmp(query, "dump") == 0) {
    consolewriteln ((const char*) "dump <devicetype>");
    consolewriteln ((const char*) "    Dump raw data held for a device type, use \"list devices\" to list types");
  }
  if (query==NULL || strcmp(query, "enable") == 0 || strcmp(query, "disable") == 0) {
    consolewriteln ((const char*) "[enable|disable] [ansi|consolelog|memory|otaonboot|showlogic|startupdelay|telnet]");
    consolewriteln ((const char*) "    Enable or disable ANSI colourisation of console text");
    consolewriteln ((const char*) "    Enable or disable logging of console input to /console.log");
    consolewriteln ((const char*) "    Enable or disable display of memory settings");
    consolewriteln ((const char*) "    Enable or disable check for over the air (ota) update on booting");
    consolewriteln ((const char*) "    Enable or disable display of rpn logic for alerts");
    consolewriteln ((const char*) "    Enable or disable 30 second delay on start");
    consolewriteln ((const char*) "    Enable or disable telnet service");
  }
  if (query==NULL || strcmp(query, "erase") == 0) {
    consolewriteln ((const char*) "erase config");
    consolewriteln ((const char*) "    Erase all configuration settings and restart");
  }
  if (query==NULL || strcmp(query, "help") == 0) {
    consolewriteln ((const char*) "help [<command>]");
    consolewriteln ((const char*) "    Display list of command and brief description");
  }
  if (query==NULL || strcmp(query, "hibernate") == 0) {
    consolewriteln ((const char*) "hibernate <number> [mins|hours|days]");
    consolewriteln ((const char*) "    Go to deep sleep from now for a period of time, default time measure is mins");
    consolewriteln ((const char*) "hibernate [<start> <end>]");
    consolewriteln ((const char*) "    Go to deep sleep daily between start and end times in hh:mm format");
    consolewriteln ((const char*) "    disabled if start and end times match");
  }
  if (query==NULL || strcmp(query, "history") == 0) {
    consolewriteln ((const char*) "history");
    consolewriteln ((const char*) "    Display recent command line history");
  }
  if (query==NULL || strcmp(query, "i2c") == 0) {
    consolewriteln ((const char*) "i2c [<0-1> <sda> <scl> [speed]]");
    consolewriteln ((const char*) "    Set i2c pins, eg: 21 and 22 for bus-0, and 5 and 4 for bus-1");
  }
  if (query==NULL || strcmp(query, "identify") == 0) {
    consolewriteln ((const char*) "identify <pin> [invert]");
    consolewriteln ((const char*) "identify [on|off|flash|fflash|sflash] [persist]");
    consolewriteln ((const char*) "    Configure ID LED pin, on, off or flash with fast and slow options");
    consolewriteln ((const char*) "    Define state. Without \"persist\" state is cleared on restart");
  }
  if (query==NULL || strcmp(query, "interval") == 0) {
    consolewriteln ((const char*) "interval [<devicetype> <seconds>]");
    consolewriteln ((const char*) "    Define time interval between each measurement(1-300 seconds)");
  }
  if (query==NULL || strcmp(query, "inventory") == 0) {
    consolewriteln ((const char*) "inventory");
    consolewriteln ((const char*) "    Show inventory of connected/configured devices");
  }
  if (query==NULL || strcmp(query, "list") == 0 || strcmp(query, "show") == 0) {
    consolewriteln ((const char*) "list|show [devices|tz|vars]");
    consolewriteln ((const char*) "    List supported device types, timezone names or sensor variables");
  }
  if (query==NULL || strcmp(query, "ntp") == 0) {
    consolewriteln ((const char*) "ntp [<server-name>|none]");
    consolewriteln ((const char*) "    Set network time protocol (NTP) server to source time signal");
  }
  if (query==NULL || strcmp(query, "onewire") == 0) {
    consolewriteln ((const char*) "onewire <bus-number> [disable|<pin>]");
    consolewriteln ((const char*) "    Configure Dallas OneWire devices");
  }
  if (query==NULL || strcmp(query, "opacity") == 0) {
    consolewriteln ((const char*) "opacity [bh1750 <number> <factor>]");
    consolewriteln ((const char*) "    Set the opacity of light sensor cover, uncovered sensor is typically 1.2");
    consolewriteln ((const char*) "    Permissable factor range is 0.5 - 1.5");
  }
  if (query==NULL || strcmp(query, "ota") == 0) {
    consolewriteln ((const char*) "ota [update|revert|<url>]");
    consolewriteln ((const char*) "    Check and apply over the air update");
    consolewriteln ((const char*) "    Revert to previous installation.");
    consolewriteln ((const char*) "    Set base URL for OTA metadata and image");
  }
  if (query==NULL || strcmp(query, "output") == 0) {
    consolewriteln ((const char*) "output [<index> disable]");
    consolewriteln ((const char*) "output [<index> default <value>]");
    consolewriteln ((const char*) "output [<index> <pin> <relay|pwm|servo|var> <name> <rpn expression>]");
    consolewriteln ((const char*) "    Configure output pins");
  }
  if (query==NULL || strcmp(query, "password") == 0) {
    consolewriteln ((const char*) "password [<password>|none]");
#ifdef USE_BLUETOOTH
    consolewriteln ((const char*) "    Set password for Bluetooth-Serial/Telnet connections");
#else
    consolewriteln ((const char*) "    Set password for Telnet connections");
#endif
  }
  if (query==NULL || strcmp(query, "qnh") == 0) {
    consolewriteln ((const char*) "qnh");
    consolewriteln ((const char*) "    set altitude based on normalised pressure (QNH)");
  }
  if (query==NULL || strcmp(query, "quit") == 0) {
    consolewriteln ((const char*) "quit [number]");
    consolewriteln ((const char*) "    Quit from telnet session, or cause a specific telnet ID to quit");
  }
  if (query==NULL || strcmp(query, "read") == 0) {
    consolewriteln ((const char*) "read <filename>");
    consolewriteln ((const char*) "    Read the contents of the file");
  }
  if (query==NULL || strcmp(query, "repeat") == 0) {
    consolewriteln ((const char*) "repeat [count [interval]]");
    consolewriteln ((const char*) "    Repeat the previous command count times at interval seconds");
    consolewriteln ((const char*) "    Default count and interval are 1 if not specified");
  }
  if (query==NULL || strcmp(query, "resistor") == 0) {
    consolewriteln ((const char*) "resistor [ina2xx <index> <milliohm>]");
    consolewriteln ((const char*) "    Set resistance of shunt resistor for voltage measurement");
  }
  if (query==NULL || strcmp(query, "restart") == 0) {
    consolewriteln ((const char*) "restart");
    consolewriteln ((const char*) "    Restart system, this is required if configuration changes have been made");
  }
  if (query==NULL || strcmp(query, "rpn") == 0) {
    consolewriteln ((const char*) "rpn <expression>");
    consolewriteln ((const char*) "    Evalute rpn expression and show stack evaluation");
  }
  if (query==NULL || strcmp(query, "serial") == 0) {
    consolewriteln ((const char*) "serial [0|1] <rx-pin> <tx-pin> <speed> <bits>");
    consolewriteln ((const char*) "serial [0|1] [disable|pms5003|zh03b|mh-z19c|winsen|ascii|nmea]");
    consolewriteln ((const char*) "    Set up serial port's I/O pins, speed and device type configuration");
    consolewriteln ((const char*) "    Winsen devices include: ze27-03, zp07/08-ch20, zc05/zp14-ch4, ze15/ze16b-co, zph01/02-pm2.5");
  }
  if (query==NULL || strcmp(query, "scan") == 0) {
    consolewriteln ((const char*) "scan");
    consolewriteln ((const char*) "    Scan device busses for attached devices");
  }
  if (query==NULL || strcmp(query, "terminate") == 0) {
    consolewriteln ((const char*) "terminate [now|<minutes>]");
    consolewriteln ((const char*) "    Terminate command mode after n minutes from start, or now");
    consolewriteln ((const char*) "    0 means do not terminate, otherwise must be between 1 and 60 minutes");
  }
  if (query==NULL || strcmp(query, "timezone") == 0) {
    consolewriteln ((const char*) "timezone [defn]");
    consolewriteln ((const char*) "    Set timezone, may be set using full TZ string or short cut");
    consolewriteln ((const char*) "      use \"list tz\" for short-cuts, or enter time specification, eg");
    consolewriteln ((const char*) "      UTC-03:00  for East African Time");
  }
  if (query==NULL || strcmp(query, "wifi") == 0 || strcmp(query, "wifi*") == 0) {
    consolewriteln ((const char*) "wifi [<number> <ssid> [<password>]]");
    consolewriteln ((const char*) "    configure wifi 0-3 with ssid and optional password");
  }
  if (query==NULL || strcmp(query, "wifimode") == 0 || strcmp(query, "wifi*") == 0) {
    consolewriteln ((const char*) "wifimode [alwayson|ondemand]");
    consolewriteln ((const char*) "    set wifi to be \"always on\" or on connect \"on demand\"");
  }
  if (query==NULL || strcmp(query, "wifiscan") == 0 || strcmp(query, "wifi*") == 0) {
    consolewriteln ((const char*) "wifiscan [enable|disable]");
    consolewriteln ((const char*) "    set wifi to scan for non-hidden ssids and use strongest signal");
    consolewriteln ((const char*) "    NB: the network used must be predefined by \"wifi\", or it will be ignored");
  }
  if (query==NULL || strcmp(query, "write") == 0) {
    consolewriteln ((const char*) "write <filename>");
    consolewriteln ((const char*) "    Write the contents of the file");
  }
  if (query==NULL || strcmp(query, "xysecret") == 0) {
    consolewriteln ((const char*) "xysecret [<string>|none]");
    consolewriteln ((const char*) "    Define a \"xysecret\" field to send when using http/s to xymon server.");
  }
  if (query==NULL || strcmp(query, "xyserver") == 0) {
    consolewriteln ((const char*) "xyserver [<DNS-name>|<IP-Address>|<URL>|none [port-number]]");
    consolewriteln ((const char*) "    Define the xymon server and port. Port 1984 used if not explicitly defined");
    consolewriteln ((const char*) "    If using a URL, the port number is ignored.");
  }
}
