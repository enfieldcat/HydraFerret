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


class bh1750 {
  private:
    const char myDevType[9] = "bh1750";
    uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      struct bh1750_s *myData;
      char msgBuffer[SENSOR_NAME_LEN];
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted
      uint32_t reading = 0;
      // uint8_t  modeSelector = 1;
      uint8_t  resolutionMode[] = { 0x20, 0x20, 0x21, 0x21, 0x21 };
      uint8_t  integrationLo[]  = {   37,   69,   69,  138,  254 };
      uint8_t  integrationHi[]  = {    0,    0,    0,    0,    0 };
      float    scalingFactor[]  = { 0.536231884, 1.0, 2.0, 4.0, 7.362318841 };
      
      loopCount = 0;
      myDevTypeID = util_get_dev_type("bh1750");
      if (myDevTypeID!=255) {
        // set hi selector value and scaling factors
        for (loopCount=0; loopCount<(sizeof(resolutionMode)/sizeof(uint8_t)); loopCount++){
          integrationHi[loopCount] = (integrationLo[loopCount] >> 5 ) | 0x40;
          integrationLo[loopCount] = (integrationLo[loopCount] & 0x1f ) | 0x60; 
        }
        myData = (struct bh1750_s*) (devData[myDevTypeID]);
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        queueData = myDevTypeID;
        if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
          for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
            myData[device].averagedOver  = 0;
            myData[device].readingCount  = 0;
            myData[device].lux_average   = 0.0;
            myData[device].lux_accum     = 0.0;
            myData[device].lux_last      = 0.0;
            myData[device].gear          = 1;
          }
          xSemaphoreGive(devTypeSem[myDevTypeID]);
        }
        // loop forever collecting data
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing bh1750 signal");
            if (ansiTerm) displayAnsi(1);
          }
          loopCount++;
          for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
            if (myData[device].isvalid) {
              util_i2c_command (myData[device].bus, myData[device].addr, (uint8_t) 0x01); // power on
            }
          }
          delay (30); // Give it a start up pause
          for (uint8_t device=0; device <devTypeCount[myDevTypeID]; device++) {
            if (myData[device].isvalid) {
              // each device may be operating with a different integration window and resolution mode
              util_i2c_command (myData[device].bus, myData[device].addr, (uint8_t) 0x07); // clear registers
              util_i2c_command (myData[device].bus, myData[device].addr, (uint8_t) (integrationHi[myData[device].gear])); // Set Integration Window time - High part
              util_i2c_command (myData[device].bus, myData[device].addr, (uint8_t) (integrationLo[myData[device].gear])); // Set Integration Window time - Low part
              util_i2c_command (myData[device].bus, myData[device].addr, (uint8_t) (resolutionMode[myData[device].gear]));
            }
          }
          delay(670);  // all devices wait for max delay at longest integration window
          for (uint8_t device=0; device <devTypeCount[myDevTypeID]; device++) {
            if (myData[device].isvalid) {
              util_i2c_read (myData[device].bus, myData[device].addr, (uint8_t) 2, (uint8_t*) msgBuffer);
              util_i2c_command (myData[device].bus, myData[device].addr, 0x00); // power off
              reading = (((uint8_t)(msgBuffer[0])) << 8) + (uint8_t) msgBuffer[1];
              if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                if (reading < 65535) {  // assume max offset is an error
                  if (myData[device].gear==1) myData[device].lux_last = (reading / myData[device].opacity);
                  else myData[device].lux_last = (reading / (scalingFactor[myData[device].gear] * myData[device].opacity));
                  myData[device].lux_accum += myData[device].lux_last;
                  myData[device].readingCount++;
                  // change reading mode, Low modes read bightest light, higher numbers lowest light: Preferred mode is 1
                  if (reading<100 && myData[device].gear<4) (myData[device].gear)++;        // Increase resolution if reading is low
                  else if (reading>250 && myData[device].gear>1) (myData[device].gear)--;   // Decrease resolution in brighter than dim light
                  else if (reading>49152 && myData[device].gear>0) (myData[device].gear)--; // Catch all decrease in resolution
                  else if (reading<16384 && myData[device].gear<1) (myData[device].gear)++; // Catch all increace in resolution
                }
                else {
                  myData[device].lux_last = 0.0;
                  if (myData[device].gear>0) myData[device].gear--; // Maxed out reading => incomplete reading or light too bright?
                }
                xSemaphoreGive(devTypeSem[myDevTypeID]);
              }
            }
          }
          if (loopCount >= updateCount) {
            for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
              if (myData[device].isvalid) {
                if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                  myData[device].averagedOver = myData[device].readingCount;
                  if (myData[device].readingCount != 0)
                    myData[device].lux_average  = myData[device].lux_accum / myData[device].readingCount;
                  else myData[device].lux_average = 0.0;
                  myData[device].lux_accum        = 0.0;
                  myData[device].readingCount     = 0;
                  loopCount = 0;
                  xSemaphoreGive(devTypeSem[myDevTypeID]);
                }
              }
            }
          }
        }
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Could not determine bh1750 type ID for update loop");
        if (ansiTerm) displayAnsi(1);
      }
      vTaskDelete( NULL );
    }

  public:

    char subtypeList[1][5] = { "lux" };
    char subtypeLen = 1;

    bh1750()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("bh1750");
      devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      bool retval = false;
      const uint8_t dev_addr[] = { 0x23, 0x5c };
      struct bh1750_s *myData;
      char devNr;
      char sensorName[SENSOR_NAME_LEN];
      char ruleName[SENSOR_NAME_LEN];
      char msgBuffer[BUFFSIZE];

      if (myDevTypeID==255) return false;   // unknown type - get out of here
      // Probe for candidate devices
      for (int bus=0; bus<2; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)); device++) {
          if (util_i2c_probe(bus, dev_addr[device])) {
            devTypeCount[myDevTypeID]++;
          }
        }
      }
      if (devTypeCount[myDevTypeID] == 0) {
        consolewriteln ((const char*) "No bh1750 sensors found.");
        return(false);  // nothing found!
      }
      // set up and inittialise structures
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(bh1750_s));
      myData = (struct bh1750_s*) devData[myDevTypeID];
      devNr = 0;
      for (int bus=0; bus<2 && devNr<devTypeCount[myDevTypeID]; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)) && devNr<devTypeCount[myDevTypeID]; device++) {
          myData[devNr].bus = bus;
          myData[devNr].addr = dev_addr[device];
          myData[devNr].isvalid = false;
          if (util_i2c_probe(bus, dev_addr[device])) {
            util_i2c_command (myData[device].bus, myData[device].addr, (uint8_t) 0x01); // Turn on
            sprintf (sensorName, "bh1750_%d", devNr);
            nvs_get_string (sensorName, myData[devNr].uniquename, sensorName, sizeof (myData[devNr].uniquename));
            if (devNr==0 && strcmp (myData[devNr].uniquename, sensorName) == 0) strcpy (myData[devNr].uniquename, device_name);
            sprintf (sensorName, "bh1750_Opacty_%d", devNr);
            myData[devNr].opacity = nvs_get_float(sensorName, 1.2);
            retval = true;
            myData[devNr].isvalid = true;
            myData[devNr].state = 0;
            for (uint8_t level=0; level<3 ; level++) {
               sprintf (ruleName, "bh17lux_%d%d", level, devNr);  // Warning Logic
               nvs_get_string (ruleName, msgBuffer, "disable", sizeof(msgBuffer));
               if (strcmp (msgBuffer, "disable") != 0) {
                 consolewrite ("Enable ");
                 consolewrite ((char*) alertLabel[level]);
                 consolewrite (": ");
                 consolewrite (ruleName);
                 consolewrite (" as ");
                 consolewriteln (msgBuffer);
                 util_getLogic (ruleName, &myData[devNr].alert[level]);
               }
               else myData[devNr].alert[level] = NULL;
             }
            metricCount[LUX]++;
            devNr++;
          }
        }
      }
      if (retval) xTaskCreate(updateloop, devType[myDevTypeID], 4096, NULL, 12, NULL);
      // inventory();
      myData = NULL;
      return (retval);
    }

    void displayOpacity()
    {
      char msgBuffer[40];
      struct bh1750_s *myData;
      
      myData = (struct bh1750_s*) devData[myDevTypeID];
      for (char n=0; n<devTypeCount[myDevTypeID]; n++) {
        sprintf (msgBuffer, "   - %s.%d (%s) %s", myDevType, n, myData[n].uniquename, util_ftos(myData[n].opacity, 2));
        consolewriteln (msgBuffer);
      }
    }

    void setOpacity (int deviceNr, float opacity)
    {
      struct bh1750_s *myData;
      char msgBuffer[20];

      sprintf (msgBuffer, "bh1750_Opacty_%d", deviceNr);
      myData = (struct bh1750_s*) devData[myDevTypeID];
      if (deviceNr<devTypeCount[myDevTypeID]) myData[deviceNr].opacity = opacity;
      nvs_put_float (msgBuffer, opacity);
    }

    void inventory()
    {
      char msgBuffer[80];
      char devStatus[9];
      struct bh1750_s *myData;

      consolewriteln ((const char*) "Test: bh1750 - Light/Lux");
      if (devTypeCount[myDevTypeID] == 0) {
        consolewriteln ((const char*) " * No bh1750 sensors found.");
        return;
      }
      myData = (struct bh1750_s*) devData[myDevTypeID];
      for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
        if (myData[device].isvalid) {
          strcpy(devStatus, "OK");
          sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x, name: %s, opacity: %s", devStatus, myDevType, device, myData[device].bus, myData[device].addr, myData[device].uniquename, util_ftos(myData[device].opacity, 2));
        }
        else {
          strcpy(devStatus, "REJECTED");
          sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x", devStatus, myDevType, device, myData[device].bus, myData[device].addr);
        }
        consolewriteln (msgBuffer);
      }
    }

    uint8_t getStatusColor()
    {
      uint8_t retVal  = GREEN;    // Worst score for all tests of this type
      uint8_t testVal = GREEN;    // Score for the currently evaluated sensor
      struct bh1750_s *myData;    // Pointer to data.

      myData = (struct bh1750_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        if (myData[devNr].isvalid) {
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
        }
        else retVal = CLEAR;
      }
      return (retVal);
    }
 
    bool getXymonStatus (char *xydata)
    {
      struct bh1750_s *myData;
      float ftcd;
      char msgBuffer[80];
      char logicNum;

      myData = (struct bh1750_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            ftcd = myData[device].lux_average * 0.092903;
            uint8_t currentState = myData[device].state;
            util_getLogicTextXymon (myData[device].alert[currentState-YELLOW], xydata, currentState, myData[device].uniquename);
            strcat (xydata, " &");
            strcat (xydata, xymonColour[currentState]);
            sprintf (msgBuffer, " %-16s %8s lux", myData[device].uniquename, util_ftos (myData[device].lux_average, 1));
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "  %8s ftcd - average over %d readings\n", util_ftos (ftcd, 2), myData[device].averagedOver);
            strcat  (xydata, msgBuffer);
          }
          else {
            sprintf (msgBuffer, " &red Invalid or faulty bh1750 found, bus %d, address %d\n", myData[device].bus, myData[device].addr);
            strcat  (xydata, msgBuffer);
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("bh1750 semaphore not released.");
      return (true);
    }

    void getXymonStats (char *xydata)
    {
      struct bh1750_s *myData;
      char msgBuffer[40];
      
      // setup pointer to data array
      myData = (struct bh1750_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            sprintf (msgBuffer, "[lux.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, "DS:index:GAUGE:600:U:U ");
            strcat  (xydata, util_ftos (myData[device].lux_average, 1));
            strcat  (xydata, "\n");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

    void printData()
    {
      struct bh1750_s *myData;
      char msgBuffer[40];
      
      sprintf (msgBuffer, "bh1750.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct bh1750_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid) {
            sprintf (msgBuffer, "bh1750.%d.lux  (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].lux_average, 1));
              consolewriteln (" lux");
            }
            sprintf (msgBuffer, "bh1750.%d.lasl (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewriteln (util_ftos (myData[device].lux_last, 2));
            sprintf (msgBuffer, "bh1750.%d.opac (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewriteln (util_ftos (myData[device].opacity, 2));
            sprintf (msgBuffer, "bh1750.%d.lsta (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].state, 0));
            consolewrite (" (");
            consolewrite ((char*) xymonColour[myData[device].state]);
            consolewriteln (")");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }     
    }

    float getData(uint8_t devNr, char *parameter)
    {
      struct bh1750_s *myData;
      float retval;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct bh1750_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (99999.99);
      if (myData[devNr].averagedOver < 1) return (9999.99);
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if (myData[devNr].averagedOver > 0) {
          if      (strcmp(parameter,"lux")  == 0) retval = myData[devNr].lux_average;
          else if (strcmp(parameter,"lasl") == 0) retval = myData[devNr].lux_last;
          else if (strcmp(parameter,"opac") == 0) retval = myData[devNr].opacity;
          else if (strcmp(parameter,"lsta") == 0) retval = myData[devNr].state;
          else retval = 0.00;
        }
        else retval=0.00;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

};


bh1750 the_bh1750;
