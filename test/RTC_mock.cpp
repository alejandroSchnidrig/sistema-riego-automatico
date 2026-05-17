#include "../src/core/RTC.h"

//Mock de funciones de RTC para tests unitarios en entorno nativo (PC) sin DS1302. Permite simular la hora actual y verificar que el sistema interactúa correctamente con el RTC.

RTC_Time mock_rtc_time;
bool mock_rtc_initialized = false;

void hal_rtc_begin(uint8_t rst, uint8_t dat, uint8_t clk) {
    mock_rtc_initialized = true;
}

RTC_Time hal_rtc_now() {
    return mock_rtc_time;
}

bool hal_rtc_set_time(const RTC_Time& t) {
    mock_rtc_time = t;
    return true;
}

bool hal_rtc_writeProtect(bool enable) {
    return true;
}