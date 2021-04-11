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


// OneWire commands
#define OW_STARTCONVO      0x44  // Tells device to take a temperature reading and put it on the scratchpad
#define OW_COPYSCRATCH     0x48  // Copy scratchpad to EEPROM
#define OW_READSCRATCH     0xBE  // Read from scratchpad
#define OW_WRITESCRATCH    0x4E  // Write to scratchpad
#define OW_RECALLSCRATCH   0xB8  // Recall from EEPROM to scratchpad
#define OW_READPOWERSUPPLY 0xB4  // Determine if device needs parasite power
#define OW_ALARMSEARCH     0xEC  // Query bus for devices with an alarm condition

// Scratchpad locations
#define OW_TEMP_LSB        0
#define OW_TEMP_MSB        1
#define OW_HIGH_ALARM_TEMP 2
#define OW_LOW_ALARM_TEMP  3
#define OW_CONFIGURATION   4
#define OW_INTERNAL_BYTE   5
#define OW_COUNT_REMAIN    6
#define OW_COUNT_PER_C     7
#define OW_SCRATCHPAD_CRC  8

// DSROM FIELDS
#define OW_DSROM_FAMILY    0
#define OW_DSROM_CRC       7

// Device resolution
#define OW_TEMP_9_BIT  0x1F //  9 bit
#define OW_TEMP_10_BIT 0x3F // 10 bit
#define OW_TEMP_11_BIT 0x5F // 11 bit
#define OW_TEMP_12_BIT 0x7F // 12 bit

#define OW_MAX_CONVERSION_TIMEOUT          750

// Model IDs
#define DS18S20MODEL 0x10  // also DS1820
#define DS18B20MODEL 0x28  // also MAX31820
#define DS1822MODEL  0x22
#define DS1825MODEL  0x3B
#define DS28EA00MODEL 0x42

// Error Codes
#define OW_DEVICE_DISCONNECTED_C -127
#define OW_DEVICE_DISCONNECTED_F -196.6
#define OW_DEVICE_DISCONNECTED_RAW -7040

// Provide for n devices to be added to live system
#define OW_ADDITIONAL_DEVICES 5

class oneWireTemperature {
  private:
    const char myDevType[9] = "ds1820";
    uint8_t myDevTypeID = 255;
    uint8_t devValid = 0;
    uint8_t allocatedSlots = 0;
    bool needsInitialisation = true;

    
    static void  updateloop(void *pvParameters)
    {
      struct dallasTemp_s *myData;
      float temperature = 0.0;
      int pollInterval;
      int updateCount, loopCount; // UpdateCount = number of loop cycles per interval, loopCount = count of updates we attempted
      char msgBuffer[SENSOR_NAME_LEN];
      int16_t raw_temp;
      uint8_t dev_limit = 0;
      uint8_t cfg_data;
      uint8_t queueData;
      uint8_t raw_data[9];
      uint8_t myDevTypeID = 255;

      loopCount = 0;
      myDevTypeID = util_get_dev_type("ds1820");
      if (myDevTypeID!=255) {
        sprintf (msgBuffer, "defaultPoll_%d", myDevTypeID);
        pollInterval = nvs_get_int (msgBuffer, DEFAULT_INTERVAL);
        updateCount = 300 / pollInterval;
        pollInterval = pollInterval * 1000; // now use poll interval in ms
        queueData = myDevTypeID;
        myData = (struct dallasTemp_s*) devData[myDevTypeID];

        // loop forever collecting data
        while (true) {
          if (xQueueReceive(devTypeQueue[myDevTypeID], &queueData, pdMS_TO_TICKS(pollInterval+1000)) != pdPASS) {
            if (ansiTerm) displayAnsi(3);
            consolewriteln ("Missing ds1820 signal");
            if (ansiTerm) displayAnsi(1);
          }
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(pollInterval-500)) == pdTRUE) {
            if (loopCount >= updateCount) loopCount = 0;
            loopCount++;
            // Request a read from all devices
            for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
              if (myData[device].isvalid) {
                one_bus[myData[device].bus]->reset();
                one_bus[myData[device].bus]->select(myData[device].address);
                one_bus[myData[device].bus]->write(OW_STARTCONVO, 1);
              }
            }
            delay(800);
            for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
              if (myData[device].isvalid) {
                one_bus[myData[device].bus]->reset();
                one_bus[myData[device].bus]->select(myData[device].address);    
                one_bus[myData[device].bus]->write(OW_READSCRATCH);
                for (uint8_t i = 0; i < 9; i++) {           // we need 9 bytes
                  raw_data[i] = one_bus[myData[device].bus]->read();
                }
                raw_temp = util_transInt (raw_data[1], raw_data[0]);
                if (one_bus[myData[device].bus]->crc8 (raw_data,8) == raw_data[8]) {
                  if (myData[device].isTypeS) {
                    raw_temp = raw_temp << 3; // 9 bit resolution default
                    if (raw_data[7] == 0x10) {
                      // "count remain" gives full 12 bit resolution
                      raw_temp = (raw_temp & 0xFFF0) + 12 - raw_data[6];
                    }
                  }
                  else {
                    cfg_data = (raw_data[4] & 0x60);
                    // at lower res, the low bits are undefined, so let's zero them
                    if      (cfg_data == 0x00) raw_temp = raw_temp & ~7;  // 9 bit resolution, 93.75 ms
                    else if (cfg_data == 0x20) raw_temp = raw_temp & ~3; // 10 bit res, 187.5 ms
                    else if (cfg_data == 0x40) raw_temp = raw_temp & ~1; // 11 bit res, 375 ms
                    // default is 12 bit resolution, 750 ms conversion time
                  }
                  temperature = (float) raw_temp / 16.0;
                  //temperature = getTemp(bus, myData[device].address) / 128.0;
                  //temperature = OW_DEVICE_DISCONNECTED_C;
                  // Ignore disconnects and extremes.
                  if (temperature != OW_DEVICE_DISCONNECTED_C && temperature > -126 && temperature < 126) {
                    myData[device].temp_last   = temperature;
                    myData[device].temp_accum += temperature;
                    myData[device].readingCount++;
                  }
                }
              }
            }
            if (loopCount >= updateCount) {
              for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
                if (myData[device].isvalid) {
                  if (myData[device].readingCount != 0) {
                    myData[device].temp_average = myData[device].temp_accum / myData[device].readingCount;
                    myData[device].averagedOver = myData[device].readingCount;
                    myData[device].temp_accum = 0.0;
                    myData[device].readingCount = 0;
                  }
                  else {
                    myData[device].averagedOver = 0;
                  }
                }
              }
              loopCount = 0;
            }
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
      }
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Could not determine ds1820 type ID for update loop");
      if (ansiTerm) displayAnsi(1);
      vTaskDelete( NULL );
    }

    void addToTable (uint8_t *address, uint8_t bus)
    {
      static OneWire *aWire = NULL;
      static char nextAllocated = 0;
      int16_t devIndex, nextDallas;
      char textAddress[17];
      char msgBuffer[BUFFSIZE];
      char sensorName[SENSOR_NAME_LEN];
      uint8_t foundEntry;
      bool notFound;
      struct dallasTemp_s *myData;;

      // avoid uninitialised data
      if (myDevTypeID>250) return;
      if (devData[myDevTypeID] == NULL) return;
      if (aWire == NULL) {
        for (uint8_t a=0; a<MAX_ONEWIRE && aWire==NULL; a++) {
          if (one_bus[a] != NULL) aWire = one_bus[a];
        }
      }
      if (aWire == NULL) return;
      // Set up some useful data
      myData = (struct dallasTemp_s*) devData[myDevTypeID];
      sprintf (textAddress, "%02x%02x%02x%02x%02x%02x%02x%02x", address[0], address[1], address[2], address[3], address[4], address[5], address[6], address[7]);
      // Test CRC of address
      if (aWire->crc8 (address,7) == address[7]) {
        // valid address, check if in table already
        notFound = true;
        for (foundEntry=0; notFound && foundEntry<allocatedSlots && foundEntry<nextAllocated; foundEntry++) {
          notFound = false;
          for (uint8_t compareIndex=0; compareIndex<8 && !notFound; compareIndex++) {
            if (address[compareIndex] != myData[foundEntry].address[compareIndex]) notFound = true;
          }
        }
        // Add entry if not found
        if (notFound && nextAllocated<allocatedSlots) {
          sprintf (msgBuffer, "Allocate to slot %d", nextAllocated);
          for (uint8_t k=0; k<8; k++) myData[nextAllocated].address[k] = address[k];
          myData[nextAllocated].isvalid = true;
          if (address[0] == DS18S20MODEL) myData[nextAllocated].isTypeS = true;
          else myData[nextAllocated].isTypeS = false;
          myData[nextAllocated].temp_accum = 0.0;
          myData[nextAllocated].temp_last  = 0.0;
          myData[nextAllocated].readingCount = 0;
          myData[nextAllocated].readingCount = 0;
          myData[nextAllocated].averagedOver = 0;
          myData[nextAllocated].bus = bus;
          myData[nextAllocated].state = GREEN;
          strcpy (msgBuffer, "D");
          strcat (msgBuffer, &textAddress[2]);
          msgBuffer[14] = '\0';
          devIndex = nvs_get_int (msgBuffer, -100);
          if (devIndex < 0) {
            devIndex = nextDallas;
            nvs_put_int (msgBuffer, nextDallas);
            nextDallas++;
          }
          myData[nextAllocated].index = devIndex;
          // Find device name
          sprintf (sensorName, "ds1820_%d", devIndex);
          nvs_get_string (sensorName, myData[nextAllocated].uniquename, msgBuffer, SENSOR_NAME_LEN);
          // Set up warning rules
          for (uint8_t level=0; level<3 ; level++) {
            sprintf (sensorName, "ds1820_%d%d", level, devIndex);  // Warning Logic
            nvs_get_string (sensorName, msgBuffer, "disable", sizeof(msgBuffer));
            if (strcmp (msgBuffer, "disable") != 0) {
              consolewrite ("Enable check: ");
              consolewrite (sensorName);
              consolewrite (" as ");
              consolewriteln (msgBuffer);
              util_getLogic (sensorName, &myData[nextAllocated].alert[level]);
            }
            else myData[nextAllocated].alert[level] = NULL;
          }
          nextAllocated++;
          metricCount[TEMP]++;
          devTypeCount[myDevTypeID]++;
        }
        else if (notFound) {
          if (ansiTerm) displayAnsi(3);
          consolewrite ("Insufficient memory allocated to ds1820 data to permit addition of ");
          consolewriteln (textAddress);
          consolewriteln ("Reboot system to allocate more memory to ds1820 sensors.");
          if (ansiTerm) displayAnsi(1);
        }
      }
      else {
        if (ansiTerm) displayAnsi(3);
        consolewrite ("Found invalid checksum on OneWire address: ");
        consolewriteln (textAddress);
        if (ansiTerm) displayAnsi(1);
      }
    }


  public:
    char subtypeList[1][5] = { "temp" };
    char subtypeLen = 1;

    void begin()
    {
      char sensorName[SENSOR_NAME_LEN];
      uint8_t address[8];
      uint8_t pin, limit, firstCount, secondCount;
      bool startReq = false;
      struct dallasTemp_s *myData;
      char msgBuffer[80];

      //
      // Initialise busses
      //
      firstCount  = 0;
      secondCount = 0;
      if (needsInitialisation) {
        myDevTypeID = util_get_dev_type((char*) myDevType);
        devTypeCount[myDevTypeID] = 0;
        needsInitialisation = false;
        devValid = 0;
        if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(5000)) == pdTRUE) {
          // Initialise busses
          for (int n=0; n<MAX_ONEWIRE; n++) {
            sprintf (sensorName, "DallasPin%d", n);
            pin = nvs_get_int (sensorName, 99);
            if (pin>=0 && pin<36) {
              one_bus[n] = new OneWire(pin);
              startReq = true;
            }
            else one_bus[n] = NULL;
          }
          //delay (1000);
          //
          // Scan and count devices and get total devices:
          //
          for (uint8_t n=0; n<MAX_ONEWIRE; n++) {
            if (one_bus[n] != NULL) {
              // Count up devices
              one_bus[n]->reset_search();
              while (one_bus[n]->search(address)) firstCount++; // initial search to wake up the world
              // delay (500);
              one_bus[n]->reset_search();
              delay (10);
              while (one_bus[n]->search(address)) {
                delay(10);
                secondCount++;
              }
            }
          }
          if (firstCount > secondCount) secondCount = firstCount;
          if (secondCount < 250) allocatedSlots = secondCount + OW_ADDITIONAL_DEVICES;
          else allocatedSlots = secondCount;
          devData[myDevTypeID] = malloc(sizeof (dallasTemp_s) * allocatedSlots);
          sprintf (msgBuffer, "OneWire support for upto %d sensors", allocatedSlots);
          if (ansiTerm) displayAnsi(4);
          consolewriteln (msgBuffer);
          if (ansiTerm) displayAnsi(1);
          //
          // Initialise data structures - probably redundant
          //
          limit = secondCount + OW_ADDITIONAL_DEVICES;
          myData = (struct dallasTemp_s*) devData[myDevTypeID];
          if (myData != NULL) {
            for (int n=0; n<limit; n++) {
              myData[n].isvalid = false;
              myData[n].temp_accum = 0.0;
              myData[n].temp_average = 0.0;
              myData[n].bus = 0;
              myData[n].state = GREEN;
              myData[n].readingCount = 0;
              myData[n].averagedOver = 0;
              for (uint8_t k=0; k<3; k++) {
                myData[n].alert[k] = NULL;
              }
            }
          }
          xSemaphoreGive(devTypeSem[myDevTypeID]);
        }
        if (startReq) {
          inventory();
          xTaskCreate(updateloop, devType[myDevTypeID], 4096, NULL, 12, NULL);
        }
      }
      else consolewriteln ("Attempted to re-initialise ds1820 devices");
    }

    void inventory()
    {
      struct dallasTemp_s *myData;
      uint8_t thermal = 0;
      uint8_t other   = 0;
      uint8_t j       = 0;
      uint8_t  address[8];
      char textAddress[17];
      char msgBuffer[80];
      struct ds_address_t *addrData;
  
      if (needsInitialisation) {
        consolewriteln ("ds1820 devices not initialised");
        begin();
      }
      if (myDevTypeID>250) return;
      //
      // Build table of data
      //
      myData = (struct dallasTemp_s*) devData[myDevTypeID];
      addrData = (struct ds_address_t*) malloc (sizeof (struct ds_address_t) * allocatedSlots * 2);
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(50000)) == pdTRUE) {
        // Try to build the table of addresses quickly to avoid timeouts on the search
        for (uint8_t n=0; n<MAX_ONEWIRE; n++) {
          if (one_bus[n] != NULL) {
            for (uint8_t q=0; q<2; q++) {
              one_bus[n]->reset_search();
              if (q==1) delay(10);
              while (one_bus[n]->search(address) && j<allocatedSlots) {
                for (uint8_t k=0; k<8; k++) addrData[j].address[k] = address[k];
                addrData[j].bus = n;
                j++;
                if (q==1) delay(10);
              }
            }
          }
        }
        // Now that we have that lets add it to the table
        for (uint8_t n=0; n<j; n++) {
          addToTable (addrData[n].address, addrData[n].bus);
        }
        // clean up
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      free (addrData);
      //
      // Now present findings, expect this data to be constant, so exclusive semaphore not required.
      //
      if (devTypeCount[myDevTypeID] > 0) consolewriteln ("OneWire temperature sensors:");
      else consolewriteln ("No OneWire sensors found");
      for (uint8_t n=0; n<devTypeCount[myDevTypeID]; n++) {
        sprintf (msgBuffer, " * ID: %02d, UID ", myData[n].index);
        consolewrite (msgBuffer);
        for (uint8_t k=0; k<8; k++) {
          sprintf (msgBuffer, "%02x", myData[n].address[k]);
          consolewrite (msgBuffer);
        }
        sprintf (msgBuffer, ", Name: %s, Type: ", myData[n].uniquename);
        consolewrite (msgBuffer);
        switch (myData[n].address[0]) {
          case DS18S20MODEL:  consolewriteln ("DS18S20/DS1820"); break;
          case DS18B20MODEL:  consolewriteln ("DS18B20/MAX31820"); break;
          case DS1822MODEL:   consolewriteln ("DS1822"); break;
          case DS1825MODEL:   consolewriteln ("DS1825"); break;
          case DS28EA00MODEL: consolewriteln ("DS28EA00"); break;
          default:            consolewriteln ("unknown (invalid)"); break;
        }
      }
    }
    
    void old_inventory()
    {
      struct dallasTemp_s *myData;
      char sensorName[SENSOR_NAME_LEN];
      char addressText[17];
      char msgBuffer[BUFFSIZE];
      uint8_t pin;
      uint8_t limit;
      uint8_t j = 0;
      uint8_t address[8];
      uint8_t thermal, other;
      int16_t devIndex, nextDallas;

      thermal = 0;
      other = 0;
      nextDallas = nvs_get_int ("nextDallas", 0);
      if (needsInitialisation) {
        consolewriteln ("ds1820 devices not initialised");
        begin();
      }
      myData = (struct dallasTemp_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(50000)) == pdTRUE) {
        for (uint8_t n=0; n<MAX_ONEWIRE; n++) {
          if (one_bus[n] != NULL) {
            // headline summary
            sprintf (msgBuffer, "--- OneWire bus %d ", n);
            consolewrite (msgBuffer);
            sprintf (sensorName, "DallasPin%d", n);
            pin = nvs_get_int (sensorName, 99);
            sprintf (msgBuffer, "pin %02d -------------------------------------------------", pin);
            consolewriteln (msgBuffer);
            // limit = devTypeCount[myDevTypeID] + OW_ADDITIONAL_DEVICES;
            limit = allocatedSlots;
            one_bus[n]->reset_search();
            while (one_bus[n]->search(address)); // initial search to wake up the world
            one_bus[n]->reset_search();
            while (one_bus[n]->search(address) && j<limit) {
              for (uint8_t k=0; k<sizeof(myData[j].address); k++) myData[j].address[k] = address[k];
              myData[j].isvalid = true;
              myData[j].isTypeS = false;
              myData[j].temp_accum = 0.0;
              myData[j].readingCount = 0;
              sprintf (addressText, "%02x%02x%02x%02x%02x%02x%02x%02x", address[0], address[1], address[2], address[3], address[4], address[5], address[6], address[7]);
              // munge the address to get NV ram name
              strcpy (msgBuffer, "D");
              strcat (msgBuffer, &addressText[2]);
              msgBuffer[14] = '\0';
              devIndex = nvs_get_int (msgBuffer, -100);
              if (devIndex < 0) {
                devIndex = nextDallas;
                nvs_put_int (msgBuffer, nextDallas);
                nextDallas++;
              }
              myData[j].index = devIndex;
              sprintf (sensorName, "ds1820_%d", devIndex);
              nvs_get_string (sensorName, myData[j].uniquename, msgBuffer, SENSOR_NAME_LEN);
              sprintf (msgBuffer, "%d - %s model: ", devIndex, myData[j].uniquename);
              consolewrite (msgBuffer);
              switch (address[0]) {
                case DS18S20MODEL:  consolewrite ("DS18S20/DS1820"); myData[j].isTypeS = true; thermal++ ; break;
                case DS18B20MODEL:  consolewrite ("DS18B20/MAX31820"); thermal++; break;
                case DS1822MODEL:   consolewrite ("DS1822"); thermal++; break;
                case DS1825MODEL:   consolewrite ("DS1825"); thermal++; break;
                case DS28EA00MODEL: consolewrite ("DS28EA00"); thermal++; break;
                default:            consolewrite ("unknown (invalid)"); other++; myData[j].isvalid = false; break;
              }
              consolewrite   ("  UID: ");
              consolewriteln (addressText);
              j++;
            }
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
        sprintf (msgBuffer, "Rescan: %d thermal and %d other OneWire devices found.", thermal, other);
        consolewriteln (msgBuffer);
        devTypeCount[myDevTypeID] = thermal + other;
        devValid = thermal;
        if (thermal>0 && metricCount[TEMP]==0) metricCount[TEMP] = thermal;
        nvs_put_int ("nextDallas", nextDallas);
      }
      else consolewriteln ("Cannot access ds1820 semaphore.");
    }

    // returns true if the bus requires parasite power
  /* bool isParasitePowerMode(uint8_t bus) {
      if (one_bus[bus] == NULL) return (false);
      return parasite[bus];
    } */

    uint8_t getStatusColor()
    {
      uint8_t retVal  = GREEN;    // Worst score for all tests of this type
      uint8_t testVal = GREEN;    // Score for the currently evaluated sensor
      struct dallasTemp_s *myData;    // Pointer to data.

      myData = (struct dallasTemp_s*) devData[myDevTypeID];
      for (int devNr=0; devNr<devTypeCount[myDevTypeID]; devNr++) {
        if (myData[devNr].isvalid) {
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
        else retVal = CLEAR;
      }
      return (retVal);
    }

    bool getXymonStatus (char *xydata)
    {
      struct dallasTemp_s *myData;
      char msgBuffer[80];
      float altValue;

      if (myDevTypeID>250) return (false);
      myData = (struct dallasTemp_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            util_getLogicTextXymon (myData[device].alert[myData[device].state-YELLOW], xydata, myData[device].state, myData[device].uniquename);
            altValue = (myData[device].temp_average * 1.8) + 32.0;
            sprintf (msgBuffer, " &%s %-16s %8s'C", xymonColour[myData[device].state], myData[device].uniquename, util_ftos (myData[device].temp_average, 2));
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "  %8s'F  (average over %d readings)\n", util_ftos (altValue, 2), myData[device].averagedOver);
            strcat  (xydata, msgBuffer);
          }
          else if (strlen(myData[device].uniquename)>0 && strlen(myData[device].uniquename)<SENSOR_NAME_LEN) {
            sprintf (msgBuffer, " &yellow No readings for %s", myData[device].uniquename);
            consolewriteln (msgBuffer);
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      else consolewriteln ("ds1820 semaphore not released.");
      return (true);
    }

    void getXymonStats (char *xydata)
    {
      struct dallasTemp_s *myData;
      char msgBuffer[40];
      
      // setup pointer to data array
      myData = (struct dallasTemp_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid && myData[device].averagedOver > 0) {
            sprintf (msgBuffer, "[temperature.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            strcat  (xydata, "DS:temperature:GAUGE:600:U:U ");
            strcat  (xydata, util_ftos (myData[device].temp_average, 2));
            strcat  (xydata, "\n");
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

    void printData()
    {
      struct dallasTemp_s *myData;
      char msgBuffer[60];

      
      sprintf (msgBuffer, "dsl1820.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct dallasTemp_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].isvalid) {
            sprintf (msgBuffer, "ds1820.%d.temp (%s) ", myData[device].index, myData[device].uniquename);
            consolewrite (msgBuffer);
            if (myData[device].averagedOver == 0) consolewriteln ("No data collected");
            else {
              consolewrite (util_ftos (myData[device].temp_average, 1));
              consolewriteln (" 'C");
            }
            sprintf (msgBuffer, "ds1820.%d.last (%s) ", myData[device].index, myData[device].uniquename);
            consolewrite (msgBuffer);
            consolewrite (util_ftos (myData[device].temp_last, 1));
            consolewriteln (" 'C");
            if (myData[device].averagedOver > 0) {
              sprintf (msgBuffer, "ds1820.%d.sos  (%s) %s m/s", myData[device].index, myData[device].uniquename, util_ftos(util_speedOfSound(myData[device].temp_average), 1));
              consolewriteln (msgBuffer);
            }
            sprintf (msgBuffer, "ds1820.%d.lass (%s) %s m/s", myData[device].index, myData[device].uniquename, util_ftos(util_speedOfSound(myData[device].temp_last), 1));
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "ds1820.%d.tsta (%s) %d", myData[device].index, myData[device].uniquename, myData[device].state);
            consolewrite (msgBuffer);
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
      struct dallasTemp_s *myData;
      float retval;
      int zoid = -99;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct dallasTemp_s*) devData[myDevTypeID];
      // Find the device index within discovered sensors
      for (uint8_t iLoop=0; iLoop<devTypeCount[myDevTypeID]; iLoop++) {
        if (myData[devNr].index == devNr) zoid = iLoop;
      }
      // return incredible data if nothing to return
      if (zoid < 0) return (99999.99);
      if (myData[zoid].averagedOver < 1) return (9999.99);
      // return the data polled by the sensor
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if (myData[zoid].averagedOver > 0) {
          if      (strcmp(parameter,"temp") == 0) retval = myData[zoid].temp_average;
          else if (strcmp(parameter,"last") == 0) retval = myData[zoid].temp_last;
          else if (strcmp(parameter,"tsta") == 0) retval = myData[zoid].state;
          else if (strcmp(parameter,"sos")  == 0) retval = util_speedOfSound(myData[devNr].temp_average);
          else if (strcmp(parameter,"lass") == 0) retval = util_speedOfSound(myData[devNr].temp_last);
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

};

oneWireTemperature the_wire;
