#include "../src/core/Arduino.h"

//SerialMock para permitir uso de Serial en RTCManager sin Arduino/ESP32 real.
SerialMock Serial;

//Implementación de delay para pruebas. En un entorno real de ESP32, esto llamaría a la función delay() de Arduino, pero aquí lo dejamos como un stub simple.
void delay(unsigned long ms){}