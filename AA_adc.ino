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
 * ADC1_CH0 - GPIO36
 * ADC1_CH1 - GPIO37
 * ADC1_CH2 - GPIO38
 * ADC1_CH3 - GPIO39
 * ADC1_CH4 - GPIO32
 * ADC1_CH5 - GPIO33
 * ADC1_CH6 - GPIO34
 * ADC1_CH7 - GPIO35
 * 
 * ADC2 not usable with WIFI enabled. - easy work around is not to useit
 *                                      to avois d wireless on/ off which
 *                                      may break telnet.
 * 
 * Is the following polynomial adjustment worth considering, to extend linear range:
 * https://bitbucket.org/Blackneron/esp32_adc/src/master/
 * 
 */

 class adConverter {
   private:
     const char myDevType[4] = "adc";
     uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      uint16_t reading;
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      struct adc_s *myData;
      char msgBuffer[SENSOR_NAME_LEN];
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted

      myDevTypeID = util_get_dev_type("adc");
      if (myDevTypeID!=255) {
        myData = (struct adc_s*) (devData[myDevTypeID]);
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        queueData = myDevTypeID;
        // loop forever collecting data
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing adc signal");
            if (ansiTerm) displayAnsi(1);
          }
          loopCount++;
          for (uint8_t device=0; device<devTypeCount[myDevTypeID]; device++) {
            // sprintf (msgBuffer, "reading %d", myData[device].pin);
            // consolewrite (msgBuffer);
            reading = analogRead(myData[device].pin);
            // sprintf (msgBuffer, " -> %d", reading);
            // consolewriteln (msgBuffer);
            if (reading < 4096) {
              if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                myData[device].accumulator += reading;
                myData[device].readingCount++;
                myData[device].lastVal = reading;
                xSemaphoreGive(devTypeSem[myDevTypeID]);
              }
            }
            delay (100);
          }
          if (loopCount >= updateCount) {
            loopCount = 0;
            if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
              for (uint8_t device=0; device<devTypeCount[myDevTypeID]; device++) {
                myData[device].averagedOver = myData[device].readingCount;
                myData[device].average = myData[device].accumulator / myData[device].readingCount;
                myData[device].readingCount = 0;
                myData[device].accumulator = 0;
                xSemaphoreGive(devTypeSem[myDevTypeID]);
              }
            }
          }
        }
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Could not determine adc type ID for update loop");
        if (ansiTerm) displayAnsi(1);
      }
      vTaskDelete( NULL );
    }

  public:

    char subtypeList[1][5] = { "adc" };
    char subtypeLen = 1;

    adConverter()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("adc");
      devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      struct adc_s *myData;
      uint32_t attenuation;
      uint8_t count = 0;
      uint8_t devNr = 0;
      char msgBuffer[BUFFSIZE];
      char sensorName[SENSOR_NAME_LEN];
      char ruleName[SENSOR_NAME_LEN];
      bool retVal = false;

      analogReadResolution(12);
      for (uint8_t pin=32; pin<40; pin++) {
        sprintf (ruleName, "adc_%d", pin);
        nvs_get_string (ruleName, sensorName, "disable", sizeof(sensorName));
        if (strcmp (sensorName, "disable") != 0 ) {
          devTypeCount[myDevTypeID]++;
          count++;
        }
      }
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(adc_s));
      myData = (struct adc_s*) devData[myDevTypeID];
      devNr = 0;
      for (uint8_t pin=32; pin<40; pin++) {
        sprintf (ruleName, "adc_%d", pin);
        nvs_get_string (ruleName, sensorName, "disable", sizeof(sensorName));
        if (strcmp (sensorName, "disable") != 0 ) {
          sprintf (ruleName, "adc_atten_%d", pin);
          attenuation  = nvs_get_int (ruleName, 3);
          if (attenuation<4 && attenuation>=0) {
            retVal = true;
            adcAttachPin(pin);
            analogSetPinAttenuation(pin, adcAttenuation[pin]);
            strcpy (myData[devNr].uniquename, sensorName);
            myData[devNr].pin = pin;
            myData[devNr].attenuation = attenuation;
            sprintf (ruleName, "adc_mult_%d", pin);
            myData[devNr].multiplier = nvs_get_float (ruleName, 1.0);
            sprintf (ruleName, "adc_offs_%d", pin);
            myData[devNr].offsetval = nvs_get_float (ruleName, 0.0);
            sprintf (ruleName, "adc_uom_%d", pin);
            nvs_get_string (ruleName, myData[devNr].uom, "unknown", sizeof(sensorName));
            myData[devNr].accumulator  = 0;
            myData[devNr].average      = 0;
            myData[devNr].averagedOver = 0;
            myData[devNr].readingCount = 0;
            myData[devNr].state        = GREEN;
            for (uint8_t level=0; level<3 ; level++) {
              sprintf (ruleName, "adc_%d%d", level, pin);  // Warning Logic
              nvs_get_string (ruleName, msgBuffer, "disable", sizeof(msgBuffer));
              if (strcmp (msgBuffer, "disable") != 0) {
                consolewrite ("Enable ");
                consolewrite ((char*)alertLabel[level]);
                consolewrite (": ");
                consolewrite (ruleName);
                consolewrite (" as ");
                consolewriteln (msgBuffer);
                util_getLogic (ruleName, &myData[devNr].alert[level]);
              }
              else myData[devNr].alert[level] = NULL;
            }
          }
          devNr++;
        }
      }
      if (retVal) xTaskCreate(updateloop, devType[myDevTypeID], 4096, NULL, 12, NULL);
      inventory();      
      return (retVal);
    }

    void inventory()
    {
      char msgBuffer[80];
      char devStatus[9];
      struct adc_s *myData;

      consolewriteln ((const char*) "Test: adc - Analogue to digital");
      if (devTypeCount[myDevTypeID] == 0) {
        consolewriteln ((const char*) " * No ADC sensors found.");
        return;
      }
      myData = (struct adc_s*) devData[myDevTypeID];
      for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
        sprintf (msgBuffer, " * OK %s.%d, name: %s, UOM: %s, attenuation %d (%sV FSD)", \
          myDevType, myData[device].pin, myData[device].uniquename, myData[device].uom, \
          myData[device].attenuation, util_ftos(adcFsd[myData[device].attenuation], 2));
        consolewrite (msgBuffer);
        sprintf (msgBuffer, ", offset: %s", util_ftos(myData[device].offsetval, 3));
        consolewrite (msgBuffer);
        sprintf (msgBuffer, ", multiplier: %s", util_ftos(myData[device].multiplier, 3));
        consolewriteln (msgBuffer);
      }
    }

    uint8_t getStatusColor()
    {
      uint8_t retVal  = GREEN;    // Worst score for all tests of this type
      uint8_t testVal = GREEN;    // Score for the currently evaluated sensor
      struct adc_s *myData;       // Pointer to data.

      myData = (struct adc_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        testVal = 0;
        for (uint8_t innerloop=0 ; innerloop<3; innerloop++) {
          struct rpnLogic_s *alertPtr = myData[devNr].alert[innerloop];
          if (alertPtr != NULL && rpn_calc(alertPtr->count, alertPtr->term)>0) testVal = innerloop+1;
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
      struct adc_s *myData;
      char msgBuffer[80];
      char logicNum;
      char *metricName;
      bool retval = false;
      uint8_t statusColor;
      uint8_t n;

      myData = (struct adc_s*) devData[myDevTypeID];
      // is reporting previously completed? Then everything is pending again.
      for (n=0 ; n<devTypeCount[myDevTypeID] && !retval; n++) {
         if (myData[n].pending) retval = true;
      }
      if (!retval) {
        for (n=0; n<devTypeCount[myDevTypeID]; n++) myData[n].pending = true;
        return (false); // No data to report
      }
      for (n=0 ; n<devTypeCount[myDevTypeID] && !myData[n].pending; n++) ;
      // Group by uom (Unit Of Measure), and find most extreme status color of the UOM
      metricName = myData[n].uom;
      statusColor = GREEN;
      for (int device=n; device<devTypeCount[myDevTypeID]; device++) if (strcmp (myData[n].uom, metricName) == 0 && myData[n].state > statusColor) statusColor = myData[n].state;
      // Now report on the UOM
      sprintf (xydata, "status %s.%s %s %s (adc) %s\n\n", device_name, metricName, xymonColour[statusColor], metricName, util_gettime());
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=n; device<devTypeCount[myDevTypeID]; device++) if (strcmp (myData[device].uom, metricName) == 0) {
          if (myData[device].averagedOver > 0) {
            myData[device].pending = false;
            uint8_t currentState = myData[device].state;
            util_getLogicTextXymon (myData[device].alert[currentState-YELLOW], xydata, currentState, myData[device].uniquename);
            strcat  (xydata, " &");
            strcat  (xydata, xymonColour[currentState]);
            sprintf (msgBuffer, " %-16s %8s %s", myData[device].uniquename, util_ftos (((myData[device].average * myData[device].multiplier) + myData[device].offsetval), 3), myData[device].uom);
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, " - average over %d readings\n", myData[device].averagedOver);
            strcat  (xydata, msgBuffer);
          }
          else {
            sprintf (msgBuffer, " &red no ADC reading on pin %d\n", myData[device].pin);
            strcat  (xydata, msgBuffer);
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("adc semaphore not released.");
      return (true);   // We should have valid data
    }

    float estVolts (uint16_t reading, uint8_t attenuation)
    {
      return ((reading * adcFsd[attenuation] * 1.282) / 4096);
    }


    float getData(uint8_t pin, char *parameter)
    {
      struct adc_s *myData;
      float retval = 0.00;
      uint8_t devNr = 99;
      char msgBuffer[3];

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct adc_s*) devData[myDevTypeID];
      if (pin < 32 || pin >39) return (99999.99);
      for (uint8_t n=0; n<devTypeCount[myDevTypeID] && devNr==99; n++) if (pin == myData[n].pin) devNr=n;
      if (devNr == 99) {
        sprintf (msgBuffer, "%d", pin);
        consolewrite ("No ADC defined on pin ");
        consolewriteln (msgBuffer);
        return (999999.99);
      }
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if      (strcmp (parameter,"coun") == 0) retval = myData[devNr].averagedOver;
        else if (strcmp (parameter,"adc")  == 0) retval = (myData[devNr].average * myData[devNr].multiplier) + myData[devNr].offsetval;
        else if (strcmp (parameter,"lasa") == 0) retval = (myData[devNr].lastVal * myData[devNr].multiplier) + myData[devNr].offsetval;
        else if (strcmp (parameter,"raw")  == 0) retval = myData[devNr].average;
        else if (strcmp (parameter,"lasr") == 0) retval = myData[devNr].lastVal;
        else if (strcmp (parameter,"atte") == 0) retval = myData[devNr].attenuation;
        else if (strcmp (parameter,"mult") == 0) retval = myData[devNr].multiplier;
        else if (strcmp (parameter,"offs") == 0) retval = myData[devNr].offsetval;
        else if (strcmp (parameter,"asta") == 0) retval = myData[devNr].state;
        else if (strcmp (parameter,"volt") == 0) retval = estVolts (myData[devNr].average, myData[devNr].attenuation);
        else if (strcmp (parameter,"lasv") == 0) retval = estVolts (myData[devNr].lastVal, myData[devNr].attenuation);
        else if (strcmp (parameter,"vsta") == 0) retval = myData[devNr].state;
        else retval=0.00;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

    void printData()
    {
      struct adc_s *myData;
      char msgBuffer[40];
      
      sprintf (msgBuffer, "adc.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct adc_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          sprintf (msgBuffer, "adc.%d.coun (%s) %d", myData[device].pin, myData[device].uniquename, myData[device].averagedOver);
          consolewriteln (msgBuffer);
          sprintf (msgBuffer, "adc.%d.adc  (%s) ", myData[device].pin, myData[device].uniquename);
          consolewrite (msgBuffer);
          if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
          else {
            consolewrite (util_ftos (((myData[device].average * myData[device].multiplier) + myData[device].offsetval), 3));
            consolewrite (" ");
            consolewriteln (myData[device].uom);
          }
          sprintf (msgBuffer, "adc.%d.lasa (%s) ", myData[device].pin, myData[device].uniquename);
          consolewrite (msgBuffer);
          consolewrite (util_ftos (((myData[device].lastVal * myData[device].multiplier) + myData[device].offsetval), 3));
          consolewrite (" ");
          consolewriteln (myData[device].uom);
          sprintf (msgBuffer, "adc.%d.raw  (%s) ", myData[device].pin, myData[device].uniquename);
          consolewrite (msgBuffer);
          if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
          else {
            consolewriteln (util_ftos (myData[device].average, 2));
          }
          sprintf (msgBuffer, "adc.%d.lasr (%s) ", myData[device].pin, myData[device].uniquename);
          consolewrite (msgBuffer);
          consolewriteln (util_ftos (myData[device].lastVal, 3));
          sprintf (msgBuffer, "adc.%d.volt (%s) ", myData[device].pin, myData[device].uniquename);
          consolewrite (msgBuffer);
          if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
          else {
            consolewrite (util_ftos (estVolts (myData[device].average, myData[device].attenuation), 3));
            consolewriteln (" V");
          }
          sprintf (msgBuffer, "adc.%d.lasv (%s) ", myData[device].pin, myData[device].uniquename);
          consolewrite (msgBuffer);
          consolewrite (util_ftos (estVolts (myData[device].lastVal, myData[device].attenuation), 3));
          consolewriteln (" V");
          sprintf (msgBuffer, "adc.%d.atte (%s) %d", myData[device].pin, myData[device].uniquename, myData[device].attenuation);
          consolewriteln (msgBuffer);
          sprintf (msgBuffer, "adc.%d.offs (%s) %s", myData[device].pin, myData[device].uniquename, util_ftos (myData[device].offsetval, 3));
          consolewriteln (msgBuffer);
          sprintf (msgBuffer, "adc.%d.mult (%s) %s", myData[device].pin, myData[device].uniquename, util_ftos (myData[device].multiplier, 6));
          consolewriteln (msgBuffer);
          sprintf (msgBuffer, "adc.%d.asta (%s) %d", myData[device].pin, myData[device].uniquename, myData[device].state);
          consolewriteln (msgBuffer);
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

    void getXymonStats (char *xydata)
    {
      struct adc_s *myData;
      char msgBuffer[40];
      
      // setup pointer to data array
      myData = (struct adc_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].averagedOver > 0) {
            sprintf (msgBuffer, "[%s.%s.rrd]\n", myData[device].uom, myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:%s:GAUGE:600:U:U ", myData[device].uom);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, util_ftos (((myData[device].average * myData[device].multiplier) + myData[device].offsetval), 1));
            strcat  (xydata, "\n");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }


 };

 adConverter the_adc;
