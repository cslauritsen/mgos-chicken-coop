load('api_gpio.js');
load('api_config.js');
load('api_rpc.js');
load('api_dht.js');
load('api_timer.js');
load('api_sensor_utils.js');
load('api_adc.js');
load('api_log.js');

let hbridge_active = 1;
let hbridge_inactive = 0;
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
// mqtt pub wrapper
let publish = function (topic, msg) {
    let ok = MQTT.pub(topic, msg, 1, true);	// QoS = 1, retain
    Log.print(Log.INFO, 'Published:' + (ok ? 'OK' : 'FAIL') + ' topic:' + topic + ' msg:' + msg);
    return ok;
};

// define variables
let client_id = Cfg.get('device.id');
let thing_id = tolowercase(client_id.slice(client_id.length - 6, client_id.length));

let device_id = Cfg.get('project.name');
let device_ip = "unknown";

// homie topic root
let bstpc = 'homie/' + thing_id + '/';
Cfg.set({mqtt: {will_topic: bstpc + '$state'}});
load('api_mqtt.js');

let nm = Cfg.get('project.name');
let dht_pin = Cfg.get('pins.dht');
print('dht_pin:', dht_pin);
let pubInt = Cfg.get('time.mqttPubInterval');
let dht = DHT.create(dht_pin, DHT.DHT22);
let counter = 0;

/**
 * North Door Open Routine
 */
let north_door_close = function() {
    if (GPIO.read(Cfg.get('pins.north_door_open_contact')) === 0) {
        // door is already open, do nothing
        Log.print(Log.INFO, 'Door already open');
        publish(bstpc + 'north-door/position', 'open');
        return false;
    }
    if (GPIO.read(Cfg.get('pins.north_door_closed_contact')) === 0) {
        // activate door lowering circuit, rely on interrupt to switch off
        // when it reaches the bottom
        Log.print(Log.INFO, 'Opening door');
        GPIO.write(Cfg.get('pins.north_door_raise', hbridge_active));
        return true;
    }
    return false;
};

/**
 * North Door Close Routine
 */
let north_door_close = function() {
    if (GPIO.read(Cfg.get('pins.north_door_close_contact')) === 0) {
        // door is already closed, do nothing
        Log.print(Log.INFO, 'Door already closed');
        publish(bstpc + 'north-door/position', 'closed');
        return false;
    }
    if (GPIO.read(Cfg.get('pins.north_door_open_contact')) === 0) {
        // activate motor circuit, rely on interrupt to switch off
        // when it reaches the end
        Log.print(Log.INFO, 'Closing door');
        GPIO.write(Cfg.get('pins.north_door_lower', hbridge_active));
        return true;
    }
    return false;
};

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
  print('Topic:', topic, 'message:', msg);
  if (msg === '1' || msg === 'true') {
    if (topic.indexOf('south-door/activate') !== -1) {
      activate_door_south();
      return;
    }
  }
  print("Message ", msg, ' was ignored');
}, null);
print('subscribed');

let homie_msg_ix = 0;
let homie_init = false;

// Asynchronously advance through the homie setup stuff until done
Timer.set(Cfg.get("homie.pubinterval"), true, function() {
  if (!MQTT.isConnected()) {
    print('Waiting for MQTT connect...');
    return;
  }
  let br = mgos_mqtt_num_unsent_bytes();
  let maxbr = Cfg.get("mqtt.max_unsent");
  if (br > maxbr) {
    print('home setup: waiting for MQTT queue to clear: ', br);
  }
  if (homie_msg_ix >= 0 && homie_msg_ix < homie_setup_msgs.length) {
    let msg = homie_setup_msgs[homie_msg_ix];
    let ret = MQTT.pub(msg.t, msg.m, 2, msg.retain);
    if (ret === 0) {
      print("homie pub failed: ", ret);
    }
    else {
      // if publication was successful, on the next go around, send the next message
      homie_msg_ix += 1;
    }
  }
}, null);

// Register ADC pins
let apins = [Cfg.get('pins.light_sensor')];
for (let i=0; i < apins.length; i++) {
    ADC.enable(apins[i]);
}

// H-Bridge is activated by shorting to ground
let hpins = [Cfg.get('pins.north_door_raise'), Cfg.get('pins.north_door_lower')];
for (let i=0; i < hpins.length; i++) {
    let p = hpins[i];
    GPIO.set_pull(p, GPIO.PULL_UP);
    GPIO.setup_output(p, 1); 
}

// Setup array for interrupt reading pin
let cpins = [ {
    pin: Cfg.get('pins.north_door_open_contact'), 
    last_r: -1
    },
    {
    pin: Cfg.get('pins.north_door_closed_contact'),
    last_r: -1
    }
];

/** 
 * Register interrupt handlers for contacts.
*/
for (let i=0; i < cpins.length; i++) {
  let cp = cpins[i];
  GPIO.set_pull(cp.pin, GPIO.PULL_UP);
  GPIO.set_mode(cp.pin, GPIO.MODE_INPUT);
  cp.last_r = GPIO.read(cp.pin);
  GPIO.set_int_handler(cp.pin, GPIO.INT_EDGE_FALLING, function(pin) {
    // shut the motor down
    for (let i=0; i < hpins.length; i++) {
        let p = hpins[i];
        GPIO.write(p, hbridge_inactive);
    }
    let v = GPIO.read(pin);
    if (v !== cp.last_r) {
        let disp = 'stuck';
        if (pin === Cfg.get('pins.north_door_open_contact')) {
            disp = 'open';
        }
        else if (pin === Cfg.get('pins.north_door_closed_contact')) {
            disp = 'closed';
        }
        MQTT.pub(bstpc + 'north-door/position', disp, 1, true);
        Log.print(Log.INFO, 'Pin', pin, cp.topic + ' got interrupt: ', v);
        MQTT.pub(bstpc + cp.topic, v === 0 ? 'false' : 'true');
    }
    cp.last_r = v;
  }, null);
  GPIO.enable_int(cp.pin); 
}

Timer.set(1000, true, function() {
  if (device_id === "unknown") {
    RPC.call(RPC.LOCAL, 'Sys.GetInfo', null, function(resp, ud) {
      device_id = resp.id;
      print('Response:', JSON.stringify(resp));
      print('device ID :', device_id);
    }, null);
  }

  let sdata = {
    dht22: {
      celsius: dht.getTemp(),
      fahrenheit: SensorUtils.fahrenheit(dht.getTemp()),
      rh: dht.getHumidity(),
    },
    doors: {
      north: {
        position: (GPIO.read(Cfg.get('pins.north_door_open_contact')) === 0) ? 'open' : (GPIO.read(Cfg.get('pins.north_door_closed_contact')) === 0 ? 'closed' : 'stuck')
      }
    },
    light: {
        luminosity: ADC.read('pins.light_sensor')
    }
  };

  print(JSON.stringify(sdata));

  if (counter++ % pubInt === 0 && homie_msg_ix >= homie_setup_msgs.length-1) {
    let qos = 1;
    let rtn = true;
    MQTT.pub(bstpc + '$state', 'ready', 1, true);
    MQTT.pub(bstpc + 'dht22/rh', JSON.stringify(sdata.dht22.rh), qos, rtn);
    MQTT.pub(bstpc + 'dht22/tempc', JSON.stringify(sdata.dht22.celsius), qos, rtn);
    MQTT.pub(bstpc + 'dht22/tempf', JSON.stringify(sdata.dht22.fahrenheit), qos, rtn);
    MQTT.pub(bstpc + 'north-door/position', JSON.stringify(sdata.doors.north.position), qos, rtn)
    MQTT.pub(bstpc + 'light-sensor/luminosity', JSON.stringify(sdata.light.luminosity), qos, rtn)
    MQTT.pub(bstpc + 'ip/address', device_ip, qos, rtn);
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
  let ret = north_door_open();
  return { value: ret ? "Activated" : "Unchanged"};
});

RPC.addHandler('NorthDoor.Close', function(args) {
  let ret = north_door_close();
  return { value: ret ? "Activated" : "Unchanged"};
});