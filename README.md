# FF_WebServer
 This library implements a fully asynchronous ESP8266 WebServer

## What's for?

This library implements on ESP8266 a fully asynchronous Web server, with MQTT connection, Arduino and Web OTA, optional telnet or serial debug, optional serial and/or syslog trace, optional external hardware watchdog and optional Domoticz connectivity.

It also has a local file system to host user and server files.

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
- /rest/values -> -> return values to be loaded in index.html and index_user.html (*)
- /json -> activate a rest request to get some (user's) values (*)
- /post  -> activate a post request to set some (user's) values (*)

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

### setDebugCommandCallback()

Set debug command callback

Parameters 
- [in]	Address of user routine to be called when an unknown debug command is received

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

Returns
- None 

### mqttPublishRaw()

Publish one MQTT topic (main topic will NOT be prepended)

Parameters
- [in]	topic	topic to send message to (main topic will NOT be prepended)
- [in]	value	value to send to topic

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

### setHelpCmd()

Adds user help commands to the standard debug command help list.

Parameters
- [in]	helpCommands	additional help commands, each line ended by \r\n    (CR/LF)

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

### DEBUG_COMMAND_CALLBACK()

This routine is called when a user's debug command is received.

User should analyze here debug command and execute them properly.

Note
- Note that standard commands are already taken in account by server and never passed here.

Parameters
- [in]	lastCmd	last debug command entered by user

Returns
- none 

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
