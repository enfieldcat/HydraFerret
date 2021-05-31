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


class ina2xx {
  private:
    const char myDevType[9] = "ina2xx";
    //const uint16_t sensorMode;
    uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      struct ina2xx_s *myData;
      float volts, amps;
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted
      int16_t intVolt;
      uint8_t readVal[2];
      uint8_t writeVal[4];
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      uint8_t shift;       // number of times to right shift data
      char msgBuffer[20];
      bool extendSign = false ; // extend sign when shifting data
      
      loopCount = 0;
      myDevTypeID = util_get_dev_type("ina2xx");
      if (myDevTypeID!=255) {
        util_deviceTimerCreate(myDevTypeID);
        myData = (struct ina2xx_s*) (devData[myDevTypeID]);
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        queueData = myDevTypeID;
        writeVal[0] = 0x00;  // Reset device
        writeVal[1] = 0xcf;
        writeVal[2] = 0xff;
        for (uint8_t index=0; index<devTypeCount[myDevTypeID]; index++) {
          if (myData[index].channel == 0) util_i2c_write (myData[index].bus, myData[index].addr, (int) 3, writeVal);
        }
        delay (100);
        writeVal[0] = 0x00;  // Use longest integration time and averaging if possible, config reg is reg 0x00
        for (uint8_t index=0; index<devTypeCount[myDevTypeID]; index++) if (myData[index].channel == 0) {
          if (strcmp("ina226", inaName[myData[index].modelNr]) == 0) {
            writeVal[1] = 0x4F;  // 1024 averages, with continuous bus and shunt voltage measurement
            if (pollInterval < 2) {
              writeVal[2] = 0x27;  // 1.1 ms integration time
            }
            else if (pollInterval < 4) {
              writeVal[2] = 0x4f;  // 2.116 ms integration time
            }
            else if (pollInterval < 8) {
              writeVal[2] = 0xb7;  // 4.156 ms integration time
            }
            else {
              writeVal[2] = 0xff;  // 8.244 ms integration time
            }
          }
          else if (strcmp("ina3221", inaName[myData[index].modelNr]) == 0) {
            if (pollInterval < 2) {
              writeVal[1] = 0x79;
            }
            else if (pollInterval < 4) {
              writeVal[1] = 0x7b;
            }
            else if (pollInterval < 8) {
              writeVal[1] = 0x7d;
            }
            else {
              writeVal[1] = 0x7f;
            }
            writeVal[2] = 0xff;
          }
          else { // assume ina219, stay with defaults so it can be re-detected on reboot as it does not hold a signature field
            writeVal[1] = 0x39;
            writeVal[2] = 0x9f;
            }
          util_i2c_write (myData[index].bus, myData[index].addr, (int) 3, writeVal);
        }
        if (pollInterval<8) delay (500+(pollInterval*1000));
        else delay (8500);
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        if (xTimerChangePeriod(devTypeTimer[myDevTypeID], pdMS_TO_TICKS(pollInterval), pdMS_TO_TICKS(1100)) != pdPASS) {
          consolewriteln("Unable to adjust current sense poll timer period, keep at 1 second");
          pollInterval = 1000;
          updateCount = 300;
          }
        // loop forever collecting data
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing ina2xx signal");
            if (ansiTerm) displayAnsi(1);
          }
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval+1000)) == pdTRUE) {
            for (uint8_t index=0; index<devTypeCount[myDevTypeID]; index++) {
              // read shunt voltage
              util_i2c_command (myData[index].bus, myData[index].addr, (uint8_t) (0x01 + (myData[index].channel * 2)));
              util_i2c_read    (myData[index].bus, myData[index].addr, (uint8_t) 2, readVal);
              intVolt = util_transInt(readVal[0], readVal[1]);
              shift = inaShuntShift[myData[index].modelNr];
              if (shift > 0) {
                if ((intVolt & 0x8000) > 0) extendSign = true;
                else extendSign = false;
                intVolt = intVolt >> shift;
                if (extendSign) intVolt | 0xe000;
              }
              volts = intVolt * inaShuntLSB[myData[index].modelNr];
              if (myData[index].resistor != 0.00) {
                amps  = (volts / myData[index].resistor);  // apply ohms law as simpler than reading and calculating register
                myData[index].shunt_last   = volts;
                myData[index].amps_last    = amps;
                myData[index].shunt_accum += volts;
                myData[index].amps_accum  += amps;
                // read bus voltage
                util_i2c_command (myData[index].bus, myData[index].addr, (uint8_t) (0x02 + (myData[index].channel * 2)));
                util_i2c_read    (myData[index].bus, myData[index].addr, (uint8_t) 2, readVal);
                intVolt = util_transInt(readVal[0], readVal[1]);
                shift = inaBusShift[myData[index].modelNr];
                if (shift > 0) {
                  if ((intVolt & 0x8000) > 0) extendSign = true;
                  else extendSign = false;
                  intVolt = intVolt >> shift;
                  if (extendSign) intVolt | 0xe000;
                }
                volts = intVolt * inaBusLSB[myData[index].modelNr];
                myData[index].volt_last   = volts;
                myData[index].watt_last   = volts * amps;
                myData[index].volt_accum += volts;
                myData[index].watt_accum += volts * amps;
                myData[index].readingCount++;
              }
            }
            if (++loopCount >= updateCount) {
              loopCount = 0;
              for (uint8_t index=0; index<devTypeCount[myDevTypeID]; index++) {
                myData[index].averagedOver = myData[index].readingCount;
                myData[index].volt_average = (myData[index].volt_accum  / myData[index].readingCount);
                myData[index].amps_average = (myData[index].amps_accum  / myData[index].readingCount);
                myData[index].watt_average = (myData[index].watt_accum  / myData[index].readingCount);
                myData[index].shunt_average= (myData[index].shunt_accum / myData[index].readingCount);
                myData[index].readingCount = 0;
                myData[index].volt_accum = 0.0;
                myData[index].amps_accum = 0.0;
                myData[index].watt_accum = 0.0;
                myData[index].shunt_accum= 0.0;
              } 
            }
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
      }
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Could not determine ina2xx type ID for update loop");
      if (ansiTerm) displayAnsi(1);
      vTaskDelete( NULL );
    }

  public:

    char subtypeList[3][5] = { "volt", "amps", "watt" };
    char subtypeLen = 3;

    ina2xx()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("ina2xx");
      devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      bool retval = false;
      struct ina2xx_s *myData;
      char devNr;
      char sensorName[SENSOR_NAME_LEN];
      char ruleName[SENSOR_NAME_LEN];
      char msgBuffer[BUFFSIZE];
      uint8_t check[2];

      if (myDevTypeID==255) return false;   // unknown type - get out of here
      // Probe for candidate devices and count them
      for (uint8_t bus=0; bus<2; bus++) if (I2C_enabled[bus]) {
        for (uint8_t device=0x40; device <= 0x4f; device++) {
          if (util_i2c_probe(bus, device)) {
            for (uint8_t supportedModel = 0; supportedModel<sizeof(inaSigAddr); supportedModel++) {
              if (inaMaxAddr[supportedModel]>=device) {
                util_i2c_command (bus, device, (uint8_t) inaSigAddr[supportedModel]);
                util_i2c_read    (bus, device, (uint8_t) 2, check);
                if (check[0]==inaSignature[supportedModel][0] && check[1]==inaSignature[supportedModel][1]) {
                  devTypeCount[myDevTypeID] += inaInputCnt[supportedModel]; // verify device type ID 2280 => ina226 die id           
                }
              }
            }
          }
        }
      }
      if (devTypeCount[myDevTypeID] == 0) {
        consolewriteln ((const char*) "No ina2xx sensors found.");
        return(false);  // nothing found!
      }
      // set up and inittialise structures
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(ina2xx_s));
      myData = (struct ina2xx_s*) devData[myDevTypeID];
      devNr = 0;
      for (uint8_t bus=0; bus<2 && devNr<devTypeCount[myDevTypeID]; bus++) if (I2C_enabled[bus]) {
        for (uint8_t device=0x40 && devNr<devTypeCount[myDevTypeID]; device <= 0x4f; device++) {
          if (util_i2c_probe(bus, device)) {
            myData[devNr].bus = bus;
            myData[devNr].addr = device;
            myData[devNr].isvalid = false;
            for (uint8_t supportedModel = 0; supportedModel<sizeof(inaSigAddr) && !myData[devNr].isvalid; supportedModel++) {
              if (inaMaxAddr[supportedModel]>=device) {
                util_i2c_command (bus, device, (uint8_t) inaSigAddr[supportedModel]);
                util_i2c_read    (bus, device, (uint8_t) 2, check);
                if (check[0]==inaSignature[supportedModel][0] && check[1]==inaSignature[supportedModel][1]) { // verify device type ID
                  for (uint8_t channel = 0; channel < inaInputCnt[supportedModel]; channel++) {
                    sprintf (sensorName, "ina2xx_%d", devNr);
                    nvs_get_string (sensorName, myData[devNr].uniquename, sensorName, sizeof (myData[devNr].uniquename));
                    if (devNr==0 && strcmp (myData[devNr].uniquename, sensorName) == 0) strcpy (myData[devNr].uniquename, device_name);
                    sprintf (sensorName, "ina2xxRes_%d", devNr);
                    myData[devNr].resistor = nvs_get_int (sensorName, DEFAULT_RESISTOR);
                    retval = true;
                    if (channel >0) {
                      myData[devNr].bus = bus;
                      myData[devNr].addr = device;
                    }
                    myData[devNr].modelNr      = supportedModel;
                    myData[devNr].volt_accum   = 0.0;
                    myData[devNr].volt_average = 0.0;
                    myData[devNr].volt_last    = 0.0;
                    myData[devNr].shunt_accum  = 0.0;
                    myData[devNr].shunt_average= 0.0;
                    myData[devNr].shunt_last   = 0.0;
                    myData[devNr].amps_accum   = 0.0;
                    myData[devNr].amps_average = 0.0;
                    myData[devNr].amps_last    = 0.0;
                    myData[devNr].watt_accum   = 0.0;
                    myData[devNr].watt_average = 0.0;
                    myData[devNr].watt_last    = 0.0;
                    myData[devNr].readingCount = 0;
                    myData[devNr].averagedOver = 0;
                    myData[devNr].channel      = channel;
                    myData[devNr].isvalid = true;
                    for (uint8_t sense=0; sense<subtypeLen; sense++) {
                      myData[devNr].state[sense] = GREEN;
                      for (uint8_t level=0; level<3 ; level++) {
                        sprintf (ruleName, "ina2%s_%d%d", subtypeList[sense], level, devNr);  // Warning Logic
                        nvs_get_string (ruleName, msgBuffer, "disable", sizeof(msgBuffer));
                        if (strcmp (msgBuffer, "disable") != 0) {
                          consolewrite ("Enable ");
                          consolewrite ((char*) alertLabel[level]);
                          consolewrite (": ");
                          consolewrite (ruleName);
                          consolewrite (" as ");
                          consolewriteln (msgBuffer);
                          util_getLogic (ruleName, &myData[devNr].alert[level+(sense*3)]);
                        }
                        else myData[devNr].alert[level+(sense*3)] = NULL;
                      }
                    }
                    metricCount[VOLT]++;
                    metricCount[AMP]++;
                    metricCount[WATT]++;
                    devNr++;
                  }
                }
              }
            }
          }
        }
      }
      if (retval) xTaskCreate(updateloop, devType[myDevTypeID], 4096, NULL, 12, NULL);
      // inventory();
      return (retval);
    }

    void displayResistor()
    {
      char msgBuffer[40];
      struct ina2xx_s *myData;
      
      myData = (struct ina2xx_s*) devData[myDevTypeID];
      for (char n=0; n<devTypeCount[myDevTypeID]; n++) {
        sprintf (msgBuffer, "   - %s.%d (%s) %s", myDevType, n, myData[n].uniquename, util_ftos(myData[n].resistor, 2));
        consolewriteln (msgBuffer);
      }
    }

    void setResistor (int deviceNr, float resistor)
    {
      struct ina2xx_s *myData;
      char msgBuffer[20];

      sprintf (msgBuffer, "ina2xx_ohm_%d", deviceNr);
      myData = (struct ina2xx_s*) devData[myDevTypeID];
      if (deviceNr<devTypeCount[myDevTypeID]) myData[deviceNr].resistor = resistor;
      nvs_put_float (msgBuffer, resistor);
    }

    void inventory()
    {
      char msgBuffer[100];
      char devStatus[9];
      struct ina2xx_s *myData;

      consolewriteln ((const char*) "Test: ina2xx - volts, amps and power");
      if (devTypeCount[myDevTypeID] == 0) {
        consolewriteln ((const char*) " * No ina2xx sensors found.");
        return;
      }
      myData = (struct ina2xx_s*) devData[myDevTypeID];
      for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
        if (myData[device].isvalid && myData[device].modelNr < sizeof(inaSigAddr)) {
          strcpy(devStatus, "OK");
          sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x, name: %s, resistor: %d milliohm", devStatus, inaName[myData[device].modelNr], device, myData[device].bus, myData[device].addr, myData[device].uniquename, myData[device].resistor);
        }
        else {
          strcpy(devStatus, "REJECTED");
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
      struct ina2xx_s *myData;    // Pointer to data.

      switch (testType) {
        case VOLT:  tStart = 0; tEnd = 3; idx = 0; break;
        case AMP:   tStart = 3; tEnd = 6; idx = 1; break;
        case WATT:  tStart = 6; tEnd = 9; idx = 2; break;
        default:    tStart = 0; tEnd = 0; idx = 0;
      }
      myData = (struct ina2xx_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        if (myData[devNr].isvalid) {
          testVal = 0;
          for (uint8_t innerloop=tStart ; innerloop<tEnd; innerloop++) {
            struct rpnLogic_s *alertPtr = myData[devNr].alert[innerloop];
            if (alertPtr != NULL && rpn_calc(alertPtr->count, alertPtr->term)>0) testVal = (innerloop-tStart)+1;
          }
          if (testVal>retVal) retVal = testVal;
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
      struct ina2xx_s *myData;
      char msgBuffer[80];
      uint8_t indx;

      // NB: try to get data to nearest milliamp / millivolt if possible
      myData = (struct ina2xx_s*) devData[myDevTypeID];
      switch (measureType) {
        case VOLT:
          indx = 0;
          break;
        case AMP:
          indx = 1;
          break;
        default:
          indx = 2;
          break;
      }
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            uint8_t currentState = myData[device].state[indx];
            util_getLogicTextXymon (myData[device].alert[(currentState-YELLOW)+(indx*3)], xydata, currentState, myData[device].uniquename);
            sprintf (msgBuffer, " &%s ", xymonColour[currentState]);
            strcat  (xydata, msgBuffer);
            if (measureType == VOLT) {
              sprintf (msgBuffer, "%-16s %8s V\n", myData[device].uniquename, util_ftos (myData[device].volt_average, 3));
              strcat  (xydata, msgBuffer);
            }
            else if (measureType == AMP) {
              sprintf (msgBuffer, "%-16s %8s A\n", myData[device].uniquename, util_ftos (myData[device].amps_average, 3));
              strcat  (xydata, msgBuffer);
            }
            else if (measureType == WATT) {
              sprintf (msgBuffer, "%-16s %8s W\n", myData[device].uniquename, util_ftos (myData[device].watt_average, 3));
              strcat  (xydata, msgBuffer);
            }
          }
          else {
            sprintf (msgBuffer, " &red Invalid or faulty ina2xx found, bus %d, address %d\n", myData[device].bus, myData[device].addr);
            strcat  (xydata, msgBuffer);
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("ina2xx semaphore not released.");
      return (true);
    }


    void getXymonStats (char *xydata)
    {
      struct ina2xx_s *myData;
      char msgBuffer[40];
      int dp;
      double tDouble;
      
      // setup pointer to data array
      myData = (struct ina2xx_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            sprintf (msgBuffer, "[volts.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, "DS:volts:GAUGE:600:U:U ");
            strcat  (xydata, util_ftos (myData[device].volt_average, 3));
            strcat  (xydata, "\n");
            sprintf (msgBuffer, "[amps.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, "DS:amps:GAUGE:600:U:U ");
            strcat  (xydata, util_ftos (myData[device].amps_average, 3));
            strcat  (xydata, "\n");
            sprintf (msgBuffer, "[watts.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, "DS:watts:GAUGE:600:U:U ");
            strcat  (xydata, util_ftos (myData[device].watt_average, 3));
            strcat  (xydata, "\n");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

    void printData()
    {
      struct ina2xx_s *myData;
      char msgBuffer[40];
      
      sprintf (msgBuffer, "ina2xx.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct ina2xx_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid) {
            sprintf (msgBuffer, "ina2xx.%d.volt (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].volt_average, 3));
              consolewriteln (" V");
            }
            sprintf (msgBuffer, "ina2xx.%d.lasv (%s) %s V", device, myData[device].uniquename, util_ftos (myData[device].volt_last, 3));
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "ina2xx.%d.shun (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].shunt_average, 3));
              consolewriteln (" mV");
            }
            sprintf (msgBuffer, "ina2xx.%d.lass (%s) %s mV", device, myData[device].uniquename, util_ftos (myData[device].shunt_last, 3));
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "ina2xx.%d.amps (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].amps_average, 3));
              consolewriteln (" A");
            }
            sprintf (msgBuffer, "ina2xx.%d.lasa (%s) %s A", device, myData[device].uniquename, util_ftos (myData[device].amps_last, 3));
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "ina2xx.%d.watt (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].watt_average, 3));
              consolewriteln (" W");
            }
            sprintf (msgBuffer, "ina2xx.%d.lasw (%s) %s W", device, myData[device].uniquename, util_ftos (myData[device].watt_last, 3));
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "ina2xx.%d.resi (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].resistor, 1));
            consolewriteln (" milliohm (shunt)");
            sprintf (msgBuffer, "ina2xx.%d.blsb (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (inaBusLSB[myData[device].modelNr], 6));
            consolewriteln (" volts per bit (bus)");
            sprintf (msgBuffer, "ina2xx.%d.slsb (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (inaShuntLSB[myData[device].modelNr], 6));
            consolewriteln (" millivolts per bit (shunt)");
            sprintf (msgBuffer, "ina2xx.%d.fsd  (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (((inaMaxShuntBits[myData[device].modelNr] * inaShuntLSB[myData[device].modelNr]) / myData[device].resistor), 3));
            consolewriteln (" +- Amps Max");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }     
    }

    float getData(uint8_t devNr, char *parameter)
    {
      struct ina2xx_s *myData;
      float retval;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct ina2xx_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (99999.99);
      if (myData[devNr].averagedOver < 1) return (9999.99);
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if (myData[devNr].averagedOver > 0) {
          if      (strcmp(parameter,"volt") == 0) retval = myData[devNr].volt_average;
          else if (strcmp(parameter,"lasv") == 0) retval = myData[devNr].volt_last;
          else if (strcmp(parameter,"amps") == 0) retval = myData[devNr].amps_average;
          else if (strcmp(parameter,"lasa") == 0) retval = myData[devNr].amps_last;
          else if (strcmp(parameter,"watt") == 0) retval = myData[devNr].watt_average;
          else if (strcmp(parameter,"lasw") == 0) retval = myData[devNr].watt_last;
          else if (strcmp(parameter,"shun") == 0) retval = myData[devNr].shunt_average;
          else if (strcmp(parameter,"lass") == 0) retval = myData[devNr].shunt_last;
          else if (strcmp(parameter,"resi") == 0) retval = myData[devNr].resistor;
          else if (strcmp(parameter,"mode") == 0) retval = myData[devNr].modelNr;
          else if (strcmp(parameter,"blsb") == 0) retval = inaBusLSB[myData[devNr].modelNr];
          else if (strcmp(parameter,"slsb") == 0) retval = inaShuntLSB[myData[devNr].modelNr];
          else if (strcmp(parameter,"fsd")  == 0) retval = ((inaMaxShuntBits[myData[devNr].modelNr] * inaShuntLSB[myData[devNr].modelNr]) / myData[devNr].resistor);
          else retval = 0.00;
        }
        else retval=0.00;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

};


ina2xx the_ina2xx;
