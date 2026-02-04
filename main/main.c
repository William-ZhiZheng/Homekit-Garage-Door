#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "hap.h"
#include "hap_apple_servs.h"
#include "hap_apple_chars.h"
#include "hap_fw_upgrade.h"

#include "garage_door.h"

static const char *TAG = "HOMEKIT_GARAGE";

// WiFi Configuration - Update these with your credentials
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// HomeKit Setup Code - Format: XXX-XX-XXX
#define HOMEKIT_SETUP_CODE      "111-22-333"
#define HOMEKIT_SETUP_ID        "1QJ8"

// GPIO Configuration
#define GPIO_RELAY              GPIO_NUM_5    // Relay control pin
#define GPIO_SENSOR_OPEN        GPIO_NUM_18   // Open limit switch
#define GPIO_SENSOR_CLOSED      GPIO_NUM_19   // Closed limit switch
#define OPERATION_TIME_MS       15000         // 15 seconds to open/close

// Global HAP accessory object
static hap_acc_t *garage_accessory = NULL;

/**
 * WiFi Event Handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/**
 * Initialize WiFi in Station mode
 */
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization complete");
}

/**
 * HomeKit Garage Door Characteristic Read/Write Callbacks
 */
static int garage_door_read_current_state(hap_char_t *hc, hap_status_t *status_code, void *serv_priv, void *read_priv)
{
    garage_door_state_t state = garage_door_get_current_state();

    hap_val_t new_val;
    new_val.u = (uint8_t)state;

    hap_char_update_val(hc, &new_val);
    *status_code = HAP_STATUS_SUCCESS;

    ESP_LOGI(TAG, "Read current state: %d", state);
    return HAP_SUCCESS;
}

static int garage_door_read_target_state(hap_char_t *hc, hap_status_t *status_code, void *serv_priv, void *read_priv)
{
    garage_door_target_state_t state = garage_door_get_target_state();

    hap_val_t new_val;
    new_val.u = (uint8_t)state;

    hap_char_update_val(hc, &new_val);
    *status_code = HAP_STATUS_SUCCESS;

    ESP_LOGI(TAG, "Read target state: %d", state);
    return HAP_SUCCESS;
}

static int garage_door_write_target_state(hap_write_data_t write_data[], int count,
                                          void *serv_priv, void *write_priv)
{
    int i, ret = HAP_SUCCESS;
    hap_write_data_t *write;

    for (i = 0; i < count; i++) {
        write = &write_data[i];

        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_TARGET_DOOR_STATE)) {
            garage_door_target_state_t target = (garage_door_target_state_t)write->val.u;
            ESP_LOGI(TAG, "Received target state: %d", target);

            // Set the target state
            garage_door_set_target_state(target);

            // Update the characteristic
            hap_char_update_val(write->hc, &(write->val));

            // Update current state characteristic to show operation in progress
            hap_char_t *current_state_char = hap_serv_get_char_by_uuid(
                hap_char_get_parent(write->hc),
                HAP_CHAR_UUID_CURRENT_DOOR_STATE
            );
            if (current_state_char) {
                hap_val_t current_val;
                current_val.u = (uint8_t)garage_door_get_current_state();
                hap_char_update_val(current_state_char, &current_val);
            }

            *(write->status) = HAP_STATUS_SUCCESS;
        } else {
            *(write->status) = HAP_STATUS_RES_ABSENT;
        }
    }

    return ret;
}

static int garage_door_read_obstruction(hap_char_t *hc, hap_status_t *status_code, void *serv_priv, void *read_priv)
{
    bool obstruction = garage_door_get_obstruction_detected();

    hap_val_t new_val;
    new_val.b = obstruction;

    hap_char_update_val(hc, &new_val);
    *status_code = HAP_STATUS_SUCCESS;

    ESP_LOGI(TAG, "Read obstruction: %d", obstruction);
    return HAP_SUCCESS;
}

/**
 * Create HomeKit Garage Door Accessory
 */
static void garage_door_accessory_create(void)
{
    // Create accessory object
    garage_accessory = hap_acc_create((hap_acc_cfg_t){
        .name = "Garage Door",
        .model = "ESP32-GarageDoor",
        .manufacturer = "ESP",
        .serial_num = "001122334455",
        .fw_rev = "1.0.0",
        .hw_rev = NULL,
        .pv = "1.1.0",
        .identify_routine = NULL,
        .cid = HAP_CID_GARAGE_DOOR_OPENER,
    });

    if (!garage_accessory) {
        ESP_LOGE(TAG, "Failed to create accessory");
        return;
    }

    // Add firmware upgrade service
    hap_serv_t *fw_upgrade_serv = hap_serv_fw_upgrade_create();
    if (fw_upgrade_serv) {
        hap_acc_add_serv(garage_accessory, fw_upgrade_serv);
    }

    // Create Garage Door Opener Service
    hap_serv_t *garage_service = hap_serv_garage_door_opener_create(
        GARAGE_DOOR_CLOSED,           // Initial current state
        GARAGE_DOOR_TARGET_CLOSED,    // Initial target state
        false                          // Initial obstruction state
    );

    if (!garage_service) {
        ESP_LOGE(TAG, "Failed to create garage door service");
        hap_acc_delete(garage_accessory);
        garage_accessory = NULL;
        return;
    }

    // Set read callbacks
    hap_serv_set_read_cb(garage_service, garage_door_read_current_state);

    // Set write callback for target state
    hap_serv_set_write_cb(garage_service, garage_door_write_target_state);

    // Get individual characteristics and set callbacks
    hap_char_t *current_state_char = hap_serv_get_char_by_uuid(garage_service, HAP_CHAR_UUID_CURRENT_DOOR_STATE);
    hap_char_t *target_state_char = hap_serv_get_char_by_uuid(garage_service, HAP_CHAR_UUID_TARGET_DOOR_STATE);
    hap_char_t *obstruction_char = hap_serv_get_char_by_uuid(garage_service, HAP_CHAR_UUID_OBSTRUCTION_DETECTED);

    if (current_state_char) {
        hap_char_set_read_cb(current_state_char, garage_door_read_current_state);
    }
    if (target_state_char) {
        hap_char_set_read_cb(target_state_char, garage_door_read_target_state);
    }
    if (obstruction_char) {
        hap_char_set_read_cb(obstruction_char, garage_door_read_obstruction);
    }

    // Add service to accessory
    hap_acc_add_serv(garage_accessory, garage_service);

    // Add accessory to HAP
    hap_add_accessory(garage_accessory);

    ESP_LOGI(TAG, "Garage Door accessory created");
}

/**
 * HomeKit Thread - Initializes and starts HAP
 */
static void homekit_thread(void *arg)
{
    hap_acc_cfg_t cfg = {
        .unique_id = "GARAGE001",
        .setup_code = HOMEKIT_SETUP_CODE,
        .setup_id = HOMEKIT_SETUP_ID,
        .max_pairings = 8,
        .model = "ESP32-Garage",
        .manufacturer = "ESP",
    };

    // Initialize HAP
    hap_init(HAP_TRANSPORT_WIFI);
    hap_set_setup_code(cfg.setup_code);
    hap_set_setup_id(cfg.setup_id);

    // Create accessory
    garage_door_accessory_create();

    // Start HAP
    hap_start();

    ESP_LOGI(TAG, "HomeKit started. Setup code: %s", cfg.setup_code);
    ESP_LOGI(TAG, "Scan QR code or enter setup code in Home app");

    // Delete task
    vTaskDelete(NULL);
}

/**
 * Main application entry point
 */
void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting HomeKit Garage Door Opener");

    // Initialize WiFi
    wifi_init();

    // Initialize garage door hardware
    garage_door_config_t garage_config = {
        .relay_gpio = GPIO_RELAY,
        .sensor_open_gpio = GPIO_SENSOR_OPEN,
        .sensor_closed_gpio = GPIO_SENSOR_CLOSED,
        .operation_time = OPERATION_TIME_MS
    };
    garage_door_init(&garage_config);

    // Wait for WiFi connection
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Start HomeKit
    xTaskCreate(homekit_thread, "homekit", 8192, NULL, 1, NULL);

    ESP_LOGI(TAG, "App initialization complete");
}