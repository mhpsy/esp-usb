/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"

#if SOC_USB_OTG_SUPPORTED
//
#include <stdio.h>
#include <string.h>
//
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
//
#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "test_task.h"
#include "sdkconfig.h"
#include "device_handling.h"
#include "tinyusb_pm.h"

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Initialise TinyUSB on Full-speed peripheral (USB OTG 1.1)
 * Awaiting: Install returns ESP_OK, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("PM: Install", "[pm][full_speed][high_speed]")
{
    // TinyUSB driver default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

#ifndef CONFIG_TINYUSB_SUSPEND_CALLBACK
void tud_suspend_cb(bool remote_wakeup_en)
{
    printf("SUSPENDED\n\n");
}
#endif // CONFIG_TINYUSB_SUSPEND_CALLBACK

#ifndef CONFIG_TINYUSB_RESUME_CALLBACK
void tud_resume_cb(void)
{
    printf("RESUMED\n\n");
}
#endif // CONFIG_TINYUSB_RESUME_CALLBACK

TEST_CASE("PM: Install", "[pm][full_speed][high_speed]")
{
    // TinyUSB driver default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    vTaskDelay(1000);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

#endif // SOC_USB_OTG_SUPPORTED
