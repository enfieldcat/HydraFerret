Introduction
============
HydraFerret allows the collection of environmental data to be collected relatively easily.
The collection is made using inexpensive IOT (Internet/Intranet Of Things) sensors and processors, collects their data centrally and displays it using xymon.
Xymon is better known as a computer monitoring and alerting tool, but its simple interface and extendable agent features lend well to this type of collection.
Data is collected in graphs which default to 2, 12, 48, and 576 day graphs.
Warning and Critical thresholds can be set for when readings are outside preset parameters.

More documentation can be found at <a href="https://conferre.cf">https://conferre.cf</a>

User interface
==============
The xymon web interface is used as the user interface.
This presents a two dimensional grid of computers (or esp monitoring units) and the datacollected or tests they've run.
A status icon shows the state of the test where green is OK, yellow a warning, red a critical state, purple for missing data and clear for no data received.
By clicking on this status icon you can drill down into detail to see the current state and recent history of the measurement.
From here it is possible to zoom in detail or to view longer trends

Collection Configuration
========================
Each collection unit consists of an ESP32 computer and several sensors.
The configuration of the unit is via a command line interface over a serial port (115200 bits/second, 8 bit, no parity, 1 stop bit).
Telnet access is also supported once WiFi has been configured.
This allows pins to be assigned to the collection of data, where this may include:
* up to 8 Counters: eg from magnetic hall effect flow sensors or light beam pedestrian counters.
* up to 8 ADC (Analog to digital) sensors, such as soil moisture sensors.
* up to 2 I2C busses supporting various devices such as temperature and light sensors. On booting these busses are scanned for devices which minimises set up and allowing for a degree of "plug and play".
* up to 4 Onewire busses supporting temperature sensors.
* up to 8 Outputs to trigger relays or perform internal actions such as reboots or firmware updates.

Alerting and output control is done using a rpn calculator which has access to many variables related to sensors and time.
Each sensor (even ones of the same type) can be set to have different alerting thresholds.
This approach differs a bit from the idea of centralising alerting thresholds using xymon.
However, it offers great flexibility.
For example, if measuring room temperature with a 20 degree target and chillers with a target temperature of 5 degrees Celsius and freezers with a target of -20 Celsius, individual thresholds can be configured.
