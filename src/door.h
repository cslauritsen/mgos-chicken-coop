#ifndef __door_h__
#define __door_h__

#include "mgos_app.h"
#include "mgos_system.h"
#include "mgos_timers.h"

#define DOOR_STRUCT_ID 0x6452 // printable bytes 'dR'
#define DF_ISSET(flags, flag) (~(flag) & (flags)) == (flag)
#define IS_PIN(x) ((x) >= 0 && (x) <= 50)
#define DF_LIGHT_TRIG 0x1

typedef enum
{
    DOOR_CLOSED,
    DOOR_OPEN,
    DOOR_STUCK,
    DOOR_ERR
} DoorState;

typedef struct
{
    uint16_t struct_id;
    uint8_t open_contact_pin;
    uint8_t open_indicator_pin;
    uint8_t closed_contact_pin;
    uint8_t closed_indicator_pin;
    uint8_t activate_pin_a;
    uint8_t activate_pin_b;
    bool next_activation;
    char name[16];
    /**
     * a value from mgos_uptime() to limit the number of 
     * attempted light-sensor triggered transitions
     */
    double last_light_trigger;
    double debounce_millis;
} Door;

typedef enum
{
    DE_OK,
    DE_BAD_PIN,
    DE_NAME_LEN,
    DE_STRUCT_ID,
    DE_NULL
} DoorError;

#define DOOR_HBRIDGE_ACTIVE 1
#define DOOR_HBRIDGE_INACTIVE 0

DoorState Door_get_state(Door *door);
void Door_all_stop(void *door);
bool Door_transition(Door *door, DoorState desiredState, int flags);
void Door_init(Door *door);
Door *Door_new(int open_contact, 
    int closed_contact, 
    int lower_act_pin, 
    int raise_act_pin, 
    const char *name,
    int open_indicator_pin,
    int closed_indicator_pin);
// FFI wrappers
void *Door_north_new(void);
char *Door_status(void *door);
bool Door_close(void *door, int flags);
bool Door_open(void *door, int flags);
DoorError Door_validate(void *door);
void Door_indicate(void *door);
#endif