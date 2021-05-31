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


#define ASCII_BUF_SIZE 3584

class zebSerial {
  private:
    const char myDevType[7] = "serial";
    uint8_t myDevTypeID;
    uint8_t inchar;
    
    static void  serialSensorMaster(void *pvParameters)
    {
      uint8_t deviceToken[2];
      uint8_t serDeviceID[2] = {0,1}; //allow reference to serialid to pass to transmit task 
      uint8_t myDevTypeID = 255;
      uint8_t inchar;
      struct zebSerial_s *myData;
      char msgBuffer[40];

      myDevTypeID = util_get_dev_type("serial");
      if (myDevTypeID!=255) {
        util_deviceTimerCreate(myDevTypeID);
        myData = (struct zebSerial_s*) (devData[myDevTypeID]);
        for (uint8_t devNr=0; devNr<2; devNr++) {
          if      ((!myData[devNr].isvalid) || strcmp (myData[devNr].devType, "disable") == 0) deviceToken[devNr] = 255;
          else if (strcmp (myData[devNr].devType, "pms5003") == 0) deviceToken[devNr] = 0;
          else if (strcmp (myData[devNr].devType, "zh03b")   == 0) deviceToken[devNr] = 1;
          else if (strcmp (myData[devNr].devType, "winsen")  == 0) deviceToken[devNr] = 2;
          else if (strcmp (myData[devNr].devType, "ascii")   == 0) deviceToken[devNr] = 3;
          else if (strcmp (myData[devNr].devType, "nmea")    == 0) deviceToken[devNr] = 4;
          else if (strcmp (myData[devNr].devType, "mh-z19c") == 0) deviceToken[devNr] = 5;
          if (deviceToken[devNr] == 5) {
            xTaskCreate (co2Transmit, "CO2Request", 2048, &serDeviceID[devNr], 12, NULL);
          }
        }
        if (myData[0].isvalid || myData[1].isvalid) {
          while (true) {
            for (uint8_t devNr=0; devNr<2; devNr++) {
              if (myData[devNr].isvalid && serial_dev[devNr].available()) {
                inchar = serial_dev[devNr].read();
                switch (deviceToken[devNr]) {
                  case 0:
                  case 1:
                    pmsHandler (myDevTypeID, &myData[devNr], inchar);
                    break;
                  case 2:
                    winsenHandler (myDevTypeID, &myData[devNr], inchar);
                    break;
                  case 3:
                    asciiHandler (myDevTypeID, &myData[devNr], inchar);
                    break;
                  case 4:
                    nmeaHandler (myDevTypeID, &myData[devNr], inchar);
                    break;
                  case 5:
                    winsenHandler (myDevTypeID, &myData[devNr], inchar);
                    break;
                }
              }
            }
            //yield();
            delay (1);
          }
        }
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("No valid serial devices, no serial monitoring possible");
        if (ansiTerm) displayAnsi(0);
      }
      vTaskDelete( NULL );
    }



    static void co2Transmit (void *pvParameters)
    {
      static uint8_t outBuffer[9] = {0xff, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
      uint8_t serNr = *(uint8_t*)pvParameters;
      int interval = 60;

      {
        char message[SENSOR_NAME_LEN];
        int n = util_get_dev_type("serial");
        sprintf (message, "defaultPoll_%d", (int) n);
        interval = nvs_get_int (message, DEFAULT_INTERVAL);
      }
      interval = interval * 1000;
      while (true) {
        serial_dev[serNr].write (outBuffer, sizeof(outBuffer));
        delay(interval);
      }
      vTaskDelete( NULL );
    }


    static void winsenHandler (uint8_t myDevTypeID, struct zebSerial_s *device, uint8_t inChar)
    {
      struct winsen_s *winsenData;
      uint8_t checksum;

      // setup pointer to data
      if (device->dataBuffer == NULL) {
        device->bufferSize = sizeof (struct winsen_s);
        device->dataBuffer = (uint8_t*) malloc(sizeof (struct winsen_s));
        device->tailPtr    = 0;
        device->headPtr    = 0;
        winsenData = (struct winsen_s*) (device->dataBuffer);
        winsenData->avgOver   = 0;
        winsenData->count     = 0;
        winsenData->sumData   = 0;
        winsenData->avgData   = 0;
        for (uint8_t a=0; a<2; a++) {
          winsenData->lastData[a]= 0;
        }
      }
      else winsenData = (struct winsen_s*) (device->dataBuffer);

      if (device->headPtr == 0 && inChar == 0xff) {
        device->tailPtr = 0;
        device->headPtr = inChar;
      }
      if (device->headPtr == 0xff && device->tailPtr < sizeof (winsenData->msgBuffer)) {
        winsenData->msgBuffer[device->tailPtr++] = inChar;
      }
      if (device->tailPtr == sizeof (winsenData->msgBuffer)) {
        checksum = 0;
        for (uint8_t n=1; n<(sizeof(winsenData->msgBuffer)-1); n++) checksum += winsenData->msgBuffer[n];
        checksum = (~checksum) +1;
        if (checksum == winsenData->msgBuffer[sizeof(winsenData->msgBuffer)-1]) {
          winsenData->devType = winsenData->msgBuffer[1];
          winsenData->unit    = winsenData->msgBuffer[2];
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
            switch (winsenData->devType) {
              case 0x18:
                winsenData->lastData[0] = util_transInt (winsenData->msgBuffer[3], winsenData->msgBuffer[4]);
                break;
              case 0x86:
                winsenData->lastData[0] = util_transInt (winsenData->msgBuffer[2], winsenData->msgBuffer[3]);
                break;
              default:
                winsenData->lastData[0] = util_transInt (winsenData->msgBuffer[4], winsenData->msgBuffer[5]);
                break;
            }
            if (winsenData->devType == 0x86) {
              winsenData->lastData[1] = 5000;  // Max value
              winsenData->unit        = 0x03;
              }
            else winsenData->lastData[1] = util_transInt (winsenData->msgBuffer[6], winsenData->msgBuffer[7]);
            if (winsenData->lastData[0] <= winsenData->lastData[1]) {
              winsenData->sumData += winsenData->lastData[0];
              winsenData->count++;
            }
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
        else {
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Cheksum error on Winsen Sensor serial device");
          if (ansiTerm) displayAnsi(1);
          }
        device->headPtr = 0;
      }
    }

    static void pmsHandler (uint8_t myDevTypeID, struct zebSerial_s *device, uint8_t inChar)
    {
      struct pms5003_s *pmsData;
      
      // setup pointer to data
      if (device->dataBuffer == NULL) {
        device->bufferSize = sizeof (struct pms5003_s);
        device->dataBuffer = (uint8_t*) malloc(sizeof (struct pms5003_s));
        device->tailPtr    = 0;
        device->headPtr    = 0;
        pmsData = (struct pms5003_s*) (device->dataBuffer);
        pmsData->avgOver   = 0;
        pmsData->count     = 0;
        for (uint8_t a=0; a<14; a++) {
          pmsData->sumData[a] = 0;
          pmsData->avgData[a] = 0;
          pmsData->lastData[a]= 0;
        }
      }
      else pmsData = (struct pms5003_s*) (device->dataBuffer);

      if (device->headPtr <= 1) {
        if (device->headPtr == 0 && inChar == 0x42) device->headPtr++;
        else if (inChar == 0x4d) {
          device->headPtr++;
          device->tailPtr    = 0;
          pmsData->checksum  = 0x42 + 0x4d;
          pmsData->byteCount = 4;
        }
        else device->headPtr=0;
      }
      else if (device->tailPtr < sizeof(pmsData->msgBuffer)) {
        if (device->tailPtr == 3) pmsData->byteCount = util_transInt (pmsData->msgBuffer[0], pmsData->msgBuffer[1]);
        if (device->tailPtr < pmsData->byteCount) pmsData->checksum += inChar;
        pmsData->msgBuffer[device->tailPtr++] = inChar;
        if (device->tailPtr == (pmsData->byteCount)+2) {
          uint16_t value[14];
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
            for (uint8_t a=2, b=0; a<=device->tailPtr; a+=2, b++) {
              value[b] = util_transInt (pmsData->msgBuffer[a], pmsData->msgBuffer[a+1]);
              if (a == (device->tailPtr)-2) {
                if (value[b] == pmsData->checksum) {
                  for (uint8_t c=0; c<b; c++) {
                    pmsData->lastData[c] = value[c];
                    if (value[c] != 0) pmsData->sumData[c] += value[c];
                  }
                }
                else {
                  char msgBuffer[40];
                  if (ansiTerm) displayAnsi(3);
                  consolewrite ("PMS5003 checksum failed, found ");
                  sprintf (msgBuffer, "%d expect %d", value[b], pmsData->checksum);
                  consolewriteln (msgBuffer);
                  if (ansiTerm) displayAnsi(1);
                }
              }
            }
            pmsData->count++;
            xSemaphoreGive(devTypeSem[myDevTypeID]);
            device->headPtr = 0;
          }
        }
      }
      else device->headPtr = 0;
    }

    static void nmeaHandler (uint8_t myDevTypeID, struct zebSerial_s *device, uint8_t inChar)
    {
      struct nmea_s *nmeaData;
      char localChk[3];
      char *charPtr;
      
      // setup pointer to data
      if (device->dataBuffer == NULL) {
        device->bufferSize = sizeof (struct nmea_s);
        device->dataBuffer = (uint8_t*) malloc(sizeof (struct nmea_s));
        device->tailPtr    = 0;
        device->headPtr    = 0;
        nmeaData = (struct nmea_s*) (device->dataBuffer);
        nmeaData->msgState    = false;
        nmeaData->hasLati     = false;
        nmeaData->hasLongi    = false;
        nmeaData->hasAlt      = false;
        nmeaData->hasHeading  = false;
        nmeaData->hasKnots    = false;
        nmeaData->hasKph      = false;
        nmeaData->hasDateTime = false;
        nmeaData->hasSatCount = false;
        nmeaData->hasGsv      = false;
        nmeaData->hasHorzPrecision = false;
        nmeaData->talker[0]   = '\0';
        nmeaData->message[0]  = '\0';
      }
      else nmeaData = (struct nmea_s*) (device->dataBuffer);
      // Fill Input buffer as appropriate
      // Check for end of line, and process
      if (nmeaData->msgState==2 && (inChar=='\n' || inChar=='\r')) {
        uint8_t checksum;

        nmeaData->msgState = 0;
        checksum = 0; //clear checksum
        if (device->tailPtr<sizeof(nmeaData->msgBuffer)) nmeaData->msgBuffer[device->tailPtr++] = '\0'; // terminate string
        for (uint8_t n=0; n<device->tailPtr; n++) checksum ^= nmeaData->msgBuffer[n];
        sprintf (localChk, "%02X", checksum);
        nmeaData->checkBuffer[device->headPtr++] = '\0';
        if (strcmp(localChk, nmeaData->checkBuffer) == 0) {
          char *field[NMEAMAXFIELD];
          uint8_t fieldCnt=0;

          // sprintf (vom, "tailPtr=%d", device->tailPtr);
          // consolewriteln (vom);
          for (uint8_t n; n<NMEAMAXFIELD; n++) field[n]=NULL;
          fieldCnt = 0;
          field[fieldCnt++] = &(nmeaData->msgBuffer)[2];
          for (uint8_t ptr=2; ptr<device->tailPtr && fieldCnt<NMEAMAXFIELD; ptr++) {
            if (nmeaData->msgBuffer[ptr] == ',') {
              nmeaData->msgBuffer[ptr] = '\0';
              field[fieldCnt++] = &(nmeaData->msgBuffer[ptr+1]);
            }
          }
          if (fieldCnt>2 && xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
            //consolewriteln ("set talker");
            nmeaData->talker[0] = nmeaData->msgBuffer[0];
            nmeaData->talker[1] = nmeaData->msgBuffer[1];
            nmeaData->talker[2] = '\0';
            //consolewriteln ("Check record type");            
            if (strcmp (field[0], "RMC") == 0 && strcmp (field[2],"A") == 0){
              //consolewriteln ("-->RMC");
              if (field[3]!=NULL && strlen (field[3]) > 0) {
                nmeaData->hasLati = true;
                nmeaData->lati = util_str2float (field[3]);
                if (strcmp (field[4], "S") == 0) nmeaData->lati = 0 - nmeaData->lati;
              }
              else nmeaData->hasLati = false;
              if (field[5]!=NULL && strlen (field[5]) > 0) {
                nmeaData->hasLongi = true;
                nmeaData->longi = util_str2float (field[5]);
                if (strcmp (field[6], "W") == 0) nmeaData->longi = 0 - nmeaData->longi;
              }
              else nmeaData->hasLongi = false;
              if (field[7]!=NULL && strlen (field[7]) > 0) {
                nmeaData->hasKnots = true;
                nmeaData->knots = util_str2float (field[7]);
              }
              else nmeaData->hasKnots = false;
              if (field[8]!=NULL && strlen (field[8]) > 0) {
                nmeaData->hasHeading = true;
                nmeaData->heading = util_str2float (field[8]);
              }
              else nmeaData->hasHeading = false;
            }
            else if (strcmp (field[0], "GGA") == 0 && field[6]!=NULL && strcmp(field[6], "0")!=0) {
              //consolewriteln ("-->GGA");
              if (field[2]!=NULL && strlen (field[2]) > 0) {
                nmeaData->hasLati = true;
                nmeaData->lati = util_str2float (field[2]);
                if (strcmp (field[3], "S") == 0) nmeaData->lati = 0 - nmeaData->lati;
              }
              else nmeaData->hasLati = false;
              if (field[4]!=NULL && strlen (field[4]) > 0) {
                nmeaData->hasLongi = true;
                nmeaData->longi = util_str2float (field[4]);
                if (strcmp (field[5], "W") == 0) nmeaData->longi = 0 - nmeaData->longi;
              }
              else nmeaData->hasLongi = false;
              if (field[7]!=NULL && strlen (field[7]) > 0) {
                nmeaData->satCount = util_str2int(field[7]);
                nmeaData->hasSatCount = true;
              }
              else nmeaData->hasSatCount = false;
              if (field[8]!=NULL && strlen (field[8]) > 0) {
                nmeaData->horzPrecision = util_str2float (field[8]);
                nmeaData->hasHorzPrecision = true;
              }
              else nmeaData->hasHorzPrecision = false;
              if (field[9]!=NULL && strlen (field[9]) > 0) {
                nmeaData->hasAlt = true;
                nmeaData->alt = util_str2float (field[9]);
                if (field[10]!=NULL) strncpy(nmeaData->altMeasure, field[10], 1);
                else nmeaData->altMeasure[0] = '\0';
              }
              else nmeaData->hasAlt = false;
            }
            else if (strcmp (field[0], "GLL") == 0 && field[6]!=NULL && strcmp (field[6], "A") == 0) {
              //consolewriteln ("-->GLL");
              if (field[1]!=NULL && strlen (field[1]) > 0) {
                nmeaData->hasLati = true;
                nmeaData->lati = util_str2float (field[1]);
                if (strcmp (field[2], "S") == 0) nmeaData->lati = 0 - nmeaData->lati;
              }
              else nmeaData->hasLati = false;
              if (field[3]!=NULL && strlen (field[3]) > 0) {
                nmeaData->hasLongi = true;
                nmeaData->longi = util_str2float (field[3]);
                if (strcmp (field[4], "W") == 0) nmeaData->longi = 0 - nmeaData->longi;
              }
              else nmeaData->hasLongi = false;
            }
            else if (strcmp (field[0], "GSV") == 0) {
              if (strcmp (field[2], "1") == 0) nmeaData->gsvCount = 0;
              for (uint8_t n=4; n<fieldCnt; n+=4) {
                for (uint8_t k=0; k<4; k++) {
                  if (field[n+k]!=NULL && strlen(field[n+k])>0) nmeaData->gsv[nmeaData->gsvCount][k] = util_str2int(field[n+k]);
                  else nmeaData->gsv[nmeaData->gsvCount][k] = 9999;
                }
                nmeaData->gsvCount++;
              }
              nmeaData->hasGsv = true;
            }
            //else if (fieldCnt>4 && strcmp (field[0], "TXT") == 0 && strlen (field[4]) > 0) {
              //consolewriteln ("-->TXT");
              //strncpy (nmeaData->message, field[4], sizeof(nmeaData->message));
            //}
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
        else {
          consolewrite ("NMEA data failing checksum: ");
          consolewriteln (nmeaData->msgBuffer);
        }
      }
      if (nmeaData->msgState == 2 && device->headPtr<sizeof(localChk)) {
        nmeaData->checkBuffer[device->headPtr++] = inChar;
      }
      // start of checksum message, take out of load buffer mode and into load checksum mode
      if (inChar == '*') {
        nmeaData->msgBuffer[device->tailPtr++] = '\0';
        nmeaData->msgState=2;
        device->headPtr = 0;
      }
      if (nmeaData->msgState==1 && device->tailPtr<sizeof(nmeaData->msgBuffer)) {
        nmeaData->msgBuffer[device->tailPtr++] = inChar;
      }
      // start of message, change to load buffer mode
      if (inChar=='$') {
        nmeaData->msgState = 1;
        device->tailPtr = 0;
      }
    }


    static void asciiHandler (uint8_t myDevTypeID, struct zebSerial_s *device, uint8_t inChar)
    {
      bool notFound = true;

      if (device->dataBuffer == NULL) {
        device->bufferSize = ASCII_BUF_SIZE;
        device->dataBuffer = (uint8_t*) malloc(ASCII_BUF_SIZE);
        device->dataBuffer[0] = '\0';
      }
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        // if head ptr (start of string) needs to shift to make room for tail ptr (end of string)
        if ((device->headPtr>0 && (device->headPtr)-1 == device->tailPtr) || (device->tailPtr==(ASCII_BUF_SIZE-1) && device->headPtr==0)) {
          // need to advance headPtr to make room for tail, skip to next line end
          notFound = true;
          while (notFound) {
            device->headPtr++;
            if (device->headPtr==ASCII_BUF_SIZE) device->headPtr = 0;
            if (device->dataBuffer[device->headPtr]=='\n' || device->dataBuffer[device->headPtr]=='\r') notFound=false;
          }
          // skip to end of line end data.
          notFound = true;
          while (notFound) {
            device->headPtr++;
            if (device->headPtr==ASCII_BUF_SIZE) device->headPtr = 0;
            if (device->dataBuffer[device->headPtr]!='\n' && device->dataBuffer[device->headPtr]!='\r') notFound=false;
          }
        }
        // Append data to tail
        device->dataBuffer[device->tailPtr++] = inChar;
        // check for wrap around
        if (device->tailPtr==ASCII_BUF_SIZE) device->tailPtr = 0;
        // terminate string
        device->dataBuffer[device->tailPtr] = '\0';
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

    const char* getWinsenType (uint8_t option, uint8_t devType)
    {
      static const char winLabel[2][6][20] = {{ "ch2o", "co2", "co", "o3", "ch4", "pm" }, {"Formaldehyde ", "Carbon Dioxide", "Carbon Monoxide", "Ozone", "Methane", "Particulate Matter" }};
      static char *result;
      
      switch (devType) {
        case 0x17:
          result = (char*) winLabel[option][0];
          break;
        case 0x04:
          result = (char*) winLabel[option][2];
          break;
        case 0x2a:
          result = (char*) winLabel[option][3];
          break;
        case 0x01:
          result = (char*) winLabel[option][4];
          break;
        case 0x18:
          result = (char*) winLabel[option][5];
          break;
        case 0x86:
          result = (char*) winLabel[option][1];
          break;
        case 0xff:
          result = (char*) winLabel[option][1];
          break;
        default:
          char msgBuffer[5];
          result = (char*) ("unknown");
          sprintf (msgBuffer, "0x%02X", devType);
          consolewrite   ("Unrecognised Winsen sensor type: ");
          consolewriteln (msgBuffer);
          break;
      }
      return (result);
    }

    const char* getWinsenUnits (uint8_t unitType)
    {
      static const char winLabel[][4] = { "ppb", "ppm" };
      static char *result;

      switch (unitType) {
        case 0x04:
          result = (char*) winLabel[0];
          break;
        case 0x03:
          result = (char*) winLabel[1];
          break;
        default:
          result = (char*) ("");
          break;
      }
      return (result);
    }



  public:

    char subtypeList[1][5] = { "var" };
    char subtypeLen = 1;

    zebSerial()
    {
      // Find our device type ID
      myDevTypeID = util_get_dev_type("serial");
      // if (myDevTypeID < 255) devTypeCount[myDevTypeID] = 0;
    }

    bool begin()
    {
      bool retval = false;
      char devNr;
      struct zebSerial_s *myData;
      char rx[2];
      char proto[4];
      char msgBuffer[BUFFSIZE];
      char sensorName[SENSOR_NAME_LEN]; 
      uint8_t protoPtr;

      if (myDevTypeID>250) return(false);
      for (devNr=0; devNr<2; devNr++) {
        sprintf (msgBuffer, "serial_rx_%d", devNr);
        rx[devNr] = nvs_get_int (msgBuffer, 99);
      }
      if (rx[0]<40 || rx[1]<40) {
        devData[myDevTypeID] = malloc (2 * sizeof(zebSerial_s));
        myData = (struct zebSerial_s*) devData[myDevTypeID];
        for (devNr=0; devNr<2; devNr++) {
          myData[devNr].rx = rx[devNr];
          myData[devNr].isvalid = false;
          sprintf (msgBuffer, "serialType_%d", devNr);
          nvs_get_string (msgBuffer, myData[devNr].devType, "disabled", SENSOR_NAME_LEN);
          if (rx[devNr] < 40 && strcmp (myData[devNr].devType, "disabled") != 0) {
            sprintf (msgBuffer, "serial_tx_%d", devNr);
            myData[devNr].tx = nvs_get_int (msgBuffer, 99);
            sprintf (msgBuffer, "serial_sp_%d", devNr);
            myData[devNr].baud = nvs_get_int (msgBuffer, 9600);
            sprintf (msgBuffer, "serial_pr_%d", devNr);
            nvs_get_string (msgBuffer, proto, msgBuffer, 4);
            protoPtr = 100;
            for (uint8_t n=0; protoPtr>99 && n<(sizeof(acceptProto)/4); n++) if (strcmp(acceptProto[n], proto) == 0) protoPtr = n;
            sprintf (msgBuffer, "serialName_%d", devNr);
            nvs_get_string (msgBuffer, myData[devNr].uniquename, msgBuffer, SENSOR_NAME_LEN);
            if (devNr==0 && strcmp (myData[devNr].uniquename, msgBuffer)==0) strcpy (myData[devNr].uniquename, device_name);
            myData[devNr].dataBuffer = NULL;
            myData[devNr].headPtr    = 0;
            myData[devNr].tailPtr    = 0;
            myData[devNr].state      = GREEN;
            if (myData[devNr].tx<32 && protoPtr<100) {
              serial_dev[devNr].begin(myData[devNr].baud, implementProto[protoPtr], myData[devNr].rx, myData[devNr].tx);
              myData[devNr].isvalid  = true;
              devTypeCount[myDevTypeID]++;
              for (uint8_t level=0; level<3 ; level++) {
                sprintf (sensorName, "serivar_%d%d", level, devNr);  // Warning Logic
                nvs_get_string (sensorName, msgBuffer, "disable", sizeof(msgBuffer));
                if (strcmp (msgBuffer, "disable") != 0) {
                  consolewrite ("Enable ");
                  consolewrite ((char*)alertLabel[level]);
                  consolewrite (": ");
                  consolewrite (sensorName);
                  consolewrite (" as ");
                  consolewriteln (msgBuffer);
                  util_getLogic (sensorName, &myData[devNr].alert[level]);
                  }
                else myData[devNr].alert[level] = NULL;
               }
            }
            else {
              if (ansiTerm) displayAnsi(3);
              sprintf (msgBuffer, "%s, tx=%d, rx=%d, %d,%s", devNr, myData[devNr].tx,myData[devNr].rx, myData[devNr].baud, proto);
              consolewrite ("Serial device misconfiguration, serial: ");
              consolewriteln (msgBuffer);
              if (ansiTerm) displayAnsi(0);
            }
          }
        }
        if (myData[0].isvalid || myData[1].isvalid) {
          xTaskCreate(serialSensorMaster, "zebSerial", 4096, NULL, 4, NULL);
        }
        else {
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Check serial device configuration - no valid devices found");
        }
      }
      else {
        consolewriteln ("No serial devices defined");
      }
      return (retval);
    }

  static void inventory ()
  {
    char rx, tx;
    char msgBuffer[80];
    char proto[4];
    char unitType[SENSOR_NAME_LEN];
    char unitName[SENSOR_NAME_LEN];
    uint32_t baud;

    consolewriteln ("Test: Serially connected sensors");
    for (uint8_t devNr=0; devNr<2; devNr++) {
      sprintf (msgBuffer, "serial_rx_%d", devNr);
      rx = nvs_get_int (msgBuffer, 99);
      sprintf (msgBuffer, "serialType_%d", devNr);
      nvs_get_string (msgBuffer, unitType, "disabled", SENSOR_NAME_LEN);
      if (rx > 39 || strcmp (unitType, "disabled") == 0) {
        sprintf (msgBuffer, " * serial.%d disabled", devNr);
        consolewriteln (msgBuffer);
      }
      else {
        sprintf (msgBuffer, "serial_tx_%d", devNr);
        tx = nvs_get_int (msgBuffer, 99);
        sprintf (msgBuffer, "serial_sp_%d", devNr);
        baud = nvs_get_int (msgBuffer, 9600);
        sprintf (msgBuffer, "serialName_%d", devNr);
        nvs_get_string (msgBuffer, unitName, device_name, SENSOR_NAME_LEN);
        sprintf (msgBuffer, "serial_pr_%d", devNr);
        nvs_get_string (msgBuffer, proto, "8n1", sizeof(proto));
        sprintf (msgBuffer, " * serial.%d %d,%s, rx: %d, tx: %d, type: %s, name: %s", devNr, baud, proto, rx, tx, unitType, unitName);
        consolewriteln (msgBuffer);
      }
    }
  }


  static void serialAverager()
    {
      struct zebSerial_s *myData;
      struct pms5003_s   *pmsData;
      struct winsen_s    *winsenData;
      uint8_t n;
      uint8_t myDevTypeID = 255;

      myDevTypeID = util_get_dev_type("serial");
      if (myDevTypeID!=255 && devData[myDevTypeID]!=NULL) {
        myData = (struct zebSerial_s*) (devData[myDevTypeID]);
        for (uint8_t devNr=0; devNr<2; devNr++) {
          if (myData[devNr].isvalid && (strcmp (myData[devNr].devType, "pms5003") == 0 || strcmp (myData[devNr].devType, "zh03b") == 0)) {
            pmsData = (struct pms5003_s*) myData->dataBuffer;
            if (pmsData!=NULL && xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
              if (pmsData->count>0) {
                pmsData->avgOver = pmsData->count;
                n = pmsData->count;
                pmsData->count = 0;
                for (uint8_t a=0; a<14; a++) {
                  pmsData->avgData[a] = pmsData->sumData[a] / n;
                  pmsData->sumData[a] = 0;
                }
              }
              xSemaphoreGive(devTypeSem[myDevTypeID]);
            }
          }
          else if (myData[devNr].isvalid && (strcmp (myData[devNr].devType, "winsen") == 0 || strcmp (myData[devNr].devType, "mh-z19c") == 0)) {
            winsenData = (struct winsen_s*) myData->dataBuffer;
            if (winsenData!=NULL && xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
              if (winsenData->count>0) {
                winsenData->avgOver = winsenData->count;
                winsenData->avgData = winsenData->sumData / winsenData->avgOver;
                winsenData->count   = 0;
                winsenData->sumData = 0.0;
              }
              xSemaphoreGive(devTypeSem[myDevTypeID]);
            }
          }
        }
      }
    }

    uint8_t getStatusColor(uint8_t devNr)
    {
      uint8_t retVal  = GREEN;    // Worst score for all tests of this type
      uint8_t testVal = GREEN;    // Score for the currently evaluated sensor
      struct zebSerial_s *myData;    // Pointer to data.

      myData = (struct zebSerial_s*) devData[myDevTypeID];
      if (devNr>=0 && devNr<2) {
        if (myData[devNr].isvalid) {
          testVal = 0;
          for (uint8_t innerloop=0 ; innerloop<3; innerloop++) {
            struct rpnLogic_s *alertPtr = myData[devNr].alert[innerloop];
            if (alertPtr != NULL && rpn_calc(alertPtr->count, alertPtr->term)>0) testVal = innerloop+1;
          }
          if (testVal>retVal) retVal = testVal;
          if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
            myData[devNr].state = testVal;
            xSemaphoreGive(devTypeSem[myDevTypeID]);
          }
        }
        // else retVal = CLEAR;
      }
      return (retVal);
    }
 


  void getXymonStatus (char *xydata)
    {
      struct zebSerial_s *myData;
      struct zebSerial_s *record;
      char msgBuffer[80];

      if (devData[myDevTypeID] == NULL) return;
      myData = (struct zebSerial_s*) devData[myDevTypeID];
      for (uint8_t devNr=0; devNr<2; devNr++) {
        record = &myData[devNr];
        if (myData[devNr].isvalid && record->dataBuffer != NULL) {
          record->state = getStatusColor(devNr);
          if (strcmp (record->devType, "pms5003") == 0 || strcmp (record->devType, "zh03b") == 0)  {
            struct pms5003_s *pmsData = (struct pms5003_s*) record->dataBuffer;
            sprintf (xydata, "status %s.pm25 %s pm2.5 - %s\n", device_name, xymonColour[record->state], util_gettime());
            util_getLogicTextXymon (record->alert[record->state-YELLOW], xydata, record->state, record->uniquename);
            if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
              sprintf (msgBuffer, "%16s.pm1.0 %5d ug/m3 (Avg of %d readings)\n", record->uniquename, pmsData->avgData[3], pmsData->avgOver);
              strcat  (xydata, msgBuffer);
              sprintf (msgBuffer, "%16s.pm2.5 %5d ug/m3\n", record->uniquename, pmsData->avgData[4]);
              strcat  (xydata, msgBuffer);
              sprintf (msgBuffer, "%16s.pm10  %5d ug/m3\n", record->uniquename, pmsData->avgData[5]);
              strcat  (xydata, msgBuffer);
              if (strcmp (record->devType, "pms5003") == 0) {
                strcat  (xydata, "\n");
                sprintf (msgBuffer, "%16s 0.3um %5d / 100ml\n", record->uniquename, pmsData->avgData[6]);
                strcat  (xydata, msgBuffer);
                sprintf (msgBuffer, "%16s 0.5um %5d / 100ml\n", record->uniquename, pmsData->avgData[7]);
                strcat  (xydata, msgBuffer);
                sprintf (msgBuffer, "%16s 1.0um %5d / 100ml\n", record->uniquename, pmsData->avgData[8]);
                strcat  (xydata, msgBuffer);
                sprintf (msgBuffer, "%16s 2.5um %5d / 100ml\n", record->uniquename, pmsData->avgData[9]);
                strcat  (xydata, msgBuffer);
                sprintf (msgBuffer, "%16s 5.0um %5d / 100ml\n", record->uniquename, pmsData->avgData[10]);
                strcat  (xydata, msgBuffer);
                sprintf (msgBuffer, "%16s 10um  %5d / 100ml\n", record->uniquename, pmsData->avgData[11]);
                strcat  (xydata, msgBuffer);
              }
              xSemaphoreGive(devTypeSem[myDevTypeID]);
            }
          }
          else if (strcmp (record->devType, "winsen") == 0 || strcmp (myData[devNr].devType, "mh-z19c") == 0)  {
            struct winsen_s *winsenData = (struct winsen_s*) record->dataBuffer;
            char *winsenType = NULL;
            char *winsenUnit = NULL;
            char *winsenDesc = NULL;
            
            winsenType = (char*) getWinsenType (0, winsenData->devType);
            winsenDesc = (char*) getWinsenType (1, winsenData->devType);
            winsenUnit = (char*) getWinsenUnits(winsenData->unit);
            if (winsenType == NULL) consolewriteln ("winsenType is null");
            else if (winsenUnit == NULL) consolewriteln ("winsenUnit is null");
            else {
              sprintf (xydata, "status %s.%s %s %s - %s - %s\n", device_name, winsenType, xymonColour[record->state], winsenType, winsenDesc, util_gettime());
              util_getLogicTextXymon (record->alert[record->state-YELLOW], xydata, record->state, record->uniquename);
              if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
                if (winsenData->unit == 0x00)
                     sprintf (msgBuffer, "%16s.%s %5s ppm (Avg of %d readings)\n", record->uniquename, winsenType, util_ftos((winsenData->avgData/2560), 1), winsenData->avgOver);
                else sprintf (msgBuffer, "%16s.%s %5d %s (Avg of %d readings)\n", record->uniquename, winsenType, winsenData->avgData, winsenUnit, winsenData->avgOver);
                strcat  (xydata, msgBuffer);
                sprintf (msgBuffer, "Full scale = %d %s\n", winsenData->lastData[1], winsenUnit);
                xSemaphoreGive(devTypeSem[myDevTypeID]);
              }
            }
          }
          else if (strcmp (record->devType, "ascii") == 0)  {
            sprintf (xydata, "status %s.ascii %s ascii serial - %s\n", device_name, xymonColour[record->state], util_gettime());
            util_getLogicTextXymon (record->alert[record->state-YELLOW], xydata, record->state, record->uniquename);
            if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
              if (record->headPtr < record->tailPtr) strncat (xydata, (const char*) &(record->dataBuffer[record->headPtr]), (record->tailPtr-record->headPtr)-1);
              else {
                strncat (xydata, (const char*) &(record->dataBuffer[record->headPtr]), (ASCII_BUF_SIZE-record->headPtr)-1);
                strncat (xydata, (const char*) record->dataBuffer, record->tailPtr-1);
              }
              xSemaphoreGive(devTypeSem[myDevTypeID]);
            }
          }
          else if (strcmp (record->devType, "nmea") == 0)  {
            struct nmea_s *nmeaData;

            nmeaData = (struct nmea_s*) (myData->dataBuffer);
            sprintf (xydata, "status %s.nmea %s nmea - %s\n", device_name, xymonColour[record->state], util_gettime());
            util_getLogicTextXymon (record->alert[record->state-YELLOW], xydata, record->state, record->uniquename);
            sprintf (msgBuffer, "<table>");
            strcat  (xydata, msgBuffer);
            if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
              if (strlen(nmeaData->talker)>0) {
                sprintf (msgBuffer, "<tr><td>Talker</td><td>%s", nmeaData->talker);
                strcat  (xydata, msgBuffer);
                if (strcmp (nmeaData->talker, "GP")==0) strcat (xydata, " - Global Positioning System");
                else if (strcmp (nmeaData->talker, "GL")==0) strcat (xydata, " - Glonass");
                else if (strcmp (nmeaData->talker, "GN")==0) strcat (xydata, " - Multiple satellite systems");
                else if (strcmp (nmeaData->talker, "GQ")==0) strcat (xydata, " - QZSS GPS augmented");
                else if (strcmp (nmeaData->talker, "GI")==0) strcat (xydata, " - NavIC, IRNSS (India)");
                else if (strcmp (nmeaData->talker, "BD")==0 || strcmp (nmeaData->talker, "GB")==0) strcat (xydata, " - BeiDou (China)");
                strcat  (xydata, "</td></tr>");
              }
              if (nmeaData->hasSatCount) {
                sprintf (msgBuffer, "<tr><td>Satellite Count</td><td align=\right\">%d</td></tr>", nmeaData->satCount);
                strcat  (xydata, msgBuffer);                
              }
              if (nmeaData->hasHorzPrecision) {
                sprintf (msgBuffer, "<tr><td>Horz Precision</td><td align=\right\">%s</td></tr>", util_ftos(nmeaData->horzPrecision,1));
                strcat  (xydata, msgBuffer);
              }
              if (nmeaData->hasLati) {
                sprintf (msgBuffer, "<tr><td>Latitude</td><td align=\right\">%s</td></tr>", util_ftos(nmeaData->lati,4));
                strcat  (xydata, msgBuffer);
              }
              if (nmeaData->hasLongi) {
                sprintf (msgBuffer, "<tr><td>Longitude</td><td align=\right\">%s</td></tr>", util_ftos(nmeaData->longi,4));
                strcat  (xydata, msgBuffer);
              }
              if (nmeaData->hasAlt) {
                sprintf (msgBuffer, "<tr><td>Altitude</td><td align=\right\">%s %s</td></tr>", util_ftos(nmeaData->alt,1), nmeaData->altMeasure);
                strcat  (xydata, msgBuffer);
              }
              if (nmeaData->hasHeading) {
                sprintf (msgBuffer, "<tr><td>heading</td><td align=\right\">%s</td></tr>", util_ftos(nmeaData->heading,1));
                strcat  (xydata, msgBuffer);
              }
              if (nmeaData->hasKnots) {
                sprintf (msgBuffer, "<tr><td>Speed (Knots)</td><td align=\right\">%s</td></tr>", util_ftos(nmeaData->knots,1));
                strcat  (xydata, msgBuffer);
              }
              if (nmeaData->hasKph) {
                sprintf (msgBuffer, "<tr><td>Speed (Km/h)</td><td align=\right\">%s</td></tr>", util_ftos(nmeaData->kph,1));
                strcat  (xydata, msgBuffer);
              }
              if (strlen(nmeaData->message)>0) {
                sprintf (msgBuffer, "<tr><td>Message</td><td>%s</td></tr>", nmeaData->message);
                strcat  (xydata, msgBuffer);
              }
              strcat  (xydata, "</table>");
              if (nmeaData->hasGsv) {
                strcat (xydata, "<br><table><tr><th>Satellite</th><th>Elevation</th><th>Azimuth (from North)</th><th>Signal2Noise</th></tr>");
                for (uint8_t n=0; n<nmeaData->gsvCount; n++) {
                  strcat (xydata, "<tr>");
                  for (uint8_t k=0; k<4; k++) {
                    strcat (xydata, "<td align=\"right\">");
                    if (nmeaData->gsv[n][k] == 9999) strcat (xydata, "N/A</td>");
                    else {
                      sprintf (msgBuffer, "%d</td>", nmeaData->gsv[n][k]);
                      strcat (xydata, msgBuffer);
                    }
                  }
                  strcat (xydata, "</tr>");
                }
              }
              xSemaphoreGive(devTypeSem[myDevTypeID]);
            }
          }
        }
      }
    }

    void printData()
    {
      struct zebSerial_s *myData;
      char msgBuffer[50];
      
      sprintf (msgBuffer, "serial.dev %d", devTypeCount[myDevTypeID]);
      consolewriteln (msgBuffer);
      // setup pointer to data array
      myData = (struct zebSerial_s*) devData[myDevTypeID];
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (myData[device].dataBuffer == NULL) continue;
          if (strcmp (myData[device].devType, "pms5003") == 0 || strcmp (myData[device].devType, "zh03b") == 0) {
            struct pms5003_s *pmsData = (struct pms5003_s*) myData[device].dataBuffer;
            sprintf (msgBuffer, "serial.%d.coun (%s) %d", device, myData[device].uniquename, pmsData->avgOver);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.pm1  (%s) %d ug/m3", device, myData[device].uniquename, pmsData->avgData[3]);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.las1 (%s) %d ug/m3", device, myData[device].uniquename, pmsData->lastData[3]);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.pm25 (%s) %d ug/m3", device, myData[device].uniquename, pmsData->avgData[4]);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.las2 (%s) %d ug/m3", device, myData[device].uniquename, pmsData->lastData[4]);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.pm10 (%s) %d ug/m3", device, myData[device].uniquename, pmsData->avgData[5]);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.las3 (%s) %d ug/m3", device, myData[device].uniquename, pmsData->lastData[5]);
            consolewriteln (msgBuffer);
            if (strcmp (myData[device].devType, "pms5003") == 0) {
              sprintf (msgBuffer, "serial.%d.p003 (%s) %d /100ml", device, myData[device].uniquename, pmsData->avgData[6]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.las4 (%s) %d /100ml", device, myData[device].uniquename, pmsData->lastData[6]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.p005 (%s) %d /100ml", device, myData[device].uniquename, pmsData->avgData[7]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.las5 (%s) %d /100ml", device, myData[device].uniquename, pmsData->lastData[7]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.p010 (%s) %d /100ml", device, myData[device].uniquename, pmsData->avgData[8]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.las6 (%s) %d /100ml", device, myData[device].uniquename, pmsData->lastData[8]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.p025 (%s) %d /100ml", device, myData[device].uniquename, pmsData->avgData[9]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.las7 (%s) %d /100ml", device, myData[device].uniquename, pmsData->lastData[9]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.p050 (%s) %d /100ml", device, myData[device].uniquename, pmsData->avgData[10]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.las8 (%s) %d /100ml", device, myData[device].uniquename, pmsData->lastData[10]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.p100 (%s) %d /100ml", device, myData[device].uniquename, pmsData->avgData[11]);
              consolewriteln (msgBuffer);
              sprintf (msgBuffer, "serial.%d.las9 (%s) %d /100ml", device, myData[device].uniquename, pmsData->lastData[11]);
              consolewriteln (msgBuffer);
            }
          }
          else if (strcmp (myData[device].devType, "winsen") == 0 || strcmp (myData[device].devType, "mh-z19c") == 0) {
            struct winsen_s *winsenData = (struct winsen_s*) myData[device].dataBuffer;
            sprintf (msgBuffer, "serial.%d.coun (%s) %d", device, myData[device].uniquename, winsenData->avgOver);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.val  (%s) %d", device, myData[device].uniquename, winsenData->avgData);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.lasv (%s) %d", device, myData[device].uniquename, winsenData->lastData[0]);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.max  (%s) %d", device, myData[device].uniquename, winsenData->lastData[1]);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.unit (%s) %d", device, myData[device].uniquename, winsenData->unit);
            consolewriteln (msgBuffer);
            sprintf (msgBuffer, "serial.%d.type (%s) %d", device, myData[device].uniquename, winsenData->devType);
            consolewriteln (msgBuffer);
          }
          else if (strcmp (myData[device].devType, "nmea") == 0) {
            struct nmea_s *sensData = (struct nmea_s*) myData[device].dataBuffer;
            if (sensData == NULL) break;
            if (sensData->hasAlt) {
              sprintf (msgBuffer, "serial.%d.alt  (%s) ", device, myData[device].uniquename);
              consolewrite (msgBuffer);
              consolewrite (util_ftos (sensData->alt, 1));
              consolewriteln ("m");
            }
            if (sensData->hasLati) {
              sprintf (msgBuffer, "serial.%d.lati (%s) ", device, myData[device].uniquename);
              consolewrite (msgBuffer);
              consolewriteln (util_ftos (sensData->lati, 4));
            }
            if (sensData->hasLongi) {
              sprintf (msgBuffer, "serial.%d.long (%s) ", device, myData[device].uniquename);
              consolewrite (msgBuffer);
              consolewriteln (util_ftos (sensData->longi, 4));
            }
            if (sensData->hasHeading) {
              sprintf (msgBuffer, "serial.%d.head (%s) ", device, myData[device].uniquename);
              consolewrite (msgBuffer);
              consolewriteln (util_ftos (sensData->heading, 2));
            }
            if (sensData->hasSatCount) {
              sprintf (msgBuffer, "serial.%d.satc (%s) %d", device, myData[device].uniquename, sensData->satCount);
              consolewriteln (msgBuffer);
            }
            if (sensData->hasKnots) {
              sprintf (msgBuffer, "serial.%d.knot (%s) ", device, myData[device].uniquename);
              consolewrite (msgBuffer);
              consolewriteln (util_ftos (sensData->knots, 2));
            }
            if (sensData->hasKph) {
              sprintf (msgBuffer, "serial.%d.kph  (%s) ", device, myData[device].uniquename);
              consolewrite (msgBuffer);
              consolewriteln (util_ftos (sensData->kph, 2));
            }
          }
          sprintf (msgBuffer, "serial.%d.vsta (%s) %d", device, myData[device].uniquename, myData[device].state);
          consolewriteln (msgBuffer);
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }


    float getData(uint8_t devNr, char *parameter)
    {
      struct zebSerial_s *myData;
      float retval = 0.00;

      if (strcmp(parameter,"dev") == 0) return (devTypeCount[myDevTypeID]);
      myData = (struct zebSerial_s*) devData[myDevTypeID];
      if (devNr >= devTypeCount[myDevTypeID]) return (99999.99);
      if (myData[devNr].isvalid && myData[devNr].dataBuffer != NULL && xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        if (strcmp (myData[devNr].devType, "pms5003") == 0 || strcmp (myData[devNr].devType, "zh03b") == 0) {
          struct pms5003_s *pmsData = (struct pms5003_s*) myData[devNr].dataBuffer;
          if      (strcmp (parameter,"coun") == 0) retval = pmsData->avgOver;
          else if (strcmp (parameter,"pm1")  == 0) retval = pmsData->avgData[3];
          else if (strcmp (parameter,"pm25") == 0) retval = pmsData->avgData[4];
          else if (strcmp (parameter,"pm10") == 0) retval = pmsData->avgData[5];
          else if (strcmp (parameter,"p003") == 0) retval = pmsData->avgData[6];
          else if (strcmp (parameter,"p005") == 0) retval = pmsData->avgData[7];
          else if (strcmp (parameter,"p010") == 0) retval = pmsData->avgData[8];
          else if (strcmp (parameter,"p025") == 0) retval = pmsData->avgData[9];
          else if (strcmp (parameter,"p050") == 0) retval = pmsData->avgData[10];
          else if (strcmp (parameter,"p100") == 0) retval = pmsData->avgData[11];
          else if (strcmp (parameter,"las1") == 0) retval = pmsData->lastData[3];
          else if (strcmp (parameter,"las2") == 0) retval = pmsData->lastData[4];
          else if (strcmp (parameter,"las3") == 0) retval = pmsData->lastData[5];
          else if (strcmp (parameter,"las4") == 0) retval = pmsData->lastData[6];
          else if (strcmp (parameter,"las5") == 0) retval = pmsData->lastData[7];
          else if (strcmp (parameter,"las6") == 0) retval = pmsData->lastData[8];
          else if (strcmp (parameter,"las7") == 0) retval = pmsData->lastData[9];
          else if (strcmp (parameter,"las8") == 0) retval = pmsData->lastData[10];
          else if (strcmp (parameter,"las9") == 0) retval = pmsData->lastData[11];
        }
        else if (strcmp (myData[devNr].devType, "nmea") == 0) {
          struct nmea_s *sensData = (struct nmea_s*) myData[devNr].dataBuffer;
          if (     sensData->hasAlt      && strcmp(parameter,"alt")  == 0) retval = sensData->alt;
          else if (sensData->hasLongi    && strcmp(parameter,"long") == 0) retval = sensData->longi;
          else if (sensData->hasLati     && strcmp(parameter,"lati") == 0) retval = sensData->lati;
          else if (sensData->hasHeading  && strcmp(parameter,"head") == 0) retval = sensData->heading;
          else if (sensData->hasSatCount && strcmp(parameter,"satc") == 0) retval = sensData->satCount;
          else if (sensData->hasKnots    && strcmp(parameter,"knot") == 0) retval = sensData->knots;
          else if (sensData->hasKph      && strcmp(parameter,"kph")  == 0) retval = sensData->kph;
          else if (sensData->hasHorzPrecision && strcmp(parameter,"horz") == 0) retval = sensData->horzPrecision;
          else retval = 0.00;
        }
        else if (strcmp (myData[devNr].devType, "winsen") == 0 || strcmp (myData[devNr].devType, "mh-z19c") == 0) {
          struct winsen_s *winsenData = (struct winsen_s*) myData[devNr].dataBuffer;
          if      (strcmp (parameter,"coun") == 0) retval = winsenData->avgOver;
          else if (strcmp (parameter,"val")  == 0) retval = winsenData->avgData;
          else if (strcmp (parameter,"lasv") == 0) retval = winsenData->lastData[0];
          else if (strcmp (parameter,"max")  == 0) retval = winsenData->lastData[1];
          else if (strcmp (parameter,"unit") == 0) retval = winsenData->unit;
          else if (strcmp (parameter,"type") == 0) retval = winsenData->devType;
        }
        else retval=0.00;
        if (strcmp(parameter,"vsta") == 0) retval = myData[devNr].state;
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
      return (retval);
    }

    void getXymonStats (char *xydata)
    {
      struct zebSerial_s *myData;
      char msgBuffer[50];

      myData = (struct zebSerial_s*) devData[myDevTypeID];
      // setup pointer to data array
      if (xSemaphoreTake(devTypeSem[myDevTypeID], pdMS_TO_TICKS(290000)) == pdTRUE) {
        for (int device=0; device<devTypeCount[myDevTypeID]; device++) {
          if (!myData[device].isvalid) break;
          if (strcmp (myData[device].devType, "pms5003") == 0 || strcmp (myData[device].devType, "zh03b") == 0) {
            struct pms5003_s *pmsData = (struct pms5003_s*) myData[device].dataBuffer;
            sprintf (msgBuffer, "[pm25.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:pm010:GAUGE:600:U:U %d\n", pmsData->avgData[3]);
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:pm025:GAUGE:600:U:U %d\n", pmsData->avgData[4]);
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:pm100:GAUGE:600:U:U %d\n", pmsData->avgData[5]);
            strcat  (xydata, msgBuffer);
            if (strcmp (myData[device].devType, "pms5003") == 0) {
              sprintf (msgBuffer, "[particle.%s.rrd]\n", myData[device].uniquename);
              strcat  (xydata, msgBuffer);
              sprintf (msgBuffer, "DS:part003:GAUGE:600:U:U %d\n", pmsData->avgData[6]);
              strcat  (xydata, msgBuffer);
              sprintf (msgBuffer, "DS:part005:GAUGE:600:U:U %d\n", pmsData->avgData[7]);
              strcat  (xydata, msgBuffer);
              sprintf (msgBuffer, "DS:part010:GAUGE:600:U:U %d\n", pmsData->avgData[8]);
              strcat  (xydata, msgBuffer);
              sprintf (msgBuffer, "DS:part025:GAUGE:600:U:U %d\n", pmsData->avgData[9]);
              strcat  (xydata, msgBuffer);
              sprintf (msgBuffer, "DS:part050:GAUGE:600:U:U %d\n", pmsData->avgData[10]);
              strcat  (xydata, msgBuffer);
              sprintf (msgBuffer, "DS:part100:GAUGE:600:U:U %d\n", pmsData->avgData[11]);
              strcat  (xydata, msgBuffer);
            }
          }
          else if (strcmp (myData[device].devType, "winsen") == 0 || strcmp (myData[device].devType, "mh-z19c") == 0) {
            struct winsen_s *sensData = (struct winsen_s*) myData[device].dataBuffer;
            if (getWinsenType(0, sensData->devType) != NULL) {
              sprintf (msgBuffer, "[%s.%s.rrd]\n", (char*) getWinsenType(0, sensData->devType), myData[device].uniquename);
              strcat  (xydata, msgBuffer);
              if (sensData->unit==0) sprintf (msgBuffer, "DS:%s:GAUGE:600:U:U %s\n", "var", util_ftos ((sensData->avgData/2560),1));
              else sprintf (msgBuffer, "DS:%s:GAUGE:600:U:U %d\n", getWinsenType(0, sensData->devType), sensData->avgData);
              strcat  (xydata, msgBuffer);
            }
          }
          else if (strcmp (myData[device].devType, "nmea") == 0) {
            struct nmea_s *sensData = (struct nmea_s*) myData[device].dataBuffer;
            sprintf (msgBuffer, "[nmea.%s.rrd]\n", myData[device].uniquename);
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:latitude:GAUGE:600:U:U %s\n", util_ftos (sensData->lati,4));
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:longitude:GAUGE:600:U:U %s\n", util_ftos (sensData->longi,4));
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:altitude:GAUGE:600:U:U %s\n", util_ftos (sensData->alt,1));
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:heading:GAUGE:600:U:U %s\n", util_ftos (sensData->heading,1));
            strcat  (xydata, msgBuffer);
            sprintf (msgBuffer, "DS:satellites:GAUGE:600:U:U %d\n", sensData->satCount);
            strcat  (xydata, msgBuffer);
          }
        }
        xSemaphoreGive(devTypeSem[myDevTypeID]);
      }
    }

};

zebSerial the_serial;
