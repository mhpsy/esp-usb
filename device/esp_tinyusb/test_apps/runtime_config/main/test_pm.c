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
#include "esp_pm.h"

static TaskHandle_t main_task_hdl = NULL;

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Initialise TinyUSB on Full-speed peripheral (USB OTG 1.1)
 * Awaiting: Install returns ESP_OK, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("PM: Install/Uninstall", "[pm][full_speed][high_speed]")
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
    printf("tud_suspend_cb\n");
    tinyusb_pm_on_suspend();
    if (main_task_hdl) {
        xTaskNotifyGive(main_task_hdl);
    }
}
#endif // CONFIG_TINYUSB_SUSPEND_CALLBACK

#ifndef CONFIG_TINYUSB_RESUME_CALLBACK
void tud_resume_cb(void)
{
    printf("tud_resume_cb\n");
    tinyusb_pm_on_resume();
    if (main_task_hdl) {
        xTaskNotifyGive(main_task_hdl);
    }
}
#endif // CONFIG_TINYUSB_RESUME_CALLBACK

TEST_CASE("PM: check lock status", "[pm][full_speed][high_speed]")
{
    main_task_hdl = xTaskGetCurrentTaskHandle();
    esp_pm_lock_instance_stats_t stats;
    // TinyUSB driver default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));

    // Lock should not be acquired when device is not attached
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(0, stats.acquired);

    test_device_wait();

    // Lock should be acquired when device is attached
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(1, stats.acquired);

    TEST_ASSERT_MESSAGE(ulTaskNotifyTake(true, pdMS_TO_TICKS(5000)), "Device auto-suspend not generated on time");

    // Lock should Not be acquired when device is suspended
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(0, stats.acquired);

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    main_task_hdl = NULL;
}

#endif // SOC_USB_OTG_SUPPORTED
