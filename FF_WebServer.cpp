/*!

	FF_WebServer

	This library implements on ESP8266 a fully asynchronous Web server with:
		- MQTT connection
		- Arduino and Web OTA
		- local file system to host user and server files
		- file and/or browser based settings
		- full file editor/upload/download
		- optional telnet or serial or MQTT debug commands
		- optional serial and/or syslog trace
		- optional external hardware watchdog
		- optional Domoticz connectivity

	This code is based on a highly modified version of https://github.com/FordPrfkt/FSBrowserNG,
		itself a fork of https://github.com/gmag11/FSBrowserNG, not anymore maintained.

	Written and maintained by Flying Domotic (https://github.com/FlyingDomotic/FF_WebServer)

*/
#include "FF_WebServer.hpp"
#include <StreamString.h>

#define XQUOTE(x) #x
#define QUOTE(x) XQUOTE(x)

const char Page_WaitAndReload[] PROGMEM = R"=====(
<meta http-equiv="refresh" content="10; URL=/config.html">
Please Wait....Configuring and Restarting.
)=====";

const char Page_Restart[] PROGMEM = R"=====(
<meta http-equiv="refresh" content="10; URL=/general.html">
Please Wait....Configuring and Restarting.
)=====";

// ----- Trace -----
extern trace_declare();

// ----- Remote debug -----
#ifdef REMOTE_DEBUG
	extern RemoteDebug Debug;
#endif

#if defined(ESP8266)
	extern "C" {bool system_update_cpu_freq(uint8_t freq);}	// ESP8266 SDK
#endif

// ----- Web server -----

void AsyncFFWebServer::error404(AsyncWebServerRequest *request) {
	if (this->error404Callback) {
		if (this->error404Callback(request)) {
			return;
		}
	}
	request->send(404, "text/plain", "FileNotFound");
}

/*

	Perform URL percent decoding.

	Decoding is done in-place and will modify the parameter.

 */

void AsyncFFWebServer::percentDecode(char *src) {
	char *dst = src;
	while (*src) {
		if (*src == '+') {
			src++;
			*dst++ = ' ';
		} else if (*src == '%') {
			// handle percent escape
			*dst = '\0';
			src++;
			if (*src >= '0' && *src <= '9') {*dst = *src++ - '0';}
			else if (*src >= 'A' && *src <= 'F') {*dst = 10 + *src++ - 'A';}
			else if (*src >= 'a' && *src <= 'f') {*dst = 10 + *src++ - 'a';}
			*dst <<= 4;
			if (*src >= '0' && *src <= '9') {*dst |= *src++ - '0';}
			else if (*src >= 'A' && *src <= 'F') {*dst |= 10 + *src++ - 'A';}
			else if (*src >= 'a' && *src <= 'f') {*dst |= 10 + *src++ - 'a';}
			dst++;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';
}

/*!

	Parse an URL parameters list and return each parameter and value in a given table.

	\note	WARNING! This function overwrites the content of this string. Pass this function a copy if you need the value preserved.
	\param[in,out]	queryString: parameters string which is to be parsed (will be overwritten)
	\param[out]	results: place to put the pairs of parameter name/value (will be overwritten)
	\param[in]	resultsMaxCt: maximum number of results, = sizeof(results)/sizeof(*results)
	\param[in]	decodeUrl: if this is true, then url escapes will be decoded as per RFC 2616
	\return	Number of parameters returned in results
*/

int AsyncFFWebServer::parseUrlParams (char *queryString, char *results[][2], const int resultsMaxCt, const boolean decodeUrl) {
	int ct = 0;

	while (queryString && *queryString && ct < resultsMaxCt) {
	results[ct][0] = strsep(&queryString, "&");
	results[ct][1] = strchr(results[ct][0], '=');
	if (*results[ct][1]) *results[ct][1]++ = '\0';
	if (decodeUrl) {
		percentDecode(results[ct][0]);
		percentDecode(results[ct][1]);
	}
	ct++;
	}
	return ct;
}

// ---- MQTT ----

// Test MQTT configuration
boolean AsyncFFWebServer::mqttTest() {
	if (configMQTT_Host == "" || configMQTT_Port == 0 || configMQTT_Interval == 0 ||configMQTT_Topic == "") {
		return false;
	} else {
		return true;
	}
}

// Connect to MQTT
void AsyncFFWebServer::connectToMqtt(void) {
	// (Re)connect only if MQTT initialized, not connected to MQTT and network connected
	if (mqttInitialized && !mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
		// Wait for 30 seconds between connection tries
		if (((millis() - lastMqttConnectTime) >= 30000) || !lastMqttConnectTime) {
			lastMqttConnectTime = millis();
		if (FF_WebServer.debugFlag) trace_debug_P("Connecting to MQTT...", NULL);
		mqttClient.connect();
	}
	}
}

// Called on MQTT connection
void AsyncFFWebServer::onMqttConnect(bool sessionPresent) {
	if (FF_WebServer.debugFlag) trace_debug_P("Connected to MQTT, session present: %d", sessionPresent);
	// Send a "we're up" message
	char tempBuffer[100];
	snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("{\"state\":\"up\",\"version\":\"%s/%s\"}"), FF_WebServer.userVersion.c_str(), FF_WebServer.serverVersion.c_str());
	FF_WebServer.mqttPublishRaw(FF_WebServer.mqttWillTopic.c_str(), tempBuffer, true);
	if (FF_WebServer.debugFlag) trace_debug_P("LWT = %s", tempBuffer);
	if (FF_WebServer.mqttConnectCallback) {
		FF_WebServer.mqttConnectCallback();
	}
	if (FF_WebServer.configMQTT_CommandTopic != "") {
		FF_WebServer.mqttSubscribeRaw(FF_WebServer.configMQTT_CommandTopic.c_str());
	}
}

// Called on MQTT disconnection
void AsyncFFWebServer::onMqttDisconnect(AsyncMqttClientDisconnectReason disconnectReason) {
	if (FF_WebServer.debugFlag) trace_debug_P("Disconnected from MQTT, reason %d", disconnectReason);
	if (FF_WebServer.mqttDisconnectCallback) {
		FF_WebServer.mqttDisconnectCallback(disconnectReason);
	}
}

// Called after MQTT subscription acknowledgment
void AsyncFFWebServer::onMqttSubscribe(uint16_t packetId, uint8_t qos) {
	if (FF_WebServer.debugFlag) trace_debug_P("Subscribe done, packetId %d, qos %d", packetId, qos);
}

// Called after MQTT unsubscription acknowledgment
void AsyncFFWebServer::onMqttUnsubscribe(uint16_t packetId) {
	if (FF_WebServer.debugFlag) trace_debug_P("Unsubscribe done, packetId %d", packetId);
}

// Called when an MQTT subscribed message is received
void AsyncFFWebServer::onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
	// Take care of (very) long payload that comes in multiple messages
	char localPayload[len+1];

	strncpy(localPayload, payload, len);
	localPayload[len] = 0;
	if (FF_WebServer.traceFlag) trace_info_P("Received: topic %s, payload %s, len %d, index %d, total %d", topic, localPayload, len, index, total);
	// Do we have a MQTT command topic defined?
	if (FF_WebServer.configMQTT_CommandTopic != "") {
		// Is this topic the same?
		if (strcmp(topic, FF_WebServer.configMQTT_CommandTopic.c_str()) == 0) {
			// Yes, execute (debug) command
			FF_WebServer.executeCommand(String(localPayload));
			return;
		}
	}
	if (FF_WebServer.mqttMessageCallback) {
		FF_WebServer.mqttMessageCallback(topic, payload, properties, len, index, total);
	}
}

// Called after full MQTT message sending
void AsyncFFWebServer::onMqttPublish(uint16_t packetId) {
	if (FF_WebServer.debugFlag) trace_debug_P("Publish done, packetId %d", packetId);
}

/*!

	Subscribe to one MQTT subtopic (main topic will be prepended)

	\param[in]	subTopic: subTopic to send message to (main topic will be prepended)
	\param[in]	qos: quality of service associated with subscription (default to 0)
	\return	true if subscription if successful, false else

*/
bool AsyncFFWebServer::mqttSubscribe (const char *subTopic, const int qos) {
	char topic[80];

	snprintf_P(topic, sizeof(topic), PSTR("%s/%s"), configMQTT_Topic.c_str(), subTopic);
	return mqttSubscribeRaw(topic, qos);
}

/*!

	Subscribe to one MQTT subtopic (main topic will NOT be prepended)

	\param[in]	topic: topic to send message to (main topic will be prepended)
	\param[in]	qos: quality of service associated with subscription (default to 0)
	\return	true if subscription if successful, false else

*/
bool AsyncFFWebServer::mqttSubscribeRaw (const char *topic, const int qos) {
	bool status = mqttClient.subscribe(topic, qos);
	if (FF_WebServer.debugFlag) trace_debug_P("subscribed to %s, qos=%d, status=%d", topic, qos, status);
	return status;
}

/*!

	Publish one MQTT subtopic (main topic will be prepended)

	\param[in]	subTopic: subTopic to send message to (main topic will be prepended)
	\param[in]	value: value to send to subTopic
	\return	none

*/

void AsyncFFWebServer::mqttPublish (const char *subTopic, const char *value, bool retain) {
	char topic[80];

	snprintf_P(topic, sizeof(topic), PSTR("%s/%s"), configMQTT_Topic.c_str(), subTopic);
	mqttPublishRaw(topic, value, retain);
}

/*!

	Publish one MQTT topic (main topic will NOT be prepended)

	\param[in]	topic: topic to send message to (main topic will NOT be prepended)
	\param[in]	value: value to send to topic
	\return	none

*/
void AsyncFFWebServer::mqttPublishRaw (const char *topic, const char *value, bool retain) {
	uint16_t packetId = mqttClient.publish(topic, 1, retain, value);
	if (debugFlag) trace_debug_P("publish %s = %s, retain=%d, packedId %d", topic, value, retain, packetId);
}

// ----- Domoticz -----
#ifdef INCLUDE_DOMOTICZ
	// Domoticz is supported on (asynchronous) MQTT

	/*!

	Send a message to Domoticz for an Energy meter

	\param	idx: Domoticz device's IDX to send message to
	\param	power: instant power value
	\param	energy: total energy value
	\return	none

	*/
	void AsyncFFWebServer::sendDomoticzPower (const int idx, const float power, const float energy) {
		char url[150];

		snprintf_P(url, sizeof(url), PSTR("%.3f;%.3f;0;0;0;0"), power, energy * 1000.0);
		this->sendDomoticzValues(idx, url);
	}

	/*!

	Send a message to Domoticz for a switch

	\param[in]	idx: Domoticz device's IDX to send message to
	\param[in]	isOn: if true, sends device on, else device off
	\return	none

	*/
	void AsyncFFWebServer::sendDomoticzSwitch (const int idx, const bool isOn) {
		char url[150];

		snprintf_P(url, sizeof(url), PSTR("\"switchlight\", \"idx\": %d, \"switchcmd\": \"%s\""), idx, isOn ? "On" : "Off");
		this->sendDomoticz(url);
	}

	/*!

	Send a message to Domoticz for a dimmer

	\param[in]	idx: Domoticz device's IDX to send message to
	\param[in]	level: level value to send
	\return	none

	*/
	// Send a message to Domoticz for a dimmer
	void AsyncFFWebServer::sendDomoticzDimmer (const int idx, const uint8_t level) {
		char url[150];

		snprintf_P(url, sizeof(url), PSTR("\"switchlight\", \"idx\": %d, \"switchcmd\":\"Set Level\", \"level\": %d"), idx, level);
		this->sendDomoticz(url);
	}

	/*!

	Send a message to Domoticz with nValue and sValue

	\param[in]	idx: Domoticz device's IDX to send message to
	\param[in]	values: comma separated values to send to Domoticz as sValue
	\param[in]	integer: numeric value to send to Domoticz as nValue
	\return	none

	*/
	void AsyncFFWebServer::sendDomoticzValues (const int idx, const char *values, const int integer) {
		char url[250];

		snprintf_P(url, sizeof(url), PSTR("\"udevice\", \"idx\": %d, \"nvalue\": %d, \"svalue\": \"%s\""), idx, integer, values);
		this->sendDomoticz(url);
	}

	// Compute Domoticz signal level
	uint8_t AsyncFFWebServer::mapRSSItoDomoticz(void) {
		long rssi = WiFi.RSSI();

		if (-50 < rssi) {return 10;}
		if (rssi <= -98) {return 0;}
		rssi = rssi + 97; // Range 0..97 => 1..9
		return (rssi / 5) + 1;
	}

	// Compute Domoticz battery level
	uint8_t AsyncFFWebServer::mapVccToDomoticz(void) {

		#if FEATURE_ADC_VCC
			// Voltage range from 2.6V .. 3.6V => 0..100%
			if (vcc < 2.6) {return 0;}
			return (vcc - 2.6) * 100;
		#else // if FEATURE_ADC_VCC
			return 255;
		#endif // if FEATURE_ADC_VCC
	}

	// Send a message to Domoticz
	void AsyncFFWebServer::sendDomoticz(const char* url){
		// load full URL
		char fullUrl[200];

		snprintf_P(fullUrl, sizeof(fullUrl), PSTR("{\"command\": %s, \"rssi\": %d, \"battery\": %d}"), url, this->mapRSSItoDomoticz(), this->mapVccToDomoticz());
		mqttPublishRaw("domoticz/in", fullUrl, true);
	}
#endif


/*!

	Reset trace keep alive timer

	Automatically called by default trace callback.
	To be called by user's callback if automatic trace callback is disabled (FF_DISABLE_DEFAULT_TRACE defined).

	\param	None
	\return	None

*/
#ifdef FF_TRACE_KEEP_ALIVE
	void AsyncFFWebServer::resetTraceKeepAlive(void) {
		lastTraceMessage = millis();
	}
#endif

// Called each time system or user config is changed
void AsyncFFWebServer::loadConfig(void) {
	if (FF_WebServer.traceFlag) trace_info_P("Load config", NULL);
	load_user_config("MQTTHost", configMQTT_Host);
	load_user_config("MQTTPass", configMQTT_Pass);
	load_user_config("MQTTPort", configMQTT_Port);
	load_user_config("MQTTTopic", configMQTT_Topic);
	load_user_config("MQTTCommandTopic", configMQTT_CommandTopic);
	load_user_config("MQTTUser", configMQTT_User);
	load_user_config("MQTTClientID", configMQTT_ClientID);
	load_user_config("MQTTInterval", configMQTT_Interval);
	#ifdef FF_TRACE_USE_SYSLOG
		load_user_config("SyslogServer", syslogServer);
		load_user_config("SyslogPort", syslogPort);
	#endif
}

// Called each time system or user config is changed
void AsyncFFWebServer::loadUserConfig(void) {
	if (FF_WebServer.traceFlag) trace_info_P("Load user config", NULL);
	if (configChangedCallback) {
		configChangedCallback();
	}
}

// Class definition
AsyncFFWebServer::AsyncFFWebServer(uint16_t port) : AsyncWebServer(port) {
	// Clear serialBuffer
	memset(serialCommand, 0, sizeof(serialCommand));
}

// Called each second
void AsyncFFWebServer::s_secondTick(void* arg) {
	AsyncFFWebServer* self = reinterpret_cast<AsyncFFWebServer*>(arg);
	if (self->_evs.count() > 0) {
		self->sendTimeData();
	}

	//Check WiFi connection timeout if enabled
	#if (AP_ENABLE_TIMEOUT > 0)
		if (self->wifiStatus == FS_STAT_CONNECTING) {
			if (++self->connectionTimout >= AP_ENABLE_TIMEOUT){
				DEBUG_ERROR_P("Connection Timeout, switching to AP Mode", NULL);
				self->configureWifiAP();
			}
		}
	#endif //AP_ENABLE_TIMEOUT
}

// Send time data
void AsyncFFWebServer::sendTimeData() {
	String timeData = PSTR("{\"time\":\"") + NTP.getTimeStr() + PSTR("\",")
		+ PSTR("\"date\":\"") + NTP.getDateStr() + PSTR("\",")
		+ PSTR("\"lastSync\":\"") + NTP.getTimeDateString(NTP.getLastNTPSync()) + PSTR("\",")
		+ PSTR("\"uptime\":\"") + NTP.getUptimeString() + PSTR("\",")
		+ PSTR("\"lastBoot\":\"") + NTP.getTimeDateString(NTP.getLastBootTime()) + PSTR("\"")
		+ PSTR("}\r\n");
	DEBUG_VERBOSE(timeData.c_str());
	_evs.send(timeData.c_str(), "timeDate");
	timeData = String();
}

// Format a size in B(ytes), KB, MB or GB
String AsyncFFWebServer::formatBytes(size_t bytes) {
	if (bytes < 1024) {
		return String(bytes) + "B";
	} else if (bytes < (1024 * 1024)) {
		return String(bytes / 1024.0) + "KB";
	} else if (bytes < (1024 * 1024 * 1024)) {
		return String(bytes / 1024.0 / 1024.0) + "MB";
	} else {
		return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
	}
}


#if (CONNECTION_LED >= 0)
	// Flash LED
	//	*** Warning *** Not asynchronous, will block CPU for delayTime * times * 2 milliseconds
	void AsyncFFWebServer::flashLED(const int pin, const int times, int delayTime) {
	int oldState = digitalRead(pin);
	DEBUG_VERBOSE_P("---Flash LED during %d ms %d times. Old state = %d", delayTime, times, oldState);

	for (int i = 0; i < times; i++) {
		digitalWrite(pin, LOW); // Turn on LED
		delay(delayTime);
		digitalWrite(pin, HIGH); // Turn on LED
		delay(delayTime);
	}
	digitalWrite(pin, oldState); // Turn on LED
	}
#endif

// Return standard help message
String AsyncFFWebServer::standardHelpCmd() {
	return String(PSTR("vars -> dump standard variables\r\n" 
		"user -> dump user variables\r\n"
		"debug -> toggle debug flag\r\n"
		"trace -> toggle trace flag\r\n"
		"wdt -> toggle watchdog flag\r\n"));
}

/*!

	Initialize FF_WebServer class

	To be called in setup()

	Will try to connect to WiFi only during the first 10 seconds of life.

	\param[in]	fs: (Little) file system to use
	\param[in]	version: user's code version (used in trace)
	\return	None

*/
void AsyncFFWebServer::begin(FS* fs, const char *version) {
	_fs = fs;
	userVersion = String(version);
	connectionTimout = 0;

	// ----- Global trace ----
	// Register Serial trace callback
	#ifndef FF_DISABLE_DEFAULT_TRACE
		trace_register((void (*)(traceLevel_t, const char*, uint16_t, const char*, const char*))&AsyncFFWebServer::defaultTraceCallback);
	#endif

	loadConfig();

	if (!load_config()) { // Try to load configuration from file system
		defaultConfig(); // Load defaults if any error
		_apConfig.APenable = true;
	}

	loadHTTPAuth();

	// Register Wifi Events
	onStationModeConnectedHandler = WiFi.onStationModeConnected([this](WiFiEventStationModeConnected data) {
		this->onWiFiConnected(data);
	});


	onStationModeDisconnectedHandler = WiFi.onStationModeDisconnected([this](WiFiEventStationModeDisconnected data) {
		this->onWiFiDisconnected(data);
	});

	onStationModeGotIPHandler = WiFi.onStationModeGotIP([this](WiFiEventStationModeGotIP data) {
		this->onWiFiConnectedGotIP(data);
	});

	#ifdef FF_TRACE_USE_SYSLOG
		syslog.server(syslogServer.c_str(), syslogPort);
		syslog.deviceHostname(FF_WebServer.getDeviceName().c_str());
		syslog.defaultPriority(LOG_KERN);
	#endif

	uint32_t chipId = 0;
	// Force client id if empty or starts with "ESP_" and not right chip id
	char tempBuffer[16];

	chipId = ESP.getChipId();
	snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("ESP_%x"), chipId);

	if ((configMQTT_ClientID == "") || (configMQTT_ClientID.startsWith("ESP_") && strcmp(configMQTT_ClientID.c_str(), tempBuffer))) {
		configMQTT_ClientID = tempBuffer;
		FF_WebServer.save_user_config("MQTTClientID", configMQTT_ClientID);
	}

	WiFi.hostname(_config.deviceName.c_str());
	////WiFi.forceSleepWake();
	////WiFi.setSleepMode(WIFI_NONE_SLEEP);
	#if (AP_ENABLE_BUTTON >= 0)
		if (_apConfig.APenable) {
			configureWifiAP(); // Set AP mode if AP button was pressed
		} else {
			configureWifi(); // Set WiFi config
		}
	#else
		configureWifi(); // Set WiFi config
	#endif

	unsigned long startWait = millis();
	// Wait for Wifi up in first 10 seconds of life
	#if (WIFI_MAX_WAIT_SECS >=1)
		while ((WiFi.status() != WL_CONNECTED) && ((millis() - startWait) <= (WIFI_MAX_WAIT_SECS * 1000))) {
		yield();
	}
	#endif

	trace_debug_P("WiFi status = %d (%sconnected)", WiFi.status(), (WiFi.status() != WL_CONNECTED) ? "NOT ":"");

	if (_config.updateNTPTimeEvery > 0) { // Enable NTP sync
		NTP.begin(_config.ntpServerName, _config.timezone / 10, _config.daylight);
		NTP.setInterval(15, _config.updateNTPTimeEvery * 60);
	}

	#ifdef REMOTE_DEBUG
		// Initialize RemoteDebug
		Debug.begin(FF_WebServer.getDeviceName().c_str()); // Initialize the WiFi server
    if (_httpAuth.wwwPassword !=  "") {
		  Debug.setPassword(_httpAuth.wwwPassword.c_str()); // Password on telnet connection
    }
		Debug.setResetCmdEnabled(true); // Enable the reset command
		Debug.showProfiler(true); // Profiler (Good to measure times, to optimize codes)
		Debug.showColors(true); // Colors
		//Debug.setSerialEnabled(true); // if you wants serial echo - only recommended if ESP is plugged in USB
		// Set help message
		Debug.setHelpProjectsCmds(PSTR("help -> display full help message"));
		Debug.setCallBackProjectCmds((void(*)())&AsyncFFWebServer::executeDebugCommand);
	#endif
	#ifdef SERIAL_DEBUG
		extern boolean _debugActive;						// Debug is only active after receive first data from Serial
		extern uint8_t _debugLevel;							// Current level of debug (init as disabled)
		extern bool _debugShowProfiler;						// Show profiler time ?
		_debugActive = true;
		_debugLevel = DEBUG_LEVEL_VERBOSE;
		_debugShowProfiler = false;
	#endif

	struct rst_info *rtc_info = system_get_rst_info();
	// Send reset reason
	trace_info_P("%s V%s/%s starting, reset reason: %x - %s", FF_WebServer.getDeviceName().c_str(), FF_WebServer.userVersion.c_str(), FF_WebServer.serverVersion.c_str(), rtc_info->reason, ESP.getResetReason().c_str());
	// In case of software restart, send additional info
	if (rtc_info->reason == REASON_WDT_RST || rtc_info->reason == REASON_EXCEPTION_RST || rtc_info->reason == REASON_SOFT_WDT_RST) {
		// If crashed, print exception
		if (rtc_info->reason == REASON_EXCEPTION_RST) {
			trace_error_P("Fatal exception (%d):", rtc_info->exccause);
		}
		trace_error_P("epc1=0x%08x, epc2=0x%08x, epc3=0x%08x, excvaddr=0x%08x, depc=0x%08x", rtc_info->epc1, rtc_info->epc2, rtc_info->epc3, rtc_info->excvaddr, rtc_info->depc);
	}

	if (mqttTest()) {
		mqttClient.onConnect((void(*)(bool))&AsyncFFWebServer::onMqttConnect);
		mqttClient.onDisconnect((void (*)(AsyncMqttClientDisconnectReason))&AsyncFFWebServer::onMqttDisconnect);
		mqttClient.onSubscribe((void (*)(uint16_t, uint8_t))&AsyncFFWebServer::onMqttSubscribe);
		mqttClient.onUnsubscribe((void (*)(uint16_t))&AsyncFFWebServer::onMqttUnsubscribe);
		mqttClient.onMessage((void (*)(char*, char*, AsyncMqttClientMessageProperties, size_t, size_t, size_t)) &AsyncFFWebServer::onMqttMessage);
		mqttClient.onPublish((void (*)(uint16_t))&AsyncFFWebServer::onMqttPublish);
		if (configMQTT_ClientID != "") {
			mqttClient.setClientId(configMQTT_ClientID.c_str());
		}
		if (configMQTT_User != "") {
			mqttClient.setCredentials(configMQTT_User.c_str(), configMQTT_Pass.c_str());
		}
		mqttWillTopic = configMQTT_Topic + "/LWT";
		mqttClient.setWill(mqttWillTopic.c_str(), 1, true,	"{\"state\":\"down\"}");

		mqttClient.setServer(configMQTT_Host.c_str(), configMQTT_Port);
	} else {
		trace_error_P("MQTT config error: Host %s Port %d User %s Pass %s Id %s Topic %s Interval %d",
			configMQTT_Host.c_str(), configMQTT_Port, configMQTT_User.c_str(), configMQTT_Pass.c_str(),
			configMQTT_ClientID.c_str(), configMQTT_Topic.c_str(), configMQTT_Interval);
	}

	#ifdef HARDWARE_WATCHDOG_PIN
		pinMode(HARDWARE_WATCHDOG_PIN, OUTPUT);
		hardwareWatchdogState = HARDWARE_WATCHDOG_INITIAL_STATE;
		digitalWrite(HARDWARE_WATCHDOG_PIN, hardwareWatchdogState ? HIGH : LOW);
		hardwareWatchdogDelay = hardwareWatchdogState ? HARDWARE_WATCHDOG_ON_DELAY : HARDWARE_WATCHDOG_OFF_DELAY;
	#endif

	#ifdef DEBUG_FF_WEBSERVER
		#ifdef FF_TRACE_USE_SERIAL
			Serial.setDebugOutput(true);
		#endif
	#endif // DEBUG_FF_WEBSERVER

	// NTP client setup
	#if (CONNECTION_LED >= 0)
		pinMode(CONNECTION_LED, OUTPUT); // CONNECTION_LED pin defined as output
		digitalWrite(CONNECTION_LED, HIGH); // Turn LED off
	#endif
	#if (AP_ENABLE_BUTTON >= 0)
		pinMode(AP_ENABLE_BUTTON, INPUT_PULLUP); // If this pin is HIGH during startup ESP will run in AP_ONLY mode. Backdoor to change WiFi settings when configured WiFi is not available.
		_apConfig.APenable = !digitalRead(AP_ENABLE_BUTTON); // Read AP button. If button is pressed activate AP
		DEBUG_VERBOSE_P("AP Enable = %d", _apConfig.APenable);
	#endif

	if (!_fs) // If LitleFS is not started
		_fs->begin();
#ifdef DEBUG_FF_WEBSERVER
	// List files
	Dir dir = _fs->openDir("/");
	while (dir.next()) {
		String fileName = dir.fileName();
		size_t fileSize = dir.fileSize();
		DEBUG_VERBOSE_P("FS File: %s, size: %s", fileName.c_str(), formatBytes(fileSize).c_str());
	}
#endif // DEBUG_FF_WEBSERVER
	_secondTk.attach(1.0f, (void (*) (void*)) &AsyncFFWebServer::s_secondTick, static_cast<void*>(this)); // Task to run periodic things every second

	AsyncWebServer::begin();								// Start underlying AsyncWebServer class
	serverInit(); // Configure and start Web server

	#ifndef DISABLE_MDNS
		MDNS.begin(_config.deviceName.c_str());
	MDNS.addService("http", "tcp", 80);
	#endif
	ConfigureOTA(_httpAuth.wwwPassword.c_str());
	serverStarted = true;
	loadUserConfig();
	if (mqttTest()) {
		mqttInitialized = true;
	}
	FF_WebServer.lastTraceLevel = trace_getLevel();			// Save current trace level
	DEBUG_VERBOSE_P("END Setup");
}

//	Load config.json file
bool AsyncFFWebServer::load_config() {
	File configFile = _fs->open(CONFIG_FILE, "r");
	if (!configFile) {
		DEBUG_ERROR_P("Failed to open %s", CONFIG_FILE);
		return false;
	}

	size_t size = configFile.size();
	/*if (size > 1024) {
	DEBUG_VERBOSE_P("Config file size is too large");
	configFile.close();
	return false;
	}*/

	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);

	// We don't use String here because ArduinoJson library requires the input
	// buffer to be mutable. If you don't use ArduinoJson, you may as well
	// use configFile.readString instead.
	configFile.readBytes(buf.get(), size);
	configFile.close();
	DEBUG_VERBOSE_P("JSON file size: %d bytes", size);
	DynamicJsonDocument jsonDoc(1024);
	auto error = deserializeJson(jsonDoc, buf.get());
	if (error) {
		DEBUG_ERROR_P("Failed to parse %s. Error: %s", CONFIG_FILE, error.c_str());
		return false;
	}

	#ifdef DEBUG_FF_WEBSERVER
		String temp;
		serializeJsonPretty(jsonDoc, temp);
		DEBUG_VERBOSE_P("Config: %s", temp.c_str());
	#endif

	_config.ssid = jsonDoc["ssid"].as<const char *>();
	_config.password = jsonDoc["pass"].as<const char *>();

	_config.ip = IPAddress(jsonDoc["ip"][0], jsonDoc["ip"][1], jsonDoc["ip"][2], jsonDoc["ip"][3]);
	_config.netmask = IPAddress(jsonDoc["netmask"][0], jsonDoc["netmask"][1], jsonDoc["netmask"][2], jsonDoc["netmask"][3]);
	_config.gateway = IPAddress(jsonDoc["gateway"][0], jsonDoc["gateway"][1], jsonDoc["gateway"][2], jsonDoc["gateway"][3]);
	_config.dns = IPAddress(jsonDoc["dns"][0], jsonDoc["dns"][1], jsonDoc["dns"][2], jsonDoc["dns"][3]);

	_config.dhcp = jsonDoc["dhcp"].as<bool>();

	_config.ntpServerName = jsonDoc["ntp"].as<const char *>();
	_config.updateNTPTimeEvery = jsonDoc["NTPperiod"].as<long>();
	_config.timezone = jsonDoc["timeZone"].as<long>();
	_config.daylight = jsonDoc["daylight"].as<long>();
	_config.deviceName = jsonDoc["deviceName"].as<const char *>();

	//config.connectionLed = jsonDoc["led"];

	DEBUG_VERBOSE_P("Data initialized, SSID: %s, PASS %s, NTP Server: %s", _config.ssid.c_str(), _config.password.c_str(), _config.ntpServerName.c_str());
	return true;
}

// Save default config
void AsyncFFWebServer::defaultConfig() {
	// DEFAULT CONFIG
	_config.ssid = "WIFI_SSID";
	_config.password = "WIFI_PASSWD";
	_config.dhcp = 1;
	_config.ip = IPAddress(192, 168, 1, 4);
	_config.netmask = IPAddress(255, 255, 255, 0);
	_config.gateway = IPAddress(192, 168, 1, 1);
	_config.dns = IPAddress(192, 168, 1, 1);
	_config.ntpServerName = "pool.ntp.org";
	_config.updateNTPTimeEvery = 15;
	_config.timezone = 10;
	_config.daylight = 1;
	_config.deviceName = "FF_WebServer";
	//config.connectionLed = CONNECTION_LED;
	save_config();
}

//	Save current config to file
bool AsyncFFWebServer::save_config() {
	//flag_config = false;
	DEBUG_VERBOSE_P("Save config");
	DynamicJsonDocument jsonDoc(512);

	jsonDoc["ssid"] = _config.ssid;
	jsonDoc["pass"] = _config.password;

	JsonArray jsonip = jsonDoc.createNestedArray("ip");
	jsonip.add(_config.ip[0]);
	jsonip.add(_config.ip[1]);
	jsonip.add(_config.ip[2]);
	jsonip.add(_config.ip[3]);

	JsonArray jsonNM = jsonDoc.createNestedArray("netmask");
	jsonNM.add(_config.netmask[0]);
	jsonNM.add(_config.netmask[1]);
	jsonNM.add(_config.netmask[2]);
	jsonNM.add(_config.netmask[3]);

	JsonArray jsonGateway = jsonDoc.createNestedArray("gateway");
	jsonGateway.add(_config.gateway[0]);
	jsonGateway.add(_config.gateway[1]);
	jsonGateway.add(_config.gateway[2]);
	jsonGateway.add(_config.gateway[3]);

	JsonArray jsondns = jsonDoc.createNestedArray("dns");
	jsondns.add(_config.dns[0]);
	jsondns.add(_config.dns[1]);
	jsondns.add(_config.dns[2]);
	jsondns.add(_config.dns[3]);

	jsonDoc["dhcp"] = _config.dhcp;
	jsonDoc["ntp"] = _config.ntpServerName;
	jsonDoc["NTPperiod"] = _config.updateNTPTimeEvery;
	jsonDoc["timeZone"] = _config.timezone;
	jsonDoc["daylight"] = _config.daylight;
	jsonDoc["deviceName"] = _config.deviceName;

	File configFile = _fs->open(CONFIG_FILE, "w");
	if (!configFile) {
		DEBUG_ERROR_P("Failed to open %s for writing", CONFIG_FILE);
		configFile.close();
		return false;
	}

	#ifdef DEBUG_FF_WEBSERVER
		String temp;
		serializeJsonPretty(jsonDoc, temp);
		DEBUG_VERBOSE_P("Saved config: %s", temp.c_str());
	#endif
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}

/*!

	Clear system configuration

	\param[in]	reset: reset ESP if true
	\return	None

*/
void AsyncFFWebServer::clearConfig(bool reset)
{
	if (_fs->exists(CONFIG_FILE)) {
		_fs->remove(CONFIG_FILE);
	}

	if (_fs->exists(SECRET_FILE)) {
		_fs->remove(SECRET_FILE);
	}

	if (reset) {
		_fs->end();
		ESP.restart();
	}
}

/*!

	Load an user's configuration String

	\param[in]	name: item name to return
	\param[out]	value: returned (String) value
	\return	false if error detected, true else

*/
bool AsyncFFWebServer::load_user_config(String name, String &value) {
	File configFile = _fs->open(USER_CONFIG_FILE, "r");
	if (!configFile) {
		DEBUG_ERROR_P("Failed to open %s", USER_CONFIG_FILE);
		return false;
	}

	size_t size = configFile.size();
	/*if (size > 1024) {
	DEBUG_ERROR_P("Config file size is too large");
	configFile.close();
	return false;
	}*/

	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);

	// We don't use String here because ArduinoJson library requires the input
	// buffer to be mutable. If you don't use ArduinoJson, you may as well
	// use configFile.readString instead.
	configFile.readBytes(buf.get(), size);
	configFile.close();
	DEBUG_VERBOSE_P("JSON file size: %d bytes", size);
	DynamicJsonDocument jsonDoc(1024);
	auto error = deserializeJson(jsonDoc, buf.get());
	if (error) {
		DEBUG_ERROR_P("Failed to parse %s. Error: %s", USER_CONFIG_FILE, error.c_str());
		return false;
	}

	value = jsonDoc[name].as<const char*>();

	#ifdef DEBUG_FF_WEBSERVER
		DEBUG_VERBOSE_P("User config: %s=%s", name.c_str(), value.c_str());
	#endif
	return true;
}

// Save one user config item (String)
bool AsyncFFWebServer::save_user_config(String name, String value) {
	//add logic to test and create if non
	DEBUG_VERBOSE_P("%s: %s", name.c_str(), value.c_str());

	File configFile;
	if (!_fs->exists(USER_CONFIG_FILE)) {
		configFile = _fs->open(USER_CONFIG_FILE, "w");
		if (!configFile) {
			DEBUG_ERROR_P("Failed to open %s for writing", USER_CONFIG_FILE);
			configFile.close();
			return false;
		}
		//create blank json file
		DEBUG_VERBOSE_P("Creating user %s for writing", USER_CONFIG_FILE);
		configFile.print("{}");
		configFile.close();
	}
	//get existing json file
	configFile = _fs->open(USER_CONFIG_FILE, "r");
	if (!configFile) {
		DEBUG_ERROR_P("Failed to open %s", USER_CONFIG_FILE);
		return false;
	}
	size_t size = configFile.size();
	/*if (size > 1024) {
	DEBUG_VERBOSE_P("Config file size is too large");
	configFile.close();
	return false;
	}*/

	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);

	// We don't use String here because ArduinoJson library requires the input
	// buffer to be mutable. If you don't use ArduinoJson, you may as well
	// use configFile.readString instead.
	configFile.readBytes(buf.get(), size);
	configFile.close();
	DEBUG_VERBOSE_P("Read JSON file size: %d bytes", size);
	DynamicJsonDocument jsonDoc(1024);
	auto error = deserializeJson(jsonDoc, buf.get());

	if (error) {
		DEBUG_ERROR_P("Failed to parse %s. Error: %s", USER_CONFIG_FILE, error.c_str());
		return false;
	} else {
		DEBUG_VERBOSE_P("Parse User config file");
	}

	jsonDoc[name] = value;

	configFile = _fs->open(USER_CONFIG_FILE, "w");
	if (!configFile) {
		DEBUG_ERROR_P("Failed to open %s for writing", USER_CONFIG_FILE);
		configFile.close();
		return false;
	}

	#ifdef DEBUG_FF_WEBSERVER
		String temp;
		serializeJsonPretty(jsonDoc, temp);
		DEBUG_VERBOSE_P("Save user config %s", temp.c_str());
	#endif
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}


/*!

	Clear user configuration

	\param[in]	reset: reset ESP if true
	\return	None

*/
void AsyncFFWebServer::clearUserConfig(bool reset) {
	if (_fs->exists(USER_CONFIG_FILE)) {
		_fs->remove(USER_CONFIG_FILE);
	}

	if (reset) {
		_fs->end();
		ESP.restart();
	}
}

/*!

	Load an user's configuration integer

	\param[in]	name: item name to return
	\param[out]	value: returned (int) value
	\return	false if error detected, true else

*/
bool AsyncFFWebServer::load_user_config(String name, int &value)
{
	String sTemp = "";
	bool bTemp = load_user_config(name, sTemp);
	value = sTemp.toInt();
	return bTemp;
}

// Save one user config item (int)
bool AsyncFFWebServer::save_user_config(String name, int value) {
	return AsyncFFWebServer::save_user_config(name, String(value));
}

/*!

	Load an user's configuration float

	\param[in]	name: item name to return
	\param[out]	value: returned (float) value
	\return	false if error detected, true else

*/
bool AsyncFFWebServer::load_user_config(String name, float &value) {
	String sTemp = "";
	bool bTemp = load_user_config(name, sTemp);
	value = sTemp.toFloat();
	return bTemp;
}

// Save one user config item (float)
bool AsyncFFWebServer::save_user_config(String name, float value) {
	return AsyncFFWebServer::save_user_config(name, String(value, 8));
}

/*!

	Load an user's configuration long

	\param[in]	name: item name to return
	\param[out]	value: returned (long) value
	\return	false if error detected, true else

*/
bool AsyncFFWebServer::load_user_config(String name, long &value) {
	String sTemp = "";
	bool bTemp = load_user_config(name, sTemp);
	value = atol(sTemp.c_str());
	return bTemp;
}

// Save one user config item (long)
bool AsyncFFWebServer::save_user_config(String name, long value) {
	return AsyncFFWebServer::save_user_config(name, String(value));
}

// Load secret file
bool AsyncFFWebServer::loadHTTPAuth() {
	File configFile = _fs->open(SECRET_FILE, "r");
	if (!configFile) {
		DEBUG_ERROR_P("Failed to open %s", SECRET_FILE);
		_httpAuth.auth = false;
		_httpAuth.wwwUsername = "";
		_httpAuth.wwwPassword = "";
		configFile.close();
		return false;
	}

	size_t size = configFile.size();
	/*if (size > 256) {
	DEBUG_VERBOSE_P("Secret file size is too large");
	httpAuth.auth = false;
	configFile.close();
	return false;
	}*/

	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);

	// We don't use String here because ArduinoJson library requires the input
	// buffer to be mutable. If you don't use ArduinoJson, you may as well
	// use configFile.readString instead.
	configFile.readBytes(buf.get(), size);
	configFile.close();
	DEBUG_VERBOSE_P("JSON secret file size: %d bytes", size);
	DynamicJsonDocument jsonDoc(256);
	auto error = deserializeJson(jsonDoc, buf.get());

	if (error) {
		#ifdef DEBUG_FF_WEBSERVER
			String temp;
			serializeJsonPretty(jsonDoc, temp);
			DEBUG_ERROR_P("Failed to parse %s. Error: %s", SECRET_FILE, error.c_str());
			DEBUG_ERROR_P("Contents %s", temp.c_str());
		#endif // DEBUG_FF_WEBSERVER
		_httpAuth.auth = false;
		return false;
	}
	#ifdef DEBUG_FF_WEBSERVER
		String temp;
		serializeJsonPretty(jsonDoc, temp);
		DEBUG_VERBOSE_P("Secret %s", temp.c_str());
	#endif // DEBUG_FF_WEBSERVER

	_httpAuth.auth = jsonDoc["auth"];
	_httpAuth.wwwUsername = jsonDoc["user"].as<String>();
	_httpAuth.wwwPassword = jsonDoc["pass"].as<String>();

	DEBUG_VERBOSE_P(_httpAuth.auth ? PSTR("Secret initialized") : PSTR("Auth disabled"));
	if (_httpAuth.auth) {
		DEBUG_VERBOSE_P("User: %s, Pass %s", _httpAuth.wwwUsername.c_str(), _httpAuth.wwwPassword.c_str());
	}
	return true;
}

/*!

	Handle FFWebServer stuff

	Should be called from main loop to make Web server servicing

	\param	None
	\return	None

*/
void AsyncFFWebServer::handle(void) {
	// Manage debug
	#ifdef REMOTE_DEBUG
		Debug.handle();
	#endif
	#ifdef SERIAL_DEBUG
		debugHandle();
	#endif
	// Manage Serial commands
	#if defined(SERIAL_COMMAND_PREFIX) || !defined(NO_SERIAL_COMMAND_CALLBACK)
		while(Serial.available()>0) {
			char c = Serial.read();
			// Check for end of line
			if (c == '\n' || c== '\r') {
				// Do we have some command?
				if (serialCommandLen) {
					#ifdef SERIAL_COMMAND_PREFIX
						// Is command starting by prefix?
						String command = serialCommand;
						if (command.startsWith(PSTR(QUOTE(SERIAL_COMMAND_PREFIX)))) {
							command = command.substring(sizeof(QUOTE(SERIAL_COMMAND_PREFIX))-1);
							trace_info_P("Executing command %s", command.c_str());
							executeCommand(command);
						} else {
					#endif
					if (this->serialCommandCallback) {
						(this->serialCommandCallback(command));
					}
					#ifdef SERIAL_COMMAND_PREFIX
						}
					#endif
				}
				// Reset command
				serialCommandLen = 0;
				serialCommand[serialCommandLen] = '\0';
			} else {
				// Do we still have room in buffer?
				if (serialCommandLen < sizeof(serialCommand)) {
						// Yes, add char
						serialCommand[serialCommandLen++] = c;
						serialCommand[serialCommandLen] = '\0';
				} else {
					// Reset command
					serialCommandLen = 0;
					serialCommand[serialCommandLen] = '\0';
				}
			}
		}
	#endif

	#ifdef HARDWARE_WATCHDOG_PIN
		if ((((millis() - hardwareWatchdogLastUpdate) > hardwareWatchdogDelay)) && watchdogFlag) {
			hardwareWatchdogLastUpdate = millis();
			hardwareWatchdogState = ! hardwareWatchdogState;
			digitalWrite(HARDWARE_WATCHDOG_PIN, hardwareWatchdogState ? HIGH : LOW);
			hardwareWatchdogDelay = hardwareWatchdogState ? HARDWARE_WATCHDOG_ON_DELAY : HARDWARE_WATCHDOG_OFF_DELAY;
		}
	#endif

	#ifdef FF_TRACE_KEEP_ALIVE
		if ((millis() - lastTraceMessage) >= traceKeepAlive) {
			trace_info_P("I'm still alive...", NULL);
			// Note that lastTraceMessage is loaded with millis() by trace routine
		}
	#endif

	// Handle OTA
	ArduinoOTA.handle();

	// Handle time udate from NTP
	if (updateTimeFromNTP) {
		NTP.begin(_config.ntpServerName, _config.timezone / 10, _config.daylight);
		NTP.setInterval(15, _config.updateNTPTimeEvery * 60);
		updateTimeFromNTP = false;
	}

	// Handle MQTT (re)connection
	if (!mqttClient.connected()) {						// If MQTT is not connected
		connectToMqtt();								// Connect to MQTT
	}
}

//	Start WiFi Access Point (after disconnecting WiFi client if needed)
void AsyncFFWebServer::configureWifiAP() {
	if (WiFi.status() == WL_CONNECTED) {
		WiFi.disconnect();
	}

	WiFi.mode(WIFI_AP);

	wifiStatus = FS_STAT_APMODE;
	connectionTimout = 0;

	String APname = _apConfig.APssid + (String)ESP.getChipId();
	if (_httpAuth.auth) {
		WiFi.softAP(APname.c_str(), _httpAuth.wwwPassword.c_str());
		DEBUG_VERBOSE_P("AP Pass enabled: %s", _httpAuth.wwwPassword.c_str());
	} else {
		WiFi.softAP(APname.c_str());
		DEBUG_VERBOSE_P("AP Pass disabled");
	}
	#if (CONNECTION_LED >= 0)
		flashLED(CONNECTION_LED, 3, 250);
	#endif

	DEBUG_ERROR_P("AP Mode enabled. SSID: %s IP: %s", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
}

//	Start WiFi client (afte disconnecting AP if needed)
void AsyncFFWebServer::configureWifi() {
	if (WiFi.status() == WL_CONNECTED) {
		WiFi.disconnect();
	}
	//encourge clean recovery after disconnect species5618, 08-March-2018
	WiFi.setAutoReconnect(true);
	WiFi.mode(WIFI_STA);


	DEBUG_VERBOSE_P("Connecting to %s", _config.ssid.c_str());
	WiFi.begin(_config.ssid.c_str(), _config.password.c_str());
	if (!_config.dhcp) {
		DEBUG_ERROR_P("NO DHCP", NULL);
		WiFi.config(_config.ip, _config.gateway, _config.netmask, _config.dns);
	}

	connectionTimout = 0;
	wifiStatus = FS_STAT_CONNECTING;
}

// Configure Arduino OTA
void AsyncFFWebServer::ConfigureOTA(String password) {
	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(_config.deviceName.c_str());

	// No authentication by default
	if (password != "") {
		ArduinoOTA.setPassword(password.c_str());
		DEBUG_VERBOSE_P("OTA password set %s", password.c_str());
	}

	#ifdef DEBUG_FF_WEBSERVER
		ArduinoOTA.onStart([]() {
			DEBUG_VERBOSE_P("StartOTA");
		});
		ArduinoOTA.onEnd(std::bind([](FS *fs) {
			fs->end();
			DEBUG_VERBOSE_P("End OTA");
		}, _fs));
		ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
			DEBUG_VERBOSE_P("OTA Progress: %u%%", (progress / (total / 100)));
		});
		ArduinoOTA.onError([](ota_error_t error) {
			if (error == OTA_AUTH_ERROR) DEBUG_ERROR_P("OTA auth Failed", NULL);
			else if (error == OTA_BEGIN_ERROR) DEBUG_ERROR_P("OTA begin Failed", NULL);
			else if (error == OTA_CONNECT_ERROR) DEBUG_ERROR_P("OTA connect Failed", NULL);
			else if (error == OTA_RECEIVE_ERROR) DEBUG_ERROR_P("OTA receive Failed", NULL);
			else if (error == OTA_END_ERROR) DEBUG_ERROR_P("OTA end Failed", NULL);
			else DEBUG_ERROR_P("OTA error %u", error);
		});
		DEBUG_VERBOSE_P("OTA Ready");
	#endif // DEBUG_FF_WEBSERVER
	ArduinoOTA.begin();
}

// On WiFi connected
void AsyncFFWebServer::onWiFiConnected(WiFiEventStationModeConnected data) {
	DEBUG_VERBOSE_P("WiFi Connected: Waiting for DHCP");
	#if (CONNECTION_LED >= 0) 
		digitalWrite(CONNECTION_LED, LOW); // Turn LED on
		DEBUG_VERBOSE_P("Led %d on", CONNECTION_LED);
	#endif
	byte mac[6];
	WiFi.macAddress(mac);
	if (FF_WebServer.lastDisconnect) {
		if (FF_WebServer.traceFlag) trace_info_P("Wifi reconnected to %s after %d seconds, MAC=%2x:%2x:%2x:%2x:%2x:%2x",
			WiFi.SSID().c_str(), (int)((millis() - FF_WebServer.lastDisconnect) / 1000), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		if (FF_WebServer.traceFlag) trace_info_P("Wifi connected to %s, MAC=%2x:%2x:%2x:%2x:%2x:%2x", WiFi.SSID().c_str(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}
	WiFi.setAutoReconnect(true);
	if (FF_WebServer.wifiConnectCallback) {
		FF_WebServer.wifiConnectCallback(data);
	}
	FF_WebServer.wifiDisconnectedSince = 0;
}

// On WiFi got IP
void AsyncFFWebServer::onWiFiConnectedGotIP(WiFiEventStationModeGotIP data) {
	DEBUG_VERBOSE_P("GotIP Address %s, gateway %S, DNS %s", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str());
	#if (CONNECTION_LED >= 0)
		digitalWrite(CONNECTION_LED, LOW); // Turn LED on
		DEBUG_VERBOSE_P("Led %d on", CONNECTION_LED);
	#endif
	FF_WebServer.wifiDisconnectedSince = 0;
	//force NTPsstart after got ip
	if (FF_WebServer._config.updateNTPTimeEvery > 0) { // Enable NTP sync
		FF_WebServer.updateTimeFromNTP = true;
	}
	FF_WebServer.connectionTimout = 0;
	FF_WebServer.wifiStatus = FS_STAT_CONNECTED;
	if (FF_WebServer.wifiGotIpCallback) {
		FF_WebServer.wifiGotIpCallback(data);
	}
}

// On WiFi disconnected
void AsyncFFWebServer::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
	#if (CONNECTION_LED >= 0) 
		digitalWrite(CONNECTION_LED, HIGH); // Turn LED off
	#endif
	if (FF_WebServer.wifiDisconnectedSince == 0) {
		FF_WebServer.wifiDisconnectedSince = millis();
	}
	DEBUG_ERROR_P("WiFi disconnected for %d seconds", (int)((millis() - FF_WebServer.wifiDisconnectedSince) / 1000));
	FF_WebServer.lastDisconnect = millis();
	if (FF_WebServer.wifiDisconnectCallback) {
		FF_WebServer.wifiDisconnectCallback(data);
	}
}

// Return file list
void AsyncFFWebServer::handleFileList(AsyncWebServerRequest *request) {
	if (!request->hasArg("dir")) {
	    request->send(500, "text/plain", "BAD ARGS");
	    return;
	}

	String path = request->arg("dir");
	DEBUG_VERBOSE_P("handleFileList: %s", path.c_str());
	Dir dir = _fs->openDir(path);
	path = String();

	String output = PSTR("[");
	while (dir.next()) {
		File entry = dir.openFile("r");
		if (output != "[") {
			output += PSTR(",");
		}
		bool isDir = false;
		output += PSTR("{\"type\":\"");
		if (isDir) {
			output += PSTR("dir");
		} else { 
			output += PSTR("file");
		}
		output += PSTR("\",\"name\":\"")
			+ String(entry.name())
			+ PSTR("\"}");
		entry.close();
	}

	output += PSTR("]");
	DEBUG_VERBOSE_P("%s", output.c_str());
	request->send(200, "text/json", output);
}

// Return data type giving file type
String AsyncFFWebServer::getContentType(String filename, AsyncWebServerRequest *request) {
	if (request->hasArg("download")) return "application/octet-stream";
	else if (filename.endsWith(".htm")) return "text/html";
	else if (filename.endsWith(".html")) return "text/html";
	else if (filename.endsWith(".css")) return "text/css";
	else if (filename.endsWith(".js")) return "application/javascript";
	else if (filename.endsWith(".json")) return "application/json";
	else if (filename.endsWith(".png")) return "image/png";
	else if (filename.endsWith(".gif")) return "image/gif";
	else if (filename.endsWith(".jpg")) return "image/jpeg";
	else if (filename.endsWith(".ico")) return "image/x-icon";
	else if (filename.endsWith(".xml")) return "text/xml";
	else if (filename.endsWith(".pdf")) return "application/x-pdf";
	else if (filename.endsWith(".zip")) return "application/x-zip";
	else if (filename.endsWith(".gz")) return "application/x-gzip";
	return "text/plain";
}

// Return content of a file
bool AsyncFFWebServer::handleFileRead(String path, AsyncWebServerRequest *request) {
	DEBUG_VERBOSE_P("handleFileRead: %s", path.c_str());
	#if (CONNECTION_LED >= 0) 
		// CANNOT RUN DELAY() INSIDE CALLBACK
		flashLED(CONNECTION_LED, 1, 25); // Show activity on LED
	#endif
	if (path.endsWith("/"))
		path += PSTR("index.htm");
	String contentType = getContentType(path, request);
	String pathWithGz = path + PSTR(".gz");
	if (_fs->exists(pathWithGz) || _fs->exists(path)) {
		if (_fs->exists(pathWithGz)) {
			path += PSTR(".gz");
		}
		DEBUG_VERBOSE_P("Content type: %s", contentType.c_str());
		AsyncWebServerResponse *response = request->beginResponse(*_fs, path, contentType);
		if (path.endsWith(PSTR(".gz")))
			response->addHeader("Content-Encoding", "gzip");
		DEBUG_VERBOSE_P("File %s exist", path.c_str());
		request->send(response);
		DEBUG_VERBOSE_P("File %s Sent", path.c_str());

		return true;
	} else {
		DEBUG_ERROR_P("Cannot find %s", path.c_str());
	}
	return false;
}

// Create a file on file system
void AsyncFFWebServer::handleFileCreate(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();
	if (request->args() == 0)
		return request->send(500, "text/plain", "BAD ARGS");
	String path = request->arg(0U);
	DEBUG_VERBOSE_P("handleFileCreate: %s", path.c_str());
	if (path == "/")
		return request->send(500, "text/plain", "BAD PATH");
	if (_fs->exists(path))
		return request->send(500, "text/plain", "FILE EXISTS");
	File file = _fs->open(path, "w");
	if (file) {
		file.close();
	} else {
		return request->send(500, "text/plain", "CREATE FAILED");
	}
	request->send(200, "text/plain", "");
	path = String();
}

// Delete a file on file system
void AsyncFFWebServer::handleFileDelete(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();
	if (request->args() == 0) return request->send(500, "text/plain", "BAD ARGS");
	String path = request->arg(0U);
	if (path.startsWith("//"))
		path = path.substring(1);
	DEBUG_ERROR_P("handleFileDelete: %s", path.c_str());
	if (path == "/")
		return request->send(500, "text/plain", "BAD PATH");
	if (!_fs->exists(path))
		return error404(request);
	_fs->remove(path);
	request->send(200, "text/plain", "");
	path = String(); // Remove? Useless statement?
}

// Download a file
void AsyncFFWebServer::handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	static File fsUploadFile;
	static size_t fileSize = 0;

	if (!index) { // Start
		DEBUG_VERBOSE_P("handleFileUpload Name: %s", filename.c_str());
		if (!filename.startsWith("/")) filename = PSTR("/") + filename;
		fsUploadFile = _fs->open(filename, "w");
		DEBUG_VERBOSE_P("First upload part");

	}
	// Continue
	if (fsUploadFile) {
		DEBUG_VERBOSE_P("Continue upload part. Size = %u", len);
		if (fsUploadFile.write(data, len) != len) {
			DEBUG_ERROR_P("Write error during upload", NULL);
		} else {
			fileSize += len;
		}
	}
	if (final) { // End
		if (fsUploadFile) {
			fsUploadFile.close();
		}
		DEBUG_VERBOSE_P("handleFileUpload Size: %u", fileSize);
		fileSize = 0;
	}
}

// Send general configuration data
void AsyncFFWebServer::send_general_configuration_values_html(AsyncWebServerRequest *request) {
	String values;
	values = PSTR("devicename|") + (String)_config.deviceName + PSTR("|input\n")
		+ PSTR("userversion|") + serverVersion + PSTR("|div\n");
	request->send(200, "text/plain", values);
}

// Send network configuration data
void AsyncFFWebServer::send_network_configuration_values_html(AsyncWebServerRequest *request) {
	String values;

	values = PSTR("ssid|") + (String)_config.ssid + PSTR("|input\n")
		+ PSTR("password|") + (String)_config.password + PSTR("|input\n")
		+ PSTR("ip_0|") + (String)_config.ip[0] + PSTR("|input\n")
		+ PSTR("ip_1|") + (String)_config.ip[1] + PSTR("|input\n")
		+ PSTR("ip_2|") + (String)_config.ip[2] + PSTR("|input\n")
		+ PSTR("ip_3|") + (String)_config.ip[3] + PSTR("|input\n")
		+ PSTR("nm_0|") + (String)_config.netmask[0] + PSTR("|input\n")
		+ PSTR("nm_1|") + (String)_config.netmask[1] + PSTR("|input\n")
		+ PSTR("nm_2|") + (String)_config.netmask[2] + PSTR("|input\n")
		+ PSTR("nm_3|") + (String)_config.netmask[3] + PSTR("|input\n")
		+ PSTR("gw_0|") + (String)_config.gateway[0] + PSTR("|input\n")
		+ PSTR("gw_1|") + (String)_config.gateway[1] + PSTR("|input\n")
		+ PSTR("gw_2|") + (String)_config.gateway[2] + PSTR("|input\n")
		+ PSTR("gw_3|") + (String)_config.gateway[3] + PSTR("|input\n")
		+ PSTR("dns_0|") + (String)_config.dns[0] + PSTR("|input\n")
		+ PSTR("dns_1|") + (String)_config.dns[1] + PSTR("|input\n")
		+ PSTR("dns_2|") + (String)_config.dns[2] + PSTR("|input\n")
		+ PSTR("dns_3|") + (String)_config.dns[3] + PSTR("|input\n")
		+ PSTR("dhcp|") + (String)(_config.dhcp ? PSTR("checked") : "") + PSTR("|chk\n");

	request->send(200, "text/plain", values);
	values = "";
}

// Send connection state
void AsyncFFWebServer::send_connection_state_values_html(AsyncWebServerRequest *request) {
	String state = PSTR("N/A");
	String Networks = "";
	if (WiFi.status() == 0) state = PSTR("Idle");
	else if (WiFi.status() == 1) state = PSTR("NO SSID AVAILBLE");
	else if (WiFi.status() == 2) state = PSTR("SCAN COMPLETED");
	else if (WiFi.status() == 3) state = PSTR("CONNECTED");
	else if (WiFi.status() == 4) state = PSTR("CONNECT FAILED");
	else if (WiFi.status() == 5) state = PSTR("CONNECTION LOST");
	else if (WiFi.status() == 6) state = PSTR("DISCONNECTED");

	WiFi.scanNetworks(true);

	String values = PSTR("connectionstate|") + state + PSTR("|div\n");
	//values += "networks|Scanning networks ...|div\n";
	request->send(200, "text/plain", values);
	state = "";
	values = "";
	Networks = "";
}

// Send information data
void AsyncFFWebServer::send_information_values_html(AsyncWebServerRequest *request) {
	String values = 
		  PSTR("x_ssid|") + (String)WiFi.SSID() + PSTR("|div\n")
		+ PSTR("x_ip|") + (String)WiFi.localIP()[0] + "." + (String)WiFi.localIP()[1] + "." + (String)WiFi.localIP()[2] + "." + (String)WiFi.localIP()[3] + PSTR("|div\n")
		+ PSTR("x_gateway|") + (String)WiFi.gatewayIP()[0] + "." + (String)WiFi.gatewayIP()[1] + "." + (String)WiFi.gatewayIP()[2] + "." + (String)WiFi.gatewayIP()[3] + PSTR("|div\n")
		+ PSTR("x_netmask|") + (String)WiFi.subnetMask()[0] + "." + (String)WiFi.subnetMask()[1] + "." + (String)WiFi.subnetMask()[2] + "." + (String)WiFi.subnetMask()[3] + PSTR("|div\n")
		+ PSTR("x_mac|") + getMacAddress() + PSTR("|div\n")
		+ PSTR("x_dns|") + (String)WiFi.dnsIP()[0] + "." + (String)WiFi.dnsIP()[1] + "." + (String)WiFi.dnsIP()[2] + "." + (String)WiFi.dnsIP()[3] + PSTR("|div\n")
		+ PSTR("x_ntp_sync|") + NTP.getTimeDateString(NTP.getLastNTPSync()) + PSTR("|div\n")
		+ PSTR("x_ntp_time|") + NTP.getTimeStr() + PSTR("|div\n")
		+ PSTR("x_ntp_date|") + NTP.getDateStr() + PSTR("|div\n")
		+ PSTR("x_uptime|") + NTP.getUptimeString() + PSTR("|div\n")
		+ PSTR("x_last_boot|") + NTP.getTimeDateString(NTP.getLastBootTime()) + PSTR("|div\n");

	request->send(200, "text/plain", values);
	//delete &values;
	values = "";
}

// Get ESP8266 MAC address
String AsyncFFWebServer::getMacAddress() {
	uint8_t mac[6];
	char macStr[18] = { 0 };
	WiFi.macAddress(mac);
	sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return String(macStr);
}

// Send NTP configuration data
void AsyncFFWebServer::send_NTP_configuration_values_html(AsyncWebServerRequest *request) {
	String values = 
		  PSTR("ntpserver|") + (String)_config.ntpServerName + PSTR("|input\n")
		+ PSTR("update|") + (String)_config.updateNTPTimeEvery + PSTR("|input\n")
		+ PSTR("tz|") + (String)_config.timezone + PSTR("|input\n")
		+ PSTR("dst|") + (String)(_config.daylight ? PSTR("checked") : "") + PSTR("|chk\n");
	request->send(200, "text/plain", values);
}

// Convert a single hex digit character to its integer value (from https://code.google.com/p/avr-netino/)
unsigned char AsyncFFWebServer::h2int(char c) {
	if (c >= '0' && c <= '9') {
		return((unsigned char)c - '0');
	}
	if (c >= 'a' && c <= 'f') {
		return((unsigned char)c - 'a' + 10);
	}
	if (c >= 'A' && c <= 'F') {
		return((unsigned char)c - 'A' + 10);
	}
	return(0);
}

// Decode an URL (replace + by space and %xx by its ascii hexa code
String AsyncFFWebServer::urldecode(String input) {		// (based on https://code.google.com/p/avr-netino/) {
	char c;
	String ret = "";

	for (byte t = 0; t < input.length(); t++) {
		c = input[t];
		if (c == '+') c = ' ';
		if (c == '%') {
			t++;
			c = input[t];
			t++;
			c = (h2int(c) << 4) | h2int(input[t]);
		}
		ret.concat(c);
	}
	return ret;

}

// Check the Values is between 0-255
boolean AsyncFFWebServer::checkRange(String Value) {
	if (Value.toInt() < 0 || Value.toInt() > 255) {
		return false;
	} else {
		return true;
	}
}

//	Send network configuration data
void AsyncFFWebServer::send_network_configuration_html(AsyncWebServerRequest *request) {
	if (request->args() > 0) { // Save Settings
		//String temp = "";
		bool oldDHCP = _config.dhcp; // Save status to avoid general.html cleares it
		_config.dhcp = false;
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUG_VERBOSE_P("Arg %d: %s", i, request->arg(i).c_str());
			if (request->argName(i) == "devicename") {
				_config.deviceName = urldecode(request->arg(i));
				_config.dhcp = oldDHCP;
				continue;
			}
			if (request->argName(i) == "ssid") { _config.ssid = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "password") { _config.password = urldecode(request->arg(i)); continue; }
			if (request->argName(i) == "ip_0") { if (checkRange(request->arg(i))) 	_config.ip[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_1") { if (checkRange(request->arg(i))) 	_config.ip[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_2") { if (checkRange(request->arg(i))) 	_config.ip[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_3") { if (checkRange(request->arg(i))) 	_config.ip[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_0") { if (checkRange(request->arg(i))) 	_config.netmask[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_1") { if (checkRange(request->arg(i))) 	_config.netmask[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_2") { if (checkRange(request->arg(i))) 	_config.netmask[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_3") { if (checkRange(request->arg(i))) 	_config.netmask[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_0") { if (checkRange(request->arg(i))) 	_config.gateway[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_1") { if (checkRange(request->arg(i))) 	_config.gateway[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_2") { if (checkRange(request->arg(i))) 	_config.gateway[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_3") { if (checkRange(request->arg(i))) 	_config.gateway[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_0") { if (checkRange(request->arg(i))) 	_config.dns[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_1") { if (checkRange(request->arg(i))) 	_config.dns[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_2") { if (checkRange(request->arg(i))) 	_config.dns[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_3") { if (checkRange(request->arg(i))) 	_config.dns[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dhcp") { _config.dhcp = true; continue; }
		}
		request->send_P(200, "text/html", Page_WaitAndReload);
		save_config();
		//yield();
		delay(1000);
		_fs->end();
		ESP.restart();
		//ConfigureWifi();
		//AdminTimeOutCounter = 0;
	} else {
		DEBUG_VERBOSE_P("URL %s", request->url().c_str());
		handleFileRead(request->url(), request);
	}
}

//	Send general configuration data
void AsyncFFWebServer::send_general_configuration_html(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();

	if (request->args() > 0) {// Save Settings
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUG_VERBOSE_P("Arg %d: %s", i, request->arg(i).c_str());
			if (request->argName(i) == "devicename") {
				_config.deviceName = urldecode(request->arg(i));
				continue;
			}
		}
		request->send_P(200, "text/html", Page_Restart);
		save_config();
		_fs->end();
		ESP.restart();
	} else {
		handleFileRead(request->url(), request);
	}
}

// Send NTP configuration data
void AsyncFFWebServer::send_NTP_configuration_html(AsyncWebServerRequest *request) {

	if (!checkAuth(request))
		return request->requestAuthentication();

	if (request->args() > 0) {// Save Settings
		_config.daylight = false;
		//String temp = "";
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "ntpserver") {
				_config.ntpServerName = urldecode(request->arg(i));
				NTP.setNtpServerName(_config.ntpServerName);
				continue;
			}
			if (request->argName(i) == "update") {
				_config.updateNTPTimeEvery = request->arg(i).toInt();
				NTP.setInterval(_config.updateNTPTimeEvery * 60);
				continue;
			}
			if (request->argName(i) == "tz") {
				_config.timezone = request->arg(i).toInt();
				NTP.setTimeZone(_config.timezone / 10);
				continue;
			}
			if (request->argName(i) == "dst") {
				_config.daylight = true;
				DEBUG_VERBOSE_P("Daylight Saving: %d", _config.daylight);
				continue;
			}
		}

		NTP.setDayLight(_config.daylight);
		save_config();
		//firstStart = true;

		setTime(NTP.getTime()); //set time
	}
	handleFileRead("/ntp.html", request);
	//server.send(200, "text/html", PAGE_NTPConfiguration);
}

// Restart ESP
void AsyncFFWebServer::restart_esp(AsyncWebServerRequest *request) {
	request->send_P(200, "text/html", Page_Restart);
	_fs->end(); // LitleFS.end();
	delay(1000);
	ESP.restart();
}

// Send authentication data
void AsyncFFWebServer::send_wwwauth_configuration_values_html(AsyncWebServerRequest *request) {
	String values = 
		  PSTR("wwwauth|") + (String)(_httpAuth.auth ? PSTR("checked") : "") + PSTR("|chk\n")
		+ PSTR("wwwuser|") + (String)_httpAuth.wwwUsername + PSTR("|input\n")
		+ PSTR("wwwpass|") + (String)_httpAuth.wwwPassword + PSTR("|input\n");

	request->send(200, "text/plain", values);
}

// Set authentication data
void AsyncFFWebServer::send_wwwauth_configuration_html(AsyncWebServerRequest *request) {
	DEBUG_VERBOSE_P("%s %d", __FUNCTION__, request->args());
	if (request->args() > 0) { // Save Settings
		_httpAuth.auth = false;
		//String temp = "";
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "wwwuser") {
				_httpAuth.wwwUsername = urldecode(request->arg(i));
				DEBUG_VERBOSE_P("User: %s", _httpAuth.wwwUsername.c_str());
				continue;
			}
			if (request->argName(i) == "wwwpass") {
				_httpAuth.wwwPassword = urldecode(request->arg(i));
				DEBUG_VERBOSE_P("Pass: %s", _httpAuth.wwwPassword.c_str());
				continue;
			}
			if (request->argName(i) == "wwwauth") {
				_httpAuth.auth = true;
				DEBUG_VERBOSE_P("HTTP Auth enabled");
				continue;
			}
		}

		saveHTTPAuth();
	}
	handleFileRead("/system.html", request);
}

// Save authentication data
bool AsyncFFWebServer::saveHTTPAuth() {
	//flag_config = false;
	DEBUG_VERBOSE_P("Save secret");
	DynamicJsonDocument jsonDoc(256);

	jsonDoc["auth"] = _httpAuth.auth;
	jsonDoc["user"] = _httpAuth.wwwUsername;
	jsonDoc["pass"] = _httpAuth.wwwPassword;

	//TODO add AP data to html
	File configFile = _fs->open(SECRET_FILE, "w");
	if (!configFile) {
		DEBUG_ERROR_P("Failed to open %s for writing", SECRET_FILE);
		configFile.close();
		return false;
	}

	#ifdef DEBUG_FF_WEBSERVER
		String temp;
		serializeJsonPretty(jsonDoc, temp);
		DEBUG_VERBOSE_P("Secret %s", temp.c_str());
	#endif // DEBUG_FF_WEBSERVER
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}

// Check for firmware update capacity
void AsyncFFWebServer::send_update_firmware_values_html(AsyncWebServerRequest *request) {
	uint32_t maxSketchSpace = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
	bool updateOK = maxSketchSpace < ESP.getFreeSketchSpace();
	DEBUG_VERBOSE_P("OTA MaxSketchSpace: %d, free %d", maxSketchSpace, ESP.getFreeSketchSpace());
	String values = PSTR("remupd|") + (String)((updateOK) ? PSTR("OK") : PSTR("ERROR")) + PSTR("|div\n");

	if (Update.hasError()) {
		StreamString result;
		Update.printError(result);
		result.trim();
		DEBUG_VERBOSE_P("OTA result :%s", result.c_str());
		values += PSTR("remupdResult|") + String(result) + PSTR("|div\n");
	} else {
		values += PSTR("remupdResult||div\n");
	}

	request->send(200, "text/plain", values);
}

// Set firmware MD5 value
void AsyncFFWebServer::setUpdateMD5(AsyncWebServerRequest *request) {
	_browserMD5 = "";
	DEBUG_VERBOSE_P("Arg number: %d", request->args());
	if (request->args() > 0) {								// Read hash
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUG_VERBOSE_P("Arg %s: %s", request->argName(i).c_str(), request->arg(i).c_str());
			if (request->argName(i) == "md5") {
				_browserMD5 = urldecode(request->arg(i));
				Update.setMD5(_browserMD5.c_str());
				continue;
			}if (request->argName(i) == "size") {
				_updateSize = request->arg(i).toInt();
				DEBUG_VERBOSE_P("Update size: %l", _updateSize);
				continue;
			}
		}
		request->send(200, "text/html", "OK --> MD5: " + _browserMD5);
	}

}

// Update firmware
void AsyncFFWebServer::updateFirmware(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	// handler for the file upload, get's the sketch bytes, and writes
	// them through the Update object
	static long totalSize = 0;
	if (!index) { //UPLOAD_FILE_START
		_fs->end();
		Update.runAsync(true);
		DEBUG_VERBOSE_P("Update start: %s", filename.c_str());
		uint32_t maxSketchSpace = ESP.getSketchSize();
		DEBUG_VERBOSE_P("Max free sketch space: %u", maxSketchSpace);
		DEBUG_VERBOSE_P("New scketch size: %u", _updateSize);
		if (_browserMD5 != NULL && _browserMD5 != "") {
			Update.setMD5(_browserMD5.c_str());
			DEBUG_VERBOSE_P("Hash from client: %s", _browserMD5.c_str());
		}
		if (!Update.begin(_updateSize)) {//start with max available size
			StreamString result;
			Update.printError(result);
			DEBUG_ERROR_P("Update error %s", result.c_str());
		}
	}

	// Get upload file, continue if not start
	totalSize += len;
	size_t written = Update.write(data, len);
	if (written != len) {
		DEBUG_VERBOSE_P("len = %d, written = %l, totalSize = %l", len, written, totalSize);
	}
	if (final) {											// UPLOAD_FILE_END
		String updateHash;
		DEBUG_VERBOSE_P("Applying update...");
		if (Update.end(true)) { //true to set the size to the current progress
			updateHash = Update.md5String();
			DEBUG_VERBOSE_P("Upload finished. Calculated MD5: %s", updateHash.c_str());
			DEBUG_VERBOSE_P("Update Success: %u - Rebooting...", request->contentLength());
		} else {
			updateHash = Update.md5String();
			DEBUG_ERROR_P("Upload failed. Calculated MD5: %s", updateHash.c_str());
			StreamString result;
			Update.printError(result);
			DEBUG_ERROR_P("Update error %s", result.c_str());
		}
	}
}

// Send user configuration data
void AsyncFFWebServer::handle_rest_config(AsyncWebServerRequest *request) {

	String values = "";
	// handle generic rest call
	//dirty processing as no split function
	unsigned int p = 0; //string ptr
	int t = 0; // temp string pointer
	String URL = request->url().substring(9);
	String name = "";
	String data = "";
	String type = "";

	while (p < URL.length()) {
		t = URL.indexOf("/", p);
		if (t >= 0) {
			name = URL.substring(p, t);
			p = t + 1;
		} else {
			name = URL.substring(p);
			p = URL.length();
		}
		if (name.substring(1, 2) == "_") {
			type = name.substring(0, 2);
			if (type == "i_") {
				type = PSTR("input");
			} else if (type == "d_") {
				type = PSTR("div");
			} else if (type == "c_") {
				type = PSTR("chk");
			}
			name = name.substring(2);
		} else {
			type = PSTR("input");
		}

		load_user_config(name, data);
		values += name + "|" + data + "|" + type + "\n";
	}
	request->send(200, "text/plain", values);
	values = "";
}

// Save user configuration data
void AsyncFFWebServer::post_rest_config(AsyncWebServerRequest *request) {

	String target = PSTR("/");

	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUG_VERBOSE_P("Arg %d: %s = %s", i, request->arg(i).c_str(), urldecode(request->arg(i)).c_str());
		//check for post redirect
		if (request->argName(i) == "afterpost") {
			target = urldecode(request->arg(i));
		} else { //or savedata in Json File
			save_user_config(request->argName(i), request->arg(i));
		}
	}

	// Reload config
	loadConfig();
	loadUserConfig();
	request->redirect(target);
}

// Initialize server served URLs
void AsyncFFWebServer::serverInit() {
	//SERVER INIT
	//list directory
	on("/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handleFileList(request);
	});
	//load editor
	on("/edit", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (!this->handleFileRead("/edit.html", request))
			error404(request);
	});
	//create file
	on("/edit", HTTP_PUT, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handleFileCreate(request);
	});	//delete file
	on("/edit", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handleFileDelete(request);
	});
	//first callback is called after the request has ended with all parsed arguments
	//second callback handles file uploads at that location
	on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", ""); }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		this->handleFileUpload(request, filename, index, data, len, final);
	});
	on("/admin/generalvalues", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_general_configuration_values_html(request);
	});
	on("/admin/values", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_network_configuration_values_html(request);
	});
	on("/admin/connectionstate", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_connection_state_values_html(request);
	});
	on("/admin/infovalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_information_values_html(request);
	});
	on("/admin/ntpvalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_NTP_configuration_values_html(request);
	});
	on("/config.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_network_configuration_html(request);
	});
	on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
		String json = PSTR("[");
		int n = WiFi.scanComplete();
		if (n == WIFI_SCAN_FAILED) {
			WiFi.scanNetworks(true);
		} else if (n) {
			for (int i = 0; i < n; ++i) {
				if (i) json += PSTR(",");
				json += PSTR("{\"rssi\":") + String(WiFi.RSSI(i))
					+ PSTR(",\"ssid\":\"") + WiFi.SSID(i) + PSTR("\"")
					+ PSTR(",\"bssid\":\"") + WiFi.BSSIDstr(i) + PSTR("\"")
					+ PSTR(",\"channel\":") + String(WiFi.channel(i))
					+ PSTR(",\"secure\":") + String(WiFi.encryptionType(i));
					+ PSTR(",\"hidden\":") + String(WiFi.isHidden(i) ? PSTR("true") : PSTR("false"))
					+ PSTR("}");
			}
			WiFi.scanDelete();
			if (WiFi.scanComplete() == WIFI_SCAN_FAILED) {
				WiFi.scanNetworks(true);
			}
		}
		json += PSTR("]");
		request->send(200, "text/json", json);
		json = "";
	});
	on("/general.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_general_configuration_html(request);
	});
	on("/ntp.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_NTP_configuration_html(request);
	});
	on("/admin/restart", [this](AsyncWebServerRequest *request) {
		DEBUG_VERBOSE(request->url().c_str());
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->restart_esp(request);
	});
	on("/admin/wwwauth", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_wwwauth_configuration_values_html(request);
	});
	on("/admin", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (!this->handleFileRead("/admin.html", request))
			error404(request);
	});
	on("/system.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_wwwauth_configuration_html(request);
	});
	on("/update/updatepossible", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_update_firmware_values_html(request);
	});
	on("/setmd5", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		//DEBUG_VERBOSE_P("md5?");
		this->setUpdateMD5(request);
	});
	on("/update", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (!this->handleFileRead("/update.html", request))
			error404(request);
	});
	on("/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting...");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
		this->_fs->end();
		ESP.restart();
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		this->updateFirmware(request, filename, index, data, len, final);
	});

	on("/rconfig", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handle_rest_config(request);
	});

	on("/pconfig", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->post_rest_config(request);
	});

	on("/json", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		char tempBuffer[250];
		tempBuffer[0] = 0;
		if (this->traceFlag) trace_info_P("Request: %s", request->url().c_str());
		if (this->jsonCommandCallback) {
			if (this->jsonCommandCallback(request)) {
				return;
			}
		}
		if (this->debugFlag) trace_debug_P("Unknown JSON request: %s", request->url().c_str());
		snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("Can't understand: %s\n"), request->url().c_str());
		request->send(400, "text/plain", tempBuffer);
	});

	on("/rest", [this](AsyncWebServerRequest *request) {
		char tempBuffer[250];
		tempBuffer[0] = 0;

		if (this->traceFlag) trace_info_P("Request: %s", request->url().c_str());
		if (this->restCommandCallback) {
			if (this->restCommandCallback(request)) {
				return;
			}
		}
		if (this->debugFlag) trace_debug_P("Unknown REST request: %s", request->url().c_str());
		snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("Can't understand: %s\n"), request->url().c_str());
		request->send(400, "text/plain", tempBuffer);
	});

	on("/post", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		char tempBuffer[250];
		tempBuffer[0] = 0;

		if (this->traceFlag) trace_info_P("Request: %s", request->url().c_str());
		if (this->postCommandCallback) {
			if (this->postCommandCallback(request)) {
				return;
			}
		}
		if (this->debugFlag) trace_debug_P("Unknown POST request: %s", request->url().c_str());
		snprintf_P(tempBuffer, sizeof(tempBuffer), PSTR("Can't understand: %s\n"), request->url().c_str());
		request->send(400, "text/plain", tempBuffer);
	});

	//called when the url is not defined here
	//use it to load content from LitleFS
	onNotFound([this](AsyncWebServerRequest *request) {
	if (!this->checkAuth(request)) {
		DEBUG_VERBOSE_P("Request authentication");
		return request->requestAuthentication();
	}
	AsyncWebServerResponse *response = request->beginResponse(200);
	response->addHeader("Connection", "close");
	response->addHeader("Access-Control-Allow-Origin", "*");
	if (!this->handleFileRead(request->url(), request)) {
		DEBUG_ERROR_P("Not found: %s", request->url().c_str());
		error404(request);
	}
	delete response; // Free up memory!
	});

	_evs.onConnect([](AsyncEventSourceClient* client) {
		DEBUG_VERBOSE_P("Event source client connected from %s", client->client()->remoteIP().toString().c_str());
	});
	addHandler(&_evs);

	#ifdef HIDE_SECRET
		on(SECRET_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
			if (!this->checkAuth(request))
				return request->requestAuthentication();
			AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
			response->addHeader("Connection", "close");
			response->addHeader("Access-Control-Allow-Origin", "*");
			request->send(response);
		});
	#endif // HIDE_SECRET

	#ifdef HIDE_CONFIG
		on(CONFIG_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
			if (!this->checkAuth(request))
				return request->requestAuthentication();
			AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
			response->addHeader("Connection", "close");
			response->addHeader("Access-Control-Allow-Origin", "*");
			request->send(response);
		});

		on(USER_CONFIG_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
			if (!this->checkAuth(request))
				return request->requestAuthentication();
			AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
			response->addHeader("Connection", "close");
			response->addHeader("Access-Control-Allow-Origin", "*");
			request->send(response);
		});

	#endif // HIDE_CONFIG

	//get heap status, analog input value and all GPIO statuses in one json call
	on("/all", HTTP_GET, [](AsyncWebServerRequest *request) {
		String json = PSTR("{\"heap\":") + String(ESP.getFreeHeap())
			+ PSTR(", \"analog\":") + String(analogRead(A0))
			+ PSTR(", \"gpio\":") + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)))
			+ PSTR("}");
		request->send(200, "text/json", json);
		json = String();
	});
	DEBUG_VERBOSE_P("HTTP server started");
}

// Check for authentication need
bool AsyncFFWebServer::checkAuth(AsyncWebServerRequest *request) {
	if (!_httpAuth.auth) {
		return true;
	} else {
		return request->authenticate(_httpAuth.wwwUsername.c_str(), _httpAuth.wwwPassword.c_str());
	}
}

/*!

	Return this host name

	\param	None
	\return	This host name

*/
const char* AsyncFFWebServer::getHostName(void) {
	return _config.deviceName.c_str();
}

/*!

	Set configuration change callback

	\param[in]	Address of user routine to be called when a configuration change occurs
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setConfigChangedCallback(CONFIG_CHANGED_CALLBACK_SIGNATURE) {
	this->configChangedCallback = configChangedCallback;
	if (serverStarted) {
		this->loadUserConfig();
	}
	return *this;
}

/*!

	Set Serial command callback

	\param[in]	Address of user routine to be called when a command has been received on Serial
	\return	A pointer to this class instance

*/
#ifndef NO_SERIAL_COMMAND_CALLBACK
	AsyncFFWebServer& AsyncFFWebServer::setSerialCommandCallback(SERIAL_COMMAND_CALLBACK_SIGNATURE) {
		this->serialCommandCallback = serialCommandCallback;
		return *this;
	}
#endif

/*!

	Set help command callback

	\param[in]	Address of user routine to be called when an help text should be displayed
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setHelpMessageCallback(HELP_MESSAGE_CALLBACK_SIGNATURE) {
	this->helpMessageCallback = helpMessageCallback;
	return *this;
}

/*!

	Set debug command callback

	\param[in]	Address of user routine to be called when an unknown debug command is received
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setDebugCommandCallback(DEBUG_COMMAND_CALLBACK_SIGNATURE) {
	this->debugCommandCallback = debugCommandCallback;
	return *this;
}

/*!

	Set REST command callback

	\param[in]	Address of user routine to be called when a REST (/rest) command is received
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setRestCommandCallback(REST_COMMAND_CALLBACK_SIGNATURE) {
	this->restCommandCallback = restCommandCallback;
	return *this;
}

/*!

	Set JSON command callback

	\param[in]	Address of user routine to be called when a JSON (/json) command is received
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setJsonCommandCallback(JSON_COMMAND_CALLBACK_SIGNATURE) {
	this->jsonCommandCallback = jsonCommandCallback;
	return *this;
}

/*!

	Set POST command callback

	\param[in]	Address of user routine to be called when a POST request command is received
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setPostCommandCallback(POST_COMMAND_CALLBACK_SIGNATURE) {
	this->postCommandCallback = postCommandCallback;
	return *this;
}

/*!

	Set error 404 callback

	User can add new URL/request intercepting commands that FF_WebServer can't serve

	\param[in]	Address of user routine to be called when a 404 (file not found) error occur.
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setError404Callback(ERROR404_CALLBACK_SIGNATURE) {
	this->error404Callback = error404Callback;
	return *this;
}

/*!

	Set WiFi connected callback

	\param[in]	Address of user routine to be called when WiFi is connected
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setWifiConnectCallback(WIFI_CONNECT_CALLBACK_SIGNATURE) {
	this->wifiConnectCallback = wifiConnectCallback;
	return *this;
}

/*!

	Set WiFi disconnected callback

	\param[in]	Address of user routine to be called when WiFi is disconnected
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setWifiDisconnectCallback(WIFI_DISCONNECT_CALLBACK_SIGNATURE) {
	this->wifiDisconnectCallback = wifiDisconnectCallback;
	return *this;
}

/*!

	Set WiFi got IP callback

	\param[in]	Address of user routine to be called when WiFi receives anIp address
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setWifiGotIpCallback(WIFI_GOT_IP_CALLBACK_SIGNATURE) {
	this->wifiGotIpCallback = wifiGotIpCallback;
	return *this;
}

/*!

	Set MQTT connected callback

	\param	None
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setMqttConnectCallback(MQTT_CONNECT_CALLBACK_SIGNATURE) {
	this->mqttConnectCallback = mqttConnectCallback;
	return *this;
}

/*!

	Set MQTT disconnected callback

	\param[in]	Address of user routine to be called when MQTT is disconnected
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setMqttDisconnectCallback(MQTT_DISCONNECT_CALLBACK_SIGNATURE) {
	this->mqttDisconnectCallback = mqttDisconnectCallback;
	return *this;
}

/*!

	Set MQTT message callback

	\param[in]	Address of user routine to be called when an MQTT subscribed message is received
	\return	A pointer to this class instance

*/
AsyncFFWebServer& AsyncFFWebServer::setMqttMessageCallback(MQTT_MESSAGE_CALLBACK_SIGNATURE) {
	this->mqttMessageCallback = mqttMessageCallback;
	return *this;
}

/*!

	Get FF_WebServer version

	\param	None
	\return	FF_WebServer version

*/
const char* AsyncFFWebServer::getWebServerVersion(void) {
	return serverVersion.c_str();
}

/*!

	Get configured device name

	\param	None
	\return	Configured device name

*/
String AsyncFFWebServer::getDeviceName(void) {
	return _config.deviceName;
}

/*!

	Stop Wifi

	\param	None
	\return	None

*/
void AsyncFFWebServer::stopWifi(void) {
	WiFi.mode(WIFI_OFF);
}

/*!

	Start Wifi client

	\param	None
	\return	None

*/
void AsyncFFWebServer::startWifi(void) {
	this->configureWifi();
}

/*!

	Start Wifi Access Point

	\param	None
	\return	None

*/
void AsyncFFWebServer::startWifiAP(void) {
	this->configureWifiAP();
}

#ifdef REMOTE_DEBUG
	// Process commands from RemoteDebug
	void AsyncFFWebServer::executeDebugCommand(void) {
		String command = Debug.getLastCommand();
		FF_WebServer.executeCommand(command);
	}
#endif

// Process commands from RemoteDebug, Serial or MQTT
void AsyncFFWebServer::executeCommand(const String command) {
	if (command == "vars") {
		struct rst_info *rtc_info = system_get_rst_info();
		trace_info_P("version=%s/%s", FF_WebServer.userVersion.c_str(), FF_WebServer.serverVersion.c_str());
		trace_info_P("uptime=%s",NTP.getUptimeString().c_str());
		time_t bootTime = NTP.getLastBootTime();
			trace_info_P("boot=%s %s",NTP.getDateStr(bootTime).c_str(), NTP.getTimeStr(bootTime).c_str());
		trace_info_P("Reset reason: %x - %s", rtc_info->reason, ESP.getResetReason().c_str());
		// In case of software restart, print additional info
		if (rtc_info->reason == REASON_WDT_RST || rtc_info->reason == REASON_EXCEPTION_RST || rtc_info->reason == REASON_SOFT_WDT_RST) {
			// If crashed, print exception
			if (rtc_info->reason == REASON_EXCEPTION_RST) {
				trace_info_P("Fatal exception (%d)", rtc_info->exccause);
			}
			trace_info_P("epc1=0x%08x, epc2=0x%08x, epc3=0x%08x, excvaddr=0x%08x, depc=0x%08x", rtc_info->epc1, rtc_info->epc2, rtc_info->epc3, rtc_info->excvaddr, rtc_info->depc);
		}
		IPAddress ip = WiFi.localIP();
		trace_info_P("IP=%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
		byte mac[6];
		WiFi.macAddress(mac);
		trace_info_P("MAC=%2x:%2x:%2x:%2x:%2x:%2x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		trace_info_P("configMQTT_Host=%s", FF_WebServer.configMQTT_Host.c_str());
		trace_info_P("configMQTT_Port=%d", FF_WebServer.configMQTT_Port);
		trace_info_P("configMQTT_ClientID=%s", FF_WebServer.configMQTT_ClientID.c_str());
		trace_info_P("configMQTT_User=%s", FF_WebServer.configMQTT_User.c_str());
		trace_info_P("configMQTT_Pass=%s", FF_WebServer.configMQTT_Pass.c_str());
		trace_info_P("configMQTT_Topic=%s", FF_WebServer.configMQTT_Topic.c_str());
		trace_info_P("configMQTT_CommandTopic=%s", FF_WebServer.configMQTT_CommandTopic.c_str());
		trace_info_P("configMQTT_Interval=%d", FF_WebServer.configMQTT_Interval);
		trace_info_P("mqttConnected()=%d", FF_WebServer.mqttClient.connected());
		trace_info_P("mqttTest()=%d", FF_WebServer.mqttTest());
		#ifdef FF_TRACE_USE_SYSLOG
			trace_info_P("syslogServer=%s", FF_WebServer.syslogServer.c_str());
			trace_info_P("syslogPort=%d", FF_WebServer.syslogPort);
		#endif
	} else if (command == "debug") {
		FF_WebServer.debugFlag = !FF_WebServer.debugFlag;
		trace_info_P("Debug is now %d", FF_WebServer.debugFlag);
	} else if (command == "wdt") {
		FF_WebServer.watchdogFlag = !FF_WebServer.watchdogFlag;
		trace_info_P("Watchdog is now %d", FF_WebServer.watchdogFlag);
	} else if (command == "trace") {
		FF_WebServer.traceFlag = !FF_WebServer.traceFlag;
		trace_info_P("Trace is now %d", FF_WebServer.traceFlag);
	// The following commands are normally treated by remoteDebug
	//		but as we can also use this callback for MQTT and/or Serial debug,
	//		they should also be incorporated here.
	} else if (command == "h" || command == "?" || command == "help") {
		String helpText = "";
		if (FF_WebServer.helpMessageCallback) {
			helpText = (FF_WebServer.helpMessageCallback());
			trace_info_P("helpText=>%s<", helpText.c_str());
		}
		trace_info_P("\r\nhelp -> display this message\r\n"
					"m -> display memory available\r\n"
					"v -> set debug level to verbose\r\n"
					"d -> set debug level to debug\r\n"
					"i -> set debug level to info\r\n"
					"w -> set debug level to warning\r\n"
					"e -> set debug level to errors\r\n"
					"s -> set debug silence on/off\r\n"
					"cpu80  -> ESP8266 CPU at 80MHz\r\n"
					"cpu160 -> ESP8266 CPU at 160MHz\r\n"
					"reset -> reset the ESP8266\r\n%s%s",
						FF_WebServer.standardHelpCmd().c_str(),
						helpText.c_str());
	} else if (command == "m") {
		trace_info_P("Free Heap RAM: %d", ESP.getFreeHeap());
	#if defined(ESP8266)
		} else if (command == "cpu80") {
			system_update_cpu_freq(80);
			trace_info_P("CPU changed to %u MHz", ESP.getCpuFreqMHz());
		} else if (command == "cpu160") {
			system_update_cpu_freq(160);
			trace_info_P("CPU changed to %u MHz", ESP.getCpuFreqMHz());
	#endif
	} else if (command == "v") {
		trace_setLevel(FF_TRACE_LEVEL_VERBOSE);
		trace_info_P("Trace level set to Verbose", NULL);
	} else if (command == "d") {
		trace_setLevel(FF_TRACE_LEVEL_DEBUG);
		trace_info_P("Trace level set to Debug", NULL);
	} else if (command == "i") {
		trace_setLevel(FF_TRACE_LEVEL_INFO);
		trace_info_P("Trace level set to Info", NULL);
	} else if (command == "w") {
		trace_setLevel(FF_TRACE_LEVEL_WARN);
		trace_info_P("Trace level set to Warning", NULL);
	} else if (command == "e") {
		trace_setLevel(FF_TRACE_LEVEL_ERROR);
		trace_info_P("Trace level set to Error", NULL);
	} else if (command == "s") {
		if (trace_getLevel() != FF_TRACE_LEVEL_NONE) {
			trace_info_P("Silence on", NULL);
			FF_WebServer.lastTraceLevel = trace_getLevel();			// Save current trace level
			trace_setLevel(FF_TRACE_LEVEL_NONE);
		} else {
			trace_setLevel(FF_WebServer.lastTraceLevel);
			trace_info_P("Silence off, level restored to %d", trace_getLevel());
		}
	} else if (command == "reset") {
		trace_error_P("Reseting ESP ...", NULL);
		delay(1000);
		ESP.restart();
	// End of dupplicated debugRemote commands
	} else {
		if (FF_WebServer.debugCommandCallback) {
			FF_WebServer.debugCommandCallback(command);
		}
	}
}

#ifndef FF_DISABLE_DEFAULT_TRACE
	// Default trace callback
	trace_callback(AsyncFFWebServer::defaultTraceCallback) {
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
				#ifdef FF_TRACE_ENABLE_SERIAL_FLUSH
					Serial.flush();
				#endif
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
