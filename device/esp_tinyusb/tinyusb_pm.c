/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_pm.h"
#include "soc/soc_caps.h"
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
    bool mounted;                   /*!< USB device configured by the host */
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

#if SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM
static esp_err_t tinyusb_pm_otg_suspend_prepare(void)
{
    const esp_err_t err = tinyusb_set_otg_suspend_state(true);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGD(TAG, "USB OTG light-sleep prepare not supported on this target/port");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to prepare USB OTG for light sleep: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "USB OTG prepared for light sleep");
    }
    return err;
}

static esp_err_t tinyusb_pm_otg_suspend_restore(void)
{
    esp_err_t err = tinyusb_set_otg_suspend_state(false);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGD(TAG, "USB OTG light-sleep restore not supported on this target/port");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restore USB OTG suspend state: %s", esp_err_to_name(err));
        return err;
    }

    err = tinyusb_clear_otg_wakeup_status();
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear USB OTG wakeup status: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "USB OTG light-sleep state restored");
    }
    return err;
}

#endif // SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM

static void tinyusb_pm_on_usb_suspended(void)
{
#if SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM
    if (!s_pm_ctx.lock_enabled) {
        return;
    }
    (void)tinyusb_pm_otg_suspend_prepare();
#else
    return;
#endif // SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM
}

static void tinyusb_pm_on_usb_resumed(void)
{
#if SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM
    if (!s_pm_ctx.lock_enabled) {
        return;
    }
    (void)tinyusb_pm_otg_suspend_restore();
#else
    return;
#endif // SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM
}

// ---------------------------------------------- Light sleep callbacks ------------------------------------------------

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS && SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM
static esp_err_t tinyusb_pm_light_sleep_enter_cb(int64_t sleep_time_us, void *arg)
{
    (void)sleep_time_us;
    (void)arg;

    if (!s_pm_ctx.lock_enabled || !s_pm_ctx.suspended) {
        return ESP_OK;
    }

    // Using ISR variant, because the callback is called from critical section
    ESP_RETURN_ON_ERROR_ISR(tinyusb_set_otg_suspend_state(true), TAG, "Light sleep exit: failed to set OTG suspend state");
    return ESP_OK;
}

static esp_err_t tinyusb_pm_light_sleep_exit_cb(int64_t sleep_time_us, void *arg)
{
    (void)sleep_time_us;
    (void)arg;

    // TODO CHeck if we need this check.
    if (!s_pm_ctx.lock_enabled || !s_pm_ctx.suspended) {
        return ESP_OK;
    }

    // Using ISR variant, because the callback is called from critical section
    ESP_RETURN_ON_ERROR_ISR(tinyusb_set_otg_suspend_state(false), TAG, "Light sleep exit: failed to clear OTG suspend state");
    ESP_RETURN_ON_ERROR_ISR(tinyusb_clear_otg_wakeup_status(), TAG, "Light sleep exit: failed to clear OTG wakeup status");
    return ESP_OK;
}

static esp_pm_sleep_cbs_register_config_t auto_light_sleep_cbs = {
    .enter_cb = tinyusb_pm_light_sleep_enter_cb,
    .exit_cb = tinyusb_pm_light_sleep_exit_cb,
};

#endif // CONFIG_PM_LIGHT_SLEEP_CALLBACKS && SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM

// --------------------------------------------------- Public API ------------------------------------------------------

esp_err_t tinyusb_pm_init(const tinyusb_pm_config_t *config)
{
    esp_err_t ret = ESP_FAIL;
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "Config can't be NULL");

    // Check if the user enabled the lock
    if (!config->no_light_sleep_lock) {
        s_pm_ctx.lock_enabled = false;
        ESP_LOGD(TAG, "PM lock disabled in configuration");
        return ESP_OK;
    }

    // Check if PM has been initialized and automatic light sleep has been enabled by user
    esp_pm_config_t pm_config;
    if (esp_pm_get_configuration(&pm_config) == ESP_OK && !pm_config.light_sleep_enable) {
        ESP_LOGW(TAG, "no_light_sleep_lock is enabled but automatic light sleep is disabled. "
                 "Configure esp_pm_configure() with light_sleep_enable=true to allow automatic light sleep "
                 "when the USB PM lock is released");
    }

    // Initialize and acquire the lock
    esp_pm_lock_handle_t pm_lock;
    ESP_RETURN_ON_FALSE(s_pm_ctx.lock == NULL, ESP_ERR_INVALID_STATE, TAG, "PM module already initialized");
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "usb_device", &pm_lock), TAG, "Failed to create PM lock");
    ESP_GOTO_ON_ERROR(tinyusb_pm_lock_acquire(), acquire_err, TAG, "Failed to acquire PM lock on init");

    // Register light sleep enter and exit callbacks
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS && SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM
    ESP_GOTO_ON_ERROR(esp_pm_light_sleep_register_cbs(&auto_light_sleep_cbs), register_err, TAG, "Failed to register light sleep callbacks");
#endif // CONFIG_PM_LIGHT_SLEEP_CALLBACKS && SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM

    // Assign PM context
    s_pm_ctx.mounted = false;
    s_pm_ctx.suspended = false;
    s_pm_ctx.lock_enabled = true;
    s_pm_ctx.lock = pm_lock;

    ESP_LOGD(TAG, "PM lock initialized and acquired");
    ret = ESP_OK;
    return ret;

register_err:
    ESP_ERROR_CHECK(tinyusb_pm_lock_release());
acquire_err:
    ESP_ERROR_CHECK(esp_pm_lock_delete(pm_lock));
    return ret;
}

esp_err_t tinyusb_pm_deinit(void)
{
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS && SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM
    ESP_RETURN_ON_ERROR(esp_pm_light_sleep_unregister_cbs(&auto_light_sleep_cbs), TAG, "Failed to unregister light sleep callbacks");
#endif // CONFIG_PM_LIGHT_SLEEP_CALLBACKS && SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM

#if SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM
    if (s_pm_ctx.suspended) {
        (void)tinyusb_pm_otg_suspend_restore();
    }
#endif // SOC_PM_SUPPORT_USB_WAKEUP && SOC_USB_UTMI_PHY_NUM

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
        s_pm_ctx.mounted = false;
        s_pm_ctx.suspended = false;
        PM_BREAK_ON_ERROR(tinyusb_pm_lock_acquire(), "Failed to acquire PM lock on detach");
        break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
    case TINYUSB_EVENT_SUSPENDED:
        PM_BREAK_ON_ERROR(tinyusb_pm_lock_release(), "Failed to release PM lock on suspend");
        s_pm_ctx.suspended = true;
        tinyusb_pm_on_usb_suspended();
        break;
#endif // CONFIG_TINYUSB_SUSPEND_CALLBACK
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
    case TINYUSB_EVENT_RESUMED:
        PM_BREAK_ON_ERROR(tinyusb_pm_lock_acquire(), "Failed to acquire PM lock on resume");
        s_pm_ctx.suspended = false;
        tinyusb_pm_on_usb_resumed();
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
        tinyusb_pm_on_usb_suspended();
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
        tinyusb_pm_on_usb_resumed();
    }
    return err;
}
#endif // !CONFIG_TINYUSB_RESUME_CALLBACK
