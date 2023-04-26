/*

	This examples shows how to implement a minimal web server with FF_WebServer class.

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

//	User configuration data (should be in line with userconfigui.json)
//		Declare here user's configuration data needed by user's code

// Declare here used callbacks
static REST_COMMAND_CALLBACK(onRestCommandCallback);

// Here are the callbacks code

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
				// -----------------
				)
			// -- Put here values of header line
			,FF_WebServer.getDeviceName().c_str()
			,VERSION, FF_WebServer.getWebServerVersion()
			,NTP.getUptimeString().c_str()
			// -- Put here values in index_user.html
			// -----------------
			);
		request->send(200, "text/plain", tempBuffer);
		return true;
	}
	return false;
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
	// Set user's callbacks
	FF_WebServer.setRestCommandCallback(&onRestCommandCallback);
}

//	This is the main loop.
//	Do what ever you want and call FF_WebServer.handle()
void loop() {
	// Manage Web Server
	FF_WebServer.handle();
}
