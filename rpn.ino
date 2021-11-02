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


#include <math.h>

class rpn {
  private:
  
  float lifoStack[LIFO_SIZE];
  int8_t stackPtr = 0;

  void push (float value)
  {
    if (stackPtr<LIFO_SIZE ) {
      lifoStack[stackPtr++] = value;
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Stack overflow in RPN calculator");
      if (ansiTerm) displayAnsi(1);
    }
  }


  float pop()
  {
    float value = 0.00;
    if (stackPtr>0) {
      value = lifoStack[--stackPtr];
    }
    else {
      if (ansiTerm) displayAnsi(3);
      consolewriteln ("Stack underflow in RPN calculator");
      if (ansiTerm) displayAnsi(1);
    }
    return value;
  }

  float eval(char op)
  {
    float temp, tempa;

    switch( op ) {
      case '+':
        return (pop() + pop());
      case '*':
        return (pop() * pop());
      case '-':
        temp = pop();
        return (pop() - temp);
      case '/': {
        temp = pop();
        if (temp == 0.00) {
          pop ();
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Divide by zero error");
          if (ansiTerm) displayAnsi(1);
          return (0.00);
        }
        return (pop() / temp);
      }
      case '%': {
        temp = pop();
        if (temp == 0.00) {
          pop ();
          if (ansiTerm) displayAnsi(3);
          consolewriteln ("Divide by zero error");
          if (ansiTerm) displayAnsi(1);
          return (0.00);
        }
        return fmod(pop(), temp);
      }
      case '!': return (1 / pop());
      case 'q': return (cbrt(pop()));
      case 'h': return (hypot(pop(), pop()));
      case '^': temp = pop();
        return (pow (pop(), temp));
      case 'v':
        return (sqrt (pop()));
      case '&':
        temp = pop();
        if (pop()>0 && temp>0) return(1.00);
        return(0.00);
      case '|':
        temp = pop();
        if (pop()>0 && temp>0) return(1.00);
        return(0.00);
      case '=':
        temp = pop();
        if (temp == pop()) return(1.00);
        return(0.00);
      case '>':
        temp = pop();
        if (temp <= pop()) return(1.00);
        return(0.00);
      case '<':
        temp = pop();
        if (temp >= pop()) return(1.00);
        return(0.00);
      case 'd': 
        temp = pop();
        return (util_dewpoint (pop(), temp));
      case 'Q': 
        temp = pop();
        return (util_compensatePressure (pop(), temp));
      case 'a': return fabs(pop());
      case 's': return sin(pop());
      case 'c': return cos(pop());
      case 't': return tan(pop());
      case 'S': return asin(pop());
      case 'C': return acos(pop());
      case 'T': return atan(pop());
      case 'l': return log10f(pop());
      case 'n': return logf(pop());
      case 'k': return getConstant(pop());
      case 'i': temp = pop(); if (temp>0.00) return floor(temp); return ceil(temp);
      case 'I': temp = pop(); if (temp>0.00) return ceil(temp); return floor(temp);
      case 'x': temp = pop(); tempa = pop(); push (temp); return (tempa);
      case 'f': return (util_ftoc (pop()));
      case 'F': return (util_ctof (pop()));
      case 'r': return (util_rtod (pop()));
      case 'R': return (util_dtor (pop()));
    }
  }

  int need (char op)
  {
    switch( op ) {
      case '+':
      case '*':
      case '-':
      case '/':
      case '%':
      case '^':
      case 'h':
      case 'x':
      case '&':
      case '|':
      case '=':
      case '<':
      case '>':
      case 'd':
      case 'Q':
        return 2;
        break;
      case '!':
      case 'v':
      case 'q':
      case 'a':
      case 's':
      case 'c':
      case 't':
      case 'S':
      case 'C':
      case 'T':
      case 'l':
      case 'i':
      case 'I':
      case 'k':
      case 'n':
        return 1;
      default:
        if (ansiTerm) displayAnsi(3);
        consolewriteln( "Invalid rpn operand!");
        if (ansiTerm) displayAnsi(1);
        return 0;
    }
  }

  int checknr (char* number)
  {
    for( ; *number; number++ )
      if((*number < '0' || *number > '9') && *number != '-' && *number != '.') return 0;
    return 1;
  }

  /*
   * Get a constant k
   */
  float getConstant(float index)
  {
    uint8_t idx;
    float retval;
    char msgBuffer[17];

    idx = floor(index);
    sprintf (msgBuffer, "const_val_%d", idx);
    retval = nvs_get_float (msgBuffer, 0.00);
    return (retval);
  }

  /*
   * Display the stack to a max depth of 10 items
   */
  void doShowStack (char op)
  {
    uint8_t tPtr = 0;
    char msgBuffer[20];

    sprintf (msgBuffer, " %c ", op);
    consolewrite (msgBuffer);
    for (tPtr=0; tPtr<stackPtr; tPtr++) {
      sprintf (msgBuffer, " %9s", util_ftos(lifoStack[tPtr], 4));
      consolewrite (msgBuffer);
    }
    consolewriteln ("");
  }

public:

  float getvar (char* varname)
  {
    float retval = 0.00;
    char  parts[20];
    char  *varDevType   = NULL;
    char  *varDevIndex  = NULL;
    char  *varDevAttrib = NULL;
    int   n, lim, err;

    lim = strlen(varname);
    if (lim<sizeof(parts)) {
      strcpy (parts, varname);
      for (n=0; n<lim && parts[n] == ' '; n++);
      varDevType = &parts[n];
      for (;n<lim && parts[n] != '[' && parts[n] != '.'; n++);
      if (n<lim) {
        for (;n<lim && (parts[n]=='[' || parts[n]==']' || parts[n]=='.'); n++) parts[n] = '\0';
        varDevIndex = &parts[n];
        for (;n<lim && parts[n] != '[' && parts[n]!=']' && parts[n] != '.'; n++);
        // if (n<lim) {
          for (;n<lim && (parts[n]=='[' || parts[n]==']' || parts[n]=='.'); n++) parts[n] = '\0';
          if (n<lim) varDevAttrib = &parts[n];  // if we are already at lim then we probably have an abreviated version of device name without the index
          else {
            lim = 0;
            varDevAttrib = varDevIndex;
          }
          for (;n<lim && parts[n] != '[' && parts[n]!=']' && parts[n] != '.'; n++);
          for (;n<lim && (parts[n]=='[' || parts[n]==']' || parts[n]=='.'); n++) parts[n] = '\0';
          if (strlen (varDevIndex) > 0) lim = util_str2int(varDevIndex);
          else {
            lim = 0;
          }
          for (n=0; n<numberOfTypes && strcmp(devType[n], varDevType)!=0; n++);
          if (n==numberOfTypes && strcmp(varDevType, "memory")!=0) {
            if (ansiTerm) displayAnsi(3);
            consolewrite ("Unrecognised device type in rpn calc: ");
            consolewriteln (varDevType);
            if (ansiTerm) displayAnsi(1);
          }
          else if (lim<0 || (strcmp(varDevType, "memory")!=0 && strcmp(varDevType, "adc")!=0 && lim>=devTypeCount[n]) || (strcmp(varDevType, "adc")==0 && (lim<32 || lim>39))) {
            if (ansiTerm) displayAnsi(3);
            consolewrite ("device index out of range in rpc calc for: ");
            consolewriteln (varname);
            if (ansiTerm) displayAnsi(1);
          }
          else {
            if      (strcmp(varDevType, "bh1750")   == 0) retval = the_bh1750.getData   (lim, varDevAttrib);
            else if (strcmp(varDevType, "bme280")   == 0) retval = the_bme280.getData   (lim, varDevAttrib);
            else if (strcmp(varDevType, "counter")  == 0) retval = theCounter.getData   (lim, varDevAttrib);
            else if (strcmp(varDevType, "css811")   == 0) retval = the_css811.getData   (lim, varDevAttrib);
            else if (strcmp(varDevType, "hdc1080")  == 0) retval = the_hdc1080.getData  (lim, varDevAttrib);
            else if (strcmp(varDevType, "pfc8583")  == 0) retval = the_pfc8583.getData  (lim, varDevAttrib);
            else if (strcmp(varDevType, "veml6075") == 0) retval = the_veml6075.getData (lim, varDevAttrib);
            else if (strcmp(varDevType, "ina2xx")   == 0) retval = the_ina2xx.getData   (lim, varDevAttrib);
            else if (strcmp(varDevType, "output")   == 0) retval = the_output.getData   (lim, varDevAttrib);
            else if (strcmp(varDevType, "serial")   == 0) retval = the_serial.getData   (lim, varDevAttrib);
            else if (strcmp(varDevType, "adc")      == 0) retval = the_adc.getData      (lim, varDevAttrib);
            else if (strcmp(varDevType, "pfc8583")  == 0) retval = the_pfc8583.getData  (lim, varDevAttrib);
            else if (strcmp(varDevType, "memory")   == 0) {
              if      (strcmp (varDevAttrib, "free") == 0) retval = ESP.getFreeHeap();
              else if (strcmp (varDevAttrib, "minf") == 0) retval = ESP.getMinFreeHeap();
              else if (strcmp (varDevAttrib, "size") == 0) retval = ESP.getHeapSize();
              else if (strcmp (varDevAttrib, "time") == 0) retval = esp_timer_get_time() / (uS_TO_S_FACTOR * 60.0);
              else if (strcmp (varDevAttrib, "freq") == 0) retval = ESP.getCpuFreqMHz();
              else if (strcmp (varDevAttrib, "xtal") == 0) retval = getXtalFrequencyMhz();
              else if (strcmp (ntp_server, "none") != 0) {
                // It may be some time thing....
                static char timestring[6];
                struct tm timeinfo;
                if(getLocalTime(&timeinfo)){
                  if      (strcmp (varDevAttrib, "year") == 0) retval = timeinfo.tm_year + 1900;
                  else if (strcmp (varDevAttrib, "mont") == 0) retval = timeinfo.tm_mon + 1;
                  else if (strcmp (varDevAttrib, "dow")  == 0) retval = timeinfo.tm_wday;
                  else if (strcmp (varDevAttrib, "dom")  == 0) retval = timeinfo.tm_mday;
                  else if (strcmp (varDevAttrib, "doy")  == 0) retval = timeinfo.tm_yday;
                  else if (strcmp (varDevAttrib, "hour") == 0) retval = timeinfo.tm_hour;
                  else if (strcmp (varDevAttrib, "min")  == 0) retval = timeinfo.tm_min;
                  else if (strcmp (varDevAttrib, "mind") == 0) retval = timeinfo.tm_min + (60 * timeinfo.tm_hour);
                  else if (strcmp (varDevAttrib, "secs") == 0) retval = timeinfo.tm_sec;
                }
              }
            }
          }
        // }
      }
    }
    return (retval);
  }

  float calc( int argc, char** argv )
    {
      int   i;
      float temp;
      bool  showStack = false;
      char  msgBuffer[20];

      i = 0;
      lifoStack[0] = 0.00;
      if (strcmp(argv[0], "rpn") == 0) {
        showStack = true;
        if (ansiTerm) displayAnsi(4);
        consolewriteln ("Op   Older <-- Stack --> Newer");
        if (ansiTerm) displayAnsi(0);
        i++;
      }
      for(; i < argc; i++ ) {
        char* token = argv[i];
        char* endptr;
        char op;

        if( strcmp(token, "-")!=0 && checknr( token ) ) {
          /* We have a valid number. */
          temp = util_str2float( token );
          push( temp);
          if (showStack) doShowStack (' ');
        } else if (strcmp (token, "e") == 0) {
          push (M_E);
          if (showStack) doShowStack (' ');
        } else if (strcmp (token, "p") == 0) {
          push (M_PI);
          if (showStack) doShowStack (' ');
        } else if (strcmp (token, "g") == 0) {
          push (9.80665);
          if (showStack) doShowStack (' ');
        }
        else {
          if( strlen( token ) != 1 ) {
            temp = getvar (token);
            push (temp);
            if (showStack) doShowStack ('~');
          }
          /* We have an operand (hopefully) */
          else {
            op = token[0];
            if( stackPtr < need(op) ) {
              if (ansiTerm) displayAnsi(3);
              consolewriteln ("Too few arguments on stack.");
              if (ansiTerm) displayAnsi(1);
            }
            else {
              push(eval(op));
              if (showStack) doShowStack (op);
            }
          }
        }
      }

      if( stackPtr > 1 ) {
        sprintf (msgBuffer, "%d", (stackPtr));
        if (ansiTerm) displayAnsi(3);
        consolewrite (msgBuffer);
        consolewriteln  (" too many arguments on stack.");
        if (ansiTerm) displayAnsi(1);
      }
      return (lifoStack[0]);
    }
};


float rpn_calc (int argc, char** argv)
{
  return (rpn().calc(argc, argv));
}
