# Chicken Coop Mongoose OS app

# Hardware Platform
ESP8266 LoLin NodeMCU. 

<img src="docs/ESP8266-NodeMCU-kit-12-E-pinout-gpio-pin.png">

It's important to select the correct PINs for the H-Bridge motor activation so that you don't get cycling as the device resets, powers on, etc. The motors H-Bridge is are controlled by 2 GPIO pins pulled low. Clockwise or counter-clockwise rotation starts by setting respective pins HI. I don't know what happens if both pins are set to HI, I suppose it could burn out the H-Bridge or the motor so this is something I try to have the code avoid at all times.

In this case, I'm using GPIO 4 & 5 to control the north door motor.

# Features
 * Door open/close detection and desired state transition
 * Luminosity Sensor
 * DHT22 temp/humidity readings
 * MQTT publishing
 * HTTP/MQTT RPC Control
 * OTA Firmware Updating

 # Desired State 
 The coop has one automated door. It has reed sensors to detect 2 states, fully open and fully closed. Additionally it includes RPC mechanisms to instruct the device to transition the door from one state to a desired state. It uses an H-Bridge to run the motor in one direction or the other and monitors the door so that the motor will be stopped when its desired state is reached, or a maximum amount of time has elapsed.

 The maximum time functions as a backstop to prevent the motor from burning out if it gets stuck, for instance.

 ## HTTP
 Issue an HTTP get like so, no paylaod is required. 
 ### Desired State: Closed
 ```bash
 curl http://192.168.1.xxx/rpc/NorthDoor.Close
 ```

 ### Desired State: Open
 ```bash
 curl http://192.168.1.xxx/rpc/NorthDoor.Open
 ```

 ## MQTT RPC
 you can send a command via MQTT. This means you don't need to have a static IP for the device, but you will need to know its unique ID. I'm working on enabling auto-discovery in services like OpenHAB by leveraging via the [homie convention](https://homieiot.github.io).

In the example below, "alpha" is "correlation ID", that is, an identifier that will be echoed back by the node in response to an RPC request which will contain information about the node's response, errors, etc. 

 ```bash
 mqttpub -h 192.168.1.5 \
    -t esp8266_7E5E96/rpc  \
    '{id: 1, "src":"alpha", "method": "NorthDoor.Open"}' 
 ```

## MQTT Trigger
TODO: It would be simpler in (especially for OpenHAB) if the relay activation could be triggered simply by the arrival of any message body `1` to a set topic, rather than having to craft and send a JSON document per MongooseOS' RPC requirements.


# OTA Firmware Update
When you use the `mos flash` command certain configurations parameters (like WiFi) configuration are lost.

The simplest method is to use an HTTP POST:
```bash
cd project-repo-root
curl -i -F filedata=@./build/fw.zip http://192.168.1.xxx/update
```