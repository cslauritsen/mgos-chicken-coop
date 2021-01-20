#include <mgos_gpio.h>
#include <mgos_sys_config.h>
#include <common/cs_dbg.h>
#include <mgos_app.h>
#include <mgos_system.h>
#include <mgos_timers.h>
#include <mgos_time.h>
#include "door.h"

struct timer_data {
    Door *door;
    DoorState desired_state;
    double start_uptime;
    double max_run_seconds;
};

static mgos_timer_id timer_id = MGOS_INVALID_TIMER_ID;

DoorState Door_get_state(Door *door) {
    // false->low voltage means switch closed & state active, since these 
    // are ground-connected reed switches with pullup resistors
    bool isclosed = ! mgos_gpio_read(door->closed_contact_pin);
    bool isopen = ! mgos_gpio_read(door->open_contact_pin);

    if (isclosed && isopen) {
        return DOOR_ERR; // both cannot not be active
    }
    if (!isclosed && !isopen) {
        return DOOR_STUCK; // not fully open or closed
    }
    return isclosed ? DOOR_CLOSED : DOOR_OPEN;
}

void Door_all_stop(Door *door) {
    mgos_gpio_write(door->raise_activate_pin, DOOR_HBRIDGE_INACTIVE);
    mgos_gpio_write(door->lower_activate_pin, DOOR_HBRIDGE_INACTIVE); 
    LOG(LL_INFO, ("Door '%s' all_stop issued", door->name));
}

/**
 * Stops the motor (and repeated invocations of this function) if either:
 *  a. the desired state is reached
 *  b. the maximum motor run time is reached
 */
static void transition_cb(void *arg) {
    if (arg == NULL) {
        LOG(LL_ERROR, ("Invalid NULL callback arg"));
        return;
    }
    struct timer_data *td = (struct timer_data *) arg;
    DoorError de = Door_validate(td->door);
    if (DE_OK != de) {
        LOG(LL_ERROR, ("Door validation failed: %d", de));
    }
    // See if its time to stop the timer
    if (DE_OK != de || td->desired_state == Door_get_state(td->door) || mgos_uptime() - td->start_uptime > td->max_run_seconds) {
        Door_all_stop(td->door);
        if (timer_id != MGOS_INVALID_TIMER_ID) {
            mgos_clear_timer(timer_id);
            timer_id = MGOS_INVALID_TIMER_ID;
        }
        free(td);
    }
}

static bool _Door_open(Door *door) {
    if (DOOR_OPEN == Door_get_state(door)) {
        LOG(LL_INFO, ("Door %s already open, doing nothing", door->name));
        return false; 
    }
    LOG(LL_INFO, ("Raising door %s", door->name));
    mgos_gpio_write(door->raise_activate_pin, DOOR_HBRIDGE_ACTIVE);
    return true;
}

static bool _Door_close(Door *door) {
    if (DOOR_CLOSED == Door_get_state(door)) {
        LOG(LL_INFO, ("Door '%s' already closed, doing nothing", door->name));
        return false; 
    }
    LOG(LL_INFO, ("Lowering door '%s'", door->name));
    mgos_gpio_write(door->lower_activate_pin, DOOR_HBRIDGE_ACTIVE);
    return true;
}

bool Door_transition(Door *door, DoorState desiredState, int flags) {
    if (Door_validate(door) != DE_OK) {
        return false;
    }

    Door_all_stop(door);
    if (desiredState != DOOR_OPEN && desiredState != DOOR_CLOSED) {
        LOG(LL_INFO, ("Invalid door transition request, doing nothing"));
        return false;
    }

    // Attempt to limit the number of light-based triggerings
    if (DF_ISSET(flags, DF_LIGHT_TRIG)) {
        double min_seconds = mgos_sys_config_get_time_light_trigger_min_hours() * 3600;
        double seconds_since_last = mgos_uptime() - door->last_light_trigger;
        if (door->last_light_trigger > 0  && seconds_since_last > min_seconds) {
            LOG(LL_INFO, ("Insufficient seconds elapsed for light-triggered transition: %f", seconds_since_last));
            // not enough time has elapsed to permit another light-based trigger
            // therefore, ignore this request
            return false;
        }
        // Either this is the first light-triggered transition since boot, or
        // enough time has elapsed since the last light-triggered transition, or
        // the last trigger has been reset. 
        // Record the current trigger time and proceed with the transition
        door->last_light_trigger = mgos_uptime();
    }

    struct timer_data *td = calloc(1, sizeof(struct timer_data));
    td->door = door;
    td->desired_state = desiredState;
    td->start_uptime = mgos_uptime();
    td->max_run_seconds = (double) mgos_sys_config_get_time_door_motor_active_seconds();
    if (timer_id != MGOS_INVALID_TIMER_ID) {
        mgos_clear_timer(timer_id);
        timer_id = MGOS_INVALID_TIMER_ID;
    }
    timer_id = mgos_set_timer(250, MGOS_TIMER_REPEAT, transition_cb, td);

    if (DOOR_OPEN == desiredState) {
        _Door_open(door);
    }
    else if (DOOR_CLOSED == desiredState) {
        _Door_close(door);
    }
    return true;
}

void Door_init(Door *door) {
    mgos_gpio_setup_input(door->open_contact_pin, MGOS_GPIO_PULL_UP);
    mgos_gpio_setup_input(door->closed_contact_pin, MGOS_GPIO_PULL_UP);
    mgos_gpio_setup_output(door->lower_activate_pin, false);
    mgos_gpio_setup_output(door->raise_activate_pin, false);
    Door_all_stop(door);
}

Door * Door_new(int open_contact, int closed_contact, int lower_act_pin, int raise_act_pin, char *name) {
    Door *door = calloc(1, sizeof(Door));
    door->struct_id = DOOR_STRUCT_ID;
    door->open_contact_pin = open_contact;
    door->closed_contact_pin = closed_contact;
    door->lower_activate_pin = lower_act_pin;
    door->raise_activate_pin = raise_act_pin;
    for (int i=0; name != NULL && *name && i < sizeof(door->name)-1; i++) {
       door->name[i] = *name++;
    }

    Door_init(door);
    return door;
}

void *Door_north_new(void) {
    Door *north_door = Door_new(
      mgos_sys_config_get_pins_north_door_open_contact(), 
      mgos_sys_config_get_pins_north_door_closed_contact(), 
      mgos_sys_config_get_pins_north_door_lower(), 
      mgos_sys_config_get_pins_north_door_raise(), 
      "NORTH");
    return (void*) north_door;
}

char *Door_status(void * vdoor) {
    Door *door = (Door *) vdoor;
    switch(Door_get_state(door)) {
        case DOOR_OPEN:
            return "open";
        case DOOR_CLOSED:
            return "closed";
        case DOOR_STUCK:
            return "stuck";
        default:
            return "unknown";
    }
}

bool Door_close(void *adoor, int flags) {
    Door *door = (Door *) adoor;
    return Door_transition(door, DOOR_CLOSED, flags);
}

bool Door_open(void *adoor, int flags) {
    Door *door = (Door *) adoor;
    return Door_transition(door, DOOR_OPEN, flags);
}

DoorError Door_validate(void * arg) {
  if (arg == NULL) {
    LOG(LL_ERROR, ("Invalid NULL door arg"));
    return DE_NULL;
  }
  Door *door = (Door *) arg;
  if (DOOR_STRUCT_ID != door->struct_id) {
    LOG(LL_ERROR, ("Unrecognized door structure"));
    return DE_STRUCT_ID;
  }
  if (!IS_PIN(door->closed_contact_pin)) {
    LOG(LL_ERROR, ("Invalid closed contact pin %d", door->closed_contact_pin));
    return DE_BAD_PIN;
  }
  if (!IS_PIN(door->open_contact_pin)) {
    LOG(LL_ERROR, ("Invalid open contact pin %d", door->open_contact_pin));
    return DE_BAD_PIN;
  }
  if (!IS_PIN(door->lower_activate_pin)) {
    LOG(LL_ERROR, ("Invalid lower activate pin %d", door->lower_activate_pin));
    return DE_BAD_PIN;
  }
  if (!IS_PIN(door->raise_activate_pin)) {
    LOG(LL_ERROR, ("Invalid raise activate pin %d", door->raise_activate_pin));
    return DE_BAD_PIN;
  }
  if (door->name == NULL) { 
    LOG(LL_ERROR, ("Invalid door name"));
    return DE_NAME_LEN;
  }
  if (strnlen(door->name, sizeof(door->name)) >= sizeof(door->name)) {
    LOG(LL_ERROR, ("Door name too long"));
    return DE_NAME_LEN;
  }
  return DE_OK;
}