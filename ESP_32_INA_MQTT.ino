#include <WiFi.h>
#include <Credentials\Credentials.h>
#include "EEPROM.h" //For storing MD5 for OTA
#include <Update.h>
#include <Wire.h>
#include <INA226.h>
#include <PubSubClient.h>

RTC_DATA_ATTR int flow;

WiFiClient espClient;
unsigned long reconnectionPeriod = 10000; //miliseconds
unsigned long lastWifiConnectionAttempt = 0;
bool HOME = true;
const char* _SSID;
const char* _PSWD;
String host;


PubSubClient client(espClient);
unsigned long lastBrokerConnectionAttempt = 0;
unsigned long lastSensorMsg = 0;
char msg[50];
int sensorRequestPeriod = 10; // seconds
const char* mqtt_server;



INA226 ina;
float busVoltage = 0.0;
float busPower = 0.0;
float shuntVoltage = 0.0;
float shuntCurrent = 0.0;

const int RELAY_PIN_BATTERY_1 = 12;
const int RELAY_PIN_BATTERY_2 = 13;
const int RELAY_PIN_CHARGER = 14;

const int SETTINGS_SWITCH_1_PIN = 12;
const int SETTINGS_SWITCH_2_PIN = 13;
const int SETTINGS_SWITCH_3_PIN = 14;
const int SETTINGS_SWITCH_4_PIN = 14;

const int ERROR_LED_PIN = 14;


//-----------------HTTP_OTA------------------------

/* Over The Air automatic firmware update from a web server.  ESP32 will contact the
*  server on every boot and check for a firmware update.  If available, the update will
*  be downloaded and installed.  Server can determine the appropriate firmware for this
*  device from combination of HTTP_OTA_FIRMWARE and firmware MD5 checksums.
*/

// Name of firmware
#define HTTP_OTA_FIRMWARE String(String(__FILE__).substring(String(__FILE__).lastIndexOf('\\')) + ".bin").substring(1)

// Variables to validate response
int contentLength = 0;
bool isValidContentType = false;
bool isNewFirmware = false;
int port = HTTP_OTA_PORT;
String binPath = String(HTTP_OTA_PATH) + HTTP_OTA_FIRMWARE;

String MD5;
int EEPROM_SIZE = 1024;
int MD5_address = 0; // in EEPROM

int sleepPeriod = 60; // Seconds




void setup() {
	Serial.begin(115200);
	pinMode(RELAY_PIN_BATTERY_1, OUTPUT);
	pinMode(RELAY_PIN_BATTERY_2, OUTPUT);
	pinMode(RELAY_PIN_CHARGER, OUTPUT);
	pinMode(ERROR_LED_PIN, OUTPUT);
	pinMode(SETTINGS_SWITCH_1_PIN, INPUT);
	pinMode(SETTINGS_SWITCH_2_PIN, INPUT);
	pinMode(SETTINGS_SWITCH_3_PIN, INPUT);
	pinMode(SETTINGS_SWITCH_4_PIN, INPUT);
	digitalWrite(RELAY_PIN_BATTERY_1, LOW);
	digitalWrite(RELAY_PIN_BATTERY_2, LOW);
	digitalWrite(RELAY_PIN_CHARGER, LOW);
	digitalWrite(ERROR_LED_PIN, LOW);
	flow = getSettings();
	switch (flow) {
	case 1://(1.0.0.0)
		Serial.println("Voltage monitoring");
		delay(100);
		if (HOME)
		{
			_SSID = SSID;
			_PSWD = PASSWORD;
			host = SERVER_IP;
			mqtt_server = SERVER_IP;
		}
		else
		{
			_SSID = SSID_1;
			_PSWD = PASSWORD_1;
			host = SERVER_IP_1;
			mqtt_server = SERVER_IP_1;
		}
		setup_wifi();
		client.setServer(mqtt_server, 1883);
		client.setCallback(callback);
		connectToBroker();
		initializeINA226();
		getSensorDataAndSendToBrocker();
		digitalWrite(ERROR_LED_PIN, HIGH);
		delay(1000);
		digitalWrite(ERROR_LED_PIN, LOW);
		sleep(sleepPeriod);
		break;

	case 2://(0.1.0.0)
		Serial.println("OTA");
		delay(100);
		for (int i = 0; i < 5; i++)
		{
			digitalWrite(ERROR_LED_PIN, HIGH);
			delay(300);
			digitalWrite(ERROR_LED_PIN, LOW);
			delay(300);
		}
		setup_wifi();
		checkEEPROM();
		delay(100);
		execOTA();
		while (true)
		{
			digitalWrite(ERROR_LED_PIN, HIGH);
			delay(2000);
			digitalWrite(ERROR_LED_PIN, LOW);
			delay(2000);
		}
		break;

	case 3://(0.0.1.0)
		Serial.println("Discharge controll");
		break;

	case 4://(0.0.0.1)
		Serial.println("Charge controll");
		delay(100);
		//setup_wifi();
		//client.setServer(mqtt_server, 1883);
		//client.setCallback(callback);
		//connectToBroker();
		sleep(sleepPeriod);
		break;

	default:
		Serial.println("Error in settings");
		while (true)
		{
			digitalWrite(ERROR_LED_PIN, HIGH);
		}
		break;
	}
}

int getSettings() {
	int switch_1 = digitalRead(SETTINGS_SWITCH_1_PIN);
	int switch_2 = digitalRead(SETTINGS_SWITCH_2_PIN);
	int switch_3 = digitalRead(SETTINGS_SWITCH_3_PIN);
	int switch_4 = digitalRead(SETTINGS_SWITCH_4_PIN);
	if (switch_1 == 1 && switch_2 == 0 && switch_3 == 0 && switch_4 == 0)
	{
		return 1;
	}
	else if(switch_1 == 0 && switch_2 == 1 && switch_3 == 0 && switch_4 == 0)
	{
		return 2;
	}
	else if (switch_1 == 0 && switch_2 == 0 && switch_3 == 1 && switch_4 == 0)
	{
		return 3;
	}
	else if (switch_1 == 0 && switch_2 == 0 && switch_3 == 0 && switch_4 == 1)
	{
		return 4;
	}
	else
	{
		return 0;
	}
}

void reconnectWifi() {
	long now = millis();
	if (now - lastWifiConnectionAttempt > reconnectionPeriod) {
		lastWifiConnectionAttempt = now;
		setup_wifi();
	}
}

void setup_wifi() {
	// We start by connecting to a WiFi network

	Serial.print(F("Connecting to "));
	Serial.println(_SSID);

	WiFi.begin(_SSID, _PSWD);
	delay(3000);

	if (WiFi.waitForConnectResult() != WL_CONNECTED) {

		Serial.println(F("Connection Failed!"));
		return;
	}
}



// Utility to extract header value from headers
String getHeaderValue(String header, String headerName) {
	return header.substring(strlen(headerName.c_str()));
}

// Used for storing of MD5 hash
void checkEEPROM() {
	if (!EEPROM.begin(EEPROM_SIZE)) {

		Serial.println("Failed to initialise EEPROM");
		Serial.println("Restarting...");

		delay(1000);
		ESP.restart();
	}
}

void saveMD5toEEPROM() {

	Serial.println("Writing MD5 to EEPROM : " + MD5);

	EEPROM.writeString(MD5_address, MD5);
	EEPROM.commit();

	if (EEPROM.readString(MD5_address) == MD5)
	{
		Serial.println("Successfully written MD5 to EEPROM : " + EEPROM.readString(MD5_address));
	}
	else
	{
		Serial.println("Failed to write MD5 to EEPROM : " + MD5);
		Serial.println("MD5 in EEPROM : " + EEPROM.readString(MD5_address));
	}

}

String loadMD5FromEEPROM() {

	Serial.println("Loaded MD5 from EEPROM : " + EEPROM.readString(MD5_address));

	return EEPROM.readString(MD5_address);
}

// OTA Logic ESP-32
void execOTA() {

	Serial.println("Connecting to: " + String(host));

	// Connect to S3
	if (espClient.connect(host.c_str(), port)) {
		// Connection Succeed.
		// Fecthing the bin

		Serial.println("Fetching Bin: " + String(binPath));

		// Get the contents of the bin file
		espClient.print(String("GET ") + binPath + " HTTP/1.1\r\n" +
			"Host: " + host + "\r\n" +
			"Cache-Control: no-cache\r\n" +
			"User-agent: esp-32\r\n" +
			"MD5: " + loadMD5FromEEPROM() + "\r\n" +
			"Connection: close\r\n\r\n");

		unsigned long timeout = millis();
		while (espClient.available() == 0) {
			if (millis() - timeout > 5000) {

				Serial.println("Client Timeout !");

				espClient.stop();
				return;
			}
		}

		while (espClient.available()) {
			// read line till /n
			String line = espClient.readStringUntil('\n');
			// remove space, to check if the line is end of headers
			line.trim();

			Serial.println(line);

			// if the the line is empty,
			// this is end of headers
			// break the while and feed the
			// remaining `client` to the
			// Update.writeStream();
			if (!line.length()) {
				//headers ended
				break; // and get the OTA started
			}

			// Check if the HTTP Response is 200
			// else break and Exit Update
			if (line.startsWith("HTTP/1.1")) {
				if (line.indexOf("200") < 0) {

					Serial.println("Got a non 200 status code from server. Exiting OTA Update.");

					break;
				}
			}

			// extract headers here
			// Start with content length
			if (line.startsWith("Content-Length: ")) {
				contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());

				Serial.println("Got " + String(contentLength) + " bytes from server");

			}

			// Next, the content type
			if (line.startsWith("Content-Type: ")) {
				String contentType = getHeaderValue(line, "Content-Type: ");

				Serial.println("Got " + contentType + " payload.");

				if (contentType == "application/octet-stream") {
					isValidContentType = true;
				}
			}
			// Get MD5 from response and compare with stored MD5
			if (line.startsWith("md5: ")) {
				MD5 = getHeaderValue(line, "md5: ");

				Serial.println("Got md5 from response : " + MD5);
				Serial.print("Size of md5 : ");
				Serial.println(sizeof(MD5));

				if (!MD5.equals(loadMD5FromEEPROM()) && sizeof(MD5) > 10) {
					isNewFirmware = true;
				}
				else
				{
					isNewFirmware = false;
				}
			}
		}
	}
	else {
		// Connect to S3 failed
		// May be try?
		// Probably a choppy network?

		Serial.println("Connection to " + String(host) + " failed. Please check your setup");

		// retry??
		// execOTA();
	}

	// Check what is the contentLength and if content type is `application/octet-stream`

	Serial.println("contentLength : " + String(contentLength));
	Serial.println("isValidContentType : " + String(isValidContentType));
	Serial.println("isNewFirmware : " + String(isNewFirmware));

	// check contentLength and content type
	if (contentLength && isValidContentType) {
		if (isNewFirmware)
		{
			// Check if there is enough to OTA Update
			bool canBegin = Update.begin(contentLength);

			// If yes, begin
			if (canBegin) {

				Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");

				// No activity would appear on the Serial monitor
				// So be patient. This may take 2 - 5mins to complete
				size_t written = Update.writeStream(espClient);

				if (written == contentLength) {
					Serial.println("Written : " + String(written) + " successfully");
				}
				else {
					Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
					// retry??
					// execOTA();
				}

				if (Update.end()) {

					Serial.println("OTA done!");

					if (Update.isFinished()) {

						Serial.println("Update successfully completed. Rebooting.");

						saveMD5toEEPROM();
						ESP.restart();
					}
					else {

						Serial.println("Update not finished? Something went wrong!");

					}
				}
				else {

					Serial.println("Error Occurred. Error #: " + String(Update.getError()));

				}
			}
			else {
				// not enough space to begin OTA
				// Understand the partitions and
				// space availability

				Serial.println("Not enough space to begin OTA");

				espClient.flush();
			}
		}
		else
		{

			Serial.println("There is no new firmware");

			espClient.flush();
		}
	}
	else {

		Serial.println("There was no content in the response");

		espClient.flush();
	}
}


void callback(char* topic, byte* payload, unsigned int length) {

	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (int i = 0; i < length; i++) {
		Serial.print((char)payload[i]);
	}
	Serial.println("");

	if (strcmp(topic, "Battery/restart") == 0) {
		//Restart ESP to update flash
		ESP.restart();
	}
	if (strcmp(topic, "Battery/sensorRequestPeriod") == 0) {
		String myString = String((char*)payload);
		sensorRequestPeriod = myString.toInt();

		Serial.println(myString);
		Serial.print("Sensor request period set to :");
		Serial.print(sensorRequestPeriod);
		Serial.println(" seconds");
	}
	if (strcmp(topic, "Battery/sleepPeriod") == 0) {
		String myString = String((char*)payload);
		Serial.println(myString);
		sleepPeriod = myString.toInt();
		String sleepPeriodMessage = String() + "Battery(ESP32) sleep period set to : " + sleepPeriod + " seconds";
		Serial.println(sleepPeriodMessage);
		client.publish("Battery/status", sleepPeriodMessage.c_str());
	}
}

//Connection to MQTT broker
void connectToBroker() {

	Serial.print("Attempting MQTT connection...");

	// Attempt to connect
	if (client.connect("Battery")) {

		Serial.println("ESP32 сonnected to MQTT broker");

		// Once connected, publish an announcement...
		client.publish("Battery/status", "Battery(ESP32) connected");
		// ... and resubscribe
		client.subscribe("Battery/sensorRequestPeriod");
		client.subscribe("Battery/restart");
		client.subscribe("Battery/sleepPeriod");
	}
	else {
		Serial.print("failed, rc=");
		Serial.print(client.state());
		Serial.println(" try again in 60 seconds");
	}
}

void reconnectToBroker() {
	long now = millis();
	if (now - lastBrokerConnectionAttempt > reconnectionPeriod) {
		lastBrokerConnectionAttempt = now;
		{
			if (WiFi.status() == WL_CONNECTED)
			{
				if (!client.connected()) {
					connectToBroker();
				}
			}
			else
			{
				reconnectWifi();
			}
		}
	}
}

void loop() {
	if (!client.connected()) {
		reconnectToBroker();
	}
	client.loop();
	sendMessageToMqttInLoop();
}


 


void sendMessageToMqttInLoop() {
	long now = millis();
	if (now - lastSensorMsg > sensorRequestPeriod * 1000) {
		lastSensorMsg = now;
		getSensorDataAndSendToBrocker();
	}
}

void getSensorDataAndSendToBrocker() {
	switchBattery(1);
	getSensorData();
	sendMessageToMqtt(1);
	switchBattery(2);
	getSensorData();
	sendMessageToMqtt(2);
	digitalWrite(RELAY_PIN_BATTERY_1, LOW);
	digitalWrite(RELAY_PIN_BATTERY_2, LOW);
}

void sendMessageToMqtt(int battery) {
	String busVoltageTopic = String() + "Battery" + battery + "/busVoltage";
	String busPowerTopic = String() + "Battery" + battery + "/busPower";
	String shuntCurrentTopic = String() + "Battery" + battery + "/shuntCurrent";
	String shuntVoltageTopic = String() + "Battery" + battery + "/shuntVoltage";

	Serial.print("Publish message to " + busVoltageTopic + " : ");
	Serial.println(busVoltage, 5);
	client.publish(busVoltageTopic.c_str(), String(busVoltage, 5).c_str());

	Serial.print("Publish message to " + busPowerTopic + " : ");
	Serial.println(busPower, 5);
	client.publish(busPowerTopic.c_str(), String(busPower, 5).c_str());

	Serial.print("Publish message to " + shuntCurrentTopic + " : ");
	Serial.println(shuntCurrent, 5);
	client.publish(shuntCurrentTopic.c_str(), String(shuntCurrent, 5).c_str());

	Serial.print("Publish message to " + shuntVoltageTopic + +" : ");
	Serial.println(shuntVoltage, 5);
	client.publish(shuntVoltageTopic.c_str(), String(shuntVoltage, 5).c_str());
}

void sleep(int sleepTimeInSeconds) {
	Serial.print("Go to deep sleep for ");
	Serial.print(sleepTimeInSeconds);
	Serial.println(" seconds");
	// Once connected, publish an announcement...
	client.publish("Battery/status", "Battery(ESP32) goes to sleep for " + sleepTimeInSeconds);
	delay(3000);
	esp_deep_sleep(sleepTimeInSeconds * 1000000);
}

void initializeINA226() {
	// Default INA226 address is 0x40
	ina.begin();
	// Configure INA226
	ina.configure(INA226_AVERAGES_128, INA226_BUS_CONV_TIME_8244US, INA226_SHUNT_CONV_TIME_8244US, INA226_MODE_SHUNT_BUS_CONT);
	// Calibrate INA226. Rshunt = 0.1 ohm, Max excepted current = 2A
	ina.calibrate(0.1, 2);
}

void switchBattery(int battery) {
	switch (battery) {
	case 1:
		digitalWrite(RELAY_PIN_BATTERY_2, LOW);
		delay(500);
		digitalWrite(RELAY_PIN_BATTERY_1, HIGH);
		delay(5000);
		break;
	case 2:
		digitalWrite(RELAY_PIN_BATTERY_1, LOW);
		delay(500);
		digitalWrite(RELAY_PIN_BATTERY_2, HIGH);
		delay(5000);
		break;
	}
}

void getSensorData() {
	busVoltage = ina.readBusVoltage();
	Serial.print("Bus voltage:   ");
	Serial.print(busVoltage, 5);
	Serial.println(" V");

	busPower = ina.readBusPower();
	Serial.print("Bus power:     ");
	Serial.print(busPower, 5);
	Serial.println(" W");

	shuntVoltage = ina.readShuntVoltage();
	Serial.print("Shunt voltage: ");
	Serial.print(shuntVoltage, 5);
	Serial.println(" V");

	shuntCurrent = ina.readShuntCurrent();
	Serial.print("Shunt current: ");
	Serial.print(shuntCurrent, 5);
	Serial.println(" A");

	Serial.println("");
}