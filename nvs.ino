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
 * Keep NVS storage routines here
 */
Preferences prefs;

void nvs_init()
{
  prefs.begin ("environMon");  
}

void nvs_get_string (char *strName, char *strDest, char *strDefault, int strSize)
{
  if (prefs.getString(strName, strDest, strSize) == 0) {
    strcpy (strDest, strDefault);
  }  
}

void nvs_get_string (char *strName, char *strDest, int strSize)
{
  prefs.getString(strName, strDest, strSize);
}

void nvs_put_string (char *strName, char *value)
{
  char oldval[80];
  oldval[0] = '\0';
  nvs_get_string (strName, oldval, "", 80);
  if (strcmp (oldval, value) != 0) {
    prefs.putString (strName, value);
    configHasChanged = true;
  }
}

int nvs_get_int (char *intName, int intDefault)
{
  return (prefs.getInt (intName, intDefault));
}

void nvs_put_int (char *intName, int value)
{
  int oldval = nvs_get_int (intName, value+1);
  if (value != oldval) {
    prefs.putInt (intName, value);
    configHasChanged = true;
  }
}

double nvs_get_double (char *doubleName, double doubleDefault)
{
  double retval = prefs.getDouble (doubleName, doubleDefault);
  if (isnan(retval)) retval = doubleDefault; 
  return (retval);
}

void nvs_put_double (char *doubleName, double value)
{
  double oldval = nvs_get_double (doubleName, value+1.00);
  if (value != oldval) {
    prefs.putDouble (doubleName, value);
    configHasChanged = true;
  }
}

float nvs_get_float (char *floatName, float floatDefault)
{
  float retval = prefs.getFloat (floatName, floatDefault);
  if (isnan(retval)) retval = floatDefault; 
  return (retval);
}

void nvs_put_float (char *floatName, float value)
{
  float oldval = nvs_get_float (floatName, value+1.00);
  if (value != oldval) {
    prefs.putFloat (floatName, value);
    configHasChanged = true;
  }
}

int nvs_get_freeEntries()
{
  return (prefs.freeEntries());
}
 
