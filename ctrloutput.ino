
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



class ctrloutput {
  private:
    const char myDevType[9] = "output";
    uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      uint8_t index;
      int8_t  wait_count;
      int pollInterval;
      char msgBuffer[30];
      float tFloat;
      
      myDevTypeID = util_get_dev_type("output");
      sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
      pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
      wait_count = (300 / pollInterval) + 1;  // wait 5 minutes for valid readings to appear
      pollInterval = pollInterval * 1000; // now use poll interval in ms
      queueData = myDevTypeID;
      if (myDevTypeID!=255) {
        for (index=0; index<MAX_OUTPUT; index++) {
          outputCtrl[index].initialised = 0;
        }
        xQueueSend (devTypeQueue[myDevTypeID], &queueData, 0);
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+5000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing output signal");
            if (ansiTerm) displayAnsi(1);
          }
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval+1000)) == pdTRUE) {
            for (index=0; index<MAX_OUTPUT; index++) if (outputCtrl[index].outputPin<40) {
              if (wait_count == 0) {
                outputCtrl[index].result = rpn_calc(outputCtrl[index].outputLogic->count, outputCtrl[index].outputLogic->term);
              }
              else {
                wait_count--;
                outputCtrl[index].result = outputCtrl[index].defaultVal;
              }
              if (outputCtrl[index].outputType==0) {
                pinMode(outputCtrl[index].outputPin, OUTPUT);
                outputCtrl[index].initialised = 0;
                if (outputCtrl[index].result > 0) {
                  outputCtrl[index].result = 1.0;
                  digitalWrite(outputCtrl[index].outputPin, HIGH);
                }
                else {
                  outputCtrl[index].result = 0.0;
                  digitalWrite(outputCtrl[index].outputPin, LOW);
                }
              }
              else if (outputCtrl[index].outputType==1 || outputCtrl[index].outputType==2) {
                if (outputCtrl[index].outputType==1) {
                  if (outputCtrl[index].result < 0.0) outputCtrl[index].result = 0.0;
                  else if (outputCtrl[index].result > 65535) outputCtrl[index].result = 65535.0;
                  tFloat = outputCtrl[index].result;
                  if (outputCtrl[index].initialised == 0) {
                    ledcSetup(index, 5000, 16);
                    ledcAttachPin(outputCtrl[index].outputPin, index);
                    outputCtrl[index].initialised = 1;
                  }
                }
                else { // servo control expected to be 5% - 10% duty cycle at 50Hz, see: https://en.wikipedia.org/wiki/Servo_control
                  if (outputCtrl[index].result < 0.0) outputCtrl[index].result = 0.0;
                  else if (outputCtrl[index].result > 100.0) outputCtrl[index].result = 100.0;
                  tFloat = (outputCtrl[index].result * 32.78) + 3278.0;
                  if (outputCtrl[index].initialised == 0) {
                    ledcSetup(index, 50, 16);
                    ledcAttachPin(outputCtrl[index].outputPin, index);
                    outputCtrl[index].initialised = 1;
                  }                  
                }
                ledcWrite(index, tFloat);
              }
              else {
                outputCtrl[index].initialised = 0;
                if (strcmp (outputCtrl[index].uniquename, "identify") == 0) {
                  if (outputCtrl[index].result < 1) mt_identity_state == ID_OFF;
                  else if (outputCtrl[index].result > 3.5 ) mt_identity_state == ID_SFLASH;
                  else mt_identity_state = floor(outputCtrl[index].result);
                }
                else if (strcmp (outputCtrl[index].uniquename, "hibernate") == 0) {
                  if (outputCtrl[index].result >= 1) {
                    sprintf (msgBuffer, "Output %d requested hibernate", index);
                    consolewriteln (msgBuffer);
                    util_start_deep_sleep(outputCtrl[index].result);
                  }
                }
                else if (strcmp (outputCtrl[index].uniquename, "restart") == 0) {
                  if (outputCtrl[index].result >= 1) {
                    sprintf (msgBuffer, "Output %d requested restart", index);
                    mt_sys_restart (msgBuffer);
                  }
                }
                else if (strcmp (outputCtrl[index].uniquename, "ota") == 0) {
                  if (outputCtrl[index].result >= 1) OTAcheck4update();
                }
              }
            }
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
      }
      vTaskDelete( NULL );
    }


  public:

    char subtypeList[1][5] = { "outp" };
    char subtypeLen = 1;

    bool begin()
    {
      bool retval = true;
      char ruleName[SENSOR_NAME_LEN];
      char msgBuffer[BUFFSIZE];
      uint8_t n;

      myDevTypeID = util_get_dev_type((char*) myDevType);
      devData[myDevTypeID] = &outputCtrl;
      devTypeCount[myDevTypeID] = MAX_OUTPUT;
      for (n=0; n<MAX_OUTPUT; n++) {
        sprintf (msgBuffer, "outputPin_%d", n);     // I/O pin
        outputCtrl[n].outputPin = nvs_get_int (msgBuffer, 99);
        if (outputCtrl[n].outputPin< 40) { 
          sprintf (msgBuffer, "outputDef_%d", n);   // default value
          outputCtrl[n].defaultVal = nvs_get_float (msgBuffer, 0.00);
          sprintf (msgBuffer, "outputName_%d", n);  // Name
          nvs_get_string (msgBuffer, outputCtrl[n].uniquename, msgBuffer, SENSOR_NAME_LEN);
          sprintf (msgBuffer, "outputType_%d", n);  // Output type eg relais
          outputCtrl[n].outputType = nvs_get_int (msgBuffer, 0);
          sprintf (msgBuffer, "outputLogi_%d", n);  // Output Logic
          util_getLogic (msgBuffer, &outputCtrl[n].outputLogic);
          for (uint8_t level=0; level<3 ; level++) {
             sprintf (ruleName, "outpoutp_%d%d", level, n);  // Warning Logic
             nvs_get_string (ruleName, msgBuffer, "disable", sizeof(msgBuffer));
             if (strcmp (msgBuffer, "disable") != 0) {
               consolewrite ("Enable check: ");
               consolewrite (ruleName);
               consolewrite (" as ");
               consolewriteln (msgBuffer);
               util_getLogic (ruleName, &outputCtrl[n].alert[level]);
             }
             else outputCtrl[n].alert[level] = NULL;
           }
        }
        else {
          outputCtrl[n].outputLogic = NULL;
          strcpy (outputCtrl[n].uniquename, "None");
          outputCtrl[n].outputType  = 0;
        }
      }
      if (retval) xTaskCreate(updateloop, devType[myDevTypeID], 4096, NULL, 12, NULL);
      // inventory();
      return (retval);
    }

    uint8_t getStatusColor()
    {
      uint8_t retVal  = GREEN;    // Worst score for all tests of this type
      uint8_t testVal = GREEN;    // Score dor the currently evaluated sensor
      struct output_s *myData;    // Pointer to data.

      myData = (struct output_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        if (myData[devNr].outputPin < 40 && myData[devNr].outputLogic != NULL) {
          testVal = 0;
          for (uint8_t innerloop=0 ; innerloop<3; innerloop++) {
            if (myData[devNr].alert[innerloop] != NULL && rpn_calc(myData[devNr].alert[innerloop]->count, myData[devNr].alert[innerloop]->term)>0) testVal = innerloop+1;
          }
          retVal = testVal;
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
            myData[devNr].state = testVal;
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
      }
      return (retVal);
    }
 
    bool getXymonStatus (char *xydata)
    {
      struct output_s *myData;
      char msgBuffer[80];
      uint8_t count = 0;
      uint8_t generalStatus = getStatusColor();

      myData = (struct output_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        sprintf (xydata, "status %s.output %s output %s\n\n", device_name, xymonColour[generalStatus], util_gettime());
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].outputPin < 40 && myData[device].outputLogic != NULL) {
            strcat  (xydata, " &");
            strcat  (xydata, xymonColour[myData[device].state]);
            sprintf (msgBuffer, " %-16s %-5s %8s (output.%d.val)\n", myData[device].uniquename, outputDescriptor[myData[device].outputType], util_ftos (myData[device].result, 2), device);
            strcat  (xydata, msgBuffer);
            count++;
          }
        }
        if (count == 0) {
          strcat (xydata, " &yellow No outputs defined.");
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("output semaphore not released.");
      return (false);  // no more data
    }

    void getXymonStats (char *xydata)
    {
      struct output_s *myData;
      char msgBuffer[40];
      
      // setup pointer to data array
      myData = (struct output_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].outputPin < 40) {
            sprintf (msgBuffer, "[output.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, "DS:val:GAUGE:600:U:U ");
            strcat  (xydata, util_ftos (myData[device].result, 2));
            strcat  (xydata, "\n");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }
  
    void printData()
    {
      struct output_s *myData;
      char msgBuffer[40];
      
      // setup pointer to data array
      myData = (struct output_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].outputPin<40) {
            sprintf (msgBuffer, "output.%d.var (%s) ", device, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewriteln (util_ftos (myData[device].result, 0));
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }     
    }

    float getData(uint8_t devNr, char *parameter)
    {
      struct output_s *myData;
      float retval;

      myData = (struct output_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (99999.99);
      if (myData[devNr].outputPin >= 40) return (9999.99);
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if (strcmp(parameter,"var") == 0) retval = myData[devNr].result;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }
};

ctrloutput the_output;
