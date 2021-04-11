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
 * ota_control
 * 
 * Decription: routine to perform ota updates
 * 
 */
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>

#define OTA_BUFFER_SIZE 4098

class ota_control {
  
  private:

    uint64_t sequence = 0;
    uint64_t newSequence = 0;
    char message[120];
    char image_name[80];
    char chksum[65];
    uint32_t image_size;
    HTTPClient *http = NULL;
    
    /*
     * Get and put sequence of last OTA update
     * typically this would just be a time sequence number (seconds since 01-Jan-1970)
     */
    uint64_t get_sequence_id() {  
      Preferences otaprefs;

      otaprefs.begin ("otaprefs");
      sequence = otaprefs.getULong64 ("sequence", 0);
      otaprefs.end();
      return (sequence);
    }

    void put_sequence_id(uint64_t id)
    {
      sequence = id; // should not be required if we restart after update
      Preferences otaprefs;

      otaprefs.begin ("otaprefs");
      otaprefs.putULong64 ("sequence", id);
      otaprefs.end();
      return;
    }

    /*
     * Open a stream to read the http/s data
     */
    WiFiClient* getHttpStream (char *url, const char *cert)
    {
      WiFiClient *retVal = NULL;
      int httpCode;
      
      http = new HTTPClient;
      if (strncmp (url, "https://", 8) == 0 && cert != NULL) {
        http->begin (url, cert);
      }
      else {
        http->begin (url);
      }
      httpCode = http->GET();
      if (httpCode == HTTP_CODE_OK) {
        retVal = http->getStreamPtr();
      }
      else if (httpCode<0) {
        sprintf (message, "Error connecting to OTA server: %d on %s", httpCode, url);
      }
      return (retVal);
    }

    /*
     * Close http stream once done
     */
    void closeHttpStream()
    {
      if (http!= NULL) {
        http->end();
        http->~HTTPClient();
        http = NULL;
      }
    }

    /*
     * Read metadata about the update
     */
    bool get_meta_data(char *url, const char *cert)
    {
      bool retVal = false;
      int32_t inByte;
      char inPtr;
      char inBuffer[80];
      WiFiClient *inStream = getHttpStream(url, cert);
      if (inStream != NULL) {
        inPtr = 0;
         // process the character stream from the http/s source
        while ((inByte = inStream->read()) >= 0) {
          if (inPtr < sizeof(inBuffer)){
            if (inByte=='\n' || inByte=='\r') {
              inBuffer[inPtr] = '\0';
              inPtr = 0;
              processParam (inBuffer);
            }
            else inBuffer[inPtr++] = (char) inByte;
          } else {
            strcpy (message, "Line too long: ");
            if ((strlen(message)+strlen(url)) < sizeof(message)) strcat (message, url);
          }
          if ((inPtr%20) == 0) delay (20); //play nice in multithreading environment
        }
        inBuffer[inPtr] = '\0';
        processParam (inBuffer);
        closeHttpStream();
        // Test for things to be set which should be set
        retVal = true;
        if (image_size == 0) {
          strcpy (message, "No image size specified in metadata");
          retVal = false;
        }
        else if (image_size > get_next_partition_size()) {
          strcpy (message, "Image size exceeds available OTA partition size");
          retVal = false;
        }
        if (newSequence == 0) {
          strcpy (message, "Missing sequence number in metadata");
          retVal = false;
        }
        else if (sequence >= newSequence) {
          strcpy (message, "Image is up to date.");
          retVal = false;
        }
        if (strlen (chksum) == 0) {
          strcpy (message, "Missing SHA256 checksum in metadata");
          retVal = false;
        }
      }
      return (retVal);
    }

    /*
     * A faily crude parser for earch line of the metadata file
     */
    void processParam (char* inBuffer)
    {
      char *paramName, *paramValue;
      char ptr, lim;

      lim = strlen (inBuffer);
      strcpy (image_name, "esp32.img"); // default value
      if (lim==0) return;
      paramName = NULL;
      paramValue = NULL;
      ptr = 0;
      // Move to first non space character
      while (ptr<lim && (inBuffer[ptr]==' ' || inBuffer[ptr]=='\t')) ptr++;
      paramName = &inBuffer[ptr];
      // Move to the end of the first word
      while (ptr<lim && inBuffer[ptr]!=' ' && inBuffer[ptr]!='\t') ptr++; // move to end of non-space chars
      if (ptr<lim) inBuffer[ptr] = '\0';
      ptr++;
      // Move to the next non-space character
      while (ptr<lim && (inBuffer[ptr]==' ' || inBuffer[ptr]=='\t')) ptr++;
      paramValue = &inBuffer[ptr];
      // Store the data we want
      if (paramValue == NULL || paramName[0] == '#' || strlen(paramName) == 0 || strcmp(paramName, "---") == 0 || strcmp(paramName, "...") == 0) {}  // Ignore comments and empty lines
      else if (strcmp (paramName, "size:") == 0) {
        image_size = atol (paramValue);
      }
      else if (strcmp (paramName, "sequence:") == 0) {
        newSequence = atol (paramValue);
      }
      else if (strcmp (paramName, "sha256:") == 0) {
        if (strlen(paramValue) < sizeof(chksum)) strcpy (chksum, paramValue);
      }
      else if (strcmp (paramName, "name:") == 0) {
        if (strlen(paramValue) < sizeof(image_name)) strcpy (image_name, paramValue);
      }
      else sprintf (message, "Parameter %s not recognised", paramName);
    }

    /*
     * Read the image into OTA update partition
     */
    bool get_ota_image(char *url, const char *cert)
    {
      bool retVal = false;
      const esp_partition_t *targetPart;
      esp_ota_handle_t targetHandle;
      int32_t inByte, totalByte;
      uint8_t *inBuffer;
      char bin2hex[3];
      mbedtls_sha256_context sha256ctx;
      int sha256status, retryCount;

      consolewriteln ("Loading new over the air image");
      targetPart = esp_ota_get_next_update_partition(NULL);
      if (targetPart == NULL) {
        sprintf (message, "Cannot identify target partition for update");
        return (false);
      }
      WiFiClient *inStream = getHttpStream(url, cert);
      if (inStream != NULL) {
        if (esp_ota_begin(targetPart, image_size, &targetHandle) == ESP_OK) {
          inBuffer = (uint8_t*) malloc (OTA_BUFFER_SIZE);
          mbedtls_sha256_init(&sha256ctx);
          sha256status = mbedtls_sha256_starts_ret(&sha256ctx, 0);
          totalByte = 0;
          retryCount = image_size / OTA_BUFFER_SIZE;
          while (totalByte < image_size && retryCount > 0) {
            inByte = inStream->read(inBuffer, OTA_BUFFER_SIZE);
            if (inByte > 0) {
              totalByte += inByte;
              if (sha256status == 0) sha256status = mbedtls_sha256_update_ret(&sha256ctx, (const unsigned char*) inBuffer, inByte);
              esp_ota_write(targetHandle, (const void*) inBuffer, inByte);
            }
            else retryCount--;
            if (inByte < OTA_BUFFER_SIZE && totalByte < image_size) {
              if (inByte < (OTA_BUFFER_SIZE/8)) delay(1000);   // We are reading faster than the server can serve, so slow down
              else if (inByte < (OTA_BUFFER_SIZE/4)) delay(500);  // read rate, rather than just spin through small reads
              else if (inByte < (OTA_BUFFER_SIZE/2)) delay(250);
              else delay(100);
            }
            else delay (10); //play nice in multithreading environment
          }
          if (sha256status == 0) {
            sha256status = mbedtls_sha256_finish_ret(&sha256ctx, (unsigned char*) inBuffer);
            message[0] = '\0'; // Truncate message buffer, then use it as a temporary store of the calculated sha256 string
            for (retryCount=0; retryCount<32; retryCount++) {
              sprintf (bin2hex, "%02x", inBuffer[retryCount]);
              strcat  (message, bin2hex);
            }
          }
          mbedtls_sha256_free (&sha256ctx);
          if (strcmp (message, chksum) == 0) {
            if (esp_ota_end(targetHandle) == ESP_OK) {
              retVal = true;
              if (esp_ota_set_boot_partition(targetPart) == ESP_OK) put_sequence_id(newSequence);
              strcpy (message, "Reboot to run updated image.");
            }
            else strcpy (message, "Could not finalise writing of over the air update");
          }
          else if (sha256status ==0) strcat (message, " <-- sha256 checksum mismatch");
          else strcpy (message, "Warning: SHA256 checksum not calculated");
          free (inBuffer);
        }
      }
      return (retVal);
    }


  public:
    ota_control()
    {
      strcpy (message, "No OTA transfer attempted");
      chksum[0] = '\0';
      image_size = 0;
    }

    /*
     * Use the base URL to get
     *   1. Metadata about the update file
     *   2. The binary package
     * 
     * Fail if: 
     *   1. either meta data or image do not exist, or
     *   2. existing image same or newer the offered package or
     *   3. insufficient space for storing image
     *   4. image transfer failed or mismatches sha256 checksum
     *   5. cannot setup connection to server
     */
    bool update(char *baseurl, const char *cert, char *metadata)
    {
      bool retVal = false;
      char url[132];

      strcpy (url, baseurl);
      strcat (url, metadata);
      if (sequence == 0) get_sequence_id();
      if (get_meta_data(url, cert)) {
        strcpy (url, baseurl);
        strcat (url, image_name);
        if (get_ota_image(url, cert)) retVal = true;
      }
      return (retVal);
    }

    /*
     * Newer ESP IDE may support rollback options
     * We'll assume we have 2 OTA type partitions and if update has been
     * run previously then roll back can toggle boot partition between the two.
     * Id last sequence is zero then return with an error
     */
    bool revert()
    {
      bool retVal = false;
      if (sequence == 0) get_sequence_id();
      // if (esp_ota_check_rollback_is_possible()) {
      if (sequence > 0) {
        // esp_ota_mark_app_invalid_rollback_and_reboot();
        esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
        strcpy (message, "Reboot to run previous image.");
        retVal = true;
      }
      else {
        strcpy (message, "No previous image to roll back to");
      }
      return (retVal);
    }

    /*
     * Return data about the current image.
     */
    const char* get_boot_partition_label()
    {
       const esp_partition_t *runningPart;
       
       runningPart = esp_ota_get_running_partition();
       return (runningPart->label);
    }

    uint32_t get_boot_partition_size()
    {
       const esp_partition_t *runningPart;
       
       runningPart = esp_ota_get_running_partition();
       return (runningPart->size);      
    }

    /*
     * Get data about the next partition
     */
    const char* get_next_partition_label()
    {
       const esp_partition_t *nextPart;
       
       nextPart = esp_ota_get_next_update_partition(NULL);
       return (nextPart->label);
    }

    uint32_t get_next_partition_size()
    {
       const esp_partition_t *nextPart;
       
       nextPart = esp_ota_get_next_update_partition(NULL);
       return (nextPart->size);      
    }

    /*
     * Get verbal description of failed OTA update.
     */
    char* get_status_message()
    {
      return (message);
    }
};


/*
 * Uncomment this next section to end of file, if you want to call the update from within this class
 * If you are making your own calls to the class, comment out or delete to the end of the file.
 */
const char* rootCACertificate = NULL;
const char* defaultCertificate = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIEkjCCA3qgAwIBAgIQCgFBQgAAAVOFc2oLheynCDANBgkqhkiG9w0BAQsFADA/\n" \
"MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\n" \
"DkRTVCBSb290IENBIFgzMB4XDTE2MDMxNzE2NDA0NloXDTIxMDMxNzE2NDA0Nlow\n" \
"SjELMAkGA1UEBhMCVVMxFjAUBgNVBAoTDUxldCdzIEVuY3J5cHQxIzAhBgNVBAMT\n" \
"GkxldCdzIEVuY3J5cHQgQXV0aG9yaXR5IFgzMIIBIjANBgkqhkiG9w0BAQEFAAOC\n" \
"AQ8AMIIBCgKCAQEAnNMM8FrlLke3cl03g7NoYzDq1zUmGSXhvb418XCSL7e4S0EF\n" \
"q6meNQhY7LEqxGiHC6PjdeTm86dicbp5gWAf15Gan/PQeGdxyGkOlZHP/uaZ6WA8\n" \
"SMx+yk13EiSdRxta67nsHjcAHJyse6cF6s5K671B5TaYucv9bTyWaN8jKkKQDIZ0\n" \
"Z8h/pZq4UmEUEz9l6YKHy9v6Dlb2honzhT+Xhq+w3Brvaw2VFn3EK6BlspkENnWA\n" \
"a6xK8xuQSXgvopZPKiAlKQTGdMDQMc2PMTiVFrqoM7hD8bEfwzB/onkxEz0tNvjj\n" \
"/PIzark5McWvxI0NHWQWM6r6hCm21AvA2H3DkwIDAQABo4IBfTCCAXkwEgYDVR0T\n" \
"AQH/BAgwBgEB/wIBADAOBgNVHQ8BAf8EBAMCAYYwfwYIKwYBBQUHAQEEczBxMDIG\n" \
"CCsGAQUFBzABhiZodHRwOi8vaXNyZy50cnVzdGlkLm9jc3AuaWRlbnRydXN0LmNv\n" \
"bTA7BggrBgEFBQcwAoYvaHR0cDovL2FwcHMuaWRlbnRydXN0LmNvbS9yb290cy9k\n" \
"c3Ryb290Y2F4My5wN2MwHwYDVR0jBBgwFoAUxKexpHsscfrb4UuQdf/EFWCFiRAw\n" \
"VAYDVR0gBE0wSzAIBgZngQwBAgEwPwYLKwYBBAGC3xMBAQEwMDAuBggrBgEFBQcC\n" \
"ARYiaHR0cDovL2Nwcy5yb290LXgxLmxldHNlbmNyeXB0Lm9yZzA8BgNVHR8ENTAz\n" \
"MDGgL6AthitodHRwOi8vY3JsLmlkZW50cnVzdC5jb20vRFNUUk9PVENBWDNDUkwu\n" \
"Y3JsMB0GA1UdDgQWBBSoSmpjBH3duubRObemRWXv86jsoTANBgkqhkiG9w0BAQsF\n" \
"AAOCAQEA3TPXEfNjWDjdGBX7CVW+dla5cEilaUcne8IkCJLxWh9KEik3JHRRHGJo\n" \
"uM2VcGfl96S8TihRzZvoroed6ti6WqEBmtzw3Wodatg+VyOeph4EYpr/1wXKtx8/\n" \
"wApIvJSwtmVi4MFU5aMqrSDE6ea73Mj2tcMyo5jMd6jmeWUHK8so/joWUoHOUgwu\n" \
"X4Po1QYz+3dszkDqMp4fklxBwXRsW10KXzPMTZ+sOPAveyxindmjkW8lGy+QsRlG\n" \
"PfZ+G6Z6h7mjem0Y+iWlkYcV4PIWL1iwBi8saCbGS5jN2p8M+X+Q7UNKEkROb3N6\n" \
"KOqkqm57TH2H3eDJAkSnh6/DNFu0Qg==\n" \
"-----END CERTIFICATE-----\n" ;

void OTAcertExists(fs::FS &fs)
{
  // File file = fs.open(CERTFILE);
  if(!fs.exists(CERTFILE)){
    consolewrite ("Missing default certificate file, creating ");
    consolewriteln (CERTFILE);
    // file.close();
    File defCertFile = fs.open( CERTFILE, FILE_WRITE);
    if(!defCertFile){
      Serial.println("  - failed to open file for writing");
    }
    else {
      defCertFile.print (defaultCertificate);
      defCertFile.close();
    }
  }
  else {
    consolewrite (CERTFILE);
    consolewriteln (" default certificate file exists, leaving untouched.");
    // file.close();
  }
}

void OTAcheck4update()
{
  ota_control theOtaControl;
  char ota_url[120];
  char ota_certFile[42];

  nvs_get_string ("ota_url", ota_url, "http://conferre.cf/projects/HydraFerret/", sizeof(ota_url));
  nvs_get_string ("ota_certFile", ota_certFile, CERTFILE, sizeof(ota_certFile));
  if (strncmp (ota_url, "https://", 8) == 0) {
    rootCACertificate = util_loadFile(SPIFFS, ota_certFile);
    if (rootCACertificate == NULL) {
      consolewrite ("When using https for OTA, place root certificate in file ");
      consolewriteln (CERTFILE);
    }
  }
  else consolewriteln ("WARNING: using unencrypted link for OTA update. https:// is preferred.");
  networkUserCount++;
  net_connect();
  if (theOtaControl.update (ota_url, rootCACertificate, "metadata.php")) {
    consolewriteln (theOtaControl.get_status_message());
    consolewriteln ("OTA update successful, will reboot in 10 seconds.");
    delay (10000);
    esp_restart();
  }
  else {
    // Do not reboot if OTA area is not updated
    consolewriteln (theOtaControl.get_status_message());
  }
  networkUserCount--;
  net_disconnect();
}

void OTAcheck4rollback()
{
  ota_control theOtaControl;
  if (theOtaControl.revert ()) {
    consolewriteln (theOtaControl.get_status_message());
    consolewriteln ("OTA revert successful, will reboot in 10 seconds.");
    delay (10000);
    esp_restart();
  }
  else {
    // Do not reboot if OTA area is not updated
    consolewriteln (theOtaControl.get_status_message());
  }
}

const char* OTAstatus()
{
  static char message[200];
  char msgBuffer[80];
  char ota_certFile[42];
  
  ota_control theOtaControl;
  nvs_get_string ("ota_url", msgBuffer, "https://conferre.cf/projects/HydraFerret/", sizeof(msgBuffer));
  nvs_get_string ("ota_certFile", ota_certFile, CERTFILE, sizeof(ota_certFile));
  sprintf (message, " * boot partition: %s, next partition: %s, partition size: %d\r\n   ota URL: ", theOtaControl.get_boot_partition_label(), theOtaControl.get_next_partition_label(), theOtaControl.get_next_partition_size());
  strcat  (message, msgBuffer);
  strcat  (message, "\r\n   ota cert file: ");
  strcat  (message, ota_certFile);
  return (message);
}
