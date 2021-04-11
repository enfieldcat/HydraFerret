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


static TimerHandle_t monitorTimer = NULL;
static QueueHandle_t monitorQueue = NULL;
static HTTPClient *monitorHttp = NULL;
static uint8_t *httpBuffer = NULL;

/**************************************************************************\
*
* To avoid doing I/O in a timer interrupt, just let the timer signal the
* monitor loop using a queue.
*
\**************************************************************************/
static void monitorTimerHandler (TimerHandle_t xTimer)
{
static int tint = 0;

// xQueueSend (QHandle, Items_to_send, ticks_to_wait)
xQueueSend (monitorQueue, &tint, 0);
}

void monitorCycle (void *pvParameters)
// This is the task to update monitoring displays.
{
  (void) pvParameters;
  int queueData;
  char *xydata;
  char status_value[8];
  char msgBuffer[40];
  char combineName[18];
  int8_t statusIndex, tStatusIndex;
  int loop_count = 0;
  int reportDev;

  consolewriteln ("Scanning for supported devices...");
  util_start_devices();
  delay (750);
  /*
   * set up timers and queues
   */
  if (monitorQueue == NULL) {
    monitorQueue = xQueueCreate (1, sizeof(int));
    xQueueSend (monitorQueue, &queueData, 0);
  }
  if (monitorTimer == NULL) {
    monitorTimer = xTimerCreate ("monitorTimer", pdMS_TO_TICKS(MONITOR_INTERVAL), pdTRUE, (void *) 0, monitorTimerHandler);
  }
  if (monitorQueue == NULL || monitorTimer == NULL) consolewriteln ("Failed to create timer for monitor cycle");
  else  {
    xTimerStart (monitorTimer, pdMS_TO_TICKS(MONITOR_INTERVAL));
    while (true) {
      if (xQueueReceive(monitorQueue, &queueData, pdMS_TO_TICKS(MONITOR_INTERVAL + 5000)) != pdPASS) {
        consolewriteln ("Missing monitor cycle timing interrupt");
      }
      /*
       * Check if alerts are to include rpn logic when triggered
       */
      if (nvs_get_int ("showlogic", 0) == 0) showLogic = 0;
      else showLogic = 1;
      /*
       * Create buffer for xymon packets and then transmit
       */
      // consolewriteln ("status update time...");
      xydata = (char*) malloc (MAX_XYMON_DATA);
      if (xydata != NULL) {
        networkUserCount++;
        if (!net_connect()) consolewriteln (" * Failed to connect to network"); 
        else {
          // deviceCollector (xydata);
          // sendPacket (xydata);
          // where devices produce unique data...
          if (loop_count > 0) {
            the_serial.serialAverager();   // some devices may need to have collections synched to reporting cycles.
            for (int reportDev=0; reportDev<numberOfTypes; reportDev++) {
              // sprintf (msgBuffer, "%d - %s (%d)", devTypeCount[reportDev], devType[reportDev], reportDev);
              // consolewriteln (msgBuffer);
              if (devTypeCount[reportDev] > 0) {
                if (strcmp (devType[reportDev], "counter") == 0) {
                  theCounter.getStatusColor();
                  while (theCounter.getXymonStatus(xydata)) sendPacket(xydata);  
                }
                else if (strcmp (devType[reportDev], "adc") == 0) {
                  the_adc.getStatusColor();
                  while (the_adc.getXymonStatus(xydata)) sendPacket(xydata);  
                }
                else if (showOutput && strcmp (devType[reportDev], "output") == 0) {
                  the_output.getXymonStatus(xydata);
                  sendPacket(xydata);  
                }
                else if (strcmp (devType[reportDev], "serial") == 0) {
                  the_serial.getXymonStatus(xydata);
                  sendPacket(xydata);  
                }
              }
            }
            // Where multiple sensor type can produce the same metric...
            // 1. Assess the alert of greatest urgency to use as the page alert
            // 2. Compile all sensors data of multiple types on one page for that test
            //
            for (int metric=TEMP; metric<=WATT; metric++) {
              if (metricCount[metric] > 0) {
                // Find greatest alert level for the metric
                statusIndex = 0;
                for (reportDev=0; reportDev<numberOfTypes; reportDev++) {
                  if (devTypeCount[reportDev] > 0) {
                    if (metric == UV                                               && strcmp (devType[reportDev], "veml6075") == 0) {
                      tStatusIndex = the_veml6075.getStatusColor();
                    }
                    else if (metric == LUX                                         && strcmp (devType[reportDev], "bh1750") == 0) {
                      tStatusIndex = the_bh1750.getStatusColor(); 
                    }
                    else if ((metric == TEMP || metric == HUMID || metric == PRES) && strcmp (devType[reportDev], "bme280") == 0) {
                      tStatusIndex = the_bme280.getStatusColor(metric);
                    }
                    else if ((metric == TEMP || metric == HUMID)                   && strcmp (devType[reportDev], "hdc1080") == 0) {
                      tStatusIndex = the_hdc1080.getStatusColor(metric);
                    }
                    else if ((metric == CO2 || metric == TVOC)                     && strcmp (devType[reportDev], "css811") == 0) {
                      tStatusIndex = the_css811.getStatusColor(metric);
                    }
                    else if ((metric == VOLT || metric == AMP || metric == WATT)   && strcmp (devType[reportDev], "ina2xx")   == 0) {
                      tStatusIndex = the_ina2xx.getStatusColor(metric);
                    }
                    else if ((metric == TEMP)                                      && strcmp (devType[reportDev], "ds1820") == 0) {
                      tStatusIndex = the_wire.getStatusColor();
                    }
                    else tStatusIndex = 0;
                    if (tStatusIndex > statusIndex) statusIndex = tStatusIndex;
                  }
                }
                strcpy (status_value, xymonColour[statusIndex]);
                // Compile status page for the metric
                sprintf (xydata, "status %s.%s %s %s - %s\n\n", device_name, metricName[metric], status_value, metricName[metric], util_gettime());
                for (reportDev=0; reportDev<numberOfTypes; reportDev++) {
                  if (devTypeCount[reportDev] > 0) {
                    if ((metric==TEMP || metric==HUMID || metric==PRES)    && strcmp (devType[reportDev], "bme280")   == 0) the_bme280.getXymonStatus (xydata, metric);
                    else if ((metric==TEMP || metric==HUMID)               && strcmp (devType[reportDev], "hdc1080")  == 0) the_hdc1080.getXymonStatus (xydata, metric);
                    else if ( metric==UV                                   && strcmp (devType[reportDev], "veml6075") == 0) the_veml6075.getXymonStatus (xydata);
                    else if ( metric==LUX                                  && strcmp (devType[reportDev], "bh1750")   == 0) the_bh1750.getXymonStatus (xydata);
                    else if ((metric==CO2  || metric==TVOC)                && strcmp (devType[reportDev], "css811")   == 0) the_css811.getXymonStatus (xydata, metric);
                    else if ((metric==VOLT || metric==AMP || metric==WATT) && strcmp (devType[reportDev], "ina2xx")   == 0) the_ina2xx.getXymonStatus (xydata, metric);
                    else if ( metric==TEMP                                 && strcmp (devType[reportDev], "ds1820")   == 0) the_wire.getXymonStatus (xydata);
                  }
                }
                // Custom footer for UV measurement
                if (metric == UV) {
                  strcat (xydata, (const char*) "\nUV-Index\n--------\n&green 0-2: No danger to average person, wear hat and sunglasses\n");
                  strcat (xydata, (const char*) "&yellow 3-5: Little risk of harm, wear hat and sunglasses, SPF 15+\n");
                  strcat (xydata, (const char*) "&red 6-7: High risk of harm, wear hat, sunglasses, cover body SPF 30+\n");
                  strcat (xydata, (const char*) "&red 8-10: Very high risk of harm, wear hat, sunglasses, cover body SPF 30+, avoid sun\n");
                  strcat (xydata, (const char*) "&purple 11+: Extreme risk of harm, stay indoors, avoid sun if possible\n");
                }
                sendPacket (xydata);
              }
            }
          }
          if (showMemory) {
            strcpy (status_value, xymonColour[0]);
            if ((esp_timer_get_time() / uS_TO_S_FACTOR) <= 900) strcpy (status_value, xymonColour[1]);
            sprintf (xydata, "status %s.memory %s Memory and system stats - %s\n\n", device_name, status_value, util_gettime());
            buildMemoryPacket(xydata);
            sendPacket (xydata);
          }
          // Collect all trends in one packet
          xydata[0] = '\0';
          if (loop_count > 0) {
            for (reportDev=0; reportDev<numberOfTypes; reportDev++) {
              if (devTypeCount[reportDev] > 0) {
                if      (strcmp (devType[reportDev], "counter")  == 0) theCounter.getXymonStats(xydata);
                else if (strcmp (devType[reportDev], "bme280")   == 0) the_bme280.getXymonStats(xydata);
                else if (strcmp (devType[reportDev], "ds1820")   == 0) the_wire.getXymonStats(xydata);
                else if (strcmp (devType[reportDev], "css811")   == 0) the_css811.getXymonStats(xydata);
                else if (strcmp (devType[reportDev], "hdc1080")  == 0) the_hdc1080.getXymonStats(xydata);
                else if (strcmp (devType[reportDev], "veml6075") == 0) the_veml6075.getXymonStats(xydata);
                else if (strcmp (devType[reportDev], "bh1750")   == 0) the_bh1750.getXymonStats(xydata);
                else if (strcmp (devType[reportDev], "ina2xx")   == 0) the_ina2xx.getXymonStats(xydata);
                else if (strcmp (devType[reportDev], "serial")   == 0) the_serial.getXymonStats(xydata);
                else if (strcmp (devType[reportDev], "adc")      == 0) the_adc.getXymonStats(xydata);
                else if (showOutput && strcmp (devType[reportDev], "output") == 0) the_output.getXymonStats(xydata);
              }
            }
          }
          if (showMemory) memtrend (xydata);
          sprintf (msgBuffer, "data %s.trends\n", device_name);
          sendPacket (msgBuffer, xydata);
          nvs_get_string ("combineName", combineName, "none", 17);
          if (strcmp(combineName, "none") != 0) {
            sprintf (msgBuffer, "data %s.trends\n", combineName);
            sendPacket (msgBuffer, xydata);
          }
        }
        endMonitorHttp();
        free (xydata);
      } else consolewriteln ("Unable to allocate memory to assemble data packet(s)");
      networkUserCount--;
      net_disconnect();
      loop_count++;
    }
  }
  vTaskDelete( NULL );
}

void buildMemoryPacket(char* xydata)
{
  int64_t uptime;
  int days, hours, mins;
  char msgBuffer[80];
  uint32_t heapSize, freeHeap, minFree, percFree;

  heapSize = ESP.getHeapSize();
  freeHeap = ESP.getFreeHeap();
  minFree  = ESP.getMinFreeHeap();
  percFree = 100 - ((freeHeap*100)/heapSize);
  uptime = esp_timer_get_time() / uS_TO_S_FACTOR;

  sprintf (msgBuffer, "   %-16s %17s\n", "Statistic", "Value");
  strcat  (xydata, msgBuffer);
  if (uptime <= 900) strcat (xydata, "&yellow ");
  else strcat (xydata, "&green ");
  days = uptime / (60*60*24);
  uptime = uptime - (days * (60*60*24));
  hours = uptime / (60*60);
  uptime = uptime - (hours *(60*60));
  mins = uptime / 60;
  sprintf (msgBuffer, "%-16s %5d days, %02d:%02d\n", "Uptime", days, hours, mins);
  strcat  (xydata, msgBuffer);
  sprintf (msgBuffer, "&clear %-16s %17s (telnet ", "IP Address", net_ip2str((uint32_t) WiFi.localIP()));
  strcat  (xydata, msgBuffer);
  if (telnetRunning) strcat (xydata, "en");
  else strcat (xydata, "dis");
  strcat  (xydata, "abled)\n");
  sprintf (msgBuffer, "&clear %-16s %17d\n", "Heap Size", heapSize);
  strcat  (xydata, msgBuffer);
  if (percFree >= 95) strcat (xydata, "&red ");
  else if (percFree >= 90) strcat (xydata, "&yellow ");
  else strcat (xydata, "&green ");
  sprintf (msgBuffer, "%-16s %17d (%d%% used)\n", "Free Heap", freeHeap, percFree);
  strcat  (xydata, msgBuffer);
  percFree = 100 - ((minFree*100)/heapSize);
  if (percFree >= 95) strcat (xydata, "&red ");
  else if (percFree >= 90) strcat (xydata, "&yellow ");
  else strcat (xydata, "&green ");
  sprintf (msgBuffer, "%-16s %17d (%d%% used)\n", "Min Free Heap", minFree, percFree);
  strcat  (xydata, msgBuffer);
  percFree = ESP.getCpuFreqMHz();
  if (percFree < 100) strcat (xydata, "&green ");
  else if (percFree < 200) strcat (xydata, "&yellow");
  else strcat (xydata, "&red ");
  sprintf (msgBuffer, "%-16s %14dMHz (Crystal freq: %dMHz)\n", "CPU Frequency", percFree, getXtalFrequencyMhz());
  strcat  (xydata, msgBuffer);
  sprintf (msgBuffer, "&clear %-16s %s-%s (%s)\n", "Version", PROJECT_NAME, VERSION, __DATE__);
  strcat  (xydata, msgBuffer);
}

void sendPacket (char* xydata)
{
  sendPacket (NULL, xydata);
/*
  WiFiClient client;
  char xyserver[80];
  int xyport;

  nvs_get_string ("xyserver", xyserver, "", sizeof(xyserver));
  if (strlen(xyserver) > 0 && strcmp(xyserver, "none") != 0) {
    xyport = nvs_get_int ("xyport", 1984);
    if (client.connect (xyserver, xyport)) {
      client.print (xydata);
      client.flush();
      client.stop();
    }
  }
*/
}

void sendPacket (char* header, char* xydata)
{
  static char *xymonCert    = NULL;
  static char xyserver[80]  = "";
  static char xysecret[80]  = "";
  static char boundary[40]  = "";
  static char boundary2[80] = "";
  static int  xymode = 9;
  static int  xyport = 0;

  //
  // Set up parameters if not already set
  //
  if (xyserver[0]=='\0') nvs_get_string ("xyserver", xyserver, "", sizeof(xyserver));
  if (xysecret[0]=='\0') nvs_get_string ("xysecret", xysecret, "none", sizeof(xysecret));
  if (xymode == 9) {
    if (strncmp (xyserver, "https://", 8) == 0) xymode = 1;
    else if (strncmp (xyserver, "http://", 7) == 0) xymode = 2;
    else xymode = 0;
    }
  if (xyport == 0) xyport = nvs_get_int ("xyport", 1984);
  //
  // Only send data if xyserver is defined
  //
  if (strlen(xyserver) > 0 && strcmp(xyserver, "none") != 0) {
    //
    // Encapsulate as http POST
    // see: https://github.com/espressif/arduino-esp32/blob/master/libraries/HTTPClient/src/HTTPClient.h
    //
    if (xymode!=0) {
      if (boundary[0] =='\0') {
        strcpy (boundary, "boundary_");
        strcat (boundary, PROJECT_NAME);
        strcat (boundary, VERSION);
        strcpy (boundary2, "multipart/form-data; boundary=");
        strcat (boundary2, boundary);
        }
      if (httpBuffer == NULL) {
        httpBuffer = (uint8_t*) malloc (MAX_XYMON_DATA + 1024);
        }
      if (monitorHttp == NULL) {
        monitorHttp = new HTTPClient;
        if (xymode==1) {
          if (xymonCert==NULL) {
            char xym_certFile[42];
            nvs_get_string ("xym_certFile", xym_certFile, CERTFILE, sizeof(xym_certFile));
            xymonCert = util_loadFile(SPIFFS, xym_certFile);
            }
          monitorHttp->begin (xyserver, xymonCert);
          }
        else monitorHttp->begin (xyserver);
        monitorHttp->setUserAgent(util_getAgent());
        }
      monitorHttp->setReuse(true);
      monitorHttp->addHeader("Content-Type", boundary2);
      // create payload to send
      strcpy ((char *) httpBuffer, "--");
      strcat ((char *) httpBuffer, boundary);
      strcat ((char *) httpBuffer, "\r\n");
      strcat ((char *) httpBuffer, "Content-Disposition: form-data; name='xymondata'\r\n\r\n");
      if (header!=NULL) strcat ((char *) httpBuffer, header);
      strcat ((char *) httpBuffer, xydata);
      if (xysecret[0] != '\0' && strcmp (xysecret, "none") != 0) {
        strcat ((char *) httpBuffer, "\r\n--");
        strcat ((char *) httpBuffer, boundary);
        strcat ((char *) httpBuffer, "\r\n");
        strcat ((char *) httpBuffer, "Content-Disposition: form-data; name='xysecret'\r\n\r\n");
        strcat ((char *) httpBuffer, xysecret);
      }
      strcat ((char *) httpBuffer, "\r\n--");
      strcat ((char *) httpBuffer, boundary);
      strcat ((char *) httpBuffer, "--\r\n");
      // send and check result
      int statusCode = monitorHttp->POST((uint8_t*) httpBuffer, strlen((char *) httpBuffer));
      // client.end();
      if (statusCode != 200) {
        char message[5];
        sprintf (message, "%d", statusCode);
        consolewrite (xyserver);
        consolewrite (" returned error ");
        consolewriteln (message);
      }
    }
    //
    // Just a standard xymon packet
    //
    else {
      WiFiClient client;
      if (client.connect (xyserver, xyport)) {
        if (header!=NULL) client.print (header);
        client.print (xydata);
        client.flush();
        client.stop();
      }
    }
  }
}

/* void deviceCollector (char* xydata)
{
  char msgBuffer[80];
  
  sprintf (xydata, "client %s %s\n", device_name, PROJECT_NAME);
  sprintf (msgBuffer, "[date]\n%s\n", util_gettime());
  strcat  (xydata, msgBuffer);
  sprintf (msgBuffer, "[uname]\n%s %s %s esp32-%d_%dMHz\n", PROJECT_NAME, device_name, VERSION, ESP.getChipRevision(), ESP.getCpuFreqMHz());
  strcat  (xydata, msgBuffer);
  sprintf (msgBuffer, "[clientversion]\n%s %s (Compile date: %s $s)\n", PROJECT_NAME, VERSION, __DATE__, __TIME__);
  strcat  (xydata, msgBuffer);
} */

void endMonitorHttp ()
{
if (monitorHttp != NULL) {
  monitorHttp->end();
  monitorHttp->~HTTPClient();
  free (monitorHttp);
  monitorHttp = NULL;
  }
if (httpBuffer != NULL) {
  free (httpBuffer);
  httpBuffer = NULL;
  }
}

void memtrend (char* xydata)
{
  double memused;
  int totalmem = ESP.getHeapSize();
  int mem = ESP.getFreeHeap();
  int minMem = ESP.getMinFreeHeap();

  if (minMem <= mem) {
    memused = ((double)(totalmem - mem) * 100.00) / (double) totalmem;
    strcat  (xydata, "[memory.heap.rrd]\nDS:realmempct:GAUGE:600:0:U ");
    strcat  (xydata, util_dtos (memused, 2));
    strcat  (xydata, "\n[memory.maxheap.rrd]\nDS:realmempct:GAUGE:600:0:U ");
    memused = ((double)(totalmem - minMem) * 100.00) / (double) totalmem;
    strcat  (xydata, util_dtos (memused, 2));
    strcat  (xydata, "\n");
  }
}
