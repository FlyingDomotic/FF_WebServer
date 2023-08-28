/*!

	FF_WebServer

	This library implements on ESP8266 a fully asynchronous Web server,
		with MQTT connection, Arduino and Web OTA,
		optional telnet or serial debug, 
		optional serial and/or syslog trace, 
		optional external hardware watchdog
		and optional Domoticz connectivity.

	It also has a local file system to host user and server files.

	This code is based on a highly modified version of https://github.com/FordPrfkt/FSBrowserNG,
		itself a fork of https://github.com/gmag11/FSBrowserNG, not anymore maintained.

	Written and maintained by Flying Domotic (https://github.com/FlyingDomotic/FF_WebServer)

*/

#ifndef _FFWEBSERVER_hpp
#define _FFWEBSERVER_hpp

#define FF_WEBSERVER_VERSION "2.9.5"						// FF WebServer version
#ifndef PLATFORMIO											// Execute this include only for Arduino IDE
	#include "FF_WebServerCfg.h"							// Include user #define
#endif

#ifdef ARDUINO
	#include <Arduino.h>
#endif

#include <Hash.h>
#include <FS.h>
#include <WiFiClient.h>
#include <Time.h>
#include <TimeLib.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESPAsyncTCP.h>									// https://github.com/me-no-dev/ESPAsyncTCP
#include <NtpClientLib.h>									// https://github.com/gmag11/NtpClient
#include <ESPAsyncWebServer.h>								// https://github.com/me-no-dev/ESPAsyncWebServer
#include <ArduinoJson.h>									// https://github.com/bblanchon/ArduinoJson
#include <AsyncMqttClient.h>								// https://github.com/marvinroger/async-mqtt-client
#include <FF_Trace.h>										// https://github.com/FlyingDomotic/FF_Trace

#ifdef FF_TRACE_USE_SYSLOG
	#include <Syslog.h>										// https://github.com/arcao/Syslog
#endif

#ifdef REMOTE_DEBUG
	// ---- Remote debug -----
	#include <RemoteDebug.h>									//https://github.com/JoaoLopesF/RemoteDebug (modified version w/o debug & websocket)
	// Disable serial debug
	#undef SERIAL_DEBUG
#endif

#ifdef SERIAL_DEBUG
	// ----- Serial debug -----
	#include <SerialDebug.h>									//https://github.com/JoaoLopesF/SerialDebug
#endif

#ifdef DEBUG_FF_WEBSERVER
	#define DEBUG_VERBOSE(...) trace_verbose(__VA_ARGS__)
	#define DEBUG_VERBOSE_P(...) trace_verbose(__VA_ARGS__)
#else
	#define DEBUG_VERBOSE(...)
	#define DEBUG_VERBOSE_P(...)
#endif
#define DEBUG_ERROR(...) trace_error(__VA_ARGS__)
#define DEBUG_ERROR_P(...) trace_error_P(__VA_ARGS__)

// #define HIDE_CONFIG
#define CONFIG_FILE "/config.json"
#define USER_CONFIG_FILE "/userconfig.json"
#define SECRET_FILE "/secret.json"

//	Define all callback signatures
#define CONFIG_CHANGED_CALLBACK_SIGNATURE std::function<void(void)> configChangedCallback
#define DEBUG_COMMAND_CALLBACK_SIGNATURE std::function<bool(const String command)> debugCommandCallback
#define HELP_MESSAGE_CALLBACK_SIGNATURE std::function<String(void)> helpMessageCallback
#ifndef NO_SERIAL_COMMAND_CALLBACK
	#define SERIAL_COMMAND_CALLBACK_SIGNATURE std::function<bool(const String command)> serialCommandCallback
#endif
#define REST_COMMAND_CALLBACK_SIGNATURE std::function<bool(AsyncWebServerRequest *request)> restCommandCallback
#define JSON_COMMAND_CALLBACK_SIGNATURE std::function<bool(AsyncWebServerRequest *request)> jsonCommandCallback
#define POST_COMMAND_CALLBACK_SIGNATURE std::function<bool(AsyncWebServerRequest *request)> postCommandCallback
#define ERROR404_CALLBACK_SIGNATURE std::function<bool(AsyncWebServerRequest *request)> error404Callback
#define WIFI_CONNECT_CALLBACK_SIGNATURE std::function<void(const WiFiEventStationModeConnected data)> wifiConnectCallback
#define WIFI_DISCONNECT_CALLBACK_SIGNATURE std::function<void(const WiFiEventStationModeDisconnected data)> wifiDisconnectCallback
#define WIFI_GOT_IP_CALLBACK_SIGNATURE std::function<void(const WiFiEventStationModeGotIP data)> wifiGotIpCallback
#define MQTT_CONNECT_CALLBACK_SIGNATURE std::function<void(void)> mqttConnectCallback
#define MQTT_MESSAGE_CALLBACK_SIGNATURE std::function<void(const char *topic, const char *payload, const AsyncMqttClientMessageProperties properties, const size_t len, const size_t index, const size_t total)> mqttMessageCallback

// Define all callbacks
#define CONFIG_CHANGED_CALLBACK(routine) void routine(void)
#define DEBUG_COMMAND_CALLBACK(routine) bool routine(const String debugCommand)
#define HELP_MESSAGE_CALLBACK(routine) String routine(void)
#ifndef NO_SERIAL_COMMAND_CALLBACK
	#define SERIAL_COMMAND_CALLBACK(routine) bool routine(const String command)
#endif
#define REST_COMMAND_CALLBACK(routine) bool routine(AsyncWebServerRequest *request)
#define JSON_COMMAND_CALLBACK(routine) bool routine(AsyncWebServerRequest *request)
#define POST_COMMAND_CALLBACK(routine) bool routine(AsyncWebServerRequest *request)
#define ERROR404_CALLBACK(routine) bool routine(AsyncWebServerRequest *request)
#define WIFI_CONNECT_CALLBACK(routine) void routine(const WiFiEventStationModeConnected data)
#define WIFI_DISCONNECT_CALLBACK(routine) void routine(const WiFiEventStationModeDisconnected data)
#define WIFI_GOT_IP_CALLBACK(routine) void routine(const WiFiEventStationModeGotIP data)
#define MQTT_CONNECT_CALLBACK(routine) void routine(void)
#define MQTT_MESSAGE_CALLBACK(routine) void routine(const char *topic, const char *payload, const AsyncMqttClientMessageProperties properties, const size_t len, const size_t index, const size_t total)

typedef struct {
	String ssid;
	String password;
	IPAddress ip;
	IPAddress netmask;
	IPAddress gateway;
	IPAddress dns;
	bool dhcp;
	String ntpServerName;
	long updateNTPTimeEvery;
	long timezone;
	bool daylight;
	String deviceName;
} strConfig;

typedef struct {
	String APssid = "ESP";									// ChipID is appended to this name
	String APpassword = "12345678";
	bool APenable = false;									// AP disabled by default
} strApConfig;

typedef struct {
	bool auth;
	String wwwUsername;
	String wwwPassword;
} strHTTPAuth;

typedef enum {
	FS_STAT_CONNECTING,
	FS_STAT_CONNECTED,
	FS_STAT_APMODE
} enWifiStatus;

class AsyncFFWebServer : public AsyncWebServer {
public:
	AsyncFFWebServer(uint16_t port);
	void begin(FS* fs, const char *version);
	void handle(void);
	const char* getHostName(void);
	void startWifi(void);
	void startWifiAP(void);
	void stopWifi(void);
	const char* getWebServerVersion(void);
	String getDeviceName(void);
	AsyncEventSource _evs = AsyncEventSource("/events");
	enWifiStatus wifiStatus;
	uint8_t connectionTimout;

	// Set callbacks
	AsyncFFWebServer& setConfigChangedCallback(CONFIG_CHANGED_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setDebugCommandCallback(DEBUG_COMMAND_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setHelpMessageCallback(HELP_MESSAGE_CALLBACK_SIGNATURE);
	#ifndef NO_SERIAL_COMMAND_CALLBACK
		AsyncFFWebServer& setSerialCommandCallback(SERIAL_COMMAND_CALLBACK_SIGNATURE);
	#endif
	AsyncFFWebServer& setRestCommandCallback(REST_COMMAND_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setJsonCommandCallback(JSON_COMMAND_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setPostCommandCallback(POST_COMMAND_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setError404Callback(ERROR404_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setWifiConnectCallback(WIFI_CONNECT_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setWifiDisconnectCallback(WIFI_DISCONNECT_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setWifiGotIpCallback(WIFI_GOT_IP_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setMqttConnectCallback(MQTT_CONNECT_CALLBACK_SIGNATURE);
	AsyncFFWebServer& setMqttMessageCallback(MQTT_MESSAGE_CALLBACK_SIGNATURE);

	bool save_user_config(String name, String value);
	bool load_user_config(String name, String &value);
	bool save_user_config(String name, int value);
	bool load_user_config(String name, int &value);
	bool save_user_config(String name, float value);
	bool load_user_config(String name, float &value);
	bool save_user_config(String name, long value);
	bool load_user_config(String name, long &value);
	String urldecode(String input); // (based on https://code.google.com/p/avr-netino/)
	void sendTimeData();
	void configureWifiAP();

	void executeCommand(const String lastCde);
	bool mqttSubscribe (const char *subTopic, const int qos = 0);
	bool mqttSubscribeRaw (const char *topic, const int qos = 0);
	void mqttPublish (const char *subTopic, const char *value);
	void mqttPublishRaw (const char *topic, const char *value);
	void connectToMqtt(void);
	int parseUrlParams (char *queryString, char *results[][2], const int resultsMaxCt, const boolean decodeUrl);
	String getContentType(String filename, AsyncWebServerRequest *request);

	// ----- Domoticz -----
	// Domoticz is supported on (asynchronous) MQTT
	void sendDomoticzDimmer (const int idx, const uint8_t level);
	void sendDomoticzPower (const int idx, const float power, const float energy);
	void sendDomoticzSwitch (const int idx, const bool isOn);
	void sendDomoticzValues (const int idx, const char *values, const int integer = 0);

	//Clear the configuration data (not the user config!) and optional reset the device
	void clearConfig(bool reset);
	//Clear the user configuration data (not the Wifi config!) and optional reset the device
	void clearUserConfig(bool reset);

	#ifdef FF_TRACE_KEEP_ALIVE
		void resetTraceKeepAlive(void);
	#endif
	bool debugFlag = false;
	bool traceFlag = false;
	bool watchdogFlag = true;

protected:
	// Callbacks
	CONFIG_CHANGED_CALLBACK_SIGNATURE;
	DEBUG_COMMAND_CALLBACK_SIGNATURE;
	HELP_MESSAGE_CALLBACK_SIGNATURE;
	#ifndef NO_SERIAL_COMMAND_CALLBACK
		SERIAL_COMMAND_CALLBACK_SIGNATURE;
	#endif
	REST_COMMAND_CALLBACK_SIGNATURE;
	JSON_COMMAND_CALLBACK_SIGNATURE;
	POST_COMMAND_CALLBACK_SIGNATURE;
	ERROR404_CALLBACK_SIGNATURE;
	WIFI_CONNECT_CALLBACK_SIGNATURE;
	WIFI_DISCONNECT_CALLBACK_SIGNATURE;
	WIFI_GOT_IP_CALLBACK_SIGNATURE;
	MQTT_CONNECT_CALLBACK_SIGNATURE;
	MQTT_MESSAGE_CALLBACK_SIGNATURE;

	// ----- MQTT -----
	AsyncMqttClient mqttClient;
	Ticker mqttReconnectTimer;
	String mqttWillTopic = "";
	int configMQTT_Interval = 0;
	int configMQTT_Port = 0;
	String configMQTT_User = "";
	String configMQTT_Pass = "";
	String configMQTT_Host = "";
	String configMQTT_Topic = "";
	String configMQTT_CommandTopic = "";
	String configMQTT_ClientID = "";
	bool mqttInitialized = false;
	bool wifiConnected = false;
	bool wifiGotIp = false;
	unsigned long lastDisconnect = 0;
	boolean mqttTest();
	static void onMqttConnect(bool sessionPresent);
	static void onMqttDisconnect(AsyncMqttClientDisconnectReason disconnectReason);
	static void onMqttSubscribe(uint16_t packetId, uint8_t qos);
	static void onMqttUnsubscribe(uint16_t packetId);
	static void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
	static void onMqttPublish(uint16_t packetId);

	// Trace callback routine
	#ifndef FF_DISABLE_DEFAULT_TRACE
		static trace_callback(defaultTraceCallback);
	#endif

	// Trace keep alive timer
	#ifdef FF_TRACE_KEEP_ALIVE
		unsigned long lastTraceMessage = 0;
		unsigned long traceKeepAlive = FF_TRACE_KEEP_ALIVE;
	#endif

	// ----- Web server -----
	void percentDecode(char *src);
	void loadConfig(void);
	void loadUserConfig(void);
	void error404(AsyncWebServerRequest *request);
	bool serverStarted = false;
	String standardHelpCmd();

	// ----- Debug -----
	#ifdef REMOTE_DEBUG
		// ---- Remote debug -----
		static void executeDebugCommand();
	#endif
	traceLevel_t lastTraceLevel;

	#if defined(SERIAL_COMMAND_PREFIX) || !defined(NO_SERIAL_COMMAND_CALLBACK)
		char serialCommand[200];							// Buffer to save serial commands
		size_t serialCommandLen = 0;						// Buffer used lenght
	#endif
	// ----- Syslog -----
	#ifdef FF_TRACE_USE_SYSLOG
		String syslogServer = "";
		int syslogPort = 0;
	#endif

	// ----- Domoticz -----
	void sendDomoticz(const char* url);

	// ----- WatchDog -----
	#ifdef HARDWARE_WATCHDOG_PIN
		bool hardwareWatchdogState = false;
		unsigned long hardwareWatchdogLastUpdate = 0;
		unsigned long hardwareWatchdogDelay = 0;
	#endif

	// Internal Webserver
	strConfig _config; // General and WiFi configuration
	strApConfig _apConfig; // Static AP config settings
	strHTTPAuth _httpAuth;
	FS* _fs;
	String userVersion = "";
	String serverVersion = FF_WEBSERVER_VERSION;
	long wifiDisconnectedSince = 0;
	String _browserMD5 = "";
	uint32_t _updateSize = 0;
	bool updateTimeFromNTP = false;
	WiFiEventHandler onStationModeConnectedHandler, onStationModeDisconnectedHandler, onStationModeGotIPHandler;
	Ticker _secondTk;
	bool _secondFlag;
	bool load_config();
	void defaultConfig();
	bool save_config();
	bool loadHTTPAuth();
	bool saveHTTPAuth();
	void configureWifi();
	void ConfigureOTA(String password);
	void serverInit();
	static void onWiFiConnected(WiFiEventStationModeConnected data);
	static void onWiFiDisconnected(WiFiEventStationModeDisconnected data);
	static void onWiFiConnectedGotIP(WiFiEventStationModeGotIP data);
	String getMacAddress();
	bool checkAuth(AsyncWebServerRequest *request);
	uint8_t mapRSSItoDomoticz(void);
	uint8_t mapVccToDomoticz(void);
	void handleFileList(AsyncWebServerRequest *request);
	bool handleFileRead(String path, AsyncWebServerRequest *request);
	void handleFileCreate(AsyncWebServerRequest *request);
	void handleFileDelete(AsyncWebServerRequest *request);
	void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
	void send_general_configuration_values_html(AsyncWebServerRequest *request);
	void send_network_configuration_values_html(AsyncWebServerRequest *request);
	void send_connection_state_values_html(AsyncWebServerRequest *request);
	void send_information_values_html(AsyncWebServerRequest *request);
	void send_NTP_configuration_values_html(AsyncWebServerRequest *request);
	void send_network_configuration_html(AsyncWebServerRequest *request);
	void send_general_configuration_html(AsyncWebServerRequest *request);
	void send_NTP_configuration_html(AsyncWebServerRequest *request);
	void restart_esp(AsyncWebServerRequest *request);
	void send_wwwauth_configuration_values_html(AsyncWebServerRequest *request);
	void send_wwwauth_configuration_html(AsyncWebServerRequest *request);
	void send_update_firmware_values_html(AsyncWebServerRequest *request);
	void setUpdateMD5(AsyncWebServerRequest *request);
	void updateFirmware(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
	void handle_rest_config(AsyncWebServerRequest *request);
	void post_rest_config(AsyncWebServerRequest *request);
	unsigned char h2int(char c);
	boolean checkRange(String Value);
	void flashLED(const int pin, const int times, int delayTime);
	String formatBytes(size_t bytes);
	static void s_secondTick(void* arg);
};

extern AsyncFFWebServer FF_WebServer;
// ----- Syslog -----
#ifdef FF_TRACE_USE_SYSLOG
	extern Syslog syslog;
#endif
#ifdef REMOTE_DEBUG
	extern RemoteDebug Debug;
#endif
#endif // _FFWEBSERVER_hpp
