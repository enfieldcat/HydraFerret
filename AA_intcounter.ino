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


class intcounter {
  private:
    const char myDevType[12] = "counter";
    uint8_t myDevTypeID;
    int  counterCount;

    static void updateloop(void *pvParameters)
    {
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      struct int_counter_s *myData;

      myDevTypeID = util_get_dev_type ("counter");
      if (myDevTypeID!=255) {
        myData = (struct int_counter_s*) (devData[myDevTypeID]);
        queueData = myDevTypeID;
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(310000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing interrupt counter signal");
            if (ansiTerm) displayAnsi(1);
          }

          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
            for (int n=0; n<devTypeCount[myDevTypeID]; n++) {
              myData[n].previous = myData[n].current; 
              if (xSemaphoreTake(myData[n].mux, pdMS_TO_TICKS(1000)) == pdTRUE) {
                myData[n].current = myData[n].accumulator;
                xSemaphoreGive(myData[n].mux);
              }
            }
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
      }
      vTaskDelete( NULL );
    }

  public:

    char subtypeList[1][5] = { "var" };
    char subtypeLen = 1;

    intcounter()
    {
      // Find our device type ID
      myDevTypeID = 255;
      for (char n=0; n<numberOfTypes && myDevTypeID==255; n++) if (strcmp (myDevType, devType[n]) == 0) myDevTypeID = n;
      if (myDevTypeID == 255) {
        consolewrite ((char*) myDevType);
        consolewriteln (" device type not found as expected device type");
      }
      // We need to defer all other initialsation that depends on stored configuration
      // to when the NV RAM is available after setup, so that is left to begin();
    }

    bool is_valid()
    {
      if (devTypeCount[myDevTypeID] > 0) return true;
      return false;
    }

    bool begin()
    {
      bool retval = false;
      char msg2[16];
      char ruleName[SENSOR_NAME_LEN];
      char msgBuffer[BUFFSIZE];
      int  tmpInt;
      struct int_counter_s *myData;

      counterCount = 0;
      // Find how many counters have been defined
      for (int n=0; n<MAX_COUNTER; n++) {
        sprintf (msgBuffer, "cntr_pin_%d", n);
        if ((nvs_get_int (msgBuffer, 99)) < 99) counterCount++;
      }
      devTypeCount[myDevTypeID] = counterCount;
      sprintf (msgBuffer, "%d counters found", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      if (devTypeCount[myDevTypeID] > 0) {
      // Allocate space to hold data and initialise it.
        devData[myDevTypeID] = heap_caps_malloc((counterCount * sizeof(struct int_counter_s)), MALLOC_CAP_8BIT);
        myData = (struct int_counter_s*) devData[myDevTypeID];
        if (myData != NULL) {
          tmpInt = 0;
          for (int n=0; n<MAX_COUNTER; n++) {
            sprintf (msgBuffer, "cntr_pin_%d", n);
            if ((nvs_get_int (msgBuffer, 99)) < 40) {
              (myData[tmpInt]).accumulator = 0;
              (myData[tmpInt]).current = 0;
              (myData[tmpInt]).previous = 0;
              (myData[tmpInt]).pending = true;
              // (myData[tmpInt]).mux = portMUX_INITIALIZER_UNLOCKED;
              (myData[tmpInt]).mux = xSemaphoreCreateMutex();
              (myData[tmpInt]).pin = nvs_get_int (msgBuffer, 99);
              sprintf (msgBuffer, "cntr_offset_%d", n);
              (myData[tmpInt]).offsetval = nvs_get_float (msgBuffer, 0.0);
              sprintf (msgBuffer, "cntr_mult_%d", n);
              (myData[tmpInt]).multiplier = nvs_get_float (msgBuffer, 1.0);
              sprintf (msgBuffer, "cntr_name_%d", n);
              nvs_get_string (msgBuffer, (myData[tmpInt]).uniquename, msgBuffer, sizeof((myData[tmpInt]).uniquename));
              sprintf (msgBuffer, "cntr_uom_%d", n);
              nvs_get_string (msgBuffer, (myData[tmpInt]).uom, "items", sizeof((myData[tmpInt]).uom));
              pinMode ((myData[tmpInt]).pin, INPUT);
              sprintf (msgBuffer, "Attaching pin %d as %s (%s) counter", (myData[tmpInt]).pin, (myData[tmpInt]).uniquename, (myData[tmpInt]).uom);
              consolewriteln (msgBuffer);
              init_interrupt(tmpInt, (myData[tmpInt]).pin);
              for (uint8_t level=0; level<3 ; level++) {
                sprintf (ruleName, "coun_%d%d", level, tmpInt);  // Warning Logic
                nvs_get_string (ruleName, msgBuffer, "disable", sizeof(msgBuffer));
                if (strcmp (msgBuffer, "disable") != 0) {
                  consolewrite ("Enable ");
                  consolewrite ((char*)alertLabel[level]);
                  consolewrite (": ");
                  consolewrite (ruleName);
                  sprintf (msg2, " on pin %d as ", (myData[tmpInt]).pin);
                  consolewrite (msg2);
                  consolewriteln (msgBuffer);
                  util_getLogic (ruleName, &myData[tmpInt].alert[level]);
                }
                else myData[tmpInt].alert[level] = NULL;
              }
              tmpInt++;
            }
          }
          xTaskCreate(updateloop, myDevType, 4096, NULL, 12, NULL);
          retval = true;
        } else {
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Could not allocate memory for counters.");
          if (ansiTerm) displayAnsi(1);
        }
      }
      if (!retval) xTimerStop (devTypeTimer[myDevTypeID], 0);
      return (retval);
    }

    char get_myDevTypeID()
    {
      return (myDevTypeID);
    }

    uint8_t getStatusColor()
    {
      uint8_t retVal  = GREEN;    // Worst score for all tests of this type
      uint8_t testVal = GREEN;    // Score for the currently evaluated sensor
      struct int_counter_s *myData;    // Pointer to data.

      myData = (struct int_counter_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        testVal = 0;
        for (uint8_t innerloop=0 ; innerloop<3; innerloop++) {
          if (myData[devNr].alert[innerloop] != NULL && rpn_calc(myData[devNr].alert[innerloop]->count, myData[devNr].alert[innerloop]->term)>0) testVal = innerloop+1;
        }
        retVal = testVal;
        if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
          myData[devNr].state = testVal;
          xSemaphoreGive(devTypeSem[myDevTypeID]);
        }
        else retVal = CLEAR;
      }
      return (retVal);
    }
    
    bool getXymonStatus (char *xydata)
    {
      bool retval = false;
      static struct int_counter_s *myData = NULL;
      int n;
      char *metricName;
      char outBuffer[32];
      int dp;
      float tFloat;
      uint8_t genStatus;

      // setup pointer to data array
      myData = (struct int_counter_s*) devData[myDevTypeID];
      // is reporting previously completed? Then everything is pending again.
      for (n=0 ; n<counterCount; n++) {
         if (myData[n].pending) retval = true;
      }
      if (!retval) {
        for (n=0; n<counterCount; n++) myData[n].pending = true;
        return (false); // No data to report
      }
      for (n=0 ; n<counterCount && !myData[n].pending; n++) ;
      metricName = myData[n].uom;
      genStatus = GREEN;
      for (uint8_t i=n; i<counterCount; i++) if (strcmp (metricName, myData[i].uom) == 0 && myData[i].state > genStatus) genStatus = myData[i].state;
      sprintf (xydata, "status %s.%s %s %s counter %s\n\n    %16s %14s %14s\n", device_name, metricName, xymonColour[genStatus], metricName, util_gettime(), "Counter", "Total", "Change");
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (;n<counterCount; n++) {
          if (strcmp (myData[n].uom, metricName) == 0) {
            myData[n].pending = false;
            util_getLogicTextXymon (myData[n].alert[(myData[n].state-YELLOW)], xydata, myData[n].state, myData[n].uniquename);
            sprintf (outBuffer, " &%s %16s ", xymonColour[myData[n].state], myData[n].uniquename);
            strcat (xydata, outBuffer);
            if (myData[n].multiplier < 0.025) dp = 3;
            else if (myData[n].multiplier < 0.25) dp = 2;
            else if (myData[n].multiplier < 1) dp = 1;
            else dp = 0;
            tFloat = ((float)(myData[n].current) * myData[n].multiplier) + myData[n].offsetval;
            sprintf (outBuffer, "%14s ", util_dtos (tFloat, dp));
            strcat (xydata, outBuffer);
            tFloat = ((float)(myData[n].current - myData[n].previous) * myData[n].multiplier) + myData[n].offsetval;
            sprintf (outBuffer, "%14s\n", util_dtos (tFloat, dp));
            strcat (xydata, outBuffer);
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (true);   // We should have valid data
    }

    void getXymonStats (char *xydata)
    {
      static struct int_counter_s *myData = NULL;
      char msgBuffer[40];
      int dp;
      float tFloat;
      
      // setup pointer to data array
      myData = (struct int_counter_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int n=0 ;n<counterCount; n++) {
          if (myData[n].multiplier < 0.025) dp = 3;
          else if (myData[n].multiplier < 0.25) dp = 2;
          else if (myData[n].multiplier < 1) dp = 1;
          else dp = 0;
          tFloat = ((float)(myData[n].current - myData[n].previous) * myData[n].multiplier) + myData[n].offsetval;
          sprintf (msgBuffer, "[%s.%s.rrd]\n", myData[n].uom, myData[n].uniquename);
          strcat  (xydata, msgBuffer);
          strcat  (xydata, "DS:value:GAUGE:600:0:U ");
          strcat  (xydata, util_dtos (tFloat, dp));
          strcat  (xydata, "\n");
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

    void printData()
    {
      struct int_counter_s *myData;
      char msgBuffer[40];
      int dp;
      float tFloat;
      
      sprintf (msgBuffer, "counter.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct int_counter_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].multiplier < 0.025) dp = 3;
          else if (myData[device].multiplier < 0.25) dp = 2;
          else if (myData[device].multiplier < 1) dp = 1;
          else dp = 0;
          tFloat = ((float)(myData[device].current - myData[device].previous) * myData[device].multiplier) + myData[device].offsetval;
          sprintf (msgBuffer, "counter[%d].diff (%s) ", device, myData[device].uniquename);
          consolewrite (msgBuffer);
          consolewriteln (util_ftos (tFloat, dp));
          tFloat = ((float)(myData[device].current) * myData[device].multiplier) + myData[device].offsetval;
          sprintf (msgBuffer, "counter[%d].tota (%s) ", device, myData[device].uniquename);
          consolewrite (msgBuffer);
          consolewriteln (util_ftos (tFloat, dp));
          dp = 3;
          sprintf (msgBuffer, "counter[%d].mult (%s) ", device, myData[device].uniquename);
          consolewrite (msgBuffer);
          consolewriteln (util_ftos (myData[device].multiplier, (dp+1)));
          sprintf (msgBuffer, "counter[%d].off (%s) ", device, myData[device].uniquename);
          consolewrite (msgBuffer);
          consolewriteln (util_ftos (myData[device].offsetval, (dp+1)));
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }     
    }

    float getData(uint8_t devNr, char *parameter)
    {
      struct int_counter_s *myData;
      float retval;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct int_counter_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (99999.99);
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if      (strcmp(parameter,"diff") == 0) retval = ((float)(myData[devNr].current - myData[devNr].previous) * myData[devNr].multiplier) + myData[devNr].offsetval;
        else if (strcmp(parameter,"tota") == 0) retval = ((float)(myData[devNr].current) * myData[devNr].multiplier) + myData[devNr].offsetval;
        else if (strcmp(parameter,"mult") == 0) retval = myData[devNr].multiplier;
        else if (strcmp(parameter,"offs") == 0) retval = myData[devNr].offsetval;
        else retval=0.00;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

};

static intcounter theCounter = intcounter();

static void IRAM_ATTR interruptCounter(char n)
{
  static struct int_counter_s *myData = NULL;
  static char myDevTypeID = 255;
  
  if (myDevTypeID == 255) {
    for (char n=0; n<numberOfTypes && myDevTypeID==255; n++) if (strcmp ("counter", devType[n]) == 0) myDevTypeID = n;
    myData = (struct int_counter_s*) devData[myDevTypeID];
  }
  if (myData != NULL) {
    for (int n=0; n<devTypeCount[myDevTypeID]; n++) {
      // portENTER_CRITICAL_ISR(&(myData[n].mux));
      // if (xSemaphoreTakeFromISR(myData[n].mux, NULL) == pdTRUE) {
      // if (xSemaphoreTake(myData[n].mux, pdMS_TO_TICKS(1000)) == pdTRUE) {
        myData[n].accumulator++;
        // portEXIT_CRITICAL_ISR(&(myData[n].mux));
        // xSemaphoreGiveFromISR(myData[n].mux, NULL);
        // xSemaphoreGive(myData[n].mux);
      // }
    }
  }
}

// fudge to work around Arduino IDE not being able to pass interrupt parameters
static void IRAM_ATTR intCounter0 () { interruptCounter((char) 0); }
static void IRAM_ATTR intCounter1 () { interruptCounter((char) 1); }
static void IRAM_ATTR intCounter2 () { interruptCounter((char) 2); }
static void IRAM_ATTR intCounter3 () { interruptCounter((char) 3); }
static void IRAM_ATTR intCounter4 () { interruptCounter((char) 4); }
static void IRAM_ATTR intCounter5 () { interruptCounter((char) 5); }
static void IRAM_ATTR intCounter6 () { interruptCounter((char) 6); }
static void IRAM_ATTR intCounter7 () { interruptCounter((char) 7); }

void init_interrupt(int tmpInt, int pin)
{
  switch (tmpInt) {
    case 0: attachInterrupt(digitalPinToInterrupt(pin), intCounter0, FALLING);
      break;
    case 1: attachInterrupt(digitalPinToInterrupt(pin), intCounter1, FALLING);
      break;
    case 2: attachInterrupt(digitalPinToInterrupt(pin), intCounter2, FALLING);
      break;
    case 3: attachInterrupt(digitalPinToInterrupt(pin), intCounter3, FALLING);
      break;
    case 4: attachInterrupt(digitalPinToInterrupt(pin), intCounter4, FALLING);
      break;
    case 5: attachInterrupt(digitalPinToInterrupt(pin), intCounter5, FALLING);
      break;
    case 6: attachInterrupt(digitalPinToInterrupt(pin), intCounter6, FALLING);
      break;
    case 7: attachInterrupt(digitalPinToInterrupt(pin), intCounter7, FALLING);
      break;
    }
}
