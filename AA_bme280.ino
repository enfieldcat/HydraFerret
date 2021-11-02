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


class bme280 {
 private:
    const char myDevType[7] = "bme280";
    uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      float latestTemp, latestHumid, latestPressure;
      float xfrmResult;
      int pollInterval;
      int updateCount, cycle_count;
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      struct bme280_s *myData;
      char msgBuffer[80];
      char myDevType[] = "bme280";
     
      updateCount = 0;
      cycle_count = 0;
      myDevTypeID = util_get_dev_type("bme280");
      if (myDevTypeID!=255) {
        myData = (struct bme280_s*) (devData[myDevTypeID]);
        for (char devNr=0; devNr<devTypeCount[myDevTypeID];devNr++) if (myData[devNr].isvalid) updateCount++;
      }
      if (myDevTypeID!=255 && updateCount > 0) {
        devRestartable[myDevTypeID] = false;
        util_deviceTimerCreate(myDevTypeID);
        // work out counts and timing intervals for this device type
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000;
        if (xTimerChangePeriod(devTypeTimer[myDevTypeID], pdMS_TO_TICKS(pollInterval), pdMS_TO_TICKS(1100)) != pdPASS) {
          consolewriteln("Unable to adjust bme280 poll timer period, keep at 1 second");
          pollInterval = 1000;
          updateCount = 300;
          }
        queueData = myDevTypeID;
        for (char devNr=0; devNr<devTypeCount[myDevTypeID];devNr++) {
          myData[devNr].bme = new Adafruit_BME280;
          if (xSemaphoreTake(wiresemaphore[myData[devNr].bus], 30000) == pdTRUE) {
            if (myData[devNr].isvalid && myData[devNr].bme->begin((uint8_t)myData[devNr].addr, &(I2C_bus[myData[devNr].bus]))) {
              myData[devNr].model = myData[devNr].bme->sensorID();
              myData[devNr].bme->setSampling();
              // Do first reading and discard result
              myData[devNr].bme->readTemperature();
              myData[devNr].bme->readHumidity();
              myData[devNr].bme->readPressure();
              myData[devNr].isvalid = true;
            } else {
              myData[devNr].isvalid = false;
              uint8_t diag = util_i2c_read (myData[devNr].bus, myData[devNr].addr, 0xd0);
              sprintf (msgBuffer, "bus %x, device 0x%02x is invalid type 0x%02x", myData[devNr].bus, myData[devNr].addr, diag);
              consolewriteln (msgBuffer);
            }
            xSemaphoreGive(wiresemaphore[myData[devNr].bus]);
          }
        }
        // loop forever collecting data
        while (devTypeCount[myDevTypeID] > 0) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            consolewriteln ("Missing bme280 signal");
          }
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
            for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
              if (myData[device].isvalid) {
                if (xSemaphoreTake(wiresemaphore[myData[device].bus], 30000) == pdTRUE) {
                  latestTemp     = ((myData[device].bme)->readTemperature());
                  latestHumid    = ((myData[device].bme)->readHumidity());
                  latestPressure = ((myData[device].bme)->readPressure());
                  xSemaphoreGive(wiresemaphore[myData[device].bus]);
                  if (latestTemp>=-40.0 && latestTemp<=85.0 && latestHumid >= 0.0 && latestHumid<= 100.0 && latestPressure >= 30000.0 && latestPressure <= 110000.0) {
                    myData[device].temp_last    = latestTemp;
                    myData[device].humid_last   = latestHumid;
                    myData[device].pres_last    = latestPressure;
                    myData[device].temp_accum  += latestTemp;
                    myData[device].humid_accum += latestHumid;
                    myData[device].pres_accum  += latestPressure;
                    myData[device].readingCount++;
                    if(myData[device].readingCount == updateCount) {
                      myData[device].averagedOver  = myData[device].readingCount;
                      myData[device].temp_average  = myData[device].temp_accum  / myData[device].averagedOver;
                      myData[device].humid_average = myData[device].humid_accum / myData[device].averagedOver;
                      myData[device].uncompensatedPres = myData[device].pres_accum  / (myData[device].averagedOver * 100); // convert pascals to hPa
                      myData[device].pres_average  = util_compensatePressure (myData[device].uncompensatedPres, myData[device].altitude, myData[device].temp_average); // compensate for altitude
                      myData[device].temp_accum    = 0.0;
                      myData[device].pres_accum    = 0.0;
                      myData[device].humid_accum   = 0.0;
                      myData[device].readingCount  = 0;
                    }
                  }
                  else {
                    if (latestTemp<-40.0         || latestTemp>85.0) {
                      sprintf (msgBuffer, "%s: Temperature reading %s�C out of range",    myDevType, util_ftos(latestTemp, 2));
                      consolewriteln (msgBuffer);
                    }
                    if (latestHumid<0.0          || latestHumid>100.0) {
                      sprintf (msgBuffer, "%s: Humidity reading %s%% out of range",       myDevType , util_ftos(latestHumid, 2));
                      consolewriteln (msgBuffer);
                    }
                    if (latestPressure < 30000.0 || latestPressure > 110000.0) {
                      sprintf (msgBuffer, "%s: Pressure reading %s pascals out of range", myDevType , util_ftos(latestPressure, 2));
                      consolewriteln (msgBuffer);
                    }
                  }
                }
              }
            }
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
          for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
            if (myData[device].isvalid && myData[device].readingCount == 0) {
              struct rpnLogic_s *xfrm_Ptr = myData[device].xfrmLogic;
              if (xfrm_Ptr != NULL) {
                xfrmResult = rpn_calc(xfrm_Ptr->count, xfrm_Ptr->term);
                if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
                  myData[device].transform = xfrmResult;
                  xSemaphoreGive(devTypeSem[myDevTypeID]);
                }
              }
            }
          }
        }
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Could not determine bme280 type ID for update loop");
        if (ansiTerm) displayAnsi(1);
        }
      if (myDevTypeID!=255) {
        util_deallocate (myDevTypeID);
      }
      vTaskDelete( NULL );
    }

  public:

    char subtypeList[3][5] = { "temp", "humi", "pres" };
    char subtypeLen = 3;

    bme280 ()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("bme280");
      devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      bool retval = false;
      const uint8_t dev_addr[] = { 0x76, 0x77 };
      struct bme280_s *myData;
      char devNr;
      char sensorName[SENSOR_NAME_LEN];
      char ruleName[SENSOR_NAME_LEN];
      char msgBuffer[BUFFSIZE];

      if (myDevTypeID==255) return false;   // unknown type - get out of here
      // Probe for candidate devices
      for (int bus=0; bus<2; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)); device++) {
          if (util_i2c_probe(bus, dev_addr[device])) devTypeCount[myDevTypeID]++;
        }
      }
      if (devTypeCount[myDevTypeID] == 0) {
        // consolewriteln ((const char*) "No bme280 sensors found.");
        return(false);  // nothing found!
      }
      // set up and inittialise structures
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(bme280_s));
      myData = (struct bme280_s*) devData[myDevTypeID];
      devNr = 0;
      for (int bus=0; bus<2 && devNr<devTypeCount[myDevTypeID]; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)) && devNr<devTypeCount[myDevTypeID]; device++) {
          myData[devNr].bus = bus;
          myData[devNr].addr = dev_addr[device];
          myData[devNr].isvalid = false;
          if (util_i2c_probe(bus, dev_addr[device])) {
            sprintf (sensorName, "%s_%d", myDevType, devNr);
            nvs_get_string (sensorName, myData[devNr].uniquename, sensorName, sizeof (myData[devNr].uniquename));
            if (devNr==0 && strcmp (myData[devNr].uniquename, sensorName) == 0) strcpy (myData[devNr].uniquename, device_name);
            sprintf (sensorName, "%sAlt_%d", myDevType, devNr);
            myData[devNr].altitude = nvs_get_float (sensorName, 0.0);
            sprintf (sensorName, "%sDP_%d", myDevType, devNr);
            nvs_get_string (sensorName, myData[devNr].dewpointName, "none", sizeof (myData[devNr].uniquename));
            myData[devNr].isvalid = true;
            myData[devNr].readingCount  = 0;
            myData[devNr].averagedOver  = 0;
            myData[devNr].temp_accum    = 0.0;
            myData[devNr].pres_accum    = 0.0;
            myData[devNr].humid_accum   = 0.0;
            myData[devNr].temp_average  = 0.0;
            myData[devNr].pres_average  = 0.0;
            myData[devNr].humid_average = 0.0;
            myData[devNr].temp_last     = 0.0;
            myData[devNr].pres_last     = 0.0;
            myData[devNr].humid_last    = 0.0;
            myData[devNr].transform     = 0.0;
            myData[devNr].uncompensatedPres = 0.0;
            //
            // get rpn transform
            //
            sprintf (msgBuffer, "bme280Xfm_%d", devNr);  // Transformation Logic
            util_getLogic (msgBuffer, &myData[devNr].xfrmLogic);
            sprintf (msgBuffer, "bme280Alt_%d", devNr);  // Transformation Name
            nvs_get_string (msgBuffer, myData[devNr].xfrmName, "transform", sizeof (myData[devNr].xfrmName));
            //
            // Get warning settings
            //
            for (uint8_t sense=0; sense<subtypeLen; sense++) {
              myData[devNr].state[sense] = GREEN;
              for (uint8_t level=0; level<3 ; level++) {
                sprintf (ruleName, "bme2%s_%d%d", subtypeList[sense], level, devNr);  // Warning Logic
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
            retval = true;
            metricCount[TEMP]++;
            metricCount[PRES]++;
            metricCount[HUMID]++;
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
      struct bme280_s *myData;

      if (devTypeCount[myDevTypeID] == 0) {
        // consolewriteln ((const char*) " * No bme280 sensors found.");
        return;
      }
      consolewriteln ((const char*) "Test: bme280 - temperature, humidity and pressure");
      myData = (struct bme280_s*) devData[myDevTypeID];
      for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
        if (myData[device].isvalid) {
          strcpy(devStatus, "OK");
          sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x, model 0x%2x, name: %s", devStatus, myDevType, device, myData[device].bus, myData[device].addr, myData[device].model, myData[device].uniquename);
        }
        else {
          strcpy(devStatus, "REJECTED");
          sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x", devStatus, myDevType, device, myData[device].bus, myData[device].addr);
        }
        consolewriteln (msgBuffer);
        // NB model numbers:
        // bmp280: Read value is 0x56 / 0x57 (samples) 0x58 (mass production)
        // bme280: Read value is 0x60
      }
    }

    uint8_t getStatusColor(uint8_t testType)
    {
      uint8_t retVal  = GREEN;    // Worst score for all tests of this type
      uint8_t testVal = GREEN;    // Score for the currently evaluated sensor
      uint8_t tStart, tEnd, idx;  // indexes to pointers for tests taken
      struct bme280_s *myData;    // Pointer to data.

      switch (testType) {
        case TEMP:  tStart = 0; tEnd = 3; idx = 0; break;
        case HUMID: tStart = 3; tEnd = 6; idx = 1; break;
        case PRES:  tStart = 6; tEnd = 9; idx = 2; break;
        default:    tStart = 0; tEnd = 0; idx = 0;
      }
      myData = (struct bme280_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        if (myData[devNr].isvalid) {
          testVal = 0;
          for (uint8_t innerloop=tStart ; innerloop<tEnd; innerloop++) {
            if (myData[devNr].alert[innerloop] != NULL && rpn_calc(myData[devNr].alert[innerloop]->count, myData[devNr].alert[innerloop]->term)>0) testVal = (innerloop-tStart)+1;
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

//    bool getXymonStatus (char *xydata)
//    {
//      return (true);
//    }

    void getXymonStatus (char *xydata, int measureType)
    {
      struct bme280_s *myData;
      float theValue, altValue, dewPoint;
      char msgBuffer[80];
      const char uom[][5]    = {"'C", "%", " hPa"};
      const char altuom[][7] = {"'F", "%", " in Hg"} ;
      const float offsetval[3] = {  32,   0, 0 };
      const float multiply[3]  = { 1.8, 1.0, 0.02952998751};
      char *suffix;
      char indx;

      if (measureType != TEMP && measureType != HUMID && measureType != PRES) return;
      // setup pointer to data array
      myData = (struct bme280_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            if (measureType == TEMP) {
              theValue = myData[device].temp_average;
              indx = 0;
            }
            else if (measureType == HUMID) {
              theValue = myData[device].humid_average;
              indx = 1;
            }
            else {
              theValue = myData[device].pres_average;
              indx = 2;
            }
            if (theValue >= -120.0 && theValue < 1100.0) {
              uint8_t currentState = myData[device].state[indx];
              util_getLogicTextXymon (myData[device].alert[(currentState-YELLOW)+(indx*3)], xydata, currentState, myData[device].uniquename);
              strcat (xydata, " &");
              strcat (xydata, xymonColour[currentState]);
              sprintf (msgBuffer, " %-16s %8s%-6s", myData[device].uniquename, util_ftos (theValue, 2), uom[indx]);
              strcat  (xydata, msgBuffer);
              if (measureType != HUMID) {
                altValue = (theValue * multiply[indx]) + offsetval[indx];
                sprintf (msgBuffer, "  %8s%s  (", util_ftos (altValue, 2), altuom[indx]);
                strcat  (xydata, msgBuffer);
                if (measureType == TEMP) {
                  sprintf (msgBuffer, "average over %d readings)\n", myData[device].averagedOver);
                  strcat  (xydata, msgBuffer);
                  if (strcmp (myData[device].dewpointName, "none") != 0) {
                    dewPoint = util_dewpoint(myData[device].temp_average, myData[device].humid_average);
                    altValue = (dewPoint * 1.8) + 32.0;
                    sprintf (msgBuffer, " &clear %-16s %8s%-6s", myData[device].dewpointName, util_ftos (dewPoint, 2), uom[indx]);
                    strcat  (xydata, msgBuffer);
                    sprintf (msgBuffer, "  %8s%s  (", util_ftos (altValue, 2), altuom[indx]);
                    strcat  (xydata, msgBuffer);
                    sprintf (msgBuffer, "dewpoint based on %s%% RH)\n", util_ftos (myData[device].humid_average, 1));
                    strcat  (xydata, msgBuffer);
                  }
                }
                else {
                  sprintf (msgBuffer, "altitude %sm / ", util_ftos (myData[device].altitude, 1));
                  strcat  (xydata, msgBuffer);
                  sprintf (msgBuffer, "%sft)\n", util_ftos ((myData[device].altitude*3.280839895 ), 1));
                  strcat  (xydata, msgBuffer);
                }
              }
              else strcat (xydata, "\n");
            }
            else if (measureType!=TEMP) { // messes up temp graphs for some reason if we do this!
              sprintf (msgBuffer, "Out of range %s value of %s%s", myDevType, util_ftos (theValue, 2), uom[indx]);
              consolewriteln (msgBuffer);
              strcat (xydata, "&red ");
              strcat (xydata, msgBuffer);
              strcat (xydata, "\n");
            }
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("bme280 semaphore not released.");
    }

    void getXymonStats (char *xydata)
    {
      struct bme280_s *myData;
      char msgBuffer[40];
      int dp;
      float tDouble;
      
      // setup pointer to data array
      myData = (struct bme280_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            if (myData[device].temp_average > -120.0 && myData[device].temp_average < 120.0) {
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
            }
            if (myData[device].humid_average >= 0.0 && myData[device].humid_average <= 100.0) {
              sprintf (msgBuffer, "[humidity.%s.rrd]\n", myData[device].uniquename);
              strcat  (xydata, msgBuffer);
              strcat  (xydata, "DS:humidity:GAUGE:600:U:U ");
              strcat  (xydata, util_ftos (myData[device].humid_average, 2));
              strcat  (xydata, "\n");
            }
            if (myData[device].pres_average>=300.0 && myData[device].pres_average<=1100.00) {
              sprintf (msgBuffer, "[pressure.%s.rrd]\n", myData[device].uniquename);
              strcat  (xydata, msgBuffer);
              strcat  (xydata, "DS:pressure:GAUGE:600:U:U ");
              strcat  (xydata, util_ftos (myData[device].pres_average, 2));
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


    float getData(uint8_t devNr, char *parameter)
    {
      struct bme280_s *myData;
      float retval;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct bme280_s*) devData[myDevTypeID];
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
          else if (strcmp(parameter,"pres") == 0) retval = myData[devNr].pres_average;
          else if (strcmp(parameter,"lasp") == 0) retval = (myData[devNr].pres_last / 100.00);
          else if (strcmp(parameter,"psta") == 0) retval = myData[devNr].state[2];
          else if (strcmp(parameter,"unco") == 0) retval = myData[devNr].uncompensatedPres;
          else if (strcmp(parameter,"dewp") == 0) retval = util_dewpoint(myData[devNr].temp_average, myData[devNr].humid_average);
          else if (strcmp(parameter,"lasd") == 0) retval = util_dewpoint(myData[devNr].temp_last,    myData[devNr].humid_last);
          else if (strcmp(parameter,"sos")  == 0) retval = util_speedOfSound(myData[devNr].temp_average, myData[devNr].humid_average);
          else if (strcmp(parameter,"lass") == 0) retval = util_speedOfSound(myData[devNr].temp_last,    myData[devNr].humid_last);
          else if (strcmp(parameter,"xfrm") == 0) retval = myData[devNr].transform;
          else retval = 0.00;
        }
        else retval=0.00;
        if (strcmp(parameter,"alti") == 0) retval = myData[devNr].altitude;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

    void printData()
    {
      struct bme280_s *myData;
      char msgBuffer[64];
      char stateType[3][5] = {"tsta", "hsta", "psta"};
      
      sprintf (msgBuffer, "bme280.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct bme280_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid) {
            sprintf (msgBuffer, "bme280.%d.temp (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].temp_average, 1));
              consolewriteln (" 'C");
            }
            sprintf (msgBuffer, "bme280.%d.last (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].temp_last, 1));
            consolewriteln (" 'C");
            sprintf (msgBuffer, "bme280.%d.humi (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].humid_average, 1));
              consolewriteln (" %");
            }
            sprintf (msgBuffer, "bme280.%d.lash (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].humid_last, 1));
            consolewriteln (" %");
            sprintf (msgBuffer, "bme280.%d.dewp (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (util_dewpoint(myData[device].temp_average, myData[device].humid_average), 1));
              consolewriteln (" 'C");
            }
            sprintf (msgBuffer, "bme280.%d.lasd (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (util_dewpoint(myData[device].temp_last, myData[device].humid_last), 1));
            consolewriteln (" 'C");
            sprintf (msgBuffer, "bme280.%d.pres (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].pres_average, 1));
              consolewriteln (" hPa");
            }
            sprintf (msgBuffer, "bme280.%d.lasp (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos ((myData[device].pres_last / 100.00), 1));
            consolewriteln (" hPa");
            sprintf (msgBuffer, "bme280.%d.unco (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].uncompensatedPres, 1));
              consolewriteln (" hPa");
            }
            sprintf (msgBuffer, "bme280.%d.sos  (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (util_speedOfSound(myData[device].temp_average, myData[device].humid_average), 1));
              consolewriteln (" m/s");
            }
            sprintf (msgBuffer, "bme280.%d.lass (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (util_speedOfSound(myData[device].temp_last, myData[device].humid_last), 1));
            consolewriteln (" m/s");
            sprintf (msgBuffer, "bme280.%d.alti (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].altitude, 1));
            consolewriteln (" m");
            for (uint8_t staLoop=0 ;staLoop<3; staLoop++) {
              sprintf (msgBuffer, "bme280.%d.%s (%s) ", device, stateType[staLoop], myData[device].uniquename);
              consolewrite (msgBuffer);
              consolewrite (util_ftos (myData[device].state[staLoop], 0));
              consolewrite (" (");
              consolewrite ((char*) xymonColour[myData[device].state[staLoop]]);
              consolewriteln (")");
            }
            if (myData[device].xfrmLogic != NULL) {
              sprintf (msgBuffer, "bme280.%d.xfrm (%s) %s (%s)", device, myData[device].uniquename, util_ftos (myData[device].transform, 2), myData[device].xfrmName);
              consolewriteln (msgBuffer);
            }
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }     
    }

};


static bme280 the_bme280;
