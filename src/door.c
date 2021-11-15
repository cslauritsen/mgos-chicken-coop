#include <mgos_gpio.h>
#include <mgos_sys_config.h>
#include <common/cs_dbg.h>
#include <mgos_app.h>
#include <mgos_system.h>
#include <mgos_time.h>
#include <mgos_mqtt.h>
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

void Door_all_stop(void *adoor) {
    Door *door = (Door*) adoor;
    if (DE_OK != Door_validate(door)) {
        LOG(LL_ERROR, ("Invalid door pointer"));
    }
    mgos_gpio_write(door->activate_pin_a, DOOR_HBRIDGE_INACTIVE);
    mgos_gpio_write(door->activate_pin_b, DOOR_HBRIDGE_INACTIVE); 
    LOG(LL_INFO, ("Door '%s' all_stop issued", door->name));
}

static bool _Door_open(Door *door, int flags) {
    Door_all_stop(door);
    if (DF_ISSET(flags, DF_FORCE)) {
        LOG(LL_WARN, ("Door %s force-opened", door->name));
    }
    else {
        if (DOOR_OPEN == Door_get_state(door)) {
            LOG(LL_INFO, ("Door %s already open, doing nothing", door->name));
            return false; 
        }
    }
    LOG(LL_INFO, ("Raising door %s", door->name));
    mgos_gpio_blink(door->open_indicator_pin, 1000, 500);
    mgos_gpio_write(door->activate_pin_a, DOOR_HBRIDGE_ACTIVE); 
    return true;
}

static bool _Door_close(Door *door, int flags) {
    Door_all_stop(door);
    if (DF_ISSET(flags, DF_FORCE)) {
        LOG(LL_WARN, ("Door %s force-closed", door->name));
    }
    else {
        if (DOOR_CLOSED == Door_get_state(door)) {
            LOG(LL_INFO, ("Door '%s' already closed, doing nothing", door->name));
            return false; 
        } 
    }
    LOG(LL_INFO, ("Lowering door '%s'", door->name));
    mgos_gpio_blink(door->closed_indicator_pin, 1000, 500);
    mgos_gpio_write(door->activate_pin_b, DOOR_HBRIDGE_ACTIVE);
    return true;
}


static void _door_interrupt(int pin, void *arg) {
    Door *door = (Door*) arg;

    if (DE_OK != Door_validate(door)) {
        LOG(LL_WARN, ("invalid door pointer"));
        return;
    }

    double interrupt_millis = mgos_uptime() * 1000.0;

    if (abs(interrupt_millis - door->debounce_millis) > mgos_sys_config_get_time_debounce_millis()) {
        char *msg = Door_status(door);
        mgos_mqtt_pub("homie/coop-7e474a/north-door/position", msg, strlen(msg), 1, true);
        bool reading = mgos_gpio_read(pin);
        LOG(LL_INFO, ("pin %d interrupt door %s: %s", pin, door->name, reading?"hi":"lo"));
        if (reading) {
            LOG(LL_INFO, ("Ignoring door interrupt on pin %d HI state", pin));
        }
        else {
            Door_all_stop(door);
        }
        Door_indicate(door);
    }
    else {
        LOG(LL_WARN, ("interrupt debounced %d", mgos_sys_config_get_time_debounce_millis()));
    }
    door->debounce_millis = interrupt_millis;
}

void Door_init(Door *door) {
    if (DE_OK != Door_validate(door)) {
        LOG(LL_ERROR, ("Cannot initiate invalid door!"));
    }
    mgos_gpio_setup_input(door->open_contact_pin, MGOS_GPIO_PULL_UP);
    mgos_gpio_setup_input(door->closed_contact_pin, MGOS_GPIO_PULL_UP);
    mgos_gpio_setup_output(door->activate_pin_a, false);
    mgos_gpio_setup_output(door->activate_pin_b, false);
    mgos_gpio_setup_output(mgos_sys_config_get_pins_indicator_red(), false);
    mgos_gpio_setup_output(mgos_sys_config_get_pins_indicator_green(), false);
    mgos_gpio_set_int_handler(door->open_contact_pin, MGOS_GPIO_INT_EDGE_NEG, _door_interrupt, door);
    mgos_gpio_enable_int(door->open_contact_pin);
    LOG(LL_INFO, ("Setup OPEN interrupt on pin %d for door %s", door->open_contact_pin, door->name));
    mgos_gpio_set_int_handler(door->closed_contact_pin, MGOS_GPIO_INT_EDGE_NEG, _door_interrupt, door);
    mgos_gpio_enable_int(door->closed_contact_pin);
    LOG(LL_INFO, ("Setup CLOSE interrupt on pin %d for door %s", door->closed_contact_pin, door->name));

    Door_all_stop(door);
}

Door * Door_new(int open_contact, 
        int closed_contact, 
        int act_pin_a, 
        int act_pin_b, 
        const char *name,
        int open_indicator_pin,
        int closed_indicator_pin) {
    Door *door = calloc(1, sizeof(Door));
    door->struct_id = DOOR_STRUCT_ID;
    door->open_contact_pin = open_contact;
    door->closed_contact_pin = closed_contact;
    door->activate_pin_a = act_pin_a;
    door->activate_pin_b = act_pin_b;
    for (int i=0; name != NULL && *name && i < sizeof(door->name)-1; i++) {
       door->name[i] = *name++;
    }

    door->open_indicator_pin = open_indicator_pin;
    door->closed_indicator_pin = closed_indicator_pin;

    Door_init(door);
    return door;
}

void *Door_north_new(void) {
    Door *north_door = Door_new(
      mgos_sys_config_get_pins_north_door_open_contact(), 
      mgos_sys_config_get_pins_north_door_closed_contact(), 
      mgos_sys_config_get_pins_north_door_lower(), 
      mgos_sys_config_get_pins_north_door_raise(), 
      mgos_sys_config_get_doors_north_name(),
      mgos_sys_config_get_pins_indicator_red(),
      mgos_sys_config_get_pins_indicator_green() 
    );
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
    return _Door_close(door, flags);
}

bool Door_open(void *adoor, int flags) {
    Door *door = (Door *) adoor;
    return _Door_open(door, flags);
}

void Door_indicate(void *adoor) {
    Door *door = (Door*) adoor;
    mgos_gpio_blink(door->closed_indicator_pin, 0, 0);
    mgos_gpio_blink(door->open_indicator_pin, 0, 0);
    mgos_gpio_write(door->closed_indicator_pin, false);
    mgos_gpio_write(door->open_indicator_pin, false);
    switch(Door_get_state(door)) {
        case DOOR_OPEN:
            mgos_gpio_write(door->open_indicator_pin, true);
            LOG(LL_INFO, ("%s is open", door->name));
            break;
        case DOOR_CLOSED:
            mgos_gpio_write(door->closed_indicator_pin, true);
            LOG(LL_INFO, ("%s is closed", door->name));
            break;
        case DOOR_STUCK:
            mgos_gpio_blink(door->closed_indicator_pin, 1000, 1000);
            LOG(LL_INFO, ("%s is stuck", door->name));
            break;
        default:
            LOG(LL_INFO, ("%s is neither open, closed, nor stuck. Wtf?", door->name));
            mgos_gpio_blink(door->closed_indicator_pin, 500, 500);
            mgos_gpio_blink(door->open_indicator_pin, 200, 200);
            break;
    }
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

void _cron_open_cb(void *userdata, mgos_cron_id_t id) {
    Door_open(userdata, 0);
}

void _cron_close_cb(void *userdata, mgos_cron_id_t id) {
    Door_close(userdata, 0);
}

void Door_cron_setup(void *aDoor) {
    Door *door = (Door *) aDoor;
    LOG(LL_INFO, ("Setting up cron jobs"));
    if (0 == door->open_cron_id) {
        LOG(LL_INFO, ("No open cron job found, adding..."));
        door->open_cron_id = mgos_cron_add("@sunrise", _cron_open_cb, aDoor);
    } 

    if (0 == door->close_cron_id) {
        LOG(LL_INFO, ("No close cron job found, adding..."));
        door->close_cron_id = mgos_cron_add("@sunset+1h", _cron_close_cb, aDoor);
    }
}

void Door_cron_next_run(void *aDoor) {
    Door *door = (Door *) aDoor;
    time_t date = 0;
    time(&date);
    date += 1;

    door->next_open_time = mgos_cron_get_next_invocation(door->open_cron_id, date);
    door->next_close_time = mgos_cron_get_next_invocation(door->close_cron_id, date);

    memset(door->next_open_time_str, 0, sizeof(door->next_open_time_str));
    mgos_strftime(door->next_open_time_str, sizeof(door->next_open_time_str)-1, "%c", door->next_open_time);

    memset(door->next_close_time_str, 0, sizeof(door->next_close_time_str));
    mgos_strftime(door->next_close_time_str, sizeof(door->next_close_time_str)-1, "%c", door->next_close_time);
}