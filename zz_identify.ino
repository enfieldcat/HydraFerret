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


/*
 * Control loop for identify flash - place here to separate logic away from all other timing
 */
#define IDFLASH_INTERVAL 500

static TimerHandle_t idFlashTimer = NULL;
static QueueHandle_t idFlashQueue = NULL;

static void idFlashTimerHandler (TimerHandle_t xTimer)
{
static uint8_t tint = 0;

xQueueSend (idFlashQueue, &tint, 0);
}

void idFlashCycle (void *pvParameters)
// This is the task to update ID flash.
{
  char loopCounter = 0;
  char lastState = 99;
  uint8_t divisor;
  uint8_t queueResult;
  int hib_start = 0;
  int hib_end = 0;
  char msgBuffer[40];
  struct tm timeinfo;
  int sleep_time = 0;
  int timenow;


  /*
   * Configure hibernation start and end.
   */
  nvs_get_string ("hib_start", msgBuffer, "00:00", 6);
  msgBuffer[2] = msgBuffer[3];
  msgBuffer[3] = msgBuffer[4];
  msgBuffer[4] = '\0';
  hib_start = util_str2int(msgBuffer);
  hib_start = ((hib_start/100)*60) + (hib_start%100); // convert to minute of day format
  nvs_get_string ("hib_end", msgBuffer, "00:00", 6);
  msgBuffer[2] = msgBuffer[3];
  msgBuffer[3] = msgBuffer[4];
  msgBuffer[4] = '\0';
  hib_end = util_str2int(msgBuffer);
  hib_end = ((hib_end/100)*60) + (hib_end%100); // convert to minute of day format

  /*
   * Run identifying flash and check for hibernation
   */
  if (mt_identity_pin == -1) mt_identity_pin = 99;
  if (idFlashQueue == NULL) {
    idFlashQueue = xQueueCreate (1, sizeof(uint8_t));
    xQueueSend (idFlashQueue, &divisor, 0);
  }
  if (idFlashTimer == NULL) {
    idFlashTimer = xTimerCreate ("idFlashTimer", pdMS_TO_TICKS(IDFLASH_INTERVAL), pdTRUE, (void *) NULL, idFlashTimerHandler);
  }
  if (idFlashQueue == NULL || idFlashTimer == NULL) {
    if (ansiTerm) displayAnsi(3);
    consolewriteln ("Failed to create timer for id flash cycle");
    if (ansiTerm) displayAnsi(1);
  }
  else  {
    xTimerStart (idFlashTimer, pdMS_TO_TICKS(IDFLASH_INTERVAL));
    while (true) {
      if (xQueueReceive(idFlashQueue, &queueResult, pdMS_TO_TICKS(IDFLASH_INTERVAL + 1000)) != pdPASS) {
        if (ansiTerm) displayAnsi(3);
        consolewriteln ("Missing monitor cycle timing interrupt");
        if (ansiTerm) displayAnsi(1);
      }
      if (mt_identity_pin != 99) {
        loopCounter++;
        switch (mt_identity_state) {
          case ID_OFF:
          case ID_ON:
            divisor = 8;
            break;
          case ID_FFLASH: // 1 second cycle
            divisor = 1;
            break;
          case ID_FLASH:  // 2 second cycle
            divisor = 2;
            break;
          case ID_SFLASH: // 4 second cycle
            divisor = 4;
            break;
        }
        if ((loopCounter % divisor) == 0 || mt_identity_state != lastState) {
          loopCounter = 0;
          mt_set_identify_output();
        }
        lastState = mt_identity_state;
      }
      /*
       * Check if we are into hibernation time...
       */
      if (hib_start != hib_end) {
        if(!getLocalTime(&timeinfo)){
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("could not get localtime value to test for hibernation");
          if (ansiTerm) displayAnsi(1);
        }
        else {
          sleep_time = 0;
          timenow = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
          if (hib_start < timenow && timenow < hib_end) sleep_time = hib_end - timenow;
          if (hib_start > hib_end && (hib_start < timenow || timenow < hib_end)) {
            if (timenow < hib_end )sleep_time = hib_end - timenow;
            else sleep_time = (hib_end + (24*60)) - timenow;
          }
          if (sleep_time > 0) {
            sprintf (msgBuffer, "Go to sleep for %d minutes", sleep_time);
            if (ansiTerm) displayAnsi(3);
            consolewriteln (msgBuffer);
            if (ansiTerm) displayAnsi(0);
            for (uint8_t killID=0; killID<MAX_TELNET_CLIENTS ; killID++) {
              if (telnetServerClients[killID]) {
                telnetServerClients[killID].stop();
              }
            }
            util_start_deep_sleep(sleep_time);
          }
        }
      }
    }
  }
  vTaskDelete( NULL );
}
