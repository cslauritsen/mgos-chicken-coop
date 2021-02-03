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
  mg_rpc_send_responsef(ri, "{ door: \"%s\", status: \"%s\", version: \"%s\" }", 
    door->name, 
    Door_status(door), 
    build_version);
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
  mg_rpc_send_responsef(ri, "{ mac: \"%s\" }", Util_get_mac());
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
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Open", NULL, open_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Close", NULL, close_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Status", NULL, status_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.Stop", NULL, stop_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "cNorthDoor.ResetLightTrigger", NULL, reset_cb, north_door);
  mg_rpc_add_handler(mgos_rpc_get_global(), "csys.ip", NULL, ip_cb, NULL);
  mg_rpc_add_handler(mgos_rpc_get_global(), "csys.mac", NULL, mac_cb, NULL);

  Door_indicate(north_door);
  return MGOS_APP_INIT_SUCCESS;
}