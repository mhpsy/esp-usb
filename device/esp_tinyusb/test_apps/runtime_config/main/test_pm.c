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
#include "esp_pm.h"

static TaskHandle_t main_task_hdl = NULL;

/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: Install and uninstall with PM lock enabled in runtime configuration
 * Awaiting: Install returns ESP_OK, device is enumerated, uninstall returns ESP_OK
 */
TEST_CASE("PM: Install/Uninstall", "[runtime_config][full_speed][high_speed]")
{
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: Install and uninstall with default PM configuration (lock disabled)
 * Awaiting: Install returns ESP_OK, device is enumerated, uninstall returns ESP_OK
 */
TEST_CASE("PM: install succeeds with lock disabled", "[runtime_config][full_speed][high_speed]")
{
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_FALSE(tusb_cfg.pm.no_light_sleep_lock);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: Query PM lock status when lock is disabled in runtime configuration
 * Awaiting: tinyusb_pm_status() returns ESP_ERR_INVALID_STATE after install
 */
TEST_CASE("PM: lock disabled (default config)", "[runtime_config][full_speed][high_speed]")
{
    esp_pm_lock_instance_stats_t stats;
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: Validate tinyusb_pm_status() argument and driver state checks
 * Awaiting: ESP_ERR_INVALID_ARG for NULL stats, ESP_ERR_INVALID_STATE before install
 */
TEST_CASE("PM: status API validation", "[runtime_config][full_speed][high_speed]")
{
    esp_pm_lock_instance_stats_t stats;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, tinyusb_pm_status(&stats));

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, tinyusb_pm_status(NULL));

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: PM lock is held while the driver is installed, including before host attach
 * Awaiting: Lock reference count is 1 after install and remains 1 after enumeration
 */
TEST_CASE("PM: lock held on install", "[runtime_config][full_speed][high_speed]")
{
    esp_pm_lock_instance_stats_t stats;
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(1, stats.acquired);

    test_device_wait();

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(1, stats.acquired);

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: Re-install driver with PM lock enabled
 * Awaiting: PM status works after deinit/reinit cycle
 */
TEST_CASE("PM: re-install with lock enabled", "[runtime_config][full_speed][high_speed]")
{
    esp_pm_lock_instance_stats_t stats;
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(1, stats.acquired);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(1, stats.acquired);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

#if !CONFIG_TINYUSB_SUSPEND_CALLBACK && CONFIG_PM_ENABLE
/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: Validate tinyusb_pm_notify_suspended() driver, configuration, and bus state checks
 * Awaiting: ESP_ERR_INVALID_STATE before install, ESP_ERR_NOT_ALLOWED when lock disabled or bus is active
 */
TEST_CASE("PM: notify suspended API validation", "[runtime_config][full_speed][high_speed]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, tinyusb_pm_notify_suspended());

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_ALLOWED, tinyusb_pm_notify_suspended());
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());

    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    TEST_ASSERT_FALSE(tud_suspended());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_ALLOWED, tinyusb_pm_notify_suspended());
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}
#endif // !CONFIG_TINYUSB_SUSPEND_CALLBACK && CONFIG_PM_ENABLE

#if !CONFIG_TINYUSB_RESUME_CALLBACK && CONFIG_PM_ENABLE
/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: Validate tinyusb_pm_notify_resumed() driver, configuration, and bus state checks
 * Awaiting: ESP_ERR_INVALID_STATE before install, ESP_ERR_NOT_ALLOWED when lock disabled or not mounted
 */
TEST_CASE("PM: notify resumed API validation", "[runtime_config][full_speed][high_speed]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, tinyusb_pm_notify_resumed());

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_ALLOWED, tinyusb_pm_notify_resumed());
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());

    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    TEST_ASSERT_FALSE(tud_mounted());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_ALLOWED, tinyusb_pm_notify_resumed());
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}
#endif // !CONFIG_TINYUSB_RESUME_CALLBACK && CONFIG_PM_ENABLE

#if !CONFIG_TINYUSB_SUSPEND_CALLBACK && !CONFIG_TINYUSB_RESUME_CALLBACK && CONFIG_PM_ENABLE
/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: notify_suspended() is rejected while the USB bus is active
 * Awaiting: ESP_ERR_NOT_ALLOWED after attach when the device is not suspended
 */
TEST_CASE("PM: notify suspended rejects active bus", "[runtime_config][full_speed][high_speed]")
{
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();

    TEST_ASSERT_TRUE(tud_mounted());
    TEST_ASSERT_FALSE(tud_suspended());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_ALLOWED, tinyusb_pm_notify_suspended());

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: notify_resumed() is rejected while the USB bus is suspended
 * Awaiting: ESP_ERR_NOT_ALLOWED after auto-suspend before host resume
 */
TEST_CASE("PM: notify resumed rejects suspended bus", "[runtime_config][full_speed][high_speed]")
{
    main_task_hdl = xTaskGetCurrentTaskHandle();
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();

    TEST_ASSERT_MESSAGE(ulTaskNotifyTake(true, pdMS_TO_TICKS(5000)), "Device auto-suspend not generated on time");
    TEST_ASSERT_TRUE(tud_suspended());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_ALLOWED, tinyusb_pm_notify_resumed());

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    main_task_hdl = NULL;
}

/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: notify_suspended() is idempotent when the bus is already suspended
 * Awaiting: Second call returns ESP_OK and keeps the PM lock released
 */
TEST_CASE("PM: notify suspended idempotent", "[runtime_config][full_speed][high_speed]")
{
    esp_pm_lock_instance_stats_t stats;
    main_task_hdl = xTaskGetCurrentTaskHandle();
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();

    TEST_ASSERT_MESSAGE(ulTaskNotifyTake(true, pdMS_TO_TICKS(5000)), "Device auto-suspend not generated on time");
    TEST_ASSERT_TRUE(tud_suspended());
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(0, stats.acquired);

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_notify_suspended());
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(0, stats.acquired);

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    main_task_hdl = NULL;
}

/**
 * @brief TinyUSB PM specific testcase
 *
 * Scenario: notify_resumed() is idempotent when PM already treats the device as active
 * Awaiting: ESP_OK on an active, mounted bus without a prior PM suspend transition
 */
TEST_CASE("PM: notify resumed idempotent on active bus", "[runtime_config][full_speed][high_speed]")
{
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.pm.no_light_sleep_lock = true;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();

    TEST_ASSERT_TRUE(tud_mounted());
    TEST_ASSERT_FALSE(tud_suspended());
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_notify_resumed());
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_notify_resumed());

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}
#endif // !CONFIG_TINYUSB_SUSPEND_CALLBACK && !CONFIG_TINYUSB_RESUME_CALLBACK && CONFIG_PM_ENABLE

#ifndef CONFIG_TINYUSB_SUSPEND_CALLBACK
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    printf("tud_suspend_cb\n");
#if CONFIG_PM_ENABLE
    tinyusb_pm_notify_suspended();
#endif // CONFIG_PM_ENABLE
    if (main_task_hdl) {
        xTaskNotifyGive(main_task_hdl);
    }
}
#endif // CONFIG_TINYUSB_SUSPEND_CALLBACK

#ifndef CONFIG_TINYUSB_RESUME_CALLBACK
void tud_resume_cb(void)
{
    printf("tud_resume_cb\n");
#if CONFIG_PM_ENABLE
    tinyusb_pm_notify_resumed();
#endif // CONFIG_PM_ENABLE
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

    // Lock should be acquired while waiting for host attach
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_pm_status(&stats));
    TEST_ASSERT_EQUAL(1, stats.acquired);

    test_device_wait();

    // Lock should remain acquired when device is attached
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
