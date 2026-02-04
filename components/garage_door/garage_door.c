#include "garage_door.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"

static const char *TAG = "GARAGE_DOOR";

// Global state
static garage_door_config_t g_config;
static garage_door_state_t g_current_state = GARAGE_DOOR_CLOSED;
static garage_door_target_state_t g_target_state = GARAGE_DOOR_TARGET_CLOSED;
static bool g_obstruction_detected = false;
static TimerHandle_t g_operation_timer = NULL;

// Forward declarations
static void garage_door_update_state(void);
static void operation_timer_callback(TimerHandle_t timer);
static void trigger_relay(void);

void garage_door_init(garage_door_config_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return;
    }

    memcpy(&g_config, config, sizeof(garage_door_config_t));

    // Configure relay GPIO (output)
    gpio_config_t relay_conf = {
        .pin_bit_mask = (1ULL << g_config.relay_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&relay_conf);
    gpio_set_level(g_config.relay_gpio, 0);

    // Configure sensor GPIOs (input with pullup)
    gpio_config_t sensor_conf = {
        .pin_bit_mask = (1ULL << g_config.sensor_open_gpio) | (1ULL << g_config.sensor_closed_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&sensor_conf);

    // Create operation timer
    g_operation_timer = xTimerCreate(
        "garage_timer",
        pdMS_TO_TICKS(g_config.operation_time),
        pdFALSE,  // One-shot timer
        NULL,
        operation_timer_callback
    );

    // Determine initial state based on sensors
    garage_door_update_state();

    ESP_LOGI(TAG, "Garage door initialized on GPIO %d", g_config.relay_gpio);
}

static void trigger_relay(void) {
    // Pulse the relay (simulates button press)
    gpio_set_level(g_config.relay_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(500));  // 500ms pulse
    gpio_set_level(g_config.relay_gpio, 0);
    ESP_LOGI(TAG, "Relay triggered");
}

static void garage_door_update_state(void) {
    bool is_open = (gpio_get_level(g_config.sensor_open_gpio) == 0);   // Active low
    bool is_closed = (gpio_get_level(g_config.sensor_closed_gpio) == 0); // Active low

    if (is_closed) {
        g_current_state = GARAGE_DOOR_CLOSED;
        g_target_state = GARAGE_DOOR_TARGET_CLOSED;
    } else if (is_open) {
        g_current_state = GARAGE_DOOR_OPEN;
        g_target_state = GARAGE_DOOR_TARGET_OPEN;
    }
    // If neither sensor is active, state remains as OPENING/CLOSING/STOPPED

    ESP_LOGI(TAG, "State updated: current=%d, target=%d", g_current_state, g_target_state);
}

static void operation_timer_callback(TimerHandle_t timer) {
    // Timer expired, update state
    if (g_target_state == GARAGE_DOOR_TARGET_OPEN) {
        g_current_state = GARAGE_DOOR_OPEN;
    } else {
        g_current_state = GARAGE_DOOR_CLOSED;
    }
    ESP_LOGI(TAG, "Operation completed, state: %d", g_current_state);
}

void garage_door_set_target_state(garage_door_target_state_t target) {
    ESP_LOGI(TAG, "Setting target state: %d", target);

    if (target == g_target_state) {
        ESP_LOGI(TAG, "Already at target state");
        return;
    }

    g_target_state = target;

    // Update current state to reflect operation in progress
    if (target == GARAGE_DOOR_TARGET_OPEN) {
        g_current_state = GARAGE_DOOR_OPENING;
    } else {
        g_current_state = GARAGE_DOOR_CLOSING;
    }

    // Trigger the relay
    trigger_relay();

    // Start timer to simulate operation completion
    if (g_operation_timer) {
        xTimerReset(g_operation_timer, 0);
    }
}

garage_door_state_t garage_door_get_current_state(void) {
    // Check sensors for real-time state
    bool is_open = (gpio_get_level(g_config.sensor_open_gpio) == 0);
    bool is_closed = (gpio_get_level(g_config.sensor_closed_gpio) == 0);

    if (is_closed && g_current_state != GARAGE_DOOR_CLOSED) {
        g_current_state = GARAGE_DOOR_CLOSED;
    } else if (is_open && g_current_state != GARAGE_DOOR_OPEN) {
        g_current_state = GARAGE_DOOR_OPEN;
    }

    return g_current_state;
}

garage_door_target_state_t garage_door_get_target_state(void) {
    return g_target_state;
}

bool garage_door_get_obstruction_detected(void) {
    // Simple obstruction detection: if state doesn't match target after timeout
    // In a real implementation, this would use additional sensors
    return g_obstruction_detected;
}
