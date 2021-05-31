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


class sdd1306 {
  private:
    char myDevType[9] = "sdd1306";
    uint8_t myDevTypeID;

    static void  updateloop(void *pvParameters)
    {
      uint8_t myDevTypeID = 255;
      uint8_t queueData;
      struct sdd1306_s *myData;
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted
      char msgBuffer[20];         // general messaging buffer
      char displayBuffer[3][20];  // DisplayBuffer
      char iterType;              // Iterator of device types
      char iterDev;               // Iterator of devices in each type
      bool moreData;              // Flag for device has more data to yield

      loopCount = 0;
      for (char n=0; n<4; n++) displayBuffer[n][0] = '\0';
      myDevTypeID = util_get_dev_type("sdd1306");
      if (myDevTypeID!=255) {
        util_deviceTimerCreate(myDevTypeID);
        myData = (struct sdd1306_s*) (devData[myDevTypeID]);
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        if (xTimerChangePeriod(devTypeTimer[myDevTypeID], pdMS_TO_TICKS(pollInterval), pdMS_TO_TICKS(1100)) != pdPASS) {
          consolewriteln("Unable to adjust sdd1306 display poll timer period, keep at 1 second");
          pollInterval = 1000;
          }
        queueData = myDevTypeID;
        iterType = 0;
        iterDev = 0;
        moreData = false;
        for (char devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
          myData[devNr].ssd1306 = new Adafruit_SSD1306((uint8_t)(myData[devNr].width), (uint8_t)(myData[devNr].height), &(I2C_bus[myData[devNr].bus]));
          if (xSemaphoreTake(wiresemaphore[myData[devNr].bus], 30000) == pdTRUE) {
            myData[devNr].ssd1306->begin(SSD1306_SWITCHCAPVCC, myData[devNr].addr);
            myData[devNr].ssd1306->clearDisplay();
            myData[devNr].ssd1306->setTextSize(2);
            myData[devNr].ssd1306->setTextColor(SSD1306_WHITE);
            myData[devNr].ssd1306->setCursor(0,0);             // Start at top-left corner
            myData[devNr].ssd1306->println(F(PROJECT_NAME));
            myData[devNr].ssd1306->println(F("Please"));
            myData[devNr].ssd1306->println(F("      Wait"));
            myData[devNr].ssd1306->display();
            xSemaphoreGive(wiresemaphore[myData[devNr].bus]);
          }
        }
        delay (10000);
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            consolewriteln ("Missing sdd1306 signal");
          }
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
            // find next device with more data
            if (!moreData) {
              iterDev++; // Move to the next device instance
              if (iterDev==devTypeCount[iterType]) {
                // we've exhausted the number of device of this type, so find next non-zero type
                iterType++;
                if (iterType==numberOfTypes) iterType = 0;
                for (;devTypeCount[iterType]==0; ++iterType) if (iterType==numberOfTypes) iterType = 0;  //at worst we should have at least the display device itself
                iterDev = 0;
              }
              moreData = true;
            }
            strcpy (displayBuffer[0], util_getDate());
            strcpy (displayBuffer[2], util_getMinute());
            for (char devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
              if (xSemaphoreTake(wiresemaphore[myData[devNr].bus], 30000) == pdTRUE) {
                myData[devNr].ssd1306->clearDisplay();
                myData[devNr].ssd1306->setTextSize(2);
                myData[devNr].ssd1306->setTextColor(SSD1306_WHITE);
                for (char line=0; line<2; line++) {
                  myData[devNr].ssd1306->setCursor(0,line*((myData[devNr].height)/4));  // Start at top-left corner
                  myData[devNr].ssd1306->print (displayBuffer[line]);
                  // myData[devNr].ssd1306->display();
                }
                myData[devNr].ssd1306->setTextSize(3);
                myData[devNr].ssd1306->setTextColor(SSD1306_WHITE);
                myData[devNr].ssd1306->setCursor(0,(myData[devNr].height)/2);  // Start at top-left corner
                myData[devNr].ssd1306->print (displayBuffer[2]);
                myData[devNr].ssd1306->display();
                xSemaphoreGive(wiresemaphore[myData[devNr].bus]);
              }
              xSemaphoreGive(devTypeSem[myDevTypeID]);            
            }
          }
        }
      }
      else consolewriteln ("Could not determine sdd1306 type ID for update loop");
      vTaskDelete( NULL );
    }

  public:

    sdd1306()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type(myDevType);
      devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      bool retval = false;
      const uint8_t dev_addr[] = { 0x3c };
      struct sdd1306_s *myData;
      char attribute[20];
      char devNr;

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
        consolewriteln ((const char*) "No sdd1306 displays found.");
        return(false);  // nothing found!
      }
      // set up and inittialise structures
      devData[myDevTypeID] = malloc (devTypeCount[myDevTypeID] * sizeof(sdd1306_s));
      myData = (struct sdd1306_s*) devData[myDevTypeID];
      devNr = 0;
      for (int bus=0; bus<2; bus++) if (I2C_enabled[bus]) {
        for (int device=0; device < (sizeof(dev_addr)/sizeof(char)); device++) {
          myData[devNr].bus = bus;
          myData[devNr].addr = dev_addr[device];
          if (util_i2c_probe(bus, dev_addr[device])) {
            myData[devNr].isvalid = true;
            sprintf (attribute, "disp_1306w_%d", devNr);
            myData[devNr].width = nvs_get_int (attribute, 128);
            sprintf (attribute, "disp_1306h_%d", devNr);
            myData[devNr].height = nvs_get_int (attribute, 64);
            retval = true;
            devNr++;
          }
          else {
            if (devNr<devTypeCount[myDevTypeID]) myData[devNr].isvalid = false;
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
      struct sdd1306_s *myData;

      consolewriteln ((const char*) "Test: sdd1306 - display");
      if (devTypeCount[myDevTypeID] == 0) {
        consolewriteln ((const char*) " * No sdd1306 displays found.");
        return;
      }
      myData = (struct sdd1306_s*) devData[myDevTypeID];
      for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
        if (myData[device].isvalid) strcpy(devStatus, "OK");
        else strcpy(devStatus, "REJECTED");
        sprintf (msgBuffer, " * %s: %s.%d on i2c bus %d, address 0x%02x", devStatus, myDevType, device, myData[device].bus, myData[device].addr);
        consolewriteln (msgBuffer);
      }
    }
};

static sdd1306 the_sdd1306;
