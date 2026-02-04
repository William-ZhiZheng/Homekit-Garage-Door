#ifndef GARAGE_DOOR_H
#define GARAGE_DOOR_H

#include <stdint.h>
#include <stdbool.h>

// Garage door states
typedef enum {
    GARAGE_DOOR_OPEN = 0,
    GARAGE_DOOR_CLOSED = 1,
    GARAGE_DOOR_OPENING = 2,
    GARAGE_DOOR_CLOSING = 3,
    GARAGE_DOOR_STOPPED = 4
} garage_door_state_t;

// Target states
typedef enum {
    GARAGE_DOOR_TARGET_OPEN = 0,
    GARAGE_DOOR_TARGET_CLOSED = 1
} garage_door_target_state_t;

// Configuration
typedef struct {
    int relay_gpio;           // GPIO for relay control
    int sensor_open_gpio;     // GPIO for open sensor (limit switch)
    int sensor_closed_gpio;   // GPIO for closed sensor (limit switch)
    uint32_t operation_time;  // Time it takes to open/close (milliseconds)
} garage_door_config_t;

// Function prototypes
void garage_door_init(garage_door_config_t *config);
void garage_door_set_target_state(garage_door_target_state_t target);
garage_door_state_t garage_door_get_current_state(void);
garage_door_target_state_t garage_door_get_target_state(void);
bool garage_door_get_obstruction_detected(void);

#endif // GARAGE_DOOR_H
