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


class css811 {
  private:
    const char myDevType[9] = "css811";
    uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      float xfrmResult;
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      uint8_t statusVal;
      uint8_t dataBuffer[8];
      uint8_t commandStr[2];
      uint8_t measurementMode = 0x00;
      uint8_t errorcount = 0;
      uint16_t input_co2, input_tvoc;
      uint16_t modeSetCount = 300;
      struct css811_s *myData;
      char msgBuffer[16];
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted

      loopCount = 0;
      myDevTypeID = util_get_dev_type("css811");
      if (myDevTypeID!=255) {
        devRestartable[myDevTypeID] = false;
        // set up polling intervals
        util_deviceTimerCreate(myDevTypeID);
        myData = (struct css811_s*) (devData[myDevTypeID]);
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        if (pollInterval < 10) measurementMode = 0x10;      // once per second
        else if (pollInterval < 60) measurementMode = 0x20; // once every 10 seconds
        else measurementMode = 0x30;                        // once a minute
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        if (xTimerChangePeriod(devTypeTimer[myDevTypeID], pdMS_TO_TICKS(pollInterval), pdMS_TO_TICKS(1100)) != pdPASS) {
          consolewriteln("Unable to adjust css811 CO2 poll timer period, keep at 1 second");
          pollInterval = 1000;
          updateCount = 300;
          }
        queueData = myDevTypeID;
        // clear counters and reset sensor
        if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
          for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
            myData[device].averagedOver = 0;
            myData[device].readingCount = 0;
            myData[device].errorCode    = 0;
            myData[device].co2_accum    = 0.0;
            myData[device].co2_average  = 0.0;
            myData[device].tvoc_accum   = 0.0;
            myData[device].tvoc_average = 0.0;
            myData[device].co2_last     = 0.0;
            myData[device].tvoc_last    = 0.0;
            myData[device].transform    = 0.0;
            myData[device].lastMode     = 99;
            resetCss811 (myData[device].bus, myData[device].addr);
          }
          xSemaphoreGive(devTypeSem[myDevTypeID]);
        }
        else consolewriteln ("css811 semaphore not acquired");
        setCss811Mode (myDevTypeID, measurementMode);
        for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
          compensate (myData, device);
        }
        delay (1000);  // delay before attempting to read any data
        consolewriteln ("Starting css811 measurement");
        while (devTypeCount[myDevTypeID]>0) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing css811 signal");
            if (ansiTerm) displayAnsi(1);
          }
          loopCount++;
          for (uint8_t device=0; device <devTypeCount[myDevTypeID]; device++) {
            if (myData[device].isvalid) {  // device is valid
              // setCss811Mode (myDevTypeID, measurementMode);
              if ((get_css811_status (myData[device].bus, myData[device].addr) & 0x88) == 0x88) { // check app running and data is available
                commandStr[0] = 0x02;
                util_i2c_write (myData[device].bus, myData[device].addr, 0x01, (uint8_t*) commandStr);
                util_i2c_read (myData[device].bus, myData[device].addr, (uint8_t) 0x08, (uint8_t*) dataBuffer);
                if ((dataBuffer[4] & 0x90) == 0x90 && (dataBuffer[4] & 0x01) != 0x01) {
                  input_co2  = (dataBuffer[0]<<8) + dataBuffer[1];
                  input_tvoc = (dataBuffer[2]<<8) + dataBuffer[3];
                  if (input_co2 >= 400 && input_co2 <=8192 && input_tvoc <= 1187) { // Sensible range check, outside this => and error
                    if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                      myData[device].co2_last    = input_co2;
                      myData[device].tvoc_last   = input_tvoc;
                      myData[device].co2_accum  += input_co2;
                      myData[device].tvoc_accum += input_tvoc;
                      myData[device].readingCount++;
                      myData[device].errorCode = 0;
                      xSemaphoreGive(devTypeSem[myDevTypeID]);
                      errorcount = 0;
                    }
                    else consolewriteln ("css811 semaphore not acquired");
                  }
                }
                else if ((dataBuffer[4] & 0x01) == 0x01) {
                  consolewrite ("Error reading css811 data: ");
                  myData[device].errorCode = dataBuffer[5];
                  if (dataBuffer[5] != 0) {
                    displayError (dataBuffer[5], device);
                    if (++errorcount == 60) {
                      if (ansiTerm) displayAnsi(4);
                      consolewriteln ("Attempting CSS811 reset");
                      resetCss811 (myData[device].bus, myData[device].addr);
                      setCss811Mode (myDevTypeID, measurementMode);
                      if (ansiTerm) displayAnsi(1);
                      errorcount = 0;
                    }
                  }
                }
              }
            }
          }
          if (loopCount >= updateCount) {
            for (int device=0; device <devTypeCount[myDevTypeID]; device++) if (myData[device].isvalid) {
              if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                myData[device].averagedOver = myData[device].readingCount;
                if (myData[device].averagedOver > 0) {
                  myData[device].co2_average  = myData[device].co2_accum  / myData[device].averagedOver;
                  myData[device].tvoc_average = myData[device].tvoc_accum / myData[device].averagedOver;
                }
                else {
                  myData[device].co2_average  = 0.00;
                  myData[device].tvoc_average = 0.00;
                }
                myData[device].readingCount = 0;
                myData[device].co2_accum    = 0.0;
                myData[device].tvoc_accum   = 0.0;
                xSemaphoreGive(devTypeSem[myDevTypeID]);
              }
              struct rpnLogic_s *xfrm_Ptr = myData[device].xfrmLogic;
              if (xfrm_Ptr != NULL) {
                xfrmResult = rpn_calc(xfrm_Ptr->count, xfrm_Ptr->term);
                if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                  myData[device].transform = xfrmResult;
                  xSemaphoreGive(devTypeSem[myDevTypeID]);
                }
              }
              if (++modeSetCount >= 24) { // keep set every 2 hours
                modeSetCount = 0;
                setCss811Mode (myDevTypeID, measurementMode);
              }
              compensate (myData, device);
            }
            loopCount = 0;
          }
        }
      }
      if (myDevTypeID!=255) {
        util_deallocate (myDevTypeID);
      }
      vTaskDelete( NULL );
    }


  static uint8_t get_css811_status(uint8_t bus, uint8_t addr)
    {
      uint8_t results;
      util_i2c_read (bus, addr, (uint8_t) 0x00, (uint8_t) 1, &results);
      return (results);
    }

  static void resetCss811(uint8_t bus, uint8_t addr)
  {
    uint8_t sw_reset_seq[] = { 0xff, 0x11, 0xe5, 0x72, 0x8a };  // Write to register 0xff
    uint8_t statusVal;
    char msgBuffer[13];
    
    statusVal = get_css811_status (bus, addr);
    if ((statusVal & 0x40) == 0x40) {   // firmware loaded
      if ((statusVal & 0x80) == 0x80) { //app is running, so send reset sequence to bring back to boot mode
        sprintf (msgBuffer, "%d, addr: %02x", bus, addr);
        consolewrite   ("css811 bus: ");
        consolewrite   (msgBuffer);
        consolewriteln (" - Sent reset sequence");
        util_i2c_write (bus, addr, (uint8_t) sizeof(sw_reset_seq), (uint8_t*) sw_reset_seq); // software reset
        delay (100);
      }
    }
    else {
      sprintf (msgBuffer, "%d, addr: %02x", bus, addr);
      consolewrite   ("css811 bus: ");
      consolewrite   (msgBuffer);
      consolewriteln (" - No firmware loaded");
    }
    sprintf (msgBuffer, "%d, addr: %02x", bus, addr);
    consolewrite   ("css811 bus: ");
    consolewrite   (msgBuffer);
    consolewriteln (" - App startup");
    util_i2c_command (bus, addr, (uint8_t) 0xF4);  // Start app
    delay (100);
  }

  static void setCss811Mode (uint8_t myDevTypeID, uint8_t desiredMode)
  {
    struct css811_s *myData;
    uint8_t statusVal;
    uint8_t commandStr[2];
    char msgBuffer[13];
    bool isDropDown = false;

    myData = (struct css811_s*) (devData[myDevTypeID]);
    for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
      if (desiredMode > myData[device].lastMode) {
        isDropDown = true;
      }
      myData[device].lastMode = desiredMode;
    }
    for (int device=0; device <devTypeCount[myDevTypeID]; device++) {
      statusVal = get_css811_status (myData[device].bus, myData[device].addr);
      if ((statusVal & 0x80) == 0x80) { //app is running
        if (isDropDown) {
          commandStr[0] = 0x01; commandStr[1] = 0x00;
          util_i2c_write (myData[device].bus, myData[device].addr, (uint8_t) sizeof(commandStr), (uint8_t*) commandStr);
          sprintf (msgBuffer, "%d, addr: %02x", myData[device].bus, myData[device].addr);
          consolewrite   ("css811 bus: ");
          consolewrite   (msgBuffer);
          consolewriteln (" - Wait 10 minutes for initialisation to restart");
        }
      }
      else {
        sprintf (msgBuffer, "%d, addr: %02x", myData[device].bus, myData[device].addr);
        consolewrite   ("css811 bus: ");
        consolewrite   (msgBuffer);
        consolewriteln (" - App restarted");
        resetCss811(myData[device].bus, myData[device].addr);
        delay (100);
        commandStr[0] = 0x01; commandStr[1] = 0x00;
        util_i2c_write (myData[device].bus, myData[device].addr, (uint8_t) sizeof(commandStr), (uint8_t*) commandStr);
        isDropDown = true;   // reset device, very odd, follow drop down rules
      }
    }
    if (isDropDown) delay (600000);   // 10 minute idle before running, forces drop down delay if measuring less frequently
    // Start running in desired mode
    // sprintf (msgBuffer, "mode=0x%02x", measurementMode);
    // consolewrite ("Starting CSS811 measurement ");
    // consolewriteln (msgBuffer);
    for (uint8_t device=0; device <devTypeCount[myDevTypeID]; device++) {
      commandStr[0] = 0x01; commandStr[1] = desiredMode;
      util_i2c_write (myData[device].bus, myData[device].addr, (uint8_t) sizeof(commandStr), (uint8_t*) commandStr);
      statusVal = util_i2c_read (myData[device].bus, myData[device].addr, (uint8_t) 0x00) & 0x01;
      if (statusVal == 0x01) {   // 0x80 = In application mode, 0x10 = firmware loaded, 0x08 = data ready, 0x01 = Error
        consolewriteln ("Error putting css811 to running mode: ");
        statusVal = util_i2c_read (myData[device].bus, myData[device].addr, (uint8_t) 0xE0);
        myData[device].errorCode = statusVal;
        if (statusVal != 0) {
          displayError (statusVal, device);
          consolewriteln ("Attempting css811 reset");
          resetCss811 (myData[device].bus, myData[device].addr);
        }
      }
    }
  }

  static void displayError (uint8_t errorCode, uint8_t device)
  {
    char msgBuffer[8];
    
    if (errorCode == 0) return;
    sprintf (msgBuffer, ".%d", device);
    consolewrite ("css811");
    consolewrite (msgBuffer);
    if ((errorCode & 0x01) == 0x01) consolewrite (" Invalid data written,");
    if ((errorCode & 0x02) == 0x02) consolewrite (" Invalid data requested,");
    if ((errorCode & 0x04) == 0x04) consolewrite (" Unsupported measurement mode,");
    if ((errorCode & 0x08) == 0x08) consolewrite (" Value exceeds max range,");
    if ((errorCode & 0x10) == 0x10) consolewrite (" Heater current not in range,");
    if ((errorCode & 0x20) == 0x20) consolewrite (" Incorrect heater voltage,");
    //else consolewriteln ("Unknown error");
    consolewriteln ("");
  }

  static void compensate (void *inData, uint8_t nr)
  {
    struct css811_s *myData = (struct css811_s*) inData;
    float temperature = 999.9;
    float humidity    = 999.9;
    static float lastTemp    = 0.0;
    static float lastHumid   = 0.0;
    bool changed;
    uint16_t raw_temp;
    uint16_t raw_humid;
    uint8_t packet[5];
    uint8_t statusVal;
    uint8_t compensationDevType = myData[nr].compensationDevType;
    uint8_t compensationDevNr   = myData[nr].compensationDevNr;
    // char msgBuffer[80];

    if (myData[nr].compensationDevType != 0xFF && myData[nr].compensationDevNr != 0xFF) {
      if (xSemaphoreTake(devTypeSem[compensationDevType], pdMS_TO_TICKS(10000)) == pdTRUE) {
        if (strcmp (devType[compensationDevType], "hdc1080") == 0) {
          temperature = ((struct hdc1080_s*) devData[compensationDevType])[compensationDevNr].temp_average;
          humidity    = ((struct hdc1080_s*) devData[compensationDevType])[compensationDevNr].humid_average;
        }
        else if (strcmp (devType[compensationDevType], "bme280") == 0) {
          temperature = ((struct bme280_s*) devData[compensationDevType])[compensationDevNr].temp_average;
          humidity    = ((struct bme280_s*) devData[compensationDevType])[compensationDevNr].humid_average;
        }
        xSemaphoreGive(devTypeSem[compensationDevType]);
        changed = false;
        if (temperature != lastTemp) {
          lastTemp = temperature;
          changed = true;
        }
        if (humidity != lastHumid) {
          lastHumid = humidity;
          changed = true;
        }
        if (changed) {
          raw_temp = (25 + temperature) * 512;
          raw_humid= humidity * 512;
          packet[0] = 0x05;
          packet[1] = (raw_humid >> 8) & 0xFF;
          packet[2] = raw_humid & 0xFF;
          packet[3] = (raw_temp >> 8)  & 0xFF;
          packet[4] = raw_temp  & 0xFF;
          // sprintf (msgBuffer, "CSS811 Adj: T=%d, H=%d, P=%02x %02x %02x %02x %02x", raw_temp, raw_humid, packet[0], packet[1], packet[2], packet[3], packet[4]);
          // consolewriteln (msgBuffer);
          util_i2c_write (myData[nr].bus, myData[nr].addr, (uint8_t) sizeof(packet), (uint8_t*) packet);
          statusVal = util_i2c_read (myData[nr].bus, myData[nr].addr, (uint8_t) 0x00) & 0x01;
          if (statusVal == 1) {
            consolewriteln ("Error temp/humidity compensating css811");
            statusVal = util_i2c_read (myData[nr].bus, myData[nr].addr, (uint8_t) 0xE0) & 0x01;
            displayError (statusVal, nr);
          }
        }
      }
    }
  }

  public:

    char subtypeList[2][5] = { "co2", "tvoc" };
    char subtypeLen = 2;

    css811()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("css811");
      devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      bool retval = false;
      const uint8_t dev_addr[] = { 0x5a, 0x5b };
      struct css811_s *myData;
      char devNr;
      char sensorName[SENSOR_NAME_LEN];
      char ruleName[SENSOR_NAME_LEN];
      char msgBuffer[BUFFSIZE];
      char compensationDev[10];
      uint8_t devHWID;
      uint8_t tComp;
      char candidateCompenDev[][8] = { "hdc1080", "bme280" };

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
        // consolewriteln ((const char*) "No css811 sensors found.");
        return(false);  // nothing found!
      }
      // set up and inittialise structures
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(css811_s));
      myData = (struct css811_s*) devData[myDevTypeID];
      devNr = 0;
      for (uint8_t bus=0; bus<2 && devNr<devTypeCount[myDevTypeID]; bus++) if (I2C_enabled[bus]) {
        for (uint8_t device=0; device < (sizeof(dev_addr)/sizeof(char)) && devNr<devTypeCount[myDevTypeID]; device++) {
          myData[devNr].bus = bus;
          myData[devNr].addr = dev_addr[device];
          myData[devNr].isvalid = false;
          if (util_i2c_probe(bus, dev_addr[device])) {
            devHWID = util_i2c_read (bus, dev_addr[device], 0x20);
            if (devHWID == 0x81) {
              for (uint8_t innerLoop=0; innerLoop<2; innerLoop++) myData[devNr].state[innerLoop] = GREEN;
              // util_i2c_write (bus, dev_addr[device], (uint8_t) 0x01, (uint8_t) 0x00); // Set initial mode to sleep
              myData[devNr].isvalid = true;
              sprintf (sensorName, "%s_%d", myDevType, devNr);
              nvs_get_string (sensorName, myData[devNr].uniquename, sensorName, sizeof (myData[devNr].uniquename));
              if (devNr==0 && strcmp (myData[devNr].uniquename, sensorName) == 0) strcpy (myData[devNr].uniquename, device_name);
              sprintf (sensorName, "%s_c%d_type", myDevType, devNr);
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
              sprintf (msgBuffer, "css811Xfm_%d", devNr);  // Transformation Logic
              util_getLogic (msgBuffer, &myData[devNr].xfrmLogic);
              sprintf (msgBuffer, "css811Alt_%d", devNr);  // Transformation Name
              nvs_get_string (msgBuffer, myData[devNr].xfrmName, "transform", sizeof (myData[devNr].xfrmName));
              //
              // Process rules
              //
              retval = true;
              myData[devNr].isvalid = true;
              for (uint8_t sense=0; sense<subtypeLen; sense++) {
                myData[devNr].state[sense] = GREEN;
                for (uint8_t level=0; level<3 ; level++) {
                  sprintf (ruleName, "css8%s_%d%d", subtypeList[sense], level, devNr);  // Warning Logic
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
              //
              // Add to counts
              //
              metricCount[CO2]++;
              metricCount[TVOC]++;
              devNr++;
            }
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
      char msgBuffer[100];
      char devStatus[9];
      struct css811_s *myData;

      if (devTypeCount[myDevTypeID] == 0) {
        // consolewriteln ((const char*) " * No css811 sensors found.");
        return;
      }
      consolewriteln ((const char*) "Test: css811 - CO2 and TVOC");
      myData = (struct css811_s*) devData[myDevTypeID];
      for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
        if (myData[device].isvalid) {
          strcpy(devStatus, "OK");
          sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x, name: %s, compensation: %s.%d", devStatus, myDevType, device, myData[device].bus, myData[device].addr,
                            myData[device].uniquename, devType[myData[device].compensationDevType], myData[device].compensationDevNr);
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
      struct css811_s *myData;    // Pointer to data.

      switch (testType) {
        case CO2:  tStart = 0; tEnd = 3; idx = 0; break;
        case TVOC: tStart = 3; tEnd = 6; idx = 1; break;
        default:   tStart = 0; tEnd = 0; idx = 0;
      }
      myData = (struct css811_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        if (myData[devNr].isvalid) {
          testVal = 0;
          for (uint8_t innerloop=tStart ; innerloop<tEnd; innerloop++) {
            struct rpnLogic_s *alertPtr = myData[devNr].alert[innerloop];
            if (alertPtr != NULL && rpn_calc(alertPtr->count, alertPtr->term)>0) testVal = (innerloop-tStart)+YELLOW;
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

    void getXymonStatus (char *xydata, int measureType)
    {
      struct css811_s *myData;
      float theValue;
      uint8_t indx;
      char uom[2][4] = { "ppm", "ppb" };
      char msgBuffer[80];

      if (measureType != CO2 && measureType != TVOC) return;
      // setup pointer to data array
      myData = (struct css811_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            if (measureType == CO2) {
              theValue = myData[device].co2_average;
              indx = 0;
            }
            else if (measureType == TVOC) {
              theValue = myData[device].tvoc_average;
              indx = 1;
            }
            uint8_t currentState = myData[device].state[indx];
            util_getLogicTextXymon (myData[device].alert[(currentState-YELLOW)+(3*indx)], xydata, currentState, myData[device].uniquename);
            sprintf (msgBuffer, " &%s %-16s %8s%-6s ", xymonColour[currentState], myData[device].uniquename, util_ftos (theValue, 2), uom[indx]);
            strcat  (xydata, msgBuffer);
            if (myData[device].xfrmLogic != NULL) {
              sprintf (msgBuffer, " %8s %s  ", util_ftos (myData[device].transform, 2), myData[device].xfrmName);
              strcat  (xydata, msgBuffer);
            }
            sprintf (msgBuffer, "(average over %d readings)\n", myData[device].averagedOver);
            strcat  (xydata, msgBuffer);
          }
          else {
            sprintf (msgBuffer, " &red %-16s - No data available:", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            if ((myData[device].errorCode & 0x3F) == 0x00) strcat (xydata, " No readings collected");
            if ((myData[device].errorCode & 0x01) == 0x01) strcat (xydata, " Invalid data written,");
            if ((myData[device].errorCode & 0x02) == 0x02) strcat (xydata, " Invalid data requested,");
            if ((myData[device].errorCode & 0x04) == 0x04) strcat (xydata, " Unsupported measurement mode,");
            if ((myData[device].errorCode & 0x08) == 0x08) strcat (xydata, " Value exceeds max range,");
            if ((myData[device].errorCode & 0x10) == 0x10) strcat (xydata, " Heater current not in range,");
            if ((myData[device].errorCode & 0x20) == 0x20) strcat (xydata, " Incorrect heater voltage,");
            strcat (xydata, "\n");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("css811 semaphore not released.");
    }

    void getXymonStats (char *xydata)
    {
      struct css811_s *myData;
      char msgBuffer[40];
      int dp;
      float tDouble;
      
      // setup pointer to data array
      myData = (struct css811_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            sprintf (msgBuffer, "[co2.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, "DS:co2:GAUGE:600:U:U ");
            strcat  (xydata, util_ftos (myData[device].co2_average, 2));
            strcat  (xydata, "\n");
            sprintf (msgBuffer, "[tvoc.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, "DS:tvoc:GAUGE:600:U:U ");
            strcat  (xydata, util_ftos (myData[device].tvoc_average, 2));
            strcat  (xydata, "\n");
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
      struct css811_s *myData;
      char msgBuffer[64];
      char stateType[2][5] = {"csta", "tsta"};
      
      sprintf (msgBuffer, "css811.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct css811_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid) {
            sprintf (msgBuffer, "css811.%d.co2  (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].co2_average, 1));
              consolewriteln (" ppm");
            }
            sprintf (msgBuffer, "css811.%d.lasc (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].co2_last, 1));
            consolewriteln (" ppm");
            sprintf (msgBuffer, "css811.%d.tvoc (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].tvoc_average, 1));
              consolewriteln (" ppb");
            }
            sprintf (msgBuffer, "css811.%d.last (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].tvoc_last, 1));
            consolewriteln (" ppb");
            if (myData[device].xfrmLogic != NULL) {
              sprintf (msgBuffer, "css811.%d.xfrm (%s) %s (%s)", device, myData[device].uniquename, util_ftos (myData[device].transform, 2), myData[device].xfrmName);
              consolewriteln (msgBuffer);
            }
            for (uint8_t staLoop=0 ;staLoop<2; staLoop++) {
              sprintf (msgBuffer, "css811.%d.%s (%s) ", device, stateType[staLoop], myData[device].uniquename);
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
      struct css811_s *myData;
      float retval;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct css811_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (99999.99);
      if (myData[devNr].averagedOver < 1) return (9999.99);
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if (myData[devNr].averagedOver > 0) {
          if (strcmp(parameter,"co2") == 0) retval = myData[devNr].co2_average;
          else if (strcmp(parameter,"lasc") == 0) retval = myData[devNr].co2_last;
          else if (strcmp(parameter,"csta") == 0) retval = myData[devNr].state[0];
          else if (strcmp(parameter,"tvoc") == 0) retval = myData[devNr].tvoc_average;
          else if (strcmp(parameter,"last") == 0) retval = myData[devNr].tvoc_last;
          else if (strcmp(parameter,"tsta") == 0) retval = myData[devNr].state[1];
          else if (strcmp(parameter,"xfrm") == 0) retval = myData[devNr].transform;
          else retval = 0.00;
        }
        else retval=0.00;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

};

static css811 the_css811;
