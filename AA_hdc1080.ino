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


class hdc1080 {
  private:
    const char myDevType[9] = "hdc1080";
    uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      struct hdc1080_s *myData;
      char msgBuffer[SENSOR_NAME_LEN];
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted
      union {
        uint8_t bytes[4] = {0x02, 0x10, 0x00, 0x00};
        uint16_t ints[2];
      } result;
      float humidity, temperature;
      uint8_t tempByte;
      
      loopCount = 0;
      myDevTypeID = util_get_dev_type("hdc1080");
      if (myDevTypeID!=255) {
        util_deviceTimerCreate(myDevTypeID);
        myData = (struct hdc1080_s*) (devData[myDevTypeID]);
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        if (xTimerChangePeriod(devTypeTimer[myDevTypeID], pdMS_TO_TICKS(pollInterval), pdMS_TO_TICKS(1100)) != pdPASS) {
          consolewriteln("Unable to adjust hdc1080 temperature poll timer period, keep at 1 second");
          pollInterval = 1000;
          updateCount = 300;
          }
        queueData = myDevTypeID;
        if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval+1000)) == pdTRUE) {
          for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
            myData[device].averagedOver  = 0;
            myData[device].readingCount  = 0;
            myData[device].temp_average  = 0.0;
            myData[device].temp_accum    = 0.0;
            myData[device].temp_last     = 0.0;
            myData[device].humid_average = 0.0;
            myData[device].humid_accum   = 0.0;
            myData[device].humid_last    = 0.0;
            util_i2c_write (myData[device].bus, myData[device].addr, 3, result.bytes);
          }
          xSemaphoreGive(devTypeSem[myDevTypeID]);
        }
        // loop forever collecting data
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing hdc1080 signal");
            if (ansiTerm) displayAnsi(1);
          }
          loopCount++;
          for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
            if (myData[device].isvalid) {
              util_i2c_command (myData[device].bus, myData[device].addr, (uint8_t) 0x00); // start read
            }
          }
          delay(350);  // data aquisition delay
          for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
            if (myData[device].isvalid) {
              util_i2c_read (myData[device].bus, myData[device].addr, (uint8_t) 4, (uint8_t*) result.bytes);
              if (result.ints[0]>65530) {
                // On failure try a second read
                util_i2c_command (myData[device].bus, myData[device].addr, (uint8_t) 0x00); // start read
                delay (250);
                util_i2c_read (myData[device].bus, myData[device].addr, (uint8_t) 4, (uint8_t*) result.bytes);
              }
              temperature =((((((uint16_t)((uint8_t)(result.bytes[0])) << 8) + (uint8_t) result.bytes[1])) * 165.00) / 65536.00) - 40.00;
              humidity    =  ((((uint16_t)((uint8_t)(result.bytes[2])) << 8) + (uint8_t) result.bytes[3])) / 655.36;
              if (result.ints[0]<65530 && xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                myData[device].temp_last    = temperature;
                myData[device].humid_last   = humidity;
                myData[device].temp_accum  += temperature;
                myData[device].humid_accum += humidity;
                myData[device].readingCount++;
                xSemaphoreGive(devTypeSem[myDevTypeID]);
              }
            }
          }
          if(loopCount >= updateCount) {  // update count = number of readings to aeverage over
            for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
              if (myData[device].isvalid) {
                if (result.ints[0]<65530 && xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                  myData[device].averagedOver  = myData[device].readingCount;
                  if (myData[device].averagedOver > 0) {
                    myData[device].temp_average  = myData[device].temp_accum  / (double)(myData[device].averagedOver);
                    myData[device].humid_average = myData[device].humid_accum / (double)(myData[device].averagedOver);
                  }
                  myData[device].readingCount  = 0;
                  myData[device].temp_accum    = 0.0;
                  myData[device].humid_accum   = 0.0;
                  xSemaphoreGive(devTypeSem[myDevTypeID]);
                }
              }
            }
            loopCount = 0;
          }
        }
      }
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Could not determine hdc1080 type ID for update loop");
      if (ansiTerm) displayAnsi(1);
      vTaskDelete( NULL );
    }

  public:

    char subtypeList[2][5] = { "temp", "humi" };
    char subtypeLen = 2;

    hdc1080()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("hdc1080");
      devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      bool retval = false;
      const uint8_t dev_addr[] = { 0x40 };
      struct hdc1080_s *myData;
      char devNr;
      char ruleName[SENSOR_NAME_LEN];
      char msgBuffer[BUFFSIZE];
      char sensorName[SENSOR_NAME_LEN];
      uint8_t check[2];

      if (myDevTypeID==255) return false;   // unknown type - get out of here
      // Probe for candidate devices
      for (uint8_t bus=0; bus<2; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)); device++) {
          if (util_i2c_probe(bus, dev_addr[device])) {
            util_i2c_command (bus, (uint8_t) (dev_addr[device]), (uint8_t) 0xff);
            util_i2c_read    (bus, (uint8_t) (dev_addr[device]), (uint8_t) 2, check);
            if (check[0]==0x10 && check[1]==0x50) devTypeCount[myDevTypeID]++; // verify device type ID
          }
        }
      }
      if (devTypeCount[myDevTypeID] == 0) {
        consolewriteln ((const char*) "No hdc1080 sensors found.");
        return(false);  // nothing found!
      }
      // set up and inittialise structures
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(hdc1080_s));
      myData = (struct hdc1080_s*) devData[myDevTypeID];
      devNr = 0;
      for (uint8_t bus=0; bus<2 && devNr<devTypeCount[myDevTypeID]; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)) && devNr<devTypeCount[myDevTypeID]; device++) {
          if (util_i2c_probe(bus, dev_addr[device])) {
            myData[devNr].bus = bus;
            myData[devNr].addr = dev_addr[device];
            myData[devNr].isvalid = false;
            util_i2c_command (bus, (uint8_t) (dev_addr[device]), (uint8_t) 0xff);
            util_i2c_read    (bus, (uint8_t) (dev_addr[device]), (uint8_t) 2, check);
            if (check[0]==0x10 && check[1]==0x50) { // verify device type ID
              sprintf (sensorName, "hdc1080_%d", devNr);
              nvs_get_string (sensorName, myData[devNr].uniquename, sensorName, sizeof (myData[devNr].uniquename));
              if (devNr==0 && strcmp (myData[devNr].uniquename, sensorName) == 0) strcpy (myData[devNr].uniquename, device_name);
              sprintf (sensorName, "hdc1080DP_%d", devNr);
              nvs_get_string (sensorName, myData[devNr].dewpointName, "none", sizeof (myData[devNr].uniquename));
              retval = true;
              //
              // Process rules
              //
              retval = true;
              myData[devNr].isvalid = true;
              for (uint8_t sense=0; sense<subtypeLen; sense++) {
                myData[devNr].state[sense] = GREEN;
                for (uint8_t level=0; level<3 ; level++) {
                  sprintf (ruleName, "hdc1%s_%d%d", subtypeList[sense], level, devNr);  // Warning Logic
                  nvs_get_string (ruleName, msgBuffer, "disable", sizeof(msgBuffer));
                  if (strcmp (msgBuffer, "disable") != 0) {
                    consolewrite ("Enable ");
                    consolewrite ((char*)alertLabel[level]);
                    consolewrite (": ");
                    consolewrite (ruleName);
                    consolewrite (" as ");
                    consolewriteln (msgBuffer);
                    util_getLogic (ruleName, &myData[devNr].alert[level+(sense*3)]);
                  }
                  else myData[devNr].alert[level+(sense*3)] = NULL;
                }
              }
              //
              // Update counts
              //
              metricCount[TEMP]++;
              metricCount[HUMID]++;
              devNr++;
            }
            if (devNr<devTypeCount[myDevTypeID]) myData[devNr].isvalid = false;
          }
          if (devNr<devTypeCount[myDevTypeID]) myData[devNr].isvalid = false;
        }
      }
      if (retval) xTaskCreate(updateloop, devType[myDevTypeID], 4096, NULL, 12, NULL);
      // inventory();
      // myData = NULL;
      return (retval);
    }

    void inventory()
    {
      char msgBuffer[80];
      char devStatus[9];
      struct hdc1080_s *myData;

      consolewriteln ((const char*) "Test: hdc1080 - Temperature & Humidity");
      if (devTypeCount[myDevTypeID] == 0) {
        consolewriteln ((const char*) " * No hdc1080 sensors found.");
        return;
      }
      myData = (struct hdc1080_s*) devData[myDevTypeID];
      for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
        if (myData[device].isvalid) {
          strcpy  (devStatus, "OK");
          sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x, name: %s", devStatus, myDevType, device, myData[device].bus, myData[device].addr, myData[device].uniquename);
        }
        else {
          strcpy  (devStatus, "REJECTED");
          sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x", devStatus, myDevType, device, myData[device].bus, myData[device].addr);
        }
        consolewriteln (msgBuffer);
      }
    }

    uint8_t getStatusColor(uint8_t testType)
    {
      uint8_t retVal  = GREEN;    // Worst score for all tests of this type
      uint8_t testVal = GREEN;    // Score for the currently evaluated sensor
      uint8_t tStart, tEnd, idx;  // indexes to pointers for tests taken
      struct hdc1080_s *myData;   // Pointer to data.

      switch (testType) {
        case TEMP:  tStart = 0; tEnd = 3; idx = 0; break;
        case HUMID: tStart = 3; tEnd = 6; idx = 1; break;
        default:    tStart = 0; tEnd = 0; idx = 0;
      }
      myData = (struct hdc1080_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        if (myData[devNr].isvalid) {
          testVal = 0;
          for (uint8_t innerloop=tStart ; innerloop<tEnd; innerloop++) {
            struct rpnLogic_s *alertPtr = myData[devNr].alert[innerloop];
            if (alertPtr != NULL && rpn_calc(alertPtr->count, alertPtr->term)>0) testVal = (innerloop-tStart)+1;
          }
          if (testVal > retVal) retVal = testVal;
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
            myData[devNr].state[idx] = testVal;
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
        // else retVal = CLEAR;
      }
      return (retVal);
    }

    bool getXymonStatus (char *xydata, int measureType)
    {
      struct hdc1080_s *myData;
      char msgBuffer[80];
      float altValue, dewPoint;
      uint8_t indx = 0;

      if (measureType == HUMID) indx = 1;
      myData = (struct hdc1080_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            uint8_t currentState = myData[device].state[indx];
            util_getLogicTextXymon (myData[device].alert[(currentState-YELLOW)+(3*indx)], xydata, currentState, myData[device].uniquename);
            if (measureType == TEMP) {
              altValue = (myData[device].temp_average * 1.8) + 32.0;
              sprintf (msgBuffer, " &%s %-16s %8s'C", xymonColour[currentState], myData[device].uniquename, util_ftos (myData[device].temp_average, 2));
              strcat  (xydata, msgBuffer);
              sprintf (msgBuffer, "  %8s'F  (average over %d readings)\n", util_ftos (altValue, 2), myData[device].averagedOver);
              strcat  (xydata, msgBuffer);
              if (strcmp (myData[device].dewpointName, "none") != 0) {
                dewPoint = util_dewpoint(myData[device].temp_average, myData[device].humid_average);
                altValue = (dewPoint * 1.8) + 32.0;
                sprintf (msgBuffer, " &clear %-16s %8s'C", myData[device].dewpointName, util_ftos (dewPoint, 2));
                strcat  (xydata, msgBuffer);
                sprintf (msgBuffer, "  %8s'F  (", util_ftos (altValue, 2));
                strcat  (xydata, msgBuffer);
                sprintf (msgBuffer, "dewpoint based on %s%% RH)\n", util_ftos (myData[device].humid_average, 1));
                strcat  (xydata, msgBuffer);
              }
            }
            else if (measureType==HUMID) {
              sprintf (msgBuffer, " &%s %-16s %8s%%\n", xymonColour[currentState], myData[device].uniquename, util_ftos (myData[device].humid_average, 2));
              strcat  (xydata, msgBuffer);
            }
          }
          else // if (measureType != TEMP) {
          {
            sprintf (msgBuffer, " &red Invalid or faulty hdc1080 found, bus %d, address %d\n", myData[device].bus, myData[device].addr);
            strcat  (xydata, msgBuffer);
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("hdc1080 semaphore not released.");
      return (true);
    }


    void getXymonStats (char *xydata)
    {
      struct hdc1080_s *myData;
      char msgBuffer[40];
      int dp;
      double tDouble;
      
      // setup pointer to data array
      myData = (struct hdc1080_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            sprintf (msgBuffer, "[temperature.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, "DS:temperature:GAUGE:600:U:U ");
            strcat  (xydata, util_ftos (myData[device].temp_average, 2));
            strcat  (xydata, "\n");
            if (strcmp (myData[device].dewpointName, "none") != 0) {
              sprintf (msgBuffer, "[temperature.%s.rrd]\n", myData[device].dewpointName);
              strcat  (xydata, msgBuffer);
              strcat  (xydata, "DS:temperature:GAUGE:600:U:U ");
              strcat  (xydata, util_ftos (util_dewpoint(myData[device].temp_average, myData[device].humid_average), 2));
              strcat  (xydata, "\n");
            }
            if (myData[device].humid_average >= 0.0 && myData[device].humid_average <= 100.0) {
              sprintf (msgBuffer, "[humidity.%s.rrd]\n", myData[device].uniquename);
              strcat  (xydata, msgBuffer);
              strcat  (xydata, "DS:humidity:GAUGE:600:U:U ");
              strcat  (xydata, util_ftos (myData[device].humid_average, 2));
              strcat  (xydata, "\n");
            }
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }
    
    float getTemperature(uint8_t devNr)
    {
      struct hdc1080_s *myData;

      myData = (struct hdc1080_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (9999.99);
      if (myData[devNr].averagedOver < 1) return (999.99);
      return (myData[devNr].temp_average);
    }
    
    float getHumidity(uint8_t devNr)
    {
      struct hdc1080_s *myData;

      myData = (struct hdc1080_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (9999.99);
      if (myData[devNr].averagedOver < 1) return (999.99);
      return (myData[devNr].humid_average);
    }

    void printData()
    {
      struct hdc1080_s *myData;
      char msgBuffer[40];
      char stateType[2][5] = {"tsta", "hsta"};      
      
      sprintf (msgBuffer, "hdc1080.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct hdc1080_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid) {
            sprintf (msgBuffer, "hdc1080.%d.temp (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].temp_average, 1));
              consolewriteln (" 'C");
            }
            sprintf (msgBuffer, "hdc1080.%d.last (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].temp_last, 1));
            consolewriteln (" 'C");
            sprintf (msgBuffer, "hdc1080.%d.humi (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].humid_average, 1));
              consolewriteln (" %");
            }
            sprintf (msgBuffer, "hdc1080.%d.lash (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].humid_last, 1));
            consolewriteln (" %");
            sprintf (msgBuffer, "hdc1080.%d.dewp (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (util_dewpoint(myData[device].temp_average, myData[device].humid_average), 1));
              consolewriteln (" 'C");
            }
            sprintf (msgBuffer, "hdc1080.%d.lasd (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (util_dewpoint(myData[device].temp_last, myData[device].humid_last), 1));
            consolewriteln (" 'C");
            sprintf (msgBuffer, "hdc1080.%d.sos  (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (util_speedOfSound(myData[device].temp_average, myData[device].humid_average), 1));
              consolewriteln (" m/s");
            }
            sprintf (msgBuffer, "hdc1080.%d.lass (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (util_speedOfSound(myData[device].temp_last, myData[device].humid_last), 1));
            consolewriteln (" m/s");
            for (uint8_t staLoop=0 ;staLoop<2; staLoop++) {
              sprintf (msgBuffer, "hdc1080.%d.%s (%s) ", device, stateType[staLoop], myData[device].uniquename);
              consolewrite (msgBuffer);
              consolewrite (util_ftos (myData[device].state[staLoop], 0));
              consolewrite (" (");
              consolewrite ((char*) xymonColour[myData[device].state[staLoop]]);
              consolewriteln (")");
            }
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }     
    }

    float getData(uint8_t devNr, char *parameter)
    {
      struct hdc1080_s *myData;
      float retval;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct hdc1080_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (99999.99);
      if (myData[devNr].averagedOver < 1) return (9999.99);
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if (myData[devNr].averagedOver > 0) {
          if      (strcmp(parameter,"temp") == 0) retval = myData[devNr].temp_average;
          else if (strcmp(parameter,"last") == 0) retval = myData[devNr].temp_last;
          else if (strcmp(parameter,"tsta") == 0) retval = myData[devNr].state[0];
          else if (strcmp(parameter,"humi") == 0) retval = myData[devNr].humid_average;
          else if (strcmp(parameter,"lash") == 0) retval = myData[devNr].humid_last;
          else if (strcmp(parameter,"hsta") == 0) retval = myData[devNr].state[1];
          else if (strcmp(parameter,"dewp") == 0) retval = util_dewpoint(myData[devNr].temp_average, myData[devNr].humid_average);
          else if (strcmp(parameter,"lasd") == 0) retval = util_dewpoint(myData[devNr].temp_last,    myData[devNr].humid_last);
          else if (strcmp(parameter,"sos")  == 0) retval = util_speedOfSound(myData[devNr].temp_average, myData[devNr].humid_average);
          else if (strcmp(parameter,"lass") == 0) retval = util_speedOfSound(myData[devNr].temp_last,    myData[devNr].humid_last);
          else retval = 0.00;
        }
        else retval=0.00;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }
};


hdc1080 the_hdc1080;
