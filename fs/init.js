load('api_gpio.js');
load('api_config.js');
load('api_rpc.js');
load('api_dht.js');
load('api_timer.js');
load('api_sensor_utils.js');
load('api_adc.js');
load('api_log.js');

// helpers
// (convert A-Z to a-z)
let tolowercase = function (s) {
    let ls = '';
    for (let i = 0; i < s.length; i++) {
        let ch = s.at(i);
        if (ch >= 0x41 && ch <= 0x5A)
            ch |= 0x20;
        ls += chr(ch);
    }
    return ls;
};
// string to integer
let str2int = ffi('int str2int(char *)');
let Door_north_new = ffi('void * Door_north_new(void)');
let Door_status = ffi('char * Door_status(void *)');
let Door_close = ffi('bool Door_close(void *, int)');
let Door_open = ffi('bool Door_open(void *, int)');

let open_thresh = Cfg.get('luminosity.openThreshold');
let close_thresh = Cfg.get('luminosity.closeThreshold');

let north_door = Door_north_new();

// mqtt pub wrapper
let publish = function (topic, msg) {
    let ok = MQTT.pub(topic, msg, 1, true);	// QoS = 1, retain
    Log.print(Log.INFO, 'Published:' + (ok ? 'OK' : 'FAIL') + ' topic:' + topic + ' msg:' + msg);
    return ok;
};

// define variables
let client_id = Cfg.get('device.id');
let thing_id = "coop-" + tolowercase(client_id.slice(client_id.length - 6, client_id.length));

let device_ip = "unknown";

let closed_cpin = Cfg.get('pins.north_door_closed_contact');
let open_cpin = Cfg.get('pins.north_door_open_contact');

// homie topic root
let bstpc = 'homie/' + thing_id + '/';
Cfg.set({mqtt: {will_topic: bstpc + '$state'}});
load('api_mqtt.js');

let nm = Cfg.get('project.name');
let dht_pin = Cfg.get('pins.dht');
Log.print(Log.INFO, 'dht_pin:', dht_pin);
let pubInt = Cfg.get('time.mqttPubInterval');
let dht = DHT.create(dht_pin, DHT.DHT22);
let counter = 0;

let mgos_mqtt_num_unsent_bytes = ffi('int mgos_mqtt_num_unsent_bytes(void)');

let homie_setup_msgs = [
  {t: bstpc + '$homie', m: "4.0", qos: 1, retain: true},
  {t: bstpc + '$name',  m:nm, qos: 1, retain: true},
  {t: bstpc + '$state',  m:'init', qos: 1, retain: true},
  {t: bstpc + '$nodes',  m:'dht22,north-door,system,light-sensor', qos: 1, retain: true},

  {t: bstpc + 'system/$name',  m:'system', qos: 1, retain: true},
  {t: bstpc + 'system/$type',  m:'system', qos: 1, retain: true},
  {t: bstpc + 'system/$properties',  m:'ip,ram,uptime', qos: 1, retain: true},

  {t: bstpc + 'system/ip/$name',  m:'IP Address', qos: 1, retain: true},
  {t: bstpc + 'system/ip/$datatype',  m:'string', qos: 1, retain: true},
  {t: bstpc + 'system/ip/$settable',  m:'false', qos: 1, retain: true},

  {t: bstpc + 'system/ram/$name',  m:'Free RAM', qos: 1, retain: true},
  {t: bstpc + 'system/ram/$datatype',  m:'integer', qos: 1, retain: true},
  {t: bstpc + 'system/ram/$settable',  m:'false', qos: 1, retain: true},

  {t: bstpc + 'system/uptime/$name',  m:'Uptime', qos: 1, retain: true},
  {t: bstpc + 'system/uptime/$datatype',  m:'integer', qos: 1, retain: true},
  {t: bstpc + 'system/uptime/$settable',  m:'false', qos: 1, retain: true},

  {t: bstpc + 'dht22/$name',  m:'DHT22 Temp & Humidity Sensor', qos: 1, retain: true},
  {t: bstpc + 'dht22/$type',  m:'DHT22', qos: 1, retain: true},
  {t: bstpc + 'dht22/$properties',  m:'tempc,tempf,rh', qos: 1, retain: true},

  {t: bstpc + 'dht22/tempf/$name',  m:'Temperature in Fahrenheit', qos: 1, retain: true},
  {t: bstpc + 'dht22/tempf/$datatype',  m:'float', qos: 1, retain: true},
  {t: bstpc + 'dht22/tempf/$settable',  m:'false', qos: 1, retain: true},
  {t: bstpc + 'dht22/tempf/$unit',  m:'°F', qos: 1, retain: true},

  {t: bstpc + 'dht22/tempc/$name',  m:'Temperature in Celsius', qos: 1, retain: true},
  {t: bstpc + 'dht22/tempc/$settable',  m:'false', qos: 1, retain: true},
  {t: bstpc + 'dht22/tempc/$datatype',  m:'float', qos: 1, retain: true},
  {t: bstpc + 'dht22/tempc/$unit',  m:'°C', qos: 1, retain: true},

  {t: bstpc + 'dht22/rh/$name',  m:'Relative Humidity', qos: 1, retain: true},
  {t: bstpc + 'dht22/rh/$settable',  m:'false', qos: 1, retain: true},
  {t: bstpc + 'dht22/rh/$datatype',  m:'float', qos: 1, retain: true},
  {t: bstpc + 'dht22/rh/$unit',  m:'%', qos: 1, retain: true},

  {t: bstpc + 'north-door/$name',  m:'North Door', qos: 1, retain: true},
  {t: bstpc + 'north-door/$type',  m:'door', qos: 1, retain: true},
  {t: bstpc + 'north-door/$properties',  m:'position', qos: 1, retain: true},

  {t: bstpc + 'north-door/position/$name',  m:'Door Status', qos: 1, retain: true},
  {t: bstpc + 'north-door/position/$settable',  m:'true', qos: 1, retain: true},
  {t: bstpc + 'north-door/position/$datatype',  m:'enum', qos: 1, retain: true},
  {t: bstpc + 'north-door/position/$format',  m:'open,closed,stuck,unknown', qos: 1, retain: true},

  {t: bstpc + 'light-sensor/$name',  m:'Light Sensor', qos: 1, retain: true},
  {t: bstpc + 'light-sensor/$type',  m:'photosensor', qos: 1, retain: true},
  {t: bstpc + 'light-sensor/$properties',  m:'luminosity', qos: 1, retain: true},
  {t: bstpc + 'light-sensor/luminosity/$datatype',  m:'integer', qos: 1, retain: true},
  {t: bstpc + 'light-sensor/luminosity/$settable',  m:'false', qos: 1, retain: true},

  {t: bstpc + '$state',  m:'ready', qos: 1, retain: true}
];

// subscribe to homie set commands
MQTT.sub(bstpc + '+/+/set', function(conn, topic, msg) {
  Log.print(Log.INFO, 'Topic:', topic, 'message:', msg);
  if (msg === '1' || msg === 'true') {
    if (topic.indexOf('north-door/position') !== -1) {
       if (msg === "closed") {
           Door_close(north_door, 0);
       }
       else if (msg === "open") {
           Door_open(north_door, 0);
       }
      return;
    }
  }
  Log.print(Log.INFO, "Message ", msg, ' was ignored');
}, null);
Log.print(Log.INFO, 'subscribed');

let homie_msg_ix = 0;
let homie_init = false;

// Asynchronously advance through the homie setup stuff until done
let homie_timer = Timer.set(Cfg.get("homie.pubinterval"), true, function() {
  if (homie_msg_ix >= homie_setup_msgs.length) {
    // We're done with homie setup, clear the timer
    Timer.del(homie_timer);
  }
  if (!MQTT.isConnected()) {
    Log.print(Log.INFO, 'Waiting for MQTT connect...');
    return;
  }
  let br = mgos_mqtt_num_unsent_bytes();
  let maxbr = Cfg.get("mqtt.max_unsent");
  if (br > maxbr) {
    Log.print(Log.INFO, 'home setup: waiting for MQTT queue to clear: ', br);
  }
  if (homie_msg_ix >= 0 && homie_msg_ix < homie_setup_msgs.length) {
    let msg = homie_setup_msgs[homie_msg_ix];
    let ret = MQTT.pub(msg.t, msg.m, 2, msg.retain);
    if (ret === 0) {
      Log.print(Log.INFO, "homie pub failed: ", ret);
    }
    else {
      // if publication was successful, on the next go around, send the next message
      homie_msg_ix += 1;
    }
  }
}, null);

// Register ADC pins
let apins = [Cfg.get('pins.lightsensor')];
for (let i=0; i < apins.length; i++) {
    ADC.enable(apins[i]);
    Log.print(Log.INFO, "Enabled ADC on ", apins[i]);
}

Timer.set(1000, true, function() {
  let sdata = {
    dht22: {
      celsius: dht.getTemp(),
      fahrenheit: SensorUtils.fahrenheit(dht.getTemp()),
      rh: dht.getHumidity(),
    },
    doors: {
      north: {
        position: Door_status(north_door)
      }
    },
    light: {
        luminosity: ADC.read(Cfg.get('pins.lightsensor'))
    }
  };

  Log.print(Log.DEBUG, JSON.stringify(sdata));

  if (sdata.light.luminosity >= 0 && sdata.light.luminosity <= 1024) {
    Log.print(Log.INFO, "Luminosity check within range " + JSON.stringify(open_thresh) +  ".." + JSON.stringify(close_thresh) +  ": " + JSON.stringify(sdata.light.luminosity));
    if (sdata.light.luminosity <= open_thresh && "closed" === Door_status(north_door)) {
      Log.print(Log.INFO, 'Opening doors lum val ' + JSON.stringify(sdata.light.luminosity));
      Door_open(north_door, 1);
    }
    if (sdata.light.luminosity >= close_thresh && "open" === Door_status(north_door)) {
      Log.print(Log.INFO, 'Closing doors lum val ' + JSON.stringify(sdata.light.luminosity));
      Door_close(north_door, 1);
    }
  }
  else {
    Log.print(Log.ERROR, "Luminosity reading not within acceptable range.");
  }

  if (counter++ % pubInt === 0 && homie_msg_ix >= homie_setup_msgs.length-1) {
    let qos = 1;
    let rtn = true;
    MQTT.pub(bstpc + '$state', 'ready', 1, true);
    MQTT.pub(bstpc + 'dht22/rh', JSON.stringify(sdata.dht22.rh), qos, rtn);
    MQTT.pub(bstpc + 'dht22/tempc', JSON.stringify(sdata.dht22.celsius), qos, rtn);
    MQTT.pub(bstpc + 'dht22/tempf', JSON.stringify(sdata.dht22.fahrenheit), qos, rtn);
    MQTT.pub(bstpc + 'north-door/position', JSON.stringify(sdata.doors.north.position), qos, rtn);
    MQTT.pub(bstpc + 'light-sensor/luminosity', JSON.stringify(sdata.light.luminosity), qos, rtn);
  }
}, null);

// *****************************************************************
// RPC Handler Registration 
//
RPC.addHandler('Temp.Read', function(args) {
  return { value: dht.getTemp() };
});

RPC.addHandler('TempF.Read', function(args) {
  return { value: SensorUtils.fahrenheit(dht.getTemp()) };
});

RPC.addHandler('RH.Read', function(args) {
  return { value: dht.getHumidity() };
});

RPC.addHandler('NorthDoor.Open', function(args) {
  return { value: Door_open(north_door, 0) ? "Opening" : "Unchanged" };
});

RPC.addHandler('NorthDoor.Close', function(args) {
  return { value: Door_close(north_door, 0) ? "Closing" : "Unchanged" };
});
RPC.addHandler('NorthDoor.Status', function(args) {
  return { value: Door_status(north_door) };
});