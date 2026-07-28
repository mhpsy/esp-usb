/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_pm.h"
#include "tinyusb_pm.h"

static const char *TAG = "TinyUSB-PM";

/**
 * @brief TinyUSB power management context
 */
typedef struct {
    bool mounted;                   /*!< USB Device mounted/umounted */
    bool suspended;                 /*!< USB Device suspended/resumed */
    bool lock_enabled;              /*!< PM lock enabled/disabled by user */
    bool lock_held;                 /*!< PM lock acquired/released */
    esp_pm_lock_handle_t lock;      /*!< PM lock object */
} tinyusb_pm_ctx_t;

static tinyusb_pm_ctx_t s_pm_ctx;

static esp_err_t tinyusb_pm_lock_acquire(void)
{
    // Graceful return if PM lock is not enabled, or is already acquired
    if (!s_pm_ctx.lock_enabled || s_pm_ctx.lock_held) {
        return ESP_OK;
    }

    // Acquire lock and mark as acquired
    const esp_err_t err = esp_pm_lock_acquire(s_pm_ctx.lock);
    if (err == ESP_OK) {
        s_pm_ctx.lock_held = true;
    } else {
        ESP_LOGE(TAG, "Lock acquire error %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t tinyusb_pm_lock_release(void)
{
    // Graceful return if PM lock is not enabled or already released
    if (!s_pm_ctx.lock_enabled || !s_pm_ctx.lock_held) {
        return ESP_OK;
    }

    // Release lock and mark as released
    esp_err_t err = esp_pm_lock_release(s_pm_ctx.lock);
    if (err == ESP_OK) {
        s_pm_ctx.lock_held = false;
    } else {
        ESP_LOGE(TAG, "Lock release error %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t tinyusb_pm_init(const tinyusb_pm_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "Config can't be NULL");

    // Check if the user enabled the lock
    if (!config->no_light_sleep_lock) {
        s_pm_ctx.lock_enabled = false;
        return ESP_OK;
    }

    // Initialize lock
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock == NULL, ESP_ERR_INVALID_STATE, TAG, "PM module already initialized");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "tusb", &s_pm_ctx.lock), TAG, "Failed to create PM lock");

    s_pm_ctx.mounted = false;
    s_pm_ctx.suspended = false;
    s_pm_ctx.lock_enabled = true;
    s_pm_ctx.lock_held = false;
    return ESP_OK;
}

esp_err_t tinyusb_pm_deinit(void)
{
    if (s_pm_ctx.lock != NULL) {
        ESP_RETURN_ON_ERROR(tinyusb_pm_lock_release(), TAG, "Failed to release PM lock");
        ESP_RETURN_ON_ERROR(esp_pm_lock_delete(s_pm_ctx.lock), TAG, "Failed to delete PM lock");
        s_pm_ctx.lock = NULL;
    }

    s_pm_ctx.mounted = false;
    s_pm_ctx.suspended = false;
    s_pm_ctx.lock_enabled = false;
    s_pm_ctx.lock_held = false;
    return ESP_OK;
}

void tinyusb_pm_on_event(tinyusb_event_id_t event)
{
    if (!s_pm_ctx.lock_enabled) {
        return;
    }

    switch (event) {
    case TINYUSB_EVENT_ATTACHED:
        s_pm_ctx.mounted = true;
        tinyusb_pm_lock_acquire();
        break;
    case TINYUSB_EVENT_DETACHED:
        s_pm_ctx.mounted = false;
        s_pm_ctx.suspended = false;
        tinyusb_pm_lock_release();
        break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
    case TINYUSB_EVENT_SUSPENDED:
        s_pm_ctx.suspended = true;
        tinyusb_pm_lock_release();
        break;
#endif // CONFIG_TINYUSB_SUSPEND_CALLBACK
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
    case TINYUSB_EVENT_RESUMED:
        s_pm_ctx.suspended = false;
        tinyusb_pm_lock_acquire();
        break;
#endif // CONFIG_TINYUSB_RESUME_CALLBACK
    default:
        return;
    }
}

esp_err_t tinyusb_pm_lock_check(esp_pm_lock_instance_stats_t *stats)
{
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock_enabled && s_pm_ctx.lock != NULL, ESP_ERR_INVALID_STATE, TAG, "PM lock is not enabled");
    return esp_pm_lock_get_stats(s_pm_ctx.lock, stats);
}

#if !CONFIG_TINYUSB_SUSPEND_CALLBACK
esp_err_t tinyusb_pm_on_suspend(void)
{
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock_enabled, ESP_ERR_NOT_ALLOWED, TAG, "PM lock is not enabled");

    if (s_pm_ctx.suspended) {
        return ESP_OK;
    }

    s_pm_ctx.suspended = true;
    return tinyusb_pm_lock_release();
}
#endif // !CONFIG_TINYUSB_SUSPEND_CALLBACK

#if !CONFIG_TINYUSB_RESUME_CALLBACK
esp_err_t tinyusb_pm_on_resume(void)
{
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock_enabled, ESP_ERR_NOT_ALLOWED, TAG, "PM lock is not enabled");

    if (!s_pm_ctx.suspended) {
        return ESP_OK;
    }

    s_pm_ctx.suspended = false;
    return tinyusb_pm_lock_acquire();
}
#endif // !CONFIG_TINYUSB_RESUME_CALLBACK
