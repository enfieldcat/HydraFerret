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


#define CONNECT_RETRIES 6
/*
 * Optionally comment out the next define if working with hiddden SSIDs
 * otherwise the unit will scan for available networks for up to 1 minute
 * before switching to a poll of available networks.
 */
#include <WiFiMulti.h>

static WiFiMulti wifiMulti;

static bool wifi_initiated = false;

/*
 * Connect to network using stored settings
 */
bool net_connect()
{
  bool net_connected = false;
  static int network_count = 0;
  static bool wifi_creds_loaded = false;
  char msgBuffer[80];
  int use_multiwifi = 1;

  if (WiFi.status() == WL_CONNECTED) return (true);
  if (wifi_initiated) {
    if (ansiTerm) displayAnsi (3);
    consolewriteln ("WiFi resume from sleep");
    if (ansiTerm) displayAnsi (1);
    WiFi.mode(WIFI_STA);
    WiFi.reconnect();
    for (uint8_t z; z<10 ; z++) {
      if (z!=0) delay(1000);
      if (WiFi.status() == WL_CONNECTED) return (true);
    }
  }
//  WiFi.mode(WIFI_ON);
  use_multiwifi = nvs_get_int ("use_multiwifi", 1);
  if (network_count == 0) {
    for (int n=0; n<4 ; n++) {
      sprintf (msgBuffer, "wifi_ssid_%d", n);
      nvs_get_string (msgBuffer, wifi_ssid[n], "none", sizeof(msgBuffer));
      if (strcmp (wifi_ssid[n], "none") != 0) {
        network_count++;
      }
    }
  }
  if (!wifi_creds_loaded) {
    wifi_creds_loaded = true;
    for (int n=0; n<4 ; n++) {
      sprintf (msgBuffer, "wifi_ssid_%d", n);
      nvs_get_string (msgBuffer, wifi_ssid[n], "none", sizeof(msgBuffer));
      if (strcmp (wifi_ssid[n], "none") != 0) {
        sprintf (msgBuffer, "wifi_passwd_%d", n);
        nvs_get_string (msgBuffer, wifi_passwd[n], "none", sizeof(msgBuffer));
        if (use_multiwifi==1 && network_count > 1) {
          if (strcmp (msgBuffer, "none") == 0) wifiMulti.addAP((const char*)wifi_ssid[n], NULL);
          else wifiMulti.addAP((const char*) wifi_ssid[n], (const char*) wifi_passwd[n]);
        }
      }
    }
  }
  WiFi.setHostname(device_name);
  if (use_multiwifi==1 && network_count > 1) {
    for (int loops = CONNECT_RETRIES; loops > 0 && !net_connected; loops--) {
      if (wifiMulti.run() == WL_CONNECTED) {
        if (ansiTerm) displayAnsi (3);
        consolewrite ("\nWiFi connected - scanned: ");
        WiFi.SSID().toCharArray(msgBuffer, sizeof(msgBuffer));
        consolewriteln (msgBuffer);
        if (ansiTerm) displayAnsi (1);
        net_connected = true;
        wifi_initiated = true;
        break;
      }
      else delay(10000);
    }
    if (wifiMulti.run() != WL_CONNECTED && net_single_connect()) net_connected = true;
  }
  else if (network_count > 0) {
    if (net_single_connect()) net_connected = true;
  }
  if (net_connected) {
    nvs_get_string ("ntp_server", ntp_server, "pool.ntp.org", sizeof(ntp_server));
    if (strcmp (ntp_server, "none") != 0) {
      // GMT Offset, DST Offset, Server
      configTime(0, 0, ntp_server);
      nvs_get_string ("timezone", msgBuffer, "WAT-01:00", sizeof(msgBuffer));  // Default West African Time - Central African Republic
      setenv("TZ", msgBuffer, 1);
      if (ansiTerm) displayAnsi (0);
      consolewrite ("Time server: ");
      consolewrite (ntp_server);
      consolewrite (", Time zone: ");
      consolewriteln (msgBuffer);
      if (ansiTerm) displayAnsi (1);
    }
  }
  return (net_connected);
}


bool net_single_connect()
{
  bool net_connected = false;
  char msgBuffer[40];

  if (WiFi.status() == WL_CONNECTED) return (true);
  for (int loops = CONNECT_RETRIES; loops>0 && !net_connected; loops--) {
    for (uint8_t n=0; n<4 && !net_connected; n++) {
      if (strcmp (wifi_ssid[n], "none") != 0) {
        if (strcmp (wifi_passwd[n], "none") == 0) WiFi.begin(wifi_ssid[n], NULL);
        else WiFi.begin(wifi_ssid[n], wifi_passwd[n]);
        WiFi.setHostname(device_name);
        delay (1000);
        for (uint8_t z=0; z<14 && WiFi.status() != WL_CONNECTED; z++) delay (1000);
        if (WiFi.status() == WL_CONNECTED) net_connected = true;
        if (net_connected) {
          wifi_initiated = true;
          if (ansiTerm) displayAnsi (3);
          consolewrite ("\nWiFi connected: ");
          WiFi.SSID().toCharArray(msgBuffer, sizeof(msgBuffer));
          consolewriteln (msgBuffer);
          if (ansiTerm) displayAnsi (1);
        }
        else {
          if (ansiTerm) displayAnsi (3);
          consolewrite ("Not connecting to WiFi network: ");
          consolewriteln (wifi_ssid[n]);
          if (ansiTerm) displayAnsi (1);
        }
      }
    }
  }
  if (WiFi.status() == WL_CONNECTED) net_connected = true;
  else net_connected = false;
  return (net_connected);
}


void net_disconnect()
{
  bool disconn = true;
  if (wifimode != 0 && networkUserCount<1 && WiFi.status() == WL_CONNECTED) {
    // wait for grace period flush time
    // avoid un-necessary disconnects
    for (uint8_t n=0; n<CONNECT_RETRIES && disconn; n++) {
      delay (1000);
      if (networkUserCount>0) disconn = false;
    }
    if (disconn) {
      WiFi.disconnect();
      WiFi.mode(WIFI_OFF);
      // WiFi.~WiFiClass();
      if (ansiTerm) displayAnsi (3);
      consolewriteln ("WiFi disconnected");
      if (ansiTerm) displayAnsi (1);
    }
  }
}

void net_display_connection()
{
  if (WiFi.status() == WL_CONNECTED) {
    char msgBuffer[40];

    sprintf (msgBuffer, " *  IP address: %s", net_ip2str((uint32_t) WiFi.localIP()));
    consolewriteln (msgBuffer);
    sprintf (msgBuffer, " * Subnet Mask: %s", net_ip2str((uint32_t) WiFi.subnetMask()));
    consolewriteln (msgBuffer);
    sprintf (msgBuffer, " *     Gateway: %s", net_ip2str((uint32_t) WiFi.gatewayIP()));
    consolewriteln (msgBuffer);
    sprintf (msgBuffer, " *        SSID: " );
    consolewrite (msgBuffer);
    WiFi.SSID().toCharArray(msgBuffer, sizeof(msgBuffer));
    consolewriteln (msgBuffer);    
  }
  else consolewriteln ("WiFi network not connected");
}

char* net_ip2str (uint32_t ipAddress)
{
  static char msgBuffer[16];
  union {
    uint32_t ipaddress;
    uint8_t  ipbyte[4];
  } ipaddr;

  ipaddr.ipaddress = ipAddress;
  sprintf (msgBuffer, "%d.%d.%d.%d", ipaddr.ipbyte[0], ipaddr.ipbyte[1], ipaddr.ipbyte[2], ipaddr.ipbyte[3]);
  return (msgBuffer);
}
