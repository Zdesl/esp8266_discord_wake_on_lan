#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WakeOnLan.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include "secrets.h"

#define DEBUG false
#define DEBUG_SERIAL \
    if (DEBUG) Serial
#define DEBUG_HTTP_RESPONSES false  // DEBUG must be true, set to true to just print the response over Serial
#define USE_BUILTIN_LED false

/* NOTES
    - WARNING this bot is susceptible to man in the middle attacks
      because we use setInsecure() to avoid having to always update the certificates
*/

/* USEFUL LINKS
    https://github.com/nodemcu/nodemcu-devkit-v1.0/tree/master
    https://www.espressif.com/sites/default/files/documentation/0a-esp8266ex_datasheet_en.pdf
    https://arduino-esp8266.readthedocs.io/en/latest/PROGMEM.html
    https://forum.arduino.cc/t/is-string-safe-to-use-on-a-esp8266/541982
    https://developer.mozilla.org/en-US/docs/Web/HTTP/Messages
*/

const char* SCHEME = "https://";
const char* DISCORD_CHANNELS_ENDPOINT = "/api/channels/";
const char* DISCORD_MESSAGES_ENDPOINT = "/messages/";
const char* DISCORD_HOST = "discord.com";
const uint16_t HTTPS_PORT = 443;

unsigned long long previous_last_message_id = 0;
bool saved_last_message_id_once = false;

bool discord_channel_has_new_message() {
    const size_t LEN_OF_MESSAGE_ID = 20;  // 19 + 1
    char LAST_MESSAGE_ID[LEN_OF_MESSAGE_ID];
    unsigned char result = request('G', LAST_MESSAGE_ID, sizeof(LAST_MESSAGE_ID));  // request channel info to get the json value of "last_message_id"
    if (result != 0) {

        DEBUG_SERIAL.print("request result: ");
        DEBUG_SERIAL.println(result);

        return false;
    }

    DEBUG_SERIAL.print("last_message_id: ");
    DEBUG_SERIAL.println(LAST_MESSAGE_ID);

    unsigned long long new_last_message_id = strtoull(LAST_MESSAGE_ID, NULL, 10);
    if (new_last_message_id == 0) {

        DEBUG_SERIAL.println("unable to parse last_message_id");

        return false;
    }

    // if the last discord message id is the same as the previous one, it's probably a deleted message
    if (previous_last_message_id == new_last_message_id) {

        DEBUG_SERIAL.println("old id == new id");
        DEBUG_SERIAL.println(new_last_message_id);
        DEBUG_SERIAL.println(previous_last_message_id);

        return false;
    }

    DEBUG_SERIAL.println("old id != new id");
    DEBUG_SERIAL.print("new_last_message_id: ");
    DEBUG_SERIAL.println(new_last_message_id);
    DEBUG_SERIAL.print("previous_last_message_id: ");
    DEBUG_SERIAL.println(previous_last_message_id);

    previous_last_message_id = new_last_message_id;

    // ensure we store the last message id at least once
    if (!saved_last_message_id_once) {

        DEBUG_SERIAL.println("storing last_message_id at least once");

        save_message_id(new_last_message_id);
        saved_last_message_id_once = true;
    }
    return true;
}

void save_message_id(unsigned long long value) {
    EEPROM.begin(512);
    EEPROM.put(0, value);
    EEPROM.commit();
    EEPROM.end();
}

void load_message_id() {
    EEPROM.begin(512);
    EEPROM.get(0, previous_last_message_id);
    EEPROM.end();
}

// get the latest discord message, delete it if possible and store the message id in flash
// - returns true if there was a deletable message
bool discord_channel_had_message() {
    if (!discord_channel_has_new_message()) {
        return false;
    }
    unsigned char result = discord_delete_last_message();
    if (result != 0) {

        DEBUG_SERIAL.print("unable to delete last msg, result: ");
        DEBUG_SERIAL.println(result);

        return false;
    }

    DEBUG_SERIAL.print("successfully deleted last msg with id: ");
    DEBUG_SERIAL.println(previous_last_message_id);
    DEBUG_SERIAL.println("storing id of deleted msg");

    // store the message id in flash so we don't wake the device everytime the ESP powers on
    save_message_id(previous_last_message_id);
    return true;
}

// returns 0 if successfull
unsigned char discord_delete_last_message() {
    char LAST_MESSAGE_ID[1] = { 0 };
    return request('D', LAST_MESSAGE_ID, 1);  // delete the message with id of previous_last_message_id
}

// returns 0 if successfull
unsigned char request(char method, char* output, size_t output_size) {
    // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/client-class.html
    // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/bearssl-client-secure-class.html
    WiFiClientSecure client;
    client.setInsecure();
    client.setNoDelay(true);

    if (!client.connect(DISCORD_HOST, HTTPS_PORT)) {

        DEBUG_SERIAL.println("connection failed");

        client.stop();
        return 1;
    }

    switch (method) {
        case 'G':
            client.print("GET ");
            break;
        case 'D':
            client.print("DELETE ");
            break;
    }
    client.print(SCHEME);
    client.print(DISCORD_HOST);
    client.print(DISCORD_CHANNELS_ENDPOINT);
    client.print(SECRET_CHANNEL_ID);
    if (method == 'D') {
        client.print(DISCORD_MESSAGES_ENDPOINT);
        client.print(previous_last_message_id);
    }
    client.print(' ');
    client.print("HTTP/1.1");
    client.println();
    client.print("Host:");
    client.print(DISCORD_HOST);
    client.println();
    client.print("Accept:application/json");
    client.println();
    client.print("Authorization:Bot ");
    client.print(SECRET_BOT_TOKEN);
    client.println();
    client.print("User-Agent:discord_wake_on_lan");
    client.println();
    client.print("Connection:close");
    client.println();
    client.println();

    while (client.connected()) {
        DEBUG_SERIAL.println("still connected");
        delay(750);
    }

// DEBUGGING print whole response
#if DEBUG_HTTP_RESPONSES
    char LINE[512] = { 0 };
    while (true) {
        if (0 == client.readBytesUntil('\r', LINE, sizeof(LINE))) {

            DEBUG_SERIAL.println("end_of_response");

            client.stop();
            return 2;
        }
        DEBUG_SERIAL.print(LINE);
    }
#endif

    // Check HTTP status
    char status[32] = { 0 };
    if (0 == client.readBytesUntil('\r', status, sizeof(status))) {
        // could not read HTTP status, something is wrong..
        // NOTE: cloudflare response is missing HEADERS if we send a bad request
        client.stop();
        return 3;
    }

    if (method == 'D') {
        if (strcmp(status, "HTTP/1.1 204 No Content") != 0) {

            DEBUG_SERIAL.print("http_status: ");
            DEBUG_SERIAL.println(status);

            client.stop();
            return 4;
        }
        client.stop();
        return 0;
    }

    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {

        DEBUG_SERIAL.print("http_status: ");
        DEBUG_SERIAL.println(status);

        client.stop();
        return 5;
    }

    DEBUG_SERIAL.println("HTTP status 200");

    // Skip HTTP headers
    char end_header[] = "\r\n\r\n";
    if (!client.find(end_header)) {

        DEBUG_SERIAL.println("invalid_response");

        client.stop();
        return 6;
    }

    if (client.findUntil("last_message_id\":\"", "\0")) {
        size_t n_bytes_read = client.readBytesUntil('"', output, output_size - 1);
        output[n_bytes_read] = '\0';

#if DEBUG
        DEBUG_SERIAL.print("B read ");
        DEBUG_SERIAL.println(n_bytes_read);
        DEBUG_SERIAL.print("last_message_id: ");
        for (size_t i = 0; i < n_bytes_read; i++) {
            DEBUG_SERIAL.print(output[i]);
        }
        DEBUG_SERIAL.println();
#endif

    } else {

        DEBUG_SERIAL.println("last_message_id not found");

        client.stop();
        return 7;
    }
    client.stop();
    return 0;
}

void blink_led(uint8_t n_times) {
    digitalWrite(LED_BUILTIN, HIGH);  // turn the LED off.(Note that LOW is the voltage level but actually
                                      // the LED is on; this is because it is acive low on the ESP8266.
    for (uint8_t i = 0; i < n_times; i++) {
        digitalWrite(LED_BUILTIN, LOW);  // turn the LED on.
        delay(100);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
    }
}

bool wifi_connection_attempt() {
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PW);
    uint8_t timeout_counter = 0;
    while (WiFi.status() != WL_CONNECTED) {
        timeout_counter++;
        if (timeout_counter >= 50) {
#if USE_BUILTIN_LED
            blink_led(1);
#endif
            return false;
        }
        DEBUG_SERIAL.print("!w");
        delay(100);
    }
    return true;
}

void setup() {
#if USE_BUILTIN_LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);  // turn off LED
#endif
#if DEBUG
    DEBUG_SERIAL.begin(9600);
#endif
    while (!wifi_connection_attempt()) {
        delay(1000);
    }
    load_message_id();

    DEBUG_SERIAL.print("loaded last message id: ");
    DEBUG_SERIAL.println(previous_last_message_id);
}

bool must_wake_device() {
    return discord_channel_had_message();
}

void wake_device() {
    WiFiUDP UDP;
    WakeOnLan WOL(UDP);
    WOL.sendMagicPacket(SECRET_MAC, sizeof(SECRET_MAC));
    UDP.stop();
}

void loop() {
    DEBUG_SERIAL.println();
    if (must_wake_device()) {
        DEBUG_SERIAL.println("wake up server");
#if USE_BUILTIN_LED
        blink_led(4);
#endif
        wake_device();
    }
    DEBUG_SERIAL.println();
    delay(30000);
    if (WiFi.status() != WL_CONNECTED) {
        while (!wifi_connection_attempt()) {
            delay(1000);
        }
    }
}