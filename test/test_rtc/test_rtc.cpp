#include <unity.h>
#include "../src/scheduler/RTCManager.h"

extern RTC_Time mock_rtc_time;
extern bool mock_rtc_initialized;

void setUp(void) {
    mock_rtc_initialized = false;
    mock_rtc_time = RTC_Time();
}

void tearDown(void) {
}

void test_rtc_initialization(void) {
    RTCManager rtc(21, 19, 18);
    rtc.begin();
    TEST_ASSERT_TRUE(mock_rtc_initialized);
}

void test_set_and_get_time(void) {
    RTCManager rtc(21, 19, 18);
    rtc.begin();
    
    //Ingreso una fecha y hora válida y verifico que se guarde correctamente.
    bool result = rtc.setTime(2025, 5, 15, 14, 30, 0);
    TEST_ASSERT_TRUE(result);
    
    RTC_Time current = rtc.now();
    TEST_ASSERT_EQUAL(2025, current.year);
    TEST_ASSERT_EQUAL(5, current.month);
    TEST_ASSERT_EQUAL(15, current.day);
    TEST_ASSERT_EQUAL(14, current.hour);
    TEST_ASSERT_EQUAL(30, current.minute);
    TEST_ASSERT_EQUAL(0, current.second);
}

void test_invalid_time_rejection(void) {
    RTCManager rtc(21, 19, 18);
    rtc.begin();
    
    //Prueba mes inválido.
    TEST_ASSERT_FALSE(rtc.setTime(2025, 13, 1, 12, 0, 0));
    
    //Prueba dia invalido para el mes.
    TEST_ASSERT_FALSE(rtc.setTime(2025, 2, 29, 12, 0, 0)); // 2025 is not a leap year
    
    //Prueba dia válido para año bisiesto.
    TEST_ASSERT_TRUE(rtc.setTime(2024, 2, 29, 12, 0, 0)); // 2024 is a leap year
    
    //Prueba hora invalida.
    TEST_ASSERT_FALSE(rtc.setTime(2025, 1, 1, 24, 0, 0));
}

void test_format_date_time(void) {
    RTC_Time t(2026, 7, 4, 9, 5, 1);
    
    String dateStr = RTCManager::formatDate(t);
    String timeStr = RTCManager::formatTime(t);
    
    TEST_ASSERT_EQUAL_STRING("2026/07/04", dateStr.c_str());
    TEST_ASSERT_EQUAL_STRING("09:05:01", timeStr.c_str());
}

void test_parse_hour_minute(void) {
    uint8_t h, m;
    
    TEST_ASSERT_TRUE(RTCManager::parseHourMinute("14:30", h, m));
    TEST_ASSERT_EQUAL(14, h);
    TEST_ASSERT_EQUAL(30, m);
    
    TEST_ASSERT_TRUE(RTCManager::parseHourMinute("09:05", h, m));
    TEST_ASSERT_EQUAL(9, h);
    TEST_ASSERT_EQUAL(5, m);
    
    //Formatos invalidos.
    TEST_ASSERT_FALSE(RTCManager::parseHourMinute("25:00", h, m)); // Invalid hour
    TEST_ASSERT_FALSE(RTCManager::parseHourMinute("12:60", h, m)); // Invalid minute
    TEST_ASSERT_FALSE(RTCManager::parseHourMinute("12-30", h, m)); // Invalid separator
    TEST_ASSERT_FALSE(RTCManager::parseHourMinute("1:30", h, m));  // Missing leading zero
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_rtc_initialization);
    RUN_TEST(test_set_and_get_time);
    RUN_TEST(test_invalid_time_rejection);
    RUN_TEST(test_format_date_time);
    RUN_TEST(test_parse_hour_minute);
    return UNITY_END();
}
