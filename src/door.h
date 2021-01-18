#ifndef __door_h__
#define __door_h__

#include "mgos_app.h"
#include "mgos_system.h"
#include "mgos_timers.h"

typedef enum { DOOR_CLOSED, DOOR_OPEN, DOOR_STUCK, DOOR_ERR} DoorState;

typedef struct { 
    int open_contact_pin;
    int closed_contact_pin;
    int lower_activate_pin;
    int raise_activate_pin;
    char name[8];
    mgos_timer_id active_timer;
} Door;


DoorState Door_get_state(Door *door);
void Door_all_stop(Door *door);
bool Door_open(Door *door);
bool Door_close(Door *door);
void Door_transition(Door *door, DoorState desiredState);
void Door_init(Door *door);
Door * Door_new(int open_contact, int closed_contact, int lower_act_pin, int raise_act_pin);
#endif