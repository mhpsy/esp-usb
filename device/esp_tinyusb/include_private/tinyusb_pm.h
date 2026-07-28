/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_pm.h"
#include "tinyusb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize TinyUSB power management according to driver configuration.
 *
 * @param[in] config PM configuration from tinyusb_config_t.pm.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_SUPPORTED if no_light_sleep_lock is enabled but CONFIG_PM_ENABLE is disabled
 *      - Other error codes from esp_pm_lock_create()
 */
esp_err_t tinyusb_pm_init(const tinyusb_pm_config_t *config);

/**
 * @brief Tear down TinyUSB power management.
 *
 * @return
 *      - ESP_OK on success
 *      - Other error codes from esp_pm_lock_delete()
 */
esp_err_t tinyusb_pm_deinit(void);

/**
 * @brief Update PM lock based on tinyusb event
 *
 * @param[in] event tinyusb event
 */
void tinyusb_pm_on_event(tinyusb_event_id_t event);

/**
 * @brief Get TinyUSB PM lock statistics.
 *
 * @param[out] stats PM lock instance statistics.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if stats is NULL
 *      - ESP_ERR_INVALID_STATE if PM lock is not enabled
 *      - ESP_ERR_NOT_SUPPORTED if CONFIG_PM_ENABLE is disabled
 */
esp_err_t tinyusb_pm_lock_check(esp_pm_lock_instance_stats_t *stats);

#if !CONFIG_TINYUSB_SUSPEND_CALLBACK
/**
 * @brief Release PM lock on USB suspend when suspend callback is user-provided
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_ALLOWED if PM lock is not enabled
 */
esp_err_t tinyusb_pm_on_suspend(void);
#endif // !CONFIG_TINYUSB_SUSPEND_CALLBACK

#if !CONFIG_TINYUSB_RESUME_CALLBACK
/**
 * @brief Acquire PM lock on USB resume when resume callback is user-provided
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_ALLOWED if PM lock is not enabled
 */
esp_err_t tinyusb_pm_on_resume(void);
#endif // !CONFIG_TINYUSB_RESUME_CALLBACK


#ifdef __cplusplus
}
#endif
