/*
 * Receiver for data of wireless weather sensors with RX868 and Raspberry Pi.
 *
 * Main program.
 */
#include <libconfig.h++>
#include <wiringPi.h>
#include <stdio.h>
#include <signal.h>
#include "Decoder.h"

#include <iostream>
#include <cstdlib>
#include <string>
#include <thread>	// For sleep
#include <atomic>
#include <chrono>
#include <cstring>
#include "mqtt/async_client.h"

using namespace std;
using namespace libconfig;

// Default configuration
string SERVER_ADDRESS = "tcp://127.0.0.1:1883";
string CLIENT_ID = "rx868relay";
string TOPIC = "weatherstation";
int  QOS = 1;
auto TIMEOUT = chrono::seconds(10);

Config config;
mqtt::token_ptr conntok = NULL;

/////////////////////////////////////////////////////////////////////////////


/**
 * A callback class for use with the main MQTT client.
 */
class callback : public virtual mqtt::callback
{
public:
	void connection_lost(const string& cause) override {
		cout << "\nConnection lost" << endl;
		if (!cause.empty())
			cout << "\tcause: " << cause << endl;
	}

	void delivery_complete(mqtt::delivery_token_ptr tok) override {
		cout << "\tDelivery complete for token: "
			<< (tok ? tok->get_message_id() : -1) << endl;
	}
};

/////////////////////////////////////////////////////////////////////////////

int setup_mqtt(mqtt::async_client &client)
{
	string username;
	string password;

	mqtt::connect_options conopts;

	cout << "Initialized for server '" <<  client.get_server_uri() << "'" << endl;

	if(config.lookupValue("username", username)
	   && config.lookupValue("password", password)) {

		conopts.set_user_name(username);
		conopts.set_password(password);
		cout << "Using username/password authentication" << endl;

	}


	try {
		cout << "Connecting ... ";
		conntok = client.connect(conopts);
		conntok->wait();
		cout << "OK" << endl;
	}
	catch (const mqtt::exception& exc) {
		cerr << exc.what() << endl;
		return 1;
	}

	return 0;
}

void create_and_send(mqtt::async_client &client, int address, string type, float value)
{
	char buff[16];
	sprintf(buff, "%.1f", value);
	string val_str = buff;
	string add_str = to_string(address);
	cout << "\nsending message..." << endl;
	mqtt::message_ptr pubmsg = mqtt::make_message(
			TOPIC + "/" + add_str + "/" + type,
			val_str);
	pubmsg->set_qos(QOS);
	client.publish(pubmsg)->wait_for(TIMEOUT);
	cout << "  ...OK" << endl;
}

void publishData(mqtt::async_client &client, DecoderOutput val)
{	
	printf("publishing temperature: %.1f\n", val.temperature);
	create_and_send(client, val.address, "temperature", val.temperature);

	if (7 == val.sensorType) {
		// Kombisensor
		printf("publishing humidity: %.0f\n", val.humidity);
		create_and_send(client, val.address, "humidity", val.humidity);

		printf("publishing wind: %.1f\n", val.wind);
		create_and_send(client, val.address, "wind", val.wind);

		printf("publishing rain sum: %d\n", val.rainSum);
		create_and_send(client, val.address, "rain_sum", val.rainSum);

		printf("publishing rain detector: %d\n", val.rainDetect);
		create_and_send(client, val.address, "rain_detect", val.rainDetect);
	}
	if (1 == val.sensorType || 4 == val.sensorType) {
		// Thermo/Hygro
		printf("publishing humidity: %.0f\n", val.humidity);
		create_and_send(client, val.address, "humidity", val.humidity);
	}
	if (4 == val.sensorType) {
		// Thermo/Hygro/Baro
		printf("publishing pressure: %d\n", val.pressure);
		create_and_send(client, val.address, "pressure", val.pressure);
	}
}

/////////////////////////////////////////////////////////////////////////////

static volatile int keepRunning = 1;
static DecoderOutput out;
static volatile int hasOut = 0;

/*
 * Signal handler to catch Ctrl-C to terminate the program safely.
 */
void sigIntHandler(int dummy) {
	keepRunning = 0;
} 

/*
 * Thread to read the output of the receiver module
 * and to decode it using Decoder.
 */
PI_THREAD (decoderThread) {
  piHiPri(50);

  // init decoder for a sample rate of 1/200µs
  Decoder decoder(4,10);
  int x;
  int len = 0;
  int lo = 0;
  int px = 0;

  while (keepRunning) {
    x = digitalRead(2);

    len++;
    if (0 == x) {
      lo++;
    }
    if (x != px && 0 != x) {
      // slope low->high
      if (decoder.pulse(len, lo)) {
        piLock(1);
        out = decoder.getDecoderOutput();
        hasOut = 1;
        piUnlock(1);
      }
      len = 1;
      lo = 0;
    }
    px = x;

    delayMicroseconds(200);
  }
}

////////////////////////////////////////////////////////////////////////////

int loadConfig()
{
	// Read the file. If there is an error, report it and exit.
	try	{
		config.readFile("config.cfg");
	}
	catch(const FileIOException &fioex)	{
		std::cerr << "I/O error while reading file." << std::endl;
		return(EXIT_FAILURE);
	}
	catch(const ParseException &pex) {
		std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
				  << " - " << pex.getError() << std::endl;
		return(EXIT_FAILURE);
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////

/*
 * MAIN
 *
 * Initialize the hardware and print the output of the Decoder.
 */
int main() {
	
	if (loadConfig() > 0) {
		cerr << "Failed to read config. Exiting." << endl;
		exit(1);
	}

	wiringPiSetup();
	/*
	BCM GPIO 27: DATA (IN) == WiPin 2
	BCM GPIO 22: EN (OUT)  == WiPin 3
	*/
	pinMode(3, OUTPUT);
	digitalWrite(3, 1); // enable rx
	pinMode(2, INPUT);
	pullUpDnControl(2, PUD_DOWN);

	string address;
	string clientID;

	if(config.lookupValue("server_address", address)
	   && config.lookupValue("client_id", clientID)) {

	}
	else {
		cerr << "Please set server address and client ID in config. Exiting." << endl;
		exit(1);
	}

	string new_topic;
	if (config.lookupValue("topic", new_topic)) {
		TOPIC = new_topic;
	}
	
	mqtt::async_client client(address, clientID);
	if (setup_mqtt(client) > 0) {
		cerr << "Failed to connect to server. Exiting." << endl;
		exit(1);
	}
	signal(SIGINT, sigIntHandler);
	piThreadCreate(decoderThread);

	while (keepRunning) {
		piLock(1);
		if (hasOut) { 
			hasOut = 0;
			piUnlock(1);
			publishData(client, out);
		}
		piUnlock(1);
		delay(100);
	}

	cout << "Clean up and exit." << endl;
	
	// Disconnect from server
	cout << "\nDisconnecting from MQTT broker ... ";
	conntok = client.disconnect();
	conntok->wait();
	cout << "OK" << endl;

	digitalWrite(3, 0); // disable rx
}       
