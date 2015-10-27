# SmartThermostat
Uses a DS3231 library that can be found here: https://github.com/rodan/ds3231
This design is based off of using three potentiometers for controling the start and end times, as well as the initial temperature.
Temperature information from the environment is taken from a thermistor. Because the range of temperatures is so small, a linear equation is used for calculating the temperature from the voltage drop. This WILL NOT be accurate for larger ranges.
