#include <mg_rpc_channel_loopback.h>
#include <mg_rpc.h>
#include <mgos_rpc.h>
#include <frozen.h>
#include <common/cs_dbg.h>

#include "util.h"

static char* ip = NULL;
static char* mac = NULL;

static void getinfo_cb(struct mg_rpc *c, void *cb_arg,
                               struct mg_rpc_frame_info *fi,
                               struct mg_str result, int error_code,
                               struct mg_str error_msg) {

    LOG(LL_INFO, ("err: %d, JSON %s", error_code, (char*) result.p));
    
    /*
    LOG(LL_INFO, ("Reading JSON for mac"));
    int ret = json_scanf(result.p, result.len, "{mac: %Q}", &mac);
    if (ret != 1) {
        LOG(LL_WARN, ("Failed to read MAC addr: %d", ret));
        if (mac != NULL) { 
            free(mac);
        }
    }
    LOG(LL_INFO, ("Found MAC: %s", mac));
    */

    LOG(LL_INFO, ("Reading JSON for sta_ip"));
    int ret = json_scanf(result.p, result.len, "wifi:{sta_ip: %Q}", &ip);
    if (ret != 1) {
        LOG(LL_WARN, ("Failed to read IP addr: %d", ret));
        if (ip != NULL) free(ip);
        ip = NULL;
    }
    LOG(LL_INFO, ("Found IP: %s", ip));

    (void) fi;
    (void) cb_arg;
}

void Util_init(void) {
    if (mac == NULL) {
        asprintf(&mac, "boobsboobsboobs");
    }

    struct mg_rpc_call_opts opts = {.dst = mg_mk_str(MGOS_RPC_LOOPBACK_ADDR) };
    void *arg = NULL;
    LOG(LL_INFO, ("Calling loopback rpc Sys.GetInfo"));
    mg_rpc_callf(mgos_rpc_get_global(), mg_mk_str("Sys.GetInfo"), getinfo_cb, arg, &opts,
                NULL, NULL);
}

const char* Util_get_ip(void) {
    if (ip == NULL) {
        LOG(LL_INFO, ("Querying RPC for IP"));
        Util_init();
    }
    return (const char*) ip;
}

const char* Util_get_mac(void) {
    if (mac == NULL) {
        LOG(LL_INFO, ("Querying RPC for MAC"));
        Util_init();
    }
    return (const char*) mac;
}

const char* Util_get_mac_suffix(void) {
    const char *m = Util_get_mac();
    return (const char*) (m + (strlen(m)-5));
}