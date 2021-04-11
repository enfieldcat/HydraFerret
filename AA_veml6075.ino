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


// #include "Adafruit_VEML6075.h"
// #include "envGeneral.h"

class veml6075 {
  private:
    const char myDevType[9] = "veml6075";
    uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      struct veml6075_s *myData;
      char msgBuffer[SENSOR_NAME_LEN];
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted
      float reading = 0.0;

      loopCount = 0;
      myDevTypeID = util_get_dev_type("veml6075");
      if (myDevTypeID!=255) {
        myData = (struct veml6075_s*) (devData[myDevTypeID]);
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        queueData = myDevTypeID;
        if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
          for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
            myData[device].averagedOver  = 0;
            myData[device].readingCount  = 0;
            myData[device].uvi_average   = 0.0;
            myData[device].uvi_accum     = 0.0;
            myData[device].uvi_last      = 0.0;
            myData[device].uva_average   = 0.0;
            myData[device].uva_accum     = 0.0;
            myData[device].uva_last      = 0.0;
            myData[device].uvb_average   = 0.0;
            myData[device].uvb_accum     = 0.0;
            myData[device].uvb_last      = 0.0;
          }
          xSemaphoreGive(devTypeSem[myDevTypeID]);
        }
        // loop forever collecting data
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing veml6075 signal");
            if (ansiTerm) displayAnsi(1);
          }
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
            if (loopCount >= updateCount) loopCount = 0;
            loopCount++;
            for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
              if (myData[device].isvalid) {
                if (xSemaphoreTake(wiresemaphore[myData[device].bus], 30000) == pdTRUE) {
                  reading = (myData[device].veml)->readUVI();
                  if (reading > 0.00 && reading < 15.0) {
                    myData[device].uva_last   = (myData[device].veml)->readUVA();
                    myData[device].uva_accum += myData[device].uva_last;
                    myData[device].uvb_last   = (myData[device].veml)->readUVB();
                    myData[device].uvb_accum += myData[device].uvb_last;
                  }
                  xSemaphoreGive(wiresemaphore[myData[device].bus]);
                  // sprintf (msgBuffer, "UVI->%s", util_ftos (reading, 2));
                  // consolewriteln (msgBuffer);
                  if (reading < 13.0) {
                    if (reading < 0.0) reading = 0.0; // No negative index
                    myData[device].uvi_last   = reading;
                    myData[device].uvi_accum += reading;
                    myData[device].readingCount++;  // reading count should be non-zero!
                    if(loopCount >= updateCount) {  // update count = number of readings to aeverage over
                      myData[device].averagedOver  = myData[device].readingCount;
                      myData[device].uvi_average   = myData[device].uvi_accum  / (double)(myData[device].averagedOver);
                      myData[device].uva_average   = myData[device].uva_accum  / (double)(myData[device].averagedOver);
                      myData[device].uvb_average   = myData[device].uvb_accum  / (double)(myData[device].averagedOver);
                      myData[device].readingCount  = 0;
                      myData[device].uvi_accum     = 0.0;
                      myData[device].uva_accum     = 0.0;
                      myData[device].uvb_accum     = 0.0;
                    }
                  }
                  else {
                    if (ansiTerm) displayAnsi(3);
                    consolewriteln ("Exceptionally high UV index reading ignored on veml6075 device\n");
                    if (ansiTerm) displayAnsi(1);
                  }
                }
              }
            }
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
      }
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Could not determine veml6075 type ID for update loop");
      if (ansiTerm) displayAnsi(1);
      vTaskDelete( NULL );
    }

  public:

    char subtypeList[1][5] = { "uv" };
    char subtypeLen = 1;

    veml6075()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("veml6075");
      if (myDevTypeID!=255) devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      bool retval = false;
      const uint8_t dev_addr[] = { 0x10 };
      struct veml6075_s *myData;
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
        consolewriteln ((const char*) "No veml6075 sensors found.");
        return(false);  // nothing found!
      }
      // set up and inittialise structures
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(veml6075_s));
      myData = (struct veml6075_s*) devData[myDevTypeID];
      devNr = 0;
      for (int bus=0; bus<2 && devNr<devTypeCount[myDevTypeID]; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)) && devNr<devTypeCount[myDevTypeID]; device++) {
          myData[devNr].bus = bus;
          myData[devNr].addr = dev_addr[device];
          myData[devNr].isvalid = false;
          if (util_i2c_probe(bus, dev_addr[device])) {
            myData[devNr].veml     = new Adafruit_VEML6075;
            for (uint8_t innerloop=0; innerloop<3; innerloop++) {
              myData[devNr].alert[innerloop] = NULL;
            }
            sprintf (sensorName, "veml6075_%d", devNr);
            nvs_get_string (sensorName, myData[devNr].uniquename, sensorName, sizeof (myData[devNr].uniquename));
            if (devNr==0 && strcmp (myData[devNr].uniquename, sensorName) == 0) strcpy (myData[devNr].uniquename, device_name);
            if (xSemaphoreTake(wiresemaphore[bus], 30000) == pdTRUE) {
              if (myData[devNr].veml->begin(VEML6075_100MS, false, false, &(I2C_bus[bus]))) {
                myData[devNr].isvalid = true;
                myData[devNr].state = 0;
                retval = true;
                metricCount[UV]++;
                for (uint8_t level=0; level<3 ; level++) {
                  sprintf (ruleName, "vemluv_%d%d", level, devNr);  // Warning Logic
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
                }
              }
              else {
                myData[devNr].isvalid = false;
                sprintf (sensorName, "%d->0x%02x", bus, dev_addr[device]);
                if (ansiTerm) displayAnsi(3);
                consolewrite ("Failed to initialise veml6075 ");
                consolewriteln (sensorName);
                if (ansiTerm) displayAnsi(1);
              }
              xSemaphoreGive(wiresemaphore[bus]);
            }
           devNr++;
           // util_dump ((char*) &(myData[devNr]), sizeof(struct veml6075_s));
          }
        }
      }
      if (retval) xTaskCreate(updateloop, devType[myDevTypeID], 4096, NULL, 12, NULL);
      // inventory();
      myData = NULL;
      return (retval);
    }

    void inventory()
    {
      char msgBuffer[80];
      char devStatus[9];
      struct veml6075_s *myData;

      consolewriteln ((const char*) "Test: veml6075 - UV Index");
      if (devTypeCount[myDevTypeID] == 0) {
        consolewriteln ((const char*) " * No veml6075 sensors found.");
        return;
      }
      myData = (struct veml6075_s*) devData[myDevTypeID];
      for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
        if (myData[device].isvalid) {
          strcpy(devStatus, "OK");
          sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x, name: %s", devStatus, myDevType, device, myData[device].bus, myData[device].addr, myData[device].uniquename);
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
      uint8_t testVal = GREEN;    // Score dor the currently evaluated sensor
      struct veml6075_s *myData;  // Pointer to data.

      myData = (struct veml6075_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        if (myData[devNr].isvalid) {
          testVal = 0;
          for (uint8_t innerloop=0 ; innerloop<3; innerloop++) {
            struct rpnLogic_s *alertPtr = myData[devNr].alert[innerloop];
            if (alertPtr != NULL && rpn_calc(alertPtr->count, alertPtr->term)>0) testVal = innerloop+YELLOW;
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
      struct veml6075_s *myData;
      char msgBuffer[80];

      myData = (struct veml6075_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            if (myData[device].uvi_average>-1.0 && myData[device].uvi_average<=20) {
              uint8_t currentState = myData[device].state;
              util_getLogicTextXymon (myData[device].alert[currentState-YELLOW], xydata, currentState, myData[device].uniquename);
              strcat (xydata, " &");
              strcat (xydata, xymonColour[currentState]);
              sprintf (msgBuffer, " %-16s %8s - average over %d readings\n", myData[device].uniquename, util_ftos (myData[device].uvi_average, 1), myData[device].averagedOver);
              strcat  (xydata, msgBuffer);
            }
            else {
              sprintf (msgBuffer, " %s out of range reading for %s: %s", myDevType, myData[device].uniquename, util_ftos (myData[device].uvi_average, 1));
              consolewriteln (msgBuffer);
              strcat (xydata, " &red ");
              strcat (xydata, msgBuffer);
              strcat (xydata, "\n");
            }
          }
          else {
            sprintf (msgBuffer, " &red Invalid or faulty veml6075 found, bus %d, address %d\n", myData[device].bus, myData[device].addr);
            strcat  (xydata, msgBuffer);
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("veml6075 semaphore not released.");
      return (true);
    }

    void getXymonStats (char *xydata)
    {
      struct veml6075_s *myData;
      char msgBuffer[40];
      int dp;
      double tDouble;
      
      // setup pointer to data array
      myData = (struct veml6075_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            if (myData[device].uvi_average>-1.0 && myData[device].uvi_average<=15) {
              sprintf (msgBuffer, "[uv.%s.rrd]\n", myData[device].uniquename);
              strcat  (xydata, msgBuffer);
              strcat  (xydata, "DS:index:GAUGE:600:U:U ");
              strcat  (xydata, util_ftos (myData[device].uvi_average, 1));
              strcat  (xydata, "\n");
            }
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

    void printData()
    {
      struct veml6075_s *myData;
      char msgBuffer[50];
      
      sprintf (msgBuffer, "veml6075.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct veml6075_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid) {
            sprintf (msgBuffer, "veml6075.%d.uvi (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0 || myData[device].uvi_average<0) consolewriteln ("No data collected");
            else consolewriteln (util_ftos (myData[device].uvi_average, 1));
            sprintf (msgBuffer, "veml6075.%d.lasi (%s) %s", device, myData[device].uniquename, util_ftos (myData[device].uvi_last, 1));
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "veml6075.%d.uva (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0 || myData[device].uva_average<0) consolewriteln ("No data collected");
            else consolewriteln (util_ftos (myData[device].uva_average, 1));
            sprintf (msgBuffer, "veml6075.%d.lasa (%s) %s", device, myData[device].uniquename, util_ftos (myData[device].uva_last, 1));
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "veml6075.%d.uvb (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0 || myData[device].uvb_average<0) consolewriteln ("No data collected");
            else consolewriteln (util_ftos (myData[device].uvb_average, 1));
            sprintf (msgBuffer, "veml6075.%d.lasb (%s) %s", device, myData[device].uniquename, util_ftos (myData[device].uvb_last, 1));
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "veml6075.%d.usta (%s) ", device, myData[device].uniquename);
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
      struct veml6075_s *myData;
      float retval;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct veml6075_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (99999.99);
      if (myData[devNr].averagedOver < 1) return (9999.99);
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if (myData[devNr].averagedOver > 0) {
          if      (strcmp(parameter,"uvi") == 0) retval = myData[devNr].uvi_average;
          else if (strcmp(parameter,"lasi")== 0) retval = myData[devNr].uvi_last;
          else if (strcmp(parameter,"uva") == 0 && myData[devNr].uva_average>0.0) retval = myData[devNr].uva_average;
          else if (strcmp(parameter,"lasa")== 0 && myData[devNr].uva_last>0.0)    retval = myData[devNr].uva_last;
          else if (strcmp(parameter,"uvb") == 0 && myData[devNr].uvb_average>0.0) retval = myData[devNr].uvb_average;
          else if (strcmp(parameter,"lasb")== 0 && myData[devNr].uvb_last>0.0)    retval = myData[devNr].uvb_last;
          else if (strcmp(parameter,"usta")== 0) retval = myData[devNr].state;
          else retval = 0.00;
        }
        else retval=0.00;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

};


static veml6075 the_veml6075;
