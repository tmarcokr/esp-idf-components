#include "unity.h"
#include "RgbLed.hpp"
#include <memory>

using namespace Espressif::Wrappers;

// GPIO 8 is the internal LED on the ESP32-C6 DevKit-M-1
static constexpr gpio_num_t TEST_GPIO = GPIO_NUM_8;

TEST_CASE("RgbLed: Initialization and Lifecycle", "[rgb_led]") {
    RgbLed led(TEST_GPIO);
    
    // Verify successful initialization
    TEST_ASSERT_EQUAL(ESP_OK, led.init());
}

TEST_CASE("RgbLed: Color Commands", "[rgb_led]") {
    RgbLed led(TEST_GPIO);
    TEST_ASSERT_EQUAL(ESP_OK, led.init());

    Color red = {255, 0, 0};
    TEST_ASSERT_EQUAL(ESP_OK, led.setColor(red));
    
    Color green = {0, 255, 0};
    TEST_ASSERT_EQUAL(ESP_OK, led.setColor(green));

    Color blue = {0, 0, 255};
    TEST_ASSERT_EQUAL(ESP_OK, led.setColor(blue));

    TEST_ASSERT_EQUAL(ESP_OK, led.clear());
}

TEST_CASE("RgbLed: Fade Transition", "[rgb_led]") {
    RgbLed led(TEST_GPIO);
    TEST_ASSERT_EQUAL(ESP_OK, led.init());

    Color target = {100, 100, 100};
    // fadeTo does not return esp_err_t, but it calls setColor internally.
    // We test it doesn't crash and performs steps.
    led.fadeTo(target, 100, 10);
    
    TEST_ASSERT_EQUAL(ESP_OK, led.clear());
}

TEST_CASE("RgbLed: Interpolation Logic", "[rgb_led]") {
    // Test mid-point
    TEST_ASSERT_EQUAL(127, RgbLed::interpolate(0, 254, 1, 2));
    
    // Test start point
    TEST_ASSERT_EQUAL(0, RgbLed::interpolate(0, 255, 0, 10));
    
    // Test end point
    TEST_ASSERT_EQUAL(255, RgbLed::interpolate(0, 255, 10, 10));
    
    // Test downward interpolation
    TEST_ASSERT_EQUAL(50, RgbLed::interpolate(100, 0, 1, 2));
}

// All tests completed.
