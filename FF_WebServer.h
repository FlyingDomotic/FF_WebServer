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

#ifndef _FFWEBSERVER_h
#define _FFWEBSERVER_h

#include <FF_WebServer.hpp>
#include <LittleFS.h>

// ----- Syslog -----
#ifdef FF_TRACE_USE_SYSLOG
	WiFiUDP udpClient;
	Syslog syslog(udpClient, SYSLOG_PROTO_IETF);
#endif

// ----- Remote debug -----
#ifdef REMOTE_DEBUG
	RemoteDebug Debug;
#endif

// Declare trace object
trace_declare();

// Declare FF_WebServer object
AsyncFFWebServer FF_WebServer(80);

#endif // _FFWEBSERVER_h
