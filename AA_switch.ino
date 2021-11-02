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


 class switchReader {
   private:
     const char myDevType[7] = "switch";
     uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      uint8_t reading;
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      struct switch_s *myData;
      char msgBuffer[SENSOR_NAME_LEN];
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted

      myDevTypeID = util_get_dev_type("switch");
      if (myDevTypeID!=255) {
        util_deviceTimerCreate(myDevTypeID);
        myData = (struct switch_s*) (devData[myDevTypeID]);
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        if (xTimerChangePeriod(devTypeTimer[myDevTypeID], pdMS_TO_TICKS(pollInterval), pdMS_TO_TICKS(1100)) != pdPASS) {
          consolewriteln("Unable to adjust switch poll timer period, keep at 1 second");
          pollInterval = 1000;
          updateCount = 300;
          }
        queueData = myDevTypeID;
        // loop forever collecting data
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing switch reader signal");
            if (ansiTerm) displayAnsi(1);
          }
          loopCount++;
          for (uint8_t device=0; device<devTypeCount[myDevTypeID]; device++) {
            reading = digitalRead(myData[device].pinNumber);
            if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
              myData[device].accumulator += reading;
              myData[device].readingCount++;
              myData[device].lastVal = reading;
              xSemaphoreGive(devTypeSem[myDevTypeID]);
            }
          }
          if (loopCount >= updateCount) {
            loopCount = 0;
            if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
              for (uint8_t device=0; device<devTypeCount[myDevTypeID]; device++) {
                myData[device].averagedOver = myData[device].readingCount;
                myData[device].average = (float)myData[device].accumulator / (float)myData[device].readingCount;
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
        consolewriteln ("Could not determine switch type ID for update loop");
        if (ansiTerm) displayAnsi(1);
      }
      vTaskDelete( NULL );
    }

  public:

    char subtypeList[1][7] = { "switch" };
    char subtypeLen = 1;

    switchReader()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("switch");
      devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      struct switch_s *myData;
      uint32_t attenuation;
      uint8_t count = 0;
      uint8_t devNr = 0;
      uint8_t pin, bias;
      char msgBuffer[BUFFSIZE];
      char sensorName[SENSOR_NAME_LEN];
      char ruleName[SENSOR_NAME_LEN];
      bool retVal = false;

      analogReadResolution(12);
      for (uint8_t switchNr=0; switchNr<MAX_SWITCH; switchNr++) {
        sprintf (ruleName, "switchPin_%d", switchNr);
        pin = nvs_get_int (ruleName, 99);
        if (pin < 40 && pin >=0) {
          devTypeCount[myDevTypeID]++;
          count++;
        }
      }
      devNr = 0;
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(switch_s));
      myData = (struct switch_s*) devData[myDevTypeID];
      for (uint8_t switchNr=0; switchNr<MAX_SWITCH; switchNr++) {
        sprintf (ruleName, "switchPin_%d", switchNr);
        pin = nvs_get_int (ruleName, 99);
        if (pin < 40 && pin >= 0) {
          retVal = true;
          sprintf (ruleName, "switch_%d", switchNr);
          nvs_get_string (ruleName, sensorName, ruleName, sizeof(sensorName));
          if (switchNr==0 && strcmp (sensorName, ruleName) == 0) strcpy (sensorName, device_name);
          strcpy (myData[devNr].uniquename, sensorName);
          pinMode(pin, INPUT);
          myData[devNr].pinNumber    = pin;
          myData[devNr].accumulator  = 0;
          myData[devNr].average      = 0;
          myData[devNr].averagedOver = 0;
          myData[devNr].readingCount = 0;
          myData[devNr].state        = GREEN;
          for (uint8_t level=0; level<3 ; level++) {
            sprintf (ruleName, "switch_%d%d", level, switchNr);  // Warning Logic
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
      struct switch_s *myData;

      if (devTypeCount[myDevTypeID] == 0) {
        // consolewriteln ((const char*) " * No switches found.");
        return;
      }
      consolewriteln ((const char*) "Test: switch - digital input");
      myData = (struct switch_s*) devData[myDevTypeID];
      for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
        sprintf (msgBuffer, " * OK %s.%d, name: %s, pin %d", myDevType, device, myData[device].uniquename, myData[device].pinNumber);
        consolewriteln (msgBuffer);
      }
    }

    uint8_t getStatusColor()
    {
      uint8_t retVal  = GREEN;    // Worst score for all tests of this type
      uint8_t testVal = GREEN;    // Score for the currently evaluated sensor
      struct switch_s *myData;    // Pointer to data.

      myData = (struct switch_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        testVal = 0;
        for (uint8_t innerloop=0 ; innerloop<3; innerloop++) {
          struct rpnLogic_s *alertPtr = myData[devNr].alert[innerloop];
          if (alertPtr != NULL && rpn_calc(alertPtr->count, alertPtr->term)>0) testVal = innerloop+1;
        }
        if (testVal>retVal) retVal = testVal;
        if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
          myData[devNr].state = testVal;
          xSemaphoreGive(devTypeSem[myDevTypeID]);
        }
        // else retVal = CLEAR;
      }
      return (retVal);
    }

 
    bool getXymonStatus (char *xydata)
    {
      struct switch_s *myData;
      char msgBuffer[80];
      char logicNum;
      bool retval = false;
      uint8_t statusColor = getStatusColor();

      myData = (struct switch_s*) devData[myDevTypeID];
      statusColor = GREEN;
      sprintf (xydata, "status %s.switch %s switch status %s\n\n", device_name, xymonColour[statusColor], util_gettime());
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].averagedOver > 0) {
            uint8_t currentState = myData[device].state;
            util_getLogicTextXymon (myData[device].alert[currentState-YELLOW], xydata, currentState, myData[device].uniquename);
            strcat  (xydata, " &");
            strcat  (xydata, xymonColour[currentState]);
            sprintf (msgBuffer, " %-16s %5s", myData[device].uniquename, util_ftos (myData[device].average, 2));
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, " -  pin %u\n", myData[device].pinNumber);
            strcat  (xydata, msgBuffer);
          }
          else {
//            sprintf (msgBuffer, " &red no reading on pin %d\n", myData[device].pinNumber);
            sprintf (msgBuffer, " &red no reading on pin %u\n", myData[device].pinNumber);
            strcat  (xydata, msgBuffer);
          }
        }
        sprintf (msgBuffer, "\nPolled %d times\n", myData[0].averagedOver);
        strcat  (xydata, msgBuffer);
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("switch semaphore not released.");
      return (true);   // We should have valid data
    }


    float getData(uint8_t devNr, char *parameter)
    {
      struct switch_s *myData;
      float retval = 0.00;
      char msgBuffer[3];

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct switch_s*) devData[myDevTypeID];
      if (devNr <= 0 || devNr >= devTypeCount[myDevTypeID]) return (99999.99);
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if      (strcmp (parameter,"coun") == 0) retval = myData[devNr].averagedOver;
        else if (strcmp (parameter,"val")  == 0) retval = myData[devNr].average;
        else if (strcmp (parameter,"lasv") == 0) retval = myData[devNr].lastVal;
        else if (strcmp (parameter,"vsta") == 0) retval = myData[devNr].state;
        else if (strcmp (parameter,"pin")  == 0) retval = myData[devNr].pinNumber;
        else retval=0.00;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

    void printData()
    {
      struct switch_s *myData;
      char msgBuffer[40];
      
      sprintf (msgBuffer, "switch.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct switch_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          sprintf (msgBuffer, "switch.%d.coun (%s) %d", device, myData[device].uniquename, myData[device].averagedOver);
          consolewriteln (msgBuffer);
          sprintf (msgBuffer, "switch.%d.val  (%s) ", device, myData[device].uniquename);
          consolewrite (msgBuffer);
          if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
          else {
            consolewriteln (util_ftos (myData[device].average, 2));
          }
          sprintf (msgBuffer, "switch.%d.lasv (%s) %u", device, myData[device].uniquename, myData[device].lastVal);
          consolewriteln (msgBuffer);
          sprintf (msgBuffer, "switch.%d.vsta (%s) %u", device, myData[device].uniquename, myData[device].state);
          consolewriteln (msgBuffer);
          sprintf (msgBuffer, "switch.%d.pin  (%s) %u", device, myData[device].uniquename, myData[device].pinNumber);
          consolewriteln (msgBuffer);
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

    void getXymonStats (char *xydata)
    {
      struct switch_s *myData;
      char msgBuffer[40];
      
      // setup pointer to data array
      myData = (struct switch_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].averagedOver > 0) {
            sprintf (msgBuffer, "[switch.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:val:GAUGE:600:U:U ");
            strcat  (xydata, msgBuffer);
            strcat  (xydata, util_ftos (myData[device].average, 2));
            strcat  (xydata, "\n");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }


 };

 switchReader the_switch;
