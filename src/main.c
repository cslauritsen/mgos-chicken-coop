#include <stdio.h>
#include <stdlib.h>
#include "mgos.h"
#include "mgos_app.h"
#include "mgos_dlsym.h"
#include "mgos_hal.h"
#include "mgos_rpc.h"
#include "mjs.h"
#include <mgos_gpio.h>
#include <mgos_sys_config.h>
#include <common/cs_dbg.h>
#include <mgos_app.h>
#include <mgos_system.h>
#include <mgos_timers.h>
#include <mgos_time.h>
#include "door.h"
#include "util.h"

// declared in build_info.c
extern char* build_version;
extern char* build_id;
extern char* build_timestamp;

// helper functions for ffi
int str2int(char *c) {
  return (int) strtol(c,NULL,10);
}

void reset_firmware_defaults() {
  LOG(LL_INFO, ("Reset to firmaware defaults"));
  mgos_config_reset(MGOS_CONFIG_LEVEL_USER);
  mgos_fs_gc();
  mgos_system_restart_after(100);
}

static void close_cb(struct mg_rpc_request_info *ri, void *cb_arg,
                   struct mg_rpc_frame_info *fi, struct mg_str args) {
  extern char* build_version;
  Door *door = (Door*) cb_arg;
  if (DE_OK != Door_validate(door)) {
    mg_rpc_send_errorf(ri, 501, "Invalid door pointer");
    return;
  }
  bool ans = Door_close(door, DF_FORCE);
  mg_rpc_send_responsef(ri, "{result: \"%s\", door: \"%s\", version: \"%s\"}", 
    ans ? "OK" : "Unchanged", 
    door->name, 
    build_version);
  (void) fi;
}

static void open_cb(struct mg_rpc_request_info *ri, void *cb_arg,
                   struct mg_rpc_frame_info *fi, struct mg_str args) {
  extern char* build_version;
  Door *door = (Door*) cb_arg;
  if (DE_OK != Door_validate(door)) {
    mg_rpc_send_errorf(ri, 501, "Invalid door pointer");
    return;
  }

  bool ans = Door_open(door, DF_FORCE);
  mg_rpc_send_responsef(ri, "{ result: \"%s\", door: \"%s\", version: \"%s\"}", 
    ans ? "OK" : "Unchanged", 
    door->name,
    build_version);
  (void) fi;
}

static void stop_cb(struct mg_rpc_request_info *ri, void *cb_arg,
                   struct mg_rpc_frame_info *fi, struct mg_str args) {
  extern char* build_version;
  Door *door = (Door*) cb_arg;
  if (DE_OK != Door_validate(door)) {
    mg_rpc_send_errorf(ri, 501, "Invalid door pointer");
    return;
  }

  Door_all_stop(door);
  Door_indicate(door);
  (void) fi;
}

static void reset_cb(struct mg_rpc_request_info *ri, void *cb_arg,
                   struct mg_rpc_frame_info *fi, struct mg_str args) {
  extern char* build_version;
  Door *door = (Door*) cb_arg; 
  if (DE_OK != Door_validate(door)) {
    mg_rpc_send_errorf(ri, 501, "Invalid door pointer");
    return;
  }
  double last = door->last_light_trigger;
  door->last_light_trigger = 0;
  mg_rpc_send_responsef(ri, "{ previous: %d, current: %d, door: \"%s\", version: \"%s\" }", 
      last, 
        door->last_light_trigger, 
        door->name, 
        build_version);
  (void) fi;
}

static void status_cb(struct mg_rpc_request_info *ri, void *cb_arg,
                   struct mg_rpc_frame_info *fi, struct mg_str args) {
  Door *door = (Door*) cb_arg;
  if (DE_OK != Door_validate(door)) {
    mg_rpc_send_errorf(ri, 501, "Invalid door pointer");
    return;
  }
  extern char* build_version;
  Door_indicate(door);
  Door_cron_next_run(door);

  char current_time[32];
  memset(current_time, 0, sizeof(current_time));
  time_t now = 0;
  time(&now);
  mgos_strftime(current_time, sizeof(current_time)-1, "%c", now);
  mg_rpc_send_responsef(ri, "{"
    "door: \"%s\", " 
    "status: \"%s\", "
    "version: \"%s\", "
    "build_timestamp: \"%s\", "
    "build_id: \"%s\", "
    "current_time: \"%s\", "
    "sched: { "
    "  next_open_timestamp: \"%s\", "
    "  next_open_time: %d, "
    "  next_close_timestamp: \"%s\", ",
    "  next_close_time: %d, ",
    "  open_cronid: %d, ",
    "  close_cronid: %d "
    " }"
    "}", 
    door->name, 
    Door_status(door), 
    build_version,
    build_timestamp,
    build_id,
    current_time,
    door->next_open_time_str,
    door->next_open_time,
    door->next_close_time_str,
    door->next_close_time,
    door->open_cron_id,
    door->close_cron_id
    );
  (void) fi;
}

static void ip_cb(struct mg_rpc_request_info *ri, void *cb_arg,
                   struct mg_rpc_frame_info *fi, struct mg_str args) {
  LOG(LL_INFO, ("in ip_cb"));
  mg_rpc_send_responsef(ri, "{ ip: \"%s\" }", Util_get_ip());
}

static void mac_cb(struct mg_rpc_request_info *ri, void *cb_arg,
                   struct mg_rpc_frame_info *fi, struct mg_str args) {
  LOG(LL_INFO, ("in mac_cb"));
  const char *mac = Util_get_mac();
  mg_rpc_send_responsef(ri, "{ mac: \"%s\" }", mac);
}

static void _doors_stop_interrupt(int pin, void *arg) {
    for (Door **p = (Door**) arg; *p; p++) {
      Door *door = *p;
      if (DE_OK != Door_validate(door)) {
          LOG(LL_WARN, ("invalid door pointer"));
          return;
      }
      Door_all_stop(door);
      LOG(LL_INFO, ("Stopped door '%s'", door->name));
    }
}

// Somewhere in init function, register the handler:
enum mgos_app_init_result mgos_app_init(void) {
  Door *north_door = Door_new(
  mgos_sys_config_get_pins_north_door_open_contact(), 
  mgos_sys_config_get_pins_north_door_closed_contact(), 
  mgos_sys_config_get_pins_north_door_lower(), 
  mgos_sys_config_get_pins_north_door_raise(), 
  "NORTH",
  mgos_sys_config_get_pins_indicator_red(),
  mgos_sys_config_get_pins_indicator_green());

  static Door * all_doors[] = { NULL, NULL };
  all_doors[0] = north_door;

  mgos_gpio_setup_input(mgos_sys_config_get_pins_door_stop(), MGOS_GPIO_PULL_UP);
  mgos_gpio_set_int_handler(mgos_sys_config_get_pins_door_stop(), MGOS_GPIO_INT_EDGE_NEG, _doors_stop_interrupt, all_doors);
  mgos_gpio_enable_int(mgos_sys_config_get_pins_door_stop());

  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Open", NULL, open_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Close", NULL, close_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Status", NULL, status_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Stop", NULL, stop_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.ResetLightTrigger", NULL, reset_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "csys.ip", NULL, ip_cb, NULL);
  mg_rpc_add_handler(mgos_rpc_get_global(), "csys.mac", NULL, mac_cb, NULL);

  for (Door* door = *all_doors; door; door++) {
    Door_indicate(door);
    Door_cron_setup(door);
  }
  return MGOS_APP_INIT_SUCCESS;
}