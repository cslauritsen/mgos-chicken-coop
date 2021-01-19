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

static void nd_close_cb(struct mg_rpc_request_info *ri, void *cb_arg,
                   struct mg_rpc_frame_info *fi, struct mg_str args) {
  Door *door = (Door*) cb_arg;
  bool ans = Door_transition(door, DOOR_CLOSED);
  mg_rpc_send_responsef(ri, "%s", ans ? "OK" : "Unchanged");
  (void) cb_arg;
  (void) fi;
}

static void nd_open_cb(struct mg_rpc_request_info *ri, void *cb_arg,
                   struct mg_rpc_frame_info *fi, struct mg_str args) {
  Door *door = (Door*) cb_arg;
  bool ans = Door_transition(door, DOOR_OPEN);
  mg_rpc_send_responsef(ri, "%s", ans ? "OK" : "Unchanged");
  (void) cb_arg;
  (void) fi;
}

// Somewhere in init function, register the handler:
enum mgos_app_init_result mgos_app_init(void) {
    Door *north_door = Door_new(
      mgos_sys_config_get_pins_north_door_open_contact(), 
      mgos_sys_config_get_pins_north_door_closed_contact(), 
      mgos_sys_config_get_pins_north_door_lower(), 
      mgos_sys_config_get_pins_north_door_raise(), 
      "NORTH");
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Open", NULL, nd_open_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Close", NULL, nd_close_cb, north_door);
  return MGOS_APP_INIT_SUCCESS;
}