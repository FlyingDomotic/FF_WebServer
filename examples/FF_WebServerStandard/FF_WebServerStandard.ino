/*

	This examples shows how to implement a full function web server with FF_WebServer class.

	It implements on ESP8266 a fully asynchronous Web server,
		with MQTT connection, Arduino and Web OTA,
		telnet serial debug, 
		serial and syslog trace, 
		external hardware watchdog
		and Domoticz connectivity.

	It also has a local file system to host user and server files.

	This code is based on a highly modified version of https://github.com/FordPrfkt/FSBrowserNG,
		itself a fork of https://github.com/gmag11/FSBrowserNG, not anymore maintained.

	Written and maintained by Flying Domotic (https://github.com/FlyingDomotic/FF_WebServer)

*/

#define VERSION "1.0.0"										// Version of this code
#include <FF_WebServer.h>									// Defines associated to FF_WebServer class

//	User internal data
//		Declare here user's data needed by user's code

int myValue;
char myString[] = "Hello!";

//	User configuration data (should be in line with userconfigui.json)
//		Declare here user's configuration data needed by user's code

String freeString;
String restrictedString;
int checkboxValue;
int numberValue;
String selectorValue;

// Declare here used callbacks
static CONFIG_CHANGED_CALLBACK(onConfigChangedCallback);
static HELP_MESSAGE_CALLBACK(onHelpMessageCallback);
static DEBUG_COMMAND_CALLBACK(onDebugCommandCallback);
static REST_COMMAND_CALLBACK(onRestCommandCallback);
static JSON_COMMAND_CALLBACK(onJsonCommandCallback);
static POST_COMMAND_CALLBACK(onPostCommandCallback);
static ERROR404_CALLBACK(onError404Callback);
static WIFI_CONNECT_CALLBACK(onWifiConnectCallback);
static WIFI_DISCONNECT_CALLBACK(onWifiDisconnectCallback);
static WIFI_GOT_IP_CALLBACK(onWifiGotIpCallback);
static MQTT_MESSAGE_CALLBACK(onMqttMessageCallback);

// Here are the callbacks code

/*!

	This routine is called when permanent configuration data has been changed.
		User should call FF_WebServer.load_user_config to get values defined in userconfigui.json.
		Values in config.json may also be get here.

	\param	none
	\return	none

*/

CONFIG_CHANGED_CALLBACK(onConfigChangedCallback) {
	trace_info_P("Entering %s", __func__);
	FF_WebServer.load_user_config("freeString", freeString);
	FF_WebServer.load_user_config("restrictedString", restrictedString);
	FF_WebServer.load_user_config("checkboxValue", checkboxValue);
	FF_WebServer.load_user_config("numberValue", numberValue);
	FF_WebServer.load_user_config("selectorValue", selectorValue);
}

/*!

	This routine is called when help message is to be printed

	\param	None
	\return	help message to be displayed

*/

HELP_MESSAGE_CALLBACK(onHelpMessageCallback) {
	return PSTR(
		"a6debug -> toggle A6 modem debug flag\r\n"
		"a6trace -> toggle A6 modem trace flag\r\n"
		"run -> toggle A6 modem run flag\r\n"
		"restart -> restart A6 modem\r\n"
		"AT or at -> send AT command\r\n"
		"> -> send command without AT prefix\r\n"
		"eof -> send EOF\r\n");
}

/*!

	This routine is called when a user's debug command is received.

	User should analyze here debug command and execute them properly.

	\note	Note that standard commands are already taken in account by server and never passed here.

	\param[in]	debugCommand: last debug command entered by user
	\return	none

*/

DEBUG_COMMAND_CALLBACK(onDebugCommandCallback) {
	trace_info_P("Entering %s", __func__);
	// "user" command is a standard one used to print user variables
	if (debugCommand == "user") {
		trace_info_P("traceFlag=%d", FF_WebServer.traceFlag);
		trace_info_P("debugFlag=%d", FF_WebServer.debugFlag);
		// -- Add here your own user variables to print
		trace_info_P("myValue=%d", myValue);
		trace_info_P("myString=%s", myString);
		trace_info_P("restrictedString=%s", restrictedString.c_str());
		trace_info_P("checkboxValue=%d", checkboxValue);
		trace_info_P("numberValue=%d", numberValue);
		trace_info_P("selectorValue=%s", selectorValue.c_str());
		// -----------
		return true;
	// Put here your own debug commands
	} else if (debugCommand == "mycmd") {
		trace_info_P("I'm inside mycmd...");
		return true;
	// -----------
	}
	return false;
}

/*!

	This routine analyze and execute REST commands sent through /rest GET command
	It should answer valid requests using a request->send(<error code>, <content type>, <content>) and returning true.

	If no valid command can be found, should return false, to let server returning an error message.

	\note	Note that minimal implementation should support at least /rest/values, which is requested by index.html
		to get list of values to display on root main page. This should at least contain "header" topic,
		displayed at top of page. Standard header contains device name, versions of user code, FF_WebServer template
		followed by device uptime. You may send something different.
		It should then contain user's values to be displayed by index_user.html file.

	\param[in]	request: AsyncWebServerRequest structure describing user's request
	\return	true for valid answered by request->send command, false else

*/
REST_COMMAND_CALLBACK(onRestCommandCallback) {
	if (request->url() == "/rest/values") {
		char tempBuffer[500];
		tempBuffer[0] = 0;

		snprintf_P(tempBuffer, sizeof(tempBuffer),
			PSTR(
				// -- Put header composition
				"header|%s V%s/%s, up since %s|div\n"
				// -- Put here user variables to be available in index_user.html
				"myValue|%d|div\n"
				"myString|%s|div\n"
				// -----------------
				)
			// -- Put here values of header line
			,FF_WebServer.getDeviceName().c_str()
			,VERSION, FF_WebServer.getWebServerVersion()
			,NTP.getUptimeString().c_str()
			// -- Put here values in index_user.html
			,myValue/1000
			,myString
			// -----------------
			);
		request->send(200, "text/plain", tempBuffer);
		return true;
	}

	trace_info_P("Entering %s", __func__);					// Put trace here as /rest/value is called every second0
	if (request->url() == "/rest/err400") {
		request->send(400, "text/plain", "Error 400 requested by user");
		return true;
	}
	return false;
}

/*!

	This routine analyze and execute JSON commands sent through /json GET command
		It should answer valid requests using a request->send(<error code>, <content type>, <JSON content>)
		and returning true.

		If no valid command can be found, should return false, to let template code returning an error message.

	\param[in]	request: AsyncWebServerRequest structure describing user's request
	\return	true for valid answered by request->send command, false else

*/

JSON_COMMAND_CALLBACK(onJsonCommandCallback) {
	trace_info_P("Entering %s", __func__);
	if (request->url() == "/json/values") {
		char tempBuffer[100];
		tempBuffer[0] = 0;

		snprintf_P(tempBuffer, sizeof(tempBuffer),
			PSTR("{"
					"\"myValue\":%d"
					",\"myString\":\"%s\""
				 "}")
			,myValue/1000
			,myString
		);
		request->send(200, "text/json", tempBuffer);
		return true;
	}

	if (request->url() == "/json/values2") {
		DynamicJsonDocument jsonDoc(250);
		jsonDoc["myValue"] = myValue/1000;
		jsonDoc["myString"] = String(myString);

		String temp;
		serializeJsonPretty(jsonDoc, temp);

		request->send(200, "text/json", temp);
		return true;
	}
	return false;
}

/*!

	This routine analyze and execute commands sent through POST command
		It should answer valid requests using a request->send(<error code>, <content type>, <content>) and returning true.

	If no valid command can be found, should return false, to let template code returning an error message.

	\param[in]	request: AsyncWebServerRequest structure describing user's request
	\return	true for valid answered by request->send command, false else

*/

POST_COMMAND_CALLBACK(onPostCommandCallback) {
	trace_info_P("Entering %s", __func__);
	return false;
}

/*!

	This routine is called when a 404 error code is to be returned by server
		User can analyze request here, and add its own. In this case, it should answer using a request->send(<error code>, <content type>, <content>) and returning true.

	If no valid answer can be found, should return false, to let template code returning an error message.

	\param[in]	request: AsyncWebServerRequest structure describing user's request
	\return	true for valid answered by request->send command, false else

*/

ERROR404_CALLBACK(onError404Callback) {
	trace_info_P("Entering %s", __func__);
	return false;
}

/*!

	This routine is called each time WiFi station is connected to an AP

	\param[in]	data: WiFiEventStationModeConnected event data
	\return	none

*/
WIFI_CONNECT_CALLBACK(onWifiConnectCallback) {
	trace_info_P("Entering %s", __func__);
}

/*!

	This routine is called each time WiFi station is disconnected from an AP

	\param[in]	data: WiFiEventStationModeDisconnected event data
	\return	none

*/
WIFI_DISCONNECT_CALLBACK(onWifiDisconnectCallback) {
	trace_info_P("Entering %s", __func__);
}

/*!

	This routine is called each time WiFi station gets an IP

	\param[in]	data: WiFiEventStationModeGotIP event data
	\return	none

*/
WIFI_GOT_IP_CALLBACK(onWifiGotIpCallback) {
	trace_info_P("Entering %s", __func__);
}

/*!

	This routine is called each time MQTT is (re)connected

	\param	none
	\return	none

*/
MQTT_CONNECT_CALLBACK(onMqttConnectCallback) {
	trace_info_P("Entering %s", __func__);
}

/*!

	This routine is called each time MQTT receives a subscribed topic

	\note	** Take care of long payload that will arrive in multiple packets **

	\param[in]	topic: received message topic
	\param[in]	payload: (part of) payload
	\param[in]	len: length of (this part of) payload
	\param[in]	index: index of (this part of) payload
	\param[in]	total: total length of all payload parts
	\return	none

*/
MQTT_MESSAGE_CALLBACK(onMqttMessageCallback) {
	trace_info_P("Entering %s", __func__);
}

//	This is the setup routine.
//		Initialize Serial, LittleFS and FF_WebServer.
//		You also have to set callbacks you need,
//		and define additional debug commands, if required.
void setup() {
	// Open serial connection
	Serial.begin(74880);
	// Start Little File System
	LittleFS.begin();
	// Start FF_WebServer
	FF_WebServer.begin(&LittleFS, VERSION);
	// Register user's trace callback, if needed
	#ifdef FF_DISABLE_DEFAULT_TRACE
		trace_register(myTraceCallback);
	#endif
	// Set user's callbacks
	FF_WebServer.setConfigChangedCallback(&onConfigChangedCallback);
	FF_WebServer.setdebugCommandCallback(&onDebugCommandCallback);
	FF_WebServer.setRestCommandCallback(&onRestCommandCallback);
	FF_WebServer.setJsonCommandCallback(&onJsonCommandCallback);
	FF_WebServer.setPostCommandCallback(&onPostCommandCallback);
	FF_WebServer.setError404Callback(&onError404Callback);
	FF_WebServer.setWifiConnectCallback(&onWifiConnectCallback);
	FF_WebServer.setWifiDisconnectCallback(&onWifiDisconnectCallback);
	FF_WebServer.setWifiGotIpCallback(&onWifiGotIpCallback);
	FF_WebServer.setMqttConnectCallback(&onMqttConnectCallback);
	FF_WebServer.setMqttMessageCallback(&onMqttMessageCallback);
}

//	This is the main loop.
//	Do what ever you want and call FF_WebServer.handle()
void loop() {
	// User part of loop
	myValue = millis();
	// Manage Web Server
	FF_WebServer.handle();
}

/*!
	This routine is called each time a trace is requested by FF_TRACE.

		It has to be declared in setup() by a "static trace_register(myTraceCallback);"

		\param[in]	_level: severity level of message (can be any FF_TRACE_LEVEL_xxxx value)
		\param[in]	_file: calling source file name with extension
		\param[in]	_line: calling source file line
		\param[in]	_function: calling calling source function name
		\param[in]	_message: text message to send
		\return	None
		\note	Note that defining FF_DISABLE_DEFAULT_TRACE will suppress default trace, if required.
				If you keep the default trace, you may add some other output media(s), like MQTT, file...
				This example code reproduce the default trace routine.
				Don't hesitate to change it, if it doesn't fit your needs.

*/
#ifdef FF_DISABLE_DEFAULT_TRACE
	trace_callback(myTraceCallback) {
		#if defined(FF_TRACE_USE_SYSLOG) || defined(FF_TRACE_USE_SERIAL) || defined(REMOTE_DEBUG) || defined(SERIAL_DEBUG)
			// Compose header with file, function, line and severity
			const char levels[] = "NEWIDV";
			char head[80];

			snprintf_P(head, sizeof(head), PSTR("%s-%s-%d-%c"), _file, _function, _line, levels[_level]);
			// Send trace to Serial if needed and not already done
			#if !defined(SERIAL_DEBUG) && defined(FF_TRACE_USE_SERIAL)
				Serial.print(head);
				Serial.print("-");
				Serial.println(_message);
			#endif
			// Send trace to syslog if needed
			#ifdef FF_TRACE_USE_SYSLOG
				syslog.deviceHostname(head);
				syslog.log(_message);
			#endif
			// Send trace to debug if needed
			#if defined(REMOTE_DEBUG) || defined(SERIAL_DEBUG)
				switch(_level) {
				case FF_TRACE_LEVEL_ERROR:
					debugE("%s-%s", head, _message);
					break;
				case FF_TRACE_LEVEL_WARN:
					debugW("%s-%s", head, _message);
					break;
				case FF_TRACE_LEVEL_INFO:
					debugI("%s-%s", head, _message);
					break;
				default:
					debugD("%s-%s", head, _message);
					break;
				}
			#endif
			#ifdef FF_TRACE_KEEP_ALIVE
				FF_WebServer.resetTraceKeepAlive();
			#endif
		#endif
	}
#endif
