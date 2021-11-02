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


class pfc8583 {
  private:
    const char myDevType[9] = "pfc8583";
    uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      float xfrmResult;
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted
      uint32_t calcResult;
      uint8_t result[3];
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      struct pfc8583_s *myData;
      char msgBuffer[SENSOR_NAME_LEN];
      
      loopCount = 0;
      myDevTypeID = util_get_dev_type("pfc8583");
      if (myDevTypeID!=255) {
        devRestartable[myDevTypeID] = false;
        util_deviceTimerCreate(myDevTypeID);
        myData = (struct pfc8583_s*) (devData[myDevTypeID]);
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        if (xTimerChangePeriod(devTypeTimer[myDevTypeID], pdMS_TO_TICKS(pollInterval), pdMS_TO_TICKS(1100)) != pdPASS) {
          consolewriteln("Unable to adjust pfc8583 event counter poll timer period, keeping at 1 second");
          pollInterval = 1000;
          updateCount = 300;
          }
        queueData = myDevTypeID;
        if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval+1000)) == pdTRUE) {
          for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
            myData[device].averagedOver  = 0;
            myData[device].readingCount  = 0;
            myData[device].dist_average  = 0.0;
            myData[device].dist_last     = 0.0;
            myData[device].count_average = 0.0;
            myData[device].count_accum   = 0.0;
            myData[device].count_last    = 0.0;
            myData[device].transform     = 0.0;
          }
          xSemaphoreGive(devTypeSem[myDevTypeID]);
        }
        for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].ref_frequency > 0) {
            startReading (myData[device].bus, myData[device].addr, myData[device].triggerPin);
          }
        }
        delay (pollInterval);
        // loop forever collecting data
        while (devTypeCount[myDevTypeID]>0) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing pfc8583 timer signal");
            if (ansiTerm) displayAnsi(1);
          }
          loopCount++;
          for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
            if (myData[device].isvalid) {
              util_i2c_write (myData[device].bus, myData[device].addr, (uint8_t) 0x00, (uint8_t) 0x60);   // Lock read latches
              util_i2c_read  (myData[device].bus, myData[device].addr, 0x01, (uint8_t) 3, (uint8_t*) result);  // read
              util_i2c_write (myData[device].bus, myData[device].addr, (uint8_t) 0x00, (uint8_t) 0x20);   // Unloack read latches
              calcResult = 0;
              if (result[0]!=0xff & result[1]!=0xff && result[2]!=0xff) {
                for (int8_t n=2; n>=0; n--) {
                  calcResult = (calcResult * 100) + (((result[n] & 0xf0) >> 4) * 10) + (result[n] & 0x0f);
                }
                if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                  myData[device].count_last = calcResult;
                  if(myData[device].ref_frequency > 1) {
                    myData[device].dist_last = get_distance (myData[device].ref_frequency, calcResult, myData[device].compensationDevType, myData[device].compensationDevNr);
                    myData[device].count_accum += calcResult;
                    }
                  else myData[device].count_accum = calcResult;
                  myData[device].readingCount++;
                  xSemaphoreGive(devTypeSem[myDevTypeID]);
                }
              }
              else {
                if (ansiTerm) displayAnsi(3);
                sprintf (msgBuffer, "%02x dev %02x", myData[device].bus, myData[device].addr);
                consolewrite ("pfc8583 overflow on bus ");
                consolewriteln (msgBuffer);
                if (ansiTerm) displayAnsi(1);
              }
            }
          }
          if(loopCount >= updateCount) {  // update count = number of readings to average over
            for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
              if (myData[device].isvalid) {
                if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                  myData[device].averagedOver  = myData[device].readingCount;
                  if (myData[device].ref_frequency > 1) {
                    if (myData[device].averagedOver > 0) {
                      myData[device].count_average = myData[device].count_accum / (double)(myData[device].averagedOver);
                      myData[device].dist_average  = get_distance (myData[device].ref_frequency, myData[device].count_average, myData[device].compensationDevType, myData[device].compensationDevNr);
                    }
                    myData[device].readingCount  = 0;
                    myData[device].count_accum   = 0.0;
                  }
                  else {
                    myData[device].count_average = myData[device].count_accum;
                    if (myData[device].ref_frequency > 0) {
                      myData[device].readingCount  = 0;
                      myData[device].count_accum   = 0.0;
                    }
                  } 
                  xSemaphoreGive(devTypeSem[myDevTypeID]);
                }
                else {
                  if (ansiTerm) displayAnsi(3);
                  sprintf (msgBuffer, "%02x dev %02x", myData[device].bus, myData[device].addr);
                  consolewrite ("pfc8583 no semaphore obtained, bus: ");
                  consolewriteln (msgBuffer);
                  if (ansiTerm) displayAnsi(1);
                }
                struct rpnLogic_s *xfrm_Ptr = myData[device].xfrmLogic;
                if (xfrm_Ptr != NULL) {
                  xfrmResult = rpn_calc(xfrm_Ptr->count, xfrm_Ptr->term);
                  if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                    myData[device].transform = xfrmResult;
                    xSemaphoreGive(devTypeSem[myDevTypeID]);
                  }
                }
              }
              else {
                if (ansiTerm) displayAnsi(3);
                sprintf (msgBuffer, "%02x dev %02x", myData[device].bus, myData[device].addr);
                consolewrite ("pfc8583 invalid on bus ");
                consolewriteln (msgBuffer);
                if (ansiTerm) displayAnsi(1);
              }
            }
            loopCount = 0;
          }
          for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
            if (myData[device].isvalid && myData[device].ref_frequency > 0) {
              startReading (myData[device].bus, myData[device].addr, myData[device].triggerPin);
            }
          }
        }
      }
      if (myDevTypeID!=255) {
        util_deallocate (myDevTypeID);
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Could not determine pfc8583 type ID for update loop");
        if (ansiTerm) displayAnsi(1);
      }
      vTaskDelete( NULL );
    }

  /*
  * Clear counter registers and if appropriate send a reset pulse
  */
  static void startReading (uint8_t bus, uint8_t addr, uint8_t triggerPin)
  {
  util_i2c_write (bus, addr, (uint8_t) 0x00, (uint8_t) 0xA0);
  for (uint8_t n=1; n<4; n++)
    util_i2c_write (bus, addr, n, (uint8_t) 0x00);
  util_i2c_write (bus, addr, (uint8_t) 0x00, (uint8_t) 0x20);
  // Pulse triggerPin
  if (triggerPin >=0 && triggerPin<36) {
    digitalWrite(triggerPin, HIGH);
    delay(10);
    digitalWrite(triggerPin, LOW);
    }
  }

  // calculate distance in cm from returned data
  // base on (pulse_count / ((Osc_freq * 2) * SpeedOfSound)
  // multiply freq by 2 as the count is for a "there and back" trip
  static float get_distance (uint32_t frequency, uint32_t count, uint8_t compType, uint8_t compNumber)
  {
    float speedOfSound = 34300.00;
    if (count == 0 || frequency == 0) return (0.00);
    // Update estimated speed of sound to improve accuracy despite temperature variance
    if (compType != 0xFF && compNumber != 0xFF) {
      float tf;
      char varName[32];
      sprintf (varName, "%s.%d.sos", devType[compType], compNumber);
      tf = util_getvar(varName) * 100.00;
      if (tf < 30000.00 || tf > 40000.00) {
        sprintf (varName, "%s.%d.lass", devType[compType], compNumber);
        tf = util_getvar(varName) * 100.00;
      }
      // Use calaculated speed of sound range if it is in the vaguely plausible range.
      if (tf > 30000.00 && tf < 40000.00) speedOfSound = tf;
    }
    // formula (pulses_counted / (pulses per meter (return trip))) = fraction of distance per second
    // distance measured = (fraction of distance per second) * (speed of sound)
    return (((float)count / (float)(frequency<<1)) * speedOfSound);
  }

  public:

    char subtypeList[1][4] = { "var" };
    char subtypeLen = 1;

    pfc8583()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("pfc8583");
      devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      bool retval = false;
      const uint8_t dev_addr[] = { 0x50, 0x51 };
      struct pfc8583_s *myData;
      char devNr;
      char ruleName[SENSOR_NAME_LEN];
      char msgBuffer[BUFFSIZE];
      char sensorName[SENSOR_NAME_LEN];
      char compensationDev[10];
      uint8_t check[2];
      uint8_t tComp;
      char candidateCompenDev[][8] = { "hdc1080", "bme280" };

      if (myDevTypeID==255) return false;   // unknown type - get out of here
      // Probe for candidate devices
      for (uint8_t bus=0; bus<2; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)); device++) {
          if (util_i2c_probe(bus, dev_addr[device])) {
            devTypeCount[myDevTypeID]++;
          }
        }
      }
      if (devTypeCount[myDevTypeID] == 0) {
        // consolewriteln ((const char*) "No pfc8583 sensors found.");
        return(false);  // nothing found!
      }
      // set up and inittialise structures
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(pfc8583_s));
      myData = (struct pfc8583_s*) devData[myDevTypeID];
      devNr = 0;
      for (uint8_t bus=0; bus<2 && devNr<devTypeCount[myDevTypeID]; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)) && devNr<devTypeCount[myDevTypeID]; device++) {
          myData[devNr].bus = bus;
          myData[devNr].addr = dev_addr[device];
          myData[devNr].isvalid = false;
          myData[devNr].transform = 0.00;
          if (util_i2c_probe(bus, dev_addr[device])) {
            sprintf (msgBuffer, "%sfrq%d", devType[myDevTypeID], devNr);
            myData[devNr].ref_frequency = nvs_get_int (msgBuffer, 1000000);
            sprintf (msgBuffer, "%strg%d", devType[myDevTypeID], devNr);
            myData[devNr].triggerPin = nvs_get_int (msgBuffer, 99);
            pinMode(myData[devNr].triggerPin, OUTPUT);
            digitalWrite(myData[devNr].triggerPin, LOW);
            sprintf (sensorName, "pfc8583_%d", devNr);
            nvs_get_string (sensorName, myData[devNr].uniquename, sensorName, sizeof (myData[devNr].uniquename));
            if (devNr==0 && strcmp (myData[devNr].uniquename, sensorName) == 0) strcpy (myData[devNr].uniquename, device_name);
            myData[devNr].isvalid = true;
            myData[devNr].state = GREEN;
            retval = true;
            //
            // find temperature / humidity compensation device
            //
            nvs_get_string (sensorName, compensationDev, "void", sizeof (compensationDev));
            if (strcmp(compensationDev, "void") == 0) {
              myData[devNr].compensationDevType = 0xFF;
              myData[devNr].compensationDevNr   = 0xFF;
            }
            else {
              myData[devNr].compensationDevType = util_get_dev_type(compensationDev);
              if (myData[devNr].compensationDevType != 0xFF) {
                sprintf (sensorName, "%s_c%d_nr", myDevType, devNr);
                myData[devNr].compensationDevNr = nvs_get_int (sensorName, 255);
                if (myData[devNr].compensationDevNr >= devTypeCount[myData[devNr].compensationDevType]) myData[devNr].compensationDevNr = 0xFF;
              }
            }
            // Attempt to find a default compensation type
            if (myData[devNr].compensationDevNr == 0xFF || myData[devNr].compensationDevType == 0xFF) {
              for (int z=0 ; z<2 && myData[devNr].compensationDevNr==0xFF; z++) {
                tComp = util_get_dev_type(candidateCompenDev[z]);
                if (devTypeCount[tComp] > 0) {
                  myData[devNr].compensationDevType = tComp;
                  myData[devNr].compensationDevNr   = 0;
                }
              }
            }
            //
            // get rpn transform
            //
            sprintf (msgBuffer, "pfc8583Xfm_%d", devNr);  // Transformation Logic
            util_getLogic (msgBuffer, &myData[devNr].xfrmLogic);
            sprintf (msgBuffer, "pfc8583Alt_%d", devNr);  // Transformation Name
            nvs_get_string (msgBuffer, myData[devNr].xfrmName, "transform", sizeof (myData[devNr].xfrmName));
            //
            // Process rules
            //
            for (uint8_t level=0; level<3 ; level++) {
              sprintf (ruleName, "pfc8%s_%d%d", subtypeList[0], level, devNr);  // Warning Logic
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
            //
            // Update counts
            //
            if (myData[devNr].ref_frequency > 1) metricCount[DIST]++;
            else  metricCount[COUN]++;
            devNr++;
          }
        }
      }
      for (uint8_t n=0; n<devTypeCount[myDevTypeID]; n++) {
        if (myData[n].bus > 1) myData[n].isvalid = false;
        if (myData[n].addr != dev_addr[0] && myData[n].addr != dev_addr[1]) myData[n].isvalid = false;
      }
      if (retval) xTaskCreate(updateloop, devType[myDevTypeID], 4096, NULL, 12, NULL);
      // inventory();
      return (retval);
    }

    void inventory()
    {
      char msgBuffer[80];
      char devStatus[9];
      struct pfc8583_s *myData;

      if (devTypeCount[myDevTypeID] == 0) {
        // consolewriteln ((const char*) " * No pfc8583 sensors found.");
        return;
      }
      consolewriteln ((const char*) "Test: pfc8583 - Distance / Event counter");
      myData = (struct pfc8583_s*) devData[myDevTypeID];
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
      uint8_t targetType;         // used to find count/distance type based on frequency
      struct pfc8583_s *myData;   // Pointer to data.

      tStart = 0;
      tEnd = 3;
      myData = (struct pfc8583_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        if (myData[devNr].ref_frequency>1) targetType = DIST;
        else targetType = COUN;
        if (myData[devNr].isvalid && targetType==testType) {
          testVal = 0;
          for (uint8_t innerloop=tStart ; innerloop<tEnd; innerloop++) {
            struct rpnLogic_s *alertPtr = myData[devNr].alert[innerloop];
            if (alertPtr != NULL && rpn_calc(alertPtr->count, alertPtr->term)>0) testVal = (innerloop-tStart)+1;
          }
          if (testVal > retVal) retVal = testVal;
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
            myData[devNr].state = testVal;
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
        // else retVal = CLEAR;
      }
      return (retVal);
    }

    bool getXymonStatus (char *xydata, uint8_t testType)
    {
      struct  pfc8583_s *myData;
      float   altValue, altInch;
      char    msgBuffer[80];
      uint8_t targetType;         // used to find count/distance type based on frequency

      myData = (struct pfc8583_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].ref_frequency>1) targetType = DIST;
          else targetType = COUN;
          if (myData[device].isvalid && targetType==testType && myData[device].averagedOver > 0) {
            uint8_t currentState = myData[device].state;
            util_getLogicTextXymon (myData[device].alert[currentState-YELLOW], xydata, currentState, myData[device].uniquename);
            if (targetType == DIST) {
              sprintf (msgBuffer, " &%s %-16s %8s", xymonColour[currentState], myData[device].uniquename, util_ftos (myData[device].dist_average, 2));
              strcat  (xydata, msgBuffer);
              altValue = myData[device].dist_average / 2.54;
              if (altValue > 13.0) {
                altInch = fmod (altValue, 12.0);
                sprintf (msgBuffer, " cm %8s ft", util_ftos ((altValue - altInch)/12.0, 0));
                strcat  (xydata, msgBuffer);
                sprintf (msgBuffer, " %s in", util_ftos (altInch, 2));
              }
              else {
                sprintf (msgBuffer, " cm %8s in", util_ftos ((myData[device].dist_average / 2.54), 2));
              }
              strcat  (xydata, msgBuffer);
            }
            else {
              sprintf (msgBuffer, " &%s %-16s %8s", xymonColour[currentState], myData[device].uniquename, util_ftos (myData[device].count_average, 2));
              strcat  (xydata, msgBuffer);
            }
            if (myData[device].xfrmLogic != NULL) {
              sprintf (msgBuffer, " %8s %s\n", util_ftos (myData[device].transform, 2), myData[device].xfrmName);
              strcat  (xydata, msgBuffer);
            }
            else strcat (xydata, "\n");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("pfc8583 semaphore not released.");
      return (true);
    }


    void getXymonStats (char *xydata, uint8_t testType)
    {
      struct pfc8583_s *myData;
      char msgBuffer[40];
      uint8_t targetType;         // used to find count/distance type based on frequency
      
      // setup pointer to data array
      myData = (struct pfc8583_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].ref_frequency>1) targetType = DIST;
          else targetType = COUN;
          if (myData[device].isvalid && targetType==testType && myData[device].averagedOver > 0) {
            if (testType == DIST) {
              sprintf (msgBuffer, "[distance.%s.rrd]\n", myData[device].uniquename);
              strcat  (xydata, msgBuffer);
              strcat  (xydata, "DS:distance:GAUGE:600:U:U ");
              strcat  (xydata, util_ftos (myData[device].dist_average, 2));
              strcat  (xydata, "\n");
            }
            else {
              sprintf (msgBuffer, "[count.%s.rrd]\n", myData[device].uniquename);
              strcat  (xydata, msgBuffer);
              strcat  (xydata, "DS:count:GAUGE:600:U:U ");
              strcat  (xydata, util_ftos (myData[device].count_average, 2));
              strcat  (xydata, "\n");
            }
            if (myData[device].xfrmLogic != NULL) {
              sprintf (msgBuffer, "[%s.%s.rrd]\n", myData[device].xfrmName, myData[device].uniquename);
              strcat  (xydata, msgBuffer);
              strcat  (xydata, "DS:val:GAUGE:600:U:U ");
              strcat  (xydata, util_ftos (myData[device].transform, 2));
              strcat  (xydata, "\n");
            }
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }
    
    void printData()
    {
      struct pfc8583_s *myData;
      char msgBuffer[64];
      
      sprintf (msgBuffer, "pfc8583.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct pfc8583_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid) {
            sprintf (msgBuffer, "pfc8583.%d.freq (%s) %d Hz", device, myData[device].uniquename, myData[device].ref_frequency);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "pfc8583.%d.pin  (%s) %d", device, myData[device].uniquename, myData[device].triggerPin);
            consolewriteln (msgBuffer);
            if (myData[device].averagedOver > 0) {
              sprintf (msgBuffer, "pfc8583.%d.coun (%s) %s", device, myData[device].uniquename, util_ftos (myData[device].count_average, 0));
              consolewriteln (msgBuffer);
            }
            sprintf (msgBuffer, "pfc8583.%d.lasc (%s) %s", device, myData[device].uniquename, util_ftos (myData[device].count_last, 0));
            consolewriteln (msgBuffer);
            if (myData[device].ref_frequency > 1) {
              if (myData[device].averagedOver > 0) {
                sprintf (msgBuffer, "pfc8583.%d.dist (%s) %s cm", device, myData[device].uniquename, util_ftos (myData[device].dist_average, 2));
                consolewriteln (msgBuffer);
              }
              sprintf (msgBuffer, "pfc8583.%d.lasd (%s) %s cm", device, myData[device].uniquename, util_ftos (myData[device].dist_last, 2));
              consolewriteln (msgBuffer);
            }
            if (myData[device].xfrmLogic != NULL) {
              sprintf (msgBuffer, "pfc8583.%d.xfrm (%s) %s (%s)", device, myData[device].uniquename, util_ftos (myData[device].transform, 2), myData[device].xfrmName);
              consolewriteln (msgBuffer);
            }
            sprintf (msgBuffer, "pfc8583.%d.csta (%s) %d (%s)", device, myData[device].uniquename, myData[device].state, xymonColour[myData[device].state]);
            consolewriteln (msgBuffer);
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

    float getData(uint8_t devNr, char *parameter)
    {
      struct pfc8583_s *myData;
      float retval;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct pfc8583_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if (myData[devNr].averagedOver > 0) {
          if      (strcmp(parameter,"freq") == 0) retval = myData[devNr].ref_frequency;
          else if (strcmp(parameter,"pin")  == 0) retval = myData[devNr].triggerPin;
          else if (strcmp(parameter,"coun") == 0) retval = myData[devNr].count_average;
          else if (strcmp(parameter,"lasc") == 0) retval = myData[devNr].count_last;
          else if (strcmp(parameter,"dist") == 0) retval = myData[devNr].dist_average;
          else if (strcmp(parameter,"lasd") == 0) retval = myData[devNr].dist_last;
          else if (strcmp(parameter,"csta") == 0) retval = myData[devNr].state;
          else if (strcmp(parameter,"xfrm") == 0) retval = myData[devNr].transform;
          else retval = 0.00;
        }
        else retval=0.00;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }
};



pfc8583 the_pfc8583;
