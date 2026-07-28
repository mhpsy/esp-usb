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
 * Macro used in switch-case branch, to break off the case when error is returned
 */
#define PM_BREAK_ON_ERROR(x, fail_msg) do {     \
        esp_err_t err_rc_ = (x);                \
        if (unlikely(err_rc_ != ESP_OK)) {      \
            ESP_LOGW(TAG, fail_msg);            \
            break;                              \
        }                                       \
    } while (0)

/**
 * @brief TinyUSB power management context
 */
typedef struct {
    bool mounted;                   /*!< USB Device mounted/umounted with PM lock held */
    bool suspended;                 /*!< USB Device suspended with PM lock released */
    bool lock_enabled;              /*!< PM lock enabled/disabled by user */
    esp_pm_lock_handle_t lock;      /*!< PM lock object */
} tinyusb_pm_ctx_t;

static tinyusb_pm_ctx_t s_pm_ctx;

static esp_err_t tinyusb_pm_lock_stats_read(esp_pm_lock_instance_stats_t *stats)
{
    ESP_RETURN_ON_FALSE(stats, ESP_ERR_INVALID_ARG, TAG, "Stats can't be NULL");
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock != NULL, ESP_ERR_INVALID_STATE, TAG, "PM lock is not initialized");
    return esp_pm_lock_get_stats(s_pm_ctx.lock, stats);
}

static esp_err_t tinyusb_pm_lock_acquire(void)
{
    if (!s_pm_ctx.lock_enabled) {
        return ESP_OK;
    }

    esp_pm_lock_instance_stats_t stats;
    ESP_RETURN_ON_ERROR(tinyusb_pm_lock_stats_read(&stats), TAG, "Failed to read PM lock stats");

    if (stats.acquired > 0) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Acquiring PM lock");
    const esp_err_t err = esp_pm_lock_acquire(s_pm_ctx.lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lock acquire error %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "PM lock acquired");
    }
    return err;
}

static esp_err_t tinyusb_pm_lock_release(void)
{
    if (!s_pm_ctx.lock_enabled) {
        return ESP_OK;
    }

    esp_pm_lock_instance_stats_t stats;
    ESP_RETURN_ON_ERROR(tinyusb_pm_lock_stats_read(&stats), TAG, "Failed to read PM lock stats");

    if (stats.acquired == 0) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Releasing PM lock");
    const esp_err_t err = esp_pm_lock_release(s_pm_ctx.lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lock release error %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "PM lock released");
    }
    return err;
}

esp_err_t tinyusb_pm_init(const tinyusb_pm_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "Config can't be NULL");

    // Check if the user enabled the lock
    if (!config->no_light_sleep_lock) {
        s_pm_ctx.lock_enabled = false;
        ESP_LOGD(TAG, "PM lock disabled in configuration");
        return ESP_OK;
    }

    // Initialize lock
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock == NULL, ESP_ERR_INVALID_STATE, TAG, "PM module already initialized");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "tusb", &s_pm_ctx.lock), TAG, "Failed to create PM lock");

    s_pm_ctx.mounted = false;
    s_pm_ctx.suspended = false;
    s_pm_ctx.lock_enabled = true;
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
    return ESP_OK;
}

void tinyusb_pm_on_event(tinyusb_event_id_t event)
{
    if (!s_pm_ctx.lock_enabled) {
        return;
    }

    switch (event) {
    case TINYUSB_EVENT_ATTACHED:
        PM_BREAK_ON_ERROR(tinyusb_pm_lock_acquire(), "Failed to acquire PM lock on attach");
        s_pm_ctx.mounted = true;
        break;
    case TINYUSB_EVENT_DETACHED:
        PM_BREAK_ON_ERROR(tinyusb_pm_lock_release(), "Failed to release PM lock on detach");
        s_pm_ctx.mounted = false;
        s_pm_ctx.suspended = false;
        break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
    case TINYUSB_EVENT_SUSPENDED:
        PM_BREAK_ON_ERROR(tinyusb_pm_lock_release(), "Failed to release PM lock on suspend");
        s_pm_ctx.suspended = true;
        break;
#endif // CONFIG_TINYUSB_SUSPEND_CALLBACK
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
    case TINYUSB_EVENT_RESUMED:
        PM_BREAK_ON_ERROR(tinyusb_pm_lock_acquire(), "Failed to acquire PM lock on resume");
        s_pm_ctx.suspended = false;
        break;
#endif // CONFIG_TINYUSB_RESUME_CALLBACK
    default:
        ESP_LOGW(TAG, "Unhandled event %d", event);
        return;
    }
}

esp_err_t tinyusb_pm_lock_check(esp_pm_lock_instance_stats_t *stats)
{
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock_enabled && s_pm_ctx.lock != NULL, ESP_ERR_INVALID_STATE, TAG, "PM lock is not enabled");

    const esp_err_t err = esp_pm_lock_get_stats(s_pm_ctx.lock, stats);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "PM lock stats: acquired=%zu", stats->acquired);
    }
    return err;
}

#if !CONFIG_TINYUSB_SUSPEND_CALLBACK
esp_err_t tinyusb_pm_on_suspend(void)
{
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock_enabled, ESP_ERR_NOT_ALLOWED, TAG, "PM lock is not enabled");

    ESP_LOGD(TAG, "User suspend notify (mounted=%d, suspended=%d)", s_pm_ctx.mounted, s_pm_ctx.suspended);

    if (s_pm_ctx.suspended) {
        esp_pm_lock_instance_stats_t stats;
        ESP_RETURN_ON_ERROR(tinyusb_pm_lock_stats_read(&stats), TAG, "Failed to read PM lock stats");

        if (stats.acquired == 0) {
            return ESP_OK;
        }
    }

    const esp_err_t err = tinyusb_pm_lock_release();
    if (err == ESP_OK) {
        s_pm_ctx.suspended = true;
    }
    return err;
}
#endif // !CONFIG_TINYUSB_SUSPEND_CALLBACK

#if !CONFIG_TINYUSB_RESUME_CALLBACK
esp_err_t tinyusb_pm_on_resume(void)
{
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock_enabled, ESP_ERR_NOT_ALLOWED, TAG, "PM lock is not enabled");

    ESP_LOGD(TAG, "User resume notify (mounted=%d, suspended=%d)", s_pm_ctx.mounted, s_pm_ctx.suspended);

    if (!s_pm_ctx.suspended) {
        return ESP_OK;
    }

    const esp_err_t err = tinyusb_pm_lock_acquire();
    if (err == ESP_OK) {
        s_pm_ctx.suspended = false;
    }
    return err;
}
#endif // !CONFIG_TINYUSB_RESUME_CALLBACK
