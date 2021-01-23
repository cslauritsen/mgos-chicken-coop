#include <mgos_gpio.h>
#include <mgos_sys_config.h>
#include <common/cs_dbg.h>
#include <mgos_app.h>
#include <mgos_system.h>
#include <mgos_time.h>
#include "door.h"

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
    mgos_gpio_write(door->activate_pin_a, DOOR_HBRIDGE_INACTIVE);
    mgos_gpio_write(door->activate_pin_b, DOOR_HBRIDGE_INACTIVE); 
    LOG(LL_INFO, ("Door '%s' all_stop issued", door->name));
}

static bool _Door_open(Door *door) {
    if (DOOR_OPEN == Door_get_state(door)) {
        LOG(LL_INFO, ("Door %s already open, doing nothing", door->name));
        return false; 
    }
    Door_all_stop(door);
    door->desired_state = DOOR_OPEN;
    LOG(LL_INFO, ("Raising door %s", door->name));
    Door_activate(door);
    return true;
}

static bool _Door_close(Door *door) {
    if (DOOR_CLOSED == Door_get_state(door)) {
        LOG(LL_INFO, ("Door '%s' already closed, doing nothing", door->name));
        return false; 
    }
    Door_all_stop(door);
    door->desired_state = DOOR_CLOSED;
    LOG(LL_INFO, ("Lowering door '%s'", door->name));
    Door_activate(door);
    return true;
}

bool Door_activate(void *arg) {
    if (!arg) {
        return false;
    }
    Door *door = (Door*) arg;
    if (Door_validate(door) != DE_OK) {
        return false;
    }
    bool ret = true;
    Door_all_stop(door);
    if (mgos_gpio_read(door->activate_pin_a) || mgos_gpio_read(door->activate_pin_b)) {
        ret = false;
    }
    else {
        LOG(LL_INFO, ("Activating %s door pin %d", door->name, door->next_activation ? door->activate_pin_a : door->activate_pin_b));
        mgos_gpio_write(door->next_activation ? door->activate_pin_a : door->activate_pin_b, true);
    }
    door->next_activation = !door->next_activation;
    return ret;
}

static void _door_interrupt(int pin, void *arg) {
    Door *door = (Door*) arg;
    if (!Door_validate(door)) {
        LOG(LL_WARN, ("invalid door pointer"));
        return;
    }
    switch(door->desired_state) {
        case DOOR_CLOSED:
        if (pin == door->closed_contact_pin) {
            Door_all_stop(door);
        }
        break;
        case DOOR_OPEN:
        if (pin == door->open_contact_pin) {
            Door_all_stop(door);
        }
        break;
        default:
            Door_all_stop(door);
        break;
    }
}

void Door_init(Door *door) {
    if (!Door_validate(door)) {
        LOG(LL_ERROR, ("Cannot initiate invalid door!"));
    }
    mgos_gpio_setup_input(door->open_contact_pin, MGOS_GPIO_PULL_UP);
    mgos_gpio_setup_input(door->closed_contact_pin, MGOS_GPIO_PULL_UP);
    mgos_gpio_setup_output(door->activate_pin_a, false);
    mgos_gpio_setup_output(door->activate_pin_b, false);
    mgos_gpio_set_int_handler(door->open_contact_pin, MGOS_GPIO_INT_EDGE_NEG, _door_interrupt, door);
    LOG(LL_INFO, ("Setup OPEN interrupt on pin %d for door %s", door->open_contact_pin, door->name));
    mgos_gpio_set_int_handler(door->closed_contact_pin, MGOS_GPIO_INT_EDGE_NEG, _door_interrupt, door);
    LOG(LL_INFO, ("Setup CLOSE interrupt on pin %d for door %s", door->closed_contact_pin, door->name));
    Door_all_stop(door);
}

Door * Door_new(int open_contact, int closed_contact, int act_pin_a, int act_pin_b, char *name) {
    Door *door = calloc(1, sizeof(Door));
    door->struct_id = DOOR_STRUCT_ID;
    door->open_contact_pin = open_contact;
    door->closed_contact_pin = closed_contact;
    door->activate_pin_a = act_pin_a;
    door->activate_pin_b = act_pin_b;
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
    return _Door_close(door);
}

bool Door_open(void *adoor, int flags) {
    Door *door = (Door *) adoor;
    return _Door_open(door);
}

DoorError Door_validate(void * arg) {
  if (arg == NULL) {
    LOG(LL_ERROR, ("Invalid NULL door pointer"));
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
  if (!IS_PIN(door->activate_pin_a)) {
    LOG(LL_ERROR, ("Invalid activate pin A %d", door->activate_pin_a));
    return DE_BAD_PIN;
  }
  if (!IS_PIN(door->activate_pin_a)) {
    LOG(LL_ERROR, ("Invalid activate pin B %d", door->activate_pin_b));
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