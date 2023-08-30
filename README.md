# FF_WebServer
 This library implements a fully asynchronous ESP8266 WebServer

## What's for?

This library implements on ESP8266 a fully asynchronous Web server, with:
- MQTT connection
- Arduino and Web OTA
- local file system to host user and server files
- file and/or browser based settings
- full file editor/upload/download
- optional telnet or serial or MQTT debug commands
- optional serial and/or syslog trace
- optional external hardware watchdog
- optional Domoticz connectivity

This code is based on a highly modified version of https://github.com/FordPrfkt/FSBrowserNG, itself a fork of https://github.com/gmag11/FSBrowserNG, not anymore maintained.

## Prerequisites

Can be used directly with Arduino IDE or PlatformIO.

The following libraries are used:
- https://github.com/me-no-dev/ESPAsyncTCP
- https://github.com/gmag11/NtpClient
- https://github.com/me-no-dev/ESPAsyncWebServer
- https://github.com/bblanchon/ArduinoJson
- https://github.com/FlyingDomotic/FF_Trace
- https://github.com/arcao/Syslog
- https://github.com/marvinroger/async-mqtt-client
- https://github.com/joaolopesf/RemoteDebug

## Installation

Clone repository somewhere on your disk.
```
cd <where_you_want_to_install_it>
git clone https://github.com/FlyingDomotic/FF_WebServer.git FF_WebServer
```

## Update

Go to code folder and pull new version:
```
cd <where_you_installed_FF_WebServer>
git pull
```

Note: if you did any changes to files and `git pull` command doesn't work for you anymore, you could stash all local changes using:
```
git stash
```
or
```
git checkout <modified file>
```
Warning: as files in data folder should be downloaded on ESP file system, it may be a good idea to check if some of them have been changed be a new version, to update them on your ESP.

Remind also that config.json and userconfig.json are specific to your installation. Don't forget to download them from your ESP before making a global file system upload.

## Documentation

Documentation could be built using doxygen, either using makedoc.sh (for Linux) or makedoc.bat (for Windows), or running
```
doxygen doxyfile
```

HTML and RTF versions will then be available in `documentation` folder.

## Files to be modified by user to implement required behavior

By design, library contains common part of code that implement standard function. Only few files should be modified to implement user's needs. These files are:
- index_user.html: contains user's specific part of index.html, which is the web server main page
- userconfigui.json: if user wants to permanently store parameters, this file describe fields to be displayed to input/modify them.

Have a look to example files to get an idea of what could be put inside them. FF_WebServerMinimal folder contains empty examples while FF_WebServerStandard shows full implementation.

Details of routines that are available in FF_WebServer are described at end of this document.

## Parameters at compile time

The following parameters are used at compile time:
	REMOTE_DEBUG: (default=defined) Enable telnet remote debug (optional)
	SERIAL_DEBUG: (default=defined) Enable serial debug (optional, only if REMOTE_DEBUG is disabled)
	SERIAL_COMMAND_PREFIX: (default=no defined) Prefix of debug commands given on Serial (i.e. `command:). No commands are allowed on Serial if not defined
	NO_SERIAL_COMMAND_CALLBACK: (default: not defined) Disable Serial command callback
	HARDWARE_WATCHDOG_PIN: (default=D4) Enable watchdog external circuit on D4 (optional)
	HARDWARE_WATCHDOG_ON_DELAY: (default=5000) Define watchdog level on delay (in ms)
	HARDWARE_WATCHDOG_OFF_DELAY: (default=1) Define watchdog level off delay (in ms)
	HARDWARE_WATCHDOG_INITIAL_STATE (default 0) Define watchdog initial state
	FF_TRACE_KEEP_ALIVE: (default=5 * 60 * 1000) Trace keep alive timer (optional)
	FF_TRACE_USE_SYSLOG: (default=defined) Send trace messages on Syslog (optional)
	FF_TRACE_USE_SERIAL: (default=defined) Send trace messages on Serial (optional)
	INCLUDE_DOMOTICZ:(default=defined) Include Domoticz support (optional)
	CONNECTION_LED: (default=-1) Connection LED pin, -1 to disable
	AP_ENABLE_BUTTON: (default=-1) Button pin to enable AP during startup for configuration, -1 to disable
	AP_ENABLE_TIMEOUT: (default=240) If the device can not connect to WiFi it will switch to AP mode after this time (Seconds, max 255),  -1 to disable
	DEBUG_FF_WEBSERVER: (default=defined) Enable internal FF_WebServer debug
	FF_DISABLE_DEFAULT_TRACE: (default=not defined) Disable default trace callback
	FF_TRACE_USE_SYSLOG: (default=defined) SYSLOG to be used for trace

### Reserving Serial for your own use

If you want to use Serial in your own application, and don't want WebServer to use it, you should:
	Not define SERIAL_DEBUG
	Not define SERIAL_COMMAND_PREFIX
	Not define FF_TRACE_USE_SERIAL
	Define NO_SERIAL_COMMAND_CALLBACK

## Parameters defined at run time

The following parameters can be defined, either in json files before loading LittleFS file system, or through internal http server.

### In config.json:
	ssid: Wifi SSID to connect to
	pass: Wifi key
	ip: this node's fix IP address (useless if DHCP is true)
	netmask: this node's netmask (useless if DHCP is true)
	gateway: this node's IP gateway (useless if DHCP is true)
	dns: this node's DHCP server (useless if DHCP is true)
	dhcp: use DHCP if true, fix IP address else
	ntp: NTP server to use
	NTPperiod: interval between two NTP updates (in minutes)
	timeZone: NTP time zone (use internal HTTP server to set)
	daylight: true if DST is used
	deviceName: this node name

### In userconfig.json:
	MQTTClientID: MQTT client ID, used during connection to MQTT
	MQTTUser: User to connect to MQTT server²
	MQTTPass: Password to connect to MQTT server
	MQTTTopic: base MQTT topic, used to set LWT topic of SMS server
	MQTTCommandTopic: topic to read debug commands to be executed
	MQTTHost: MQTT server to connect to
	MQTTPort: MQTT port to connect to
	MQTTInterval: Domoticz update interval (useless)
	SyslogServer: syslog server to use (empty if not to be used)
	SyslogPort: syslog port to use (empty if not to be used)
	mqttSendTopic: MQTT topic to write received SMS to
	mqttGetTopic: MQTT topic to read SMS messages to send
	mqttLwtTopic: root MQTT last will topic to to read application status
	allowedNumbers: allowed senders phone numbers, comma separated

## Debug commands

Debug commands are available to help understanding what happens, and may be a good starting point to help troubleshooting.

Debug output is available on Telnet, Serial and Syslog. Note that settings can disable some of these outputs.

### Access
Debug is available through:
- Telnet: connect with a telnet tool on ESP's port 23, and strike commands on keyboard
- MQTT: send the raw command to mqttCommandTopic (don't set the retain flag unless you want the command to be executed at each ESP restart)
- Serial: send the command on Serial, prefixing it by SERIAL_COMMAND_PREFIX (i.e. command:vars`)

### Commands
The following commands are allowed:
	- ? or h or help: display this message
	- m: display memory available
	- v: set debug level to verbose
	- d: set debug level to debug
	- i: set debug level to info
	- w: set debug level to warning
	- e: set debug level to errors
	- s: set debug silence on/off
	- cpu80 : ESP8266 CPU at 80MHz
	- cpu160: ESP8266 CPU at 160MHz
	- reset: reset the ESP8266
	- vars: Dump standard variables
	- user: Dump user variables
	- debug: Toggle debug flag
	- trace: Toggle trace flag

## Available Web pages

- / and /index.htm -> index root file
- /admin.html -> administration menu
- /general.html -> general settings (device name)
- /ntp.html -> ntp settings
- /system.html -> system configuration (file system edit, HTTP OTA update, reboot, authentication parameters)
- /config.html -> network settings
- /user.html -> user specific settings

## Interactions with web server

Three root URLs are defined to communicate with web server, namely /rest, /json and /post. /rest is used to pass/return information in an unstructured way, while /json uses JSON message format. They both are using GET HTTP format, while /post is using HTTP POST format.

## Available URLs

WebServer answers to the following URLs. If authentication is turned on, some of them needs user to be authenticated before being served.

### Internal URLs
- /list?dir=/ -> list file system content
- /edit -> load editor (GET) , create file (PUT), delete file (DELETE)
- /admin/generalvalues -> return deviceName and userVersion in json format
- /admin/values -> return values to be loaded in index.html and indexuser.html
- /admin/connectionstate -> return connection state
- /admin/infovalues -> return network info data
- /admin/ntpvalues -> return ntp data
- /scan -> return wifi network scan data
- /admin/restart -> restart ESP
- /admin/wwwauth -> ask for authentication
- /admin -> return admin.html contents
- /update/updatepossible
- /setmd5 -> set MD5 OTA file value
- /update -> update system with OTA file
- /rconfig (GET) -> get configuration data
- /pconfig (POST) -> set configuration data
- /rest -> activate a rest request to get some (user's) values (*)
- /rest/values -> return values to be loaded in index.html and index_user.html (*)
- /json -> activate a rest request to get some (user's) values (*)
- /post -> activate a post request to set some (user's) values (*)

### HTML files (in addition to available web pages described before)
- /index_user.html -> user specific part of index.html (*)
- /favicon.ico -> icon to be displayed by browser

### JSON files
- /config.json -> system configuration data (!)
- /userconfig.json -> user's configuration data (!)
- /userconfigui.json -> user's specific configuration UI data (*)

### JavaScript files
- /microajax.js -> ajax minimalistic routines
- /browser.js -> FF_WebServer browser side JS functions
- /spark-md5.js -> md5 calculation function

### CSS files
- style.css -> CSS style file

(*) Should be adapted by user to specific configuration
(!) Could be adapted by user to initially set configuration values (i.e. server's ip or port, SSID...)

## Examples

- FF_WebServerMinimal: contains minimalistic data and routines to be provided to template
- FF_WebServerStandard: contains all data and routines to implement common functionalities

Files destination: FF_WebServerCFG.h should be copied in FF_WebServer library folder, other .h files should be moved into root folder, to be compiled. .json and .html files should be moved into data folder, to be loaded on ESP file system.

Note also that /data folder of FF_WebServer library should be copied into /data folder in user implementation code.

## Routines available in FF_WebServer class

### begin()

Initialize FF_WebServer class

To be called in setup()

Will try to connect to WiFi only during the first 10 seconds of life.

Parameters
- [in]	fs	(Little) file system to use
- [in]	version	user's code version (used in trace)

Returns
- None 

### setConfigChangedCallback()

Set configuration change callback

Parameters 
- [in]	Address of user routine to be called when a configuration change occurs

Returns
-	None

### setHelpMessageCallback()

Set help message callback

Parameters 
- [in]	Address of user routine to be called to load user's help message

Returns
-	None

### setDebugCommandCallback()

Set debug command callback

Parameters 
- [in]	Address of user routine to be called when an unknown debug command is received

Returns
-	None

### setSerialCommandCallback()

Set serial command callback

Parameters 
- [in]	Address of user routine to be called when a command has been received on Serial

Returns
-	None

### setRestCommandCallback()

Set REST command callback

Parameters 
- [in]	Address of user routine to be called when a REST (/rest) command is received

Returns
-	None

### setJsonCommandCallback()

Set JSON command callback

Parameters 
- [in]	Address of user routine to be called when a JSON (/json) command is received

Returns
-	None

### setPostCommandCallback()

Set POST command callback

Parameters 
- [in]	Address of user routine to be called when a POST request command is received

Returns
-	None

### setError404Callback()

Set error 404 callback

User can add new URL/request intercepting commands that FF_WebServer can't serve

Parameters 
- [in]	Address of user routine to be called when a 404 (file not found) error occur.

Returns
-	None

### setWifiConnectCallback()

Set WiFi connected callback

Parameters 
- [in]	Address of user routine to be called when WiFi is connected

Returns
-	None

### setWifiDisconnectCallback()

Set WiFi disconnected callback

Parameters 
- [in]	Address of user routine to be called when WiFi is disconnected

Returns
-	None

### setWifiGotIpCallback()

Set WiFi got IP callback

Parameters 
- [in]	Address of user routine to be called when WiFi receives anIp address

Returns
-	None

### setMqttConnectCallback()

Set MQTT connected callback

Parameters 
- [in]	Address of user routine to be called when MQTT is connected

Returns
-	None

### setMqttDisconnectCallback()

Set MQTT disconnected callback

Parameters 
- [in]	Address of user routine to be called when MQTT is disconnected

Returns
-	None

### setMqttMessageCallback()

Set MQTT message callback

Parameters 
- [in]	Address of user routine to be called when an MQTT subscribed message is received

Returns
-	None

### clearConfig()

Clear system configuration

Parameters
- [in]	reset	reset ESP if true

Returns
- None 

### clearUserConfig()

Clear user configuration

Parameters
- [in]	reset	reset ESP if true

Returns
- None 


### getHostName()

Return this host name

Parameters
- None

Returns
- This host name *const char * AsyncFFWebServer::getHostName 	( 	void  		) 	

### getWebServerVersion()

Get FF_WebServer version

Parameters
- None

Returns
-  FF_WebServer version

### handle()

Handle FFWebServer stuff

Should be called from main loop to make Web server servicing

Parameters
- None	

Returns
- None 

### load_user_config

Load an user's configuration in a string, integer, long or float

Parameters
- in]	name	item name to return
- [out]	value	returned value

Returns
- false if error detected, true else 

### mqttPublish()

Publish one MQTT subtopic (main topic will be prepended)

Parameters
- [in]	subTopic	subTopic to send message to (main topic will be prepended)
- [in]	value	value to send to subTopic
- {in]	retain	True if message should be retained, false else
Returns
- None 

### mqttPublishRaw()

Publish one MQTT topic (main topic will NOT be prepended)

Parameters
- [in]	topic	topic to send message to (main topic will NOT be prepended)
- [in]	value	value to send to topic
- {in]	retain	True if message should be retained, false else

Returns
- None 

### mqttSubscribe()

Subscribe to one MQTT subtopic (main topic will be prepended)

Parameters
- [in]	subTopic	subTopic to send message to (main topic will be prepended)
- [in]	qos	quality of service associated with subscription (default to 0)

Returns
- true if subscription if successful, false else 

### mqttSubscribeRaw()

Subscribe to one MQTT subtopic (main topic will NOT be prepended)

Parameters
- [in]	topic	topic to send message to (main topic will be prepended)
- [in]	qos	quality of service associated with subscription (default to 0)

Returns
- true if subscription if successful, false else 

### parseUrlParams()

Parse an URL parameters list and return each parameter and value in a given table.

Note
    WARNING! This function overwrites the content of this string. Pass this function a copy if you need the value preserved. 

Parameters
- [in,out]	queryString	parameters string which is to be parsed (will be overwritten)
- [out]	results	place to put the pairs of parameter name/value (will be overwritten)
- [in]	resultsMaxCt	maximum number of results, = sizeof(results)/sizeof(*results)
- [in]	decodeUrl	if this is true, then url escapes will be decoded as per RFC 2616

Returns
- Number of parameters returned in results 

### resetTraceKeepAlive()

Reset trace keep alive timer

Automatically called by default trace callback. To be called by user's callback if automatic trace callback is disabled (FF_DISABLE_DEFAULT_TRACE defined).

Parameters
- None	

Returns
- None 

### sendDomoticzDimmer()

Send a message to Domoticz for a dimmer

Parameters
- [in]	idx	Domoticz device's IDX to send message to
- [in]	level	level value to send

Returns
- None 

### sendDomoticzPower()

Send a message to Domoticz for an Energy meter

Parameters
- [in]	idx	Domoticz device's IDX to send message to
- [in]	power	instant power value
- [in]	energy	total energy value

Returns
- None 

### sendDomoticzSwitch()

Send a message to Domoticz for a switch

Parameters
- [in]	idx	Domoticz device's IDX to send message to
- [in]	isOn	if true, sends device on, else device off

Returns
- None 

### sendDomoticzValues()

Send a message to Domoticz with nValue and sValue

Parameters
- [in]	idx	Domoticz device's IDX to send message to
- [in]	values	comma separated values to send to Domoticz as sValue
- [in]	integer	numeric value to send to Domoticz as nValue

Returns
- None 

### startWifi()

Start Wifi client

Parameters
- None

Returns
- None

### startWifiAP()

Start Wifi Access Point

Parameters
- None

Returns
- None

### stopWifi()

Stop Wifi

Parameters
- None

Returns
- None

## Callbacks that can be implemented in user code

### CONFIG_CHANGED_CALLBACK()

This routine is called when permanent configuration data has been changed. User should call FF_WebServer.load_user_config to get values defined in userconfigui.json. Values in config.json may also be get here.

Parameters
- none

Returns
- none 

### HELP_MESSAGE_CALLBACK()

Adds user help commands to the standard debug command help list.

Parameters
- None 

Returns
- additional help commands, each line ended by \r\n    (CR/LF)

### DEBUG_COMMAND_CALLBACK()

This routine is called when a user's debug command is received.

User should analyze here debug command and execute them properly.

Note
- Note that standard commands are already taken in account by server and never passed here.

Parameters
- [in]	command	last debug command entered by user

Returns
- none 

### SERIAL_COMMAND_CALLBACK()

This routine is called when a Serial command is received.

User should analyze here Serial command and execute them properly.

Note
- Note that standard Serial commands are already taken in account by server and never passed here.

Parameters
- [in]	command	Serial command entered by user

Returns
- true if command is known, else false 


### ERROR404_CALLBACK()

This routine is called when a 404 error code is to be returned by server User can analyze request here, and add its own. In this case, it should answer using a request->send(<error code>, <content type>, <content>) and returning true.

If no valid answer can be found, should return false, to let template code returning an error message.

Parameters
- [in]	request	AsyncWebServerRequest structure describing user's request

Returns
- true for valid answered by request->send command, false else 

### JSON_COMMAND_CALLBACK()

This routine analyze and execute JSON commands sent through /json GET command It should answer valid requests using a request->send(<error code>, <content type>, <JSON content>) and returning true.

If no valid command can be found, should return false, to let template code returning an error message.

Parameters
- [in]	request	AsyncWebServerRequest structure describing user's request

Returns
- true for valid answered by request->send command, false else 

### MQTT_CONNECT_CALLBACK()

This routine is called each time MQTT is (re)connected

Parameters
- none

Returns
- none 

### MQTT_MESSAGE_CALLBACK()

This routine is called each time MQTT receives a subscribed topic

Note
- ** Take care of long payload that will arrive in multiple packets **

Parameters
- [in]	topic	received message topic
- [in]	payload	(part of) payload
- [in]	len	length of (this part of) payload
- [in]	index	index of (this part of) payload
- [in]	total	total length of all payload parts

Returns
- None 

### POST_COMMAND_CALLBACK()

This routine analyze and execute commands sent through POST command It should answer valid requests using a request->send(<error code>, <content type>, <content>) and returning true.

If no valid command can be found, should return false, to let template code returning an error message.

Parameters
- [in]	request	AsyncWebServerRequest structure describing user's request

Returns
- true for valid answered by request->send command, false else 

### REST_COMMAND_CALLBACK()

This routine analyze and execute REST commands sent through /rest GET command It should answer valid requests using a request->send(<error code>, <content type>, <content>) and returning true.

If no valid command can be found, should return false, to let server returning an error message.

Note
- Note that minimal implementation should support at least /rest/values, which is requested by index.html to get list of values to display on root main page. This should at least contain "header" topic, displayed at top of page. Standard header contains device name, versions of user code, FF_WebServer template followed by device uptime. You may send something different. It should then contain user's values to be displayed by index_user.html file.

Parameters
- [in]	request	AsyncWebServerRequest structure describing user's request

Returns
- true for valid answered by request->send command, false else 

### trace_callback()

This routine is called each time a trace is requested by FF_TRACE.

It has to be declared in setup() by a "static trace_register(myTraceCallback);"

Parameters

- _level: severity level of message (can be any FF_TRACE_LEVEL_xxxx value)
- _file: calling source file name with extension
- _line: calling source file line
- _function: calling calling source function name
- _message: text message to send

Returns
- None

Note
- Note that defining FF_DISABLE_DEFAULT_TRACE will suppress default trace, if required. If you keep the default trace, you may add some other output media(s), like MQTT, file... This example code reproduce the default trace routine. Don't hesitate to change it, if it doesn't fit your needs.

### WIFI_CONNECT_CALLBACK()

This routine is called each time WiFi station is connected to an AP

Parameters
- [in]	data	WiFiEventStationModeConnected event data

Returns
- none 

### WIFI_DISCONNECT_CALLBACK()

This routine is called each time WiFi station is disconnected from an AP

Parameters
- [in]	data	WiFiEventStationModeDisconnected event data

Returns
- none 

### WIFI_GOT_IP_CALLBACK()

This routine is called each time WiFi station gets an IP

Parameters
- [in]	data	WiFiEventStationModeGotIP event data

Returns
- none 


## Content of a typical user INO (or cpp) file

Easiest way is to have a look at "FF_WebServerStandard" example, which contains all available functions. You may want to copy it, and remove unused/not needed parts before adding your own code.
