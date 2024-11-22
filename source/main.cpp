/* Sockets Example
 * Copyright (c) 2016-2020 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "wifi_helper.h"
#include "mbed-trace/mbed_trace.h"
#include <Fingerprint.h>
#include <utility>
#include <string>
#include <cstdio>

#if MBED_CONF_APP_USE_TLS_SOCKET
#include "root_ca_cert.h"

#ifndef DEVICE_TRNG
#error "mbed-os-example-tls-socket requires a device which supports TRNG"
#endif
#endif // MBED_CONF_APP_USE_TLS_SOCKET


///////////////////////////
//////   NETWORK   ////////
///////////////////////////

class Net {
    static constexpr size_t MAX_NUMBER_OF_ACCESS_POINTS = 10;
public:
    Net() : _net(NetworkInterface::get_default_instance())
    {
    }

    ~Net()
    {
        if (_net) {
            _net->disconnect();
        }
    }

    NetworkInterface* get_netif() {
        return _net;
    }

    void preinit()
    {
        if (!_net) {
            printf("Error! No network interface found.\r\n");
            return;
        }

        /* if we're using a wifi interface run a quick scan */
        if (_net->wifiInterface()) {
            /* the scan is not required to connect and only serves to show visible access points */
            wifi_scan();

            /* in this example we use credentials configured at compile time which are used by
             * NetworkInterface::connect() but it's possible to do this at runtime by using the
             * WiFiInterface::connect() which takes these parameters as arguments */
        }

        /* connect will perform the action appropriate to the interface type to connect to the network */

        printf("Connecting to the network...\r\n");

        nsapi_size_or_error_t result = _net->connect();
        if (result != 0) {
            printf("Error! _net->connect() returned: %d\r\n", result);
            return;
        }

        print_network_info();
    }

    void wifi_scan()
    {
        WiFiInterface *wifi = _net->wifiInterface();

        WiFiAccessPoint ap[MAX_NUMBER_OF_ACCESS_POINTS];

        /* scan call returns number of access points found */
        int result = wifi->scan(ap, MAX_NUMBER_OF_ACCESS_POINTS);

        if (result <= 0) {
            printf("WiFiInterface::scan() failed with return value: %d\r\n", result);
            return;
        }

        printf("%d networks available:\r\n", result);

        for (int i = 0; i < result; i++) {
            printf("Network: %s secured: %s BSSID: %hhX:%hhX:%hhX:%hhx:%hhx:%hhx RSSI: %hhd Ch: %hhd\r\n",
                   ap[i].get_ssid(), get_security_string(ap[i].get_security()),
                   ap[i].get_bssid()[0], ap[i].get_bssid()[1], ap[i].get_bssid()[2],
                   ap[i].get_bssid()[3], ap[i].get_bssid()[4], ap[i].get_bssid()[5],
                   ap[i].get_rssi(), ap[i].get_channel());
        }
        printf("\r\n");
    }

    void print_network_info()
    {
        /* print the network info */
        SocketAddress a;
        _net->get_ip_address(&a);
        printf("IP address: %s\r\n", a.get_ip_address() ? a.get_ip_address() : "None");
        _net->get_netmask(&a);
        printf("Netmask: %s\r\n", a.get_ip_address() ? a.get_ip_address() : "None");
        _net->get_gateway(&a);
        printf("Gateway: %s\r\n", a.get_ip_address() ? a.get_ip_address() : "None");
    }
private:
    NetworkInterface *_net;
};


///////////////////////////
//////   SOCKETS   ////////
///////////////////////////


class SocketDemo {
    static constexpr size_t MAX_MESSAGE_RECEIVED_LENGTH = 1000;

#if MBED_CONF_APP_USE_TLS_SOCKET
    static constexpr size_t REMOTE_PORT = 443; // tls port
#else
    static constexpr size_t REMOTE_PORT = 80; // standard HTTP port
#endif // MBED_CONF_APP_USE_TLS_SOCKET

public:
    SocketDemo(NetworkInterface* net) : _net(net)
    {
    }

    ~SocketDemo()
    {
        if (_net) {
            _net->disconnect();
        }
    }

    void initSocket() {
#if MBED_CONF_APP_USE_TLS_SOCKET
        nsapi_size_or_error_t result = _socket.set_root_ca_cert(root_ca_cert);
        if (result != NSAPI_ERROR_OK) {
            printf("Error: _socket.set_root_ca_cert() returned %d\n", result);
            return;
        }
        _socket.set_hostname(MBED_CONF_APP_HOSTNAME);
#endif // MBED_CONF_APP_USE_TLS_SOCKET

        /* opening the socket only allocates resources */
        result = _socket.open(_net);
        if (result != 0) {
            printf("Error! _socket.open() returned: %d\r\n", result);
            return;
        }
    }

    void connectSocket(SocketAddress& address) {
        /* we are connected to the network but since we're using a connection oriented
        * protocol we still need to open a connection on the socket */
        printf("Opening connection to remote port %d\r\n", REMOTE_PORT);
        nsapi_size_or_error_t result = _socket.connect(address);
        if (result != 0) {
            printf("Error! _socket.connect() returned: %d\r\n", result);
            return;
        }
    }

    void closeSocket() {
        _socket.set_timeout(0); // Force TLS connection reset
        _socket.close();
        // ThisThread::sleep_for(100ms);
    }

    void apiPing() {
        initSocket();
        SocketAddress address;
        if (!resolve_hostname(address)) {
            closeSocket();
            return;
        }
        address.set_port(REMOTE_PORT);
        connectSocket(address);

        if (!send_http_ping_request()) {
            closeSocket();
            return;
        }

        if (!receive_http_ping_response()) {
            closeSocket();
            return;
        }

        closeSocket();
    }

    std::pair<int, char*> apiPOST(const char* endpoint, const char* json_data) {
        initSocket();
        SocketAddress address;

        if (!resolve_hostname(address)) {
            closeSocket();
            return {-1, nullptr}; 
        }
        
        address.set_port(REMOTE_PORT);
        connectSocket(address);
        printf("Sending HTTP POST Request to %s...\r\n", endpoint);
        if (!send_http_post_request(endpoint, json_data)) {
            closeSocket();
            return {-1, nullptr};  // Return an error code (-1) and an empty string if the POST request fails
        }

        printf("Waiting for HTTP POST Response...\r\n");
        std::pair<int, char*> response = receive_http_post_response();
        int status_code = response.first;
        char* raw_response = response.second;

        if (status_code < 0 || !raw_response) {
            closeSocket();
            return {status_code, nullptr};  // Return the status code and an empty string if the response is invalid
        }

        closeSocket();
        return {status_code, raw_response};  // Return the status code and the response body as a pair
    }

private:
    bool resolve_hostname(SocketAddress &address)
    {
        const char hostname[] = MBED_CONF_APP_HOSTNAME;

        /* get the host address */
        printf("\nResolve hostname %s\r\n", hostname);
        nsapi_size_or_error_t result = _net->gethostbyname(hostname, &address);
        if (result != 0) {
            printf("Error! gethostbyname(%s) returned: %d\r\n", hostname, result);
            return false;
        }

        printf("%s address is %s\r\n", hostname, (address.get_ip_address() ? address.get_ip_address() : "None") );

        return true;
    }

    bool send_http_ping_request()
    {
        /* loop until whole request sent */
        const char buffer[] = "GET /api/ping HTTP/1.1\r\n"
                              "Host: iot-auth-project.rezo-rm.fr\r\n"
                              "Connection: close\r\n"
                              "\r\n";

        nsapi_size_t bytes_to_send = strlen(buffer);
        nsapi_size_or_error_t bytes_sent = 0;

        printf("\r\nSending message: \r\n%s", buffer);

        while (bytes_to_send) {
            bytes_sent = _socket.send(buffer + bytes_sent, bytes_to_send);
            if (bytes_sent < 0) {
                printf("Error! _socket.send() returned: %d\r\n", bytes_sent);
                return false;
            } else {
                printf("sent %d bytes\r\n", bytes_sent);
            }

            bytes_to_send -= bytes_sent;
        }

        printf("Complete message sent\r\n");

        return true;
    }

    bool receive_http_ping_response() {
        char buffer[MAX_MESSAGE_RECEIVED_LENGTH];
        int remaining_bytes = MAX_MESSAGE_RECEIVED_LENGTH;
        int received_bytes = 0;

        /* Loop until there is nothing received or we've run out of buffer space */
        nsapi_size_or_error_t result = remaining_bytes;
        while (result > 0 && remaining_bytes > 0) {
            result = _socket.recv(buffer + received_bytes, remaining_bytes);
            if (result < 0) {
                printf("Error! _socket.recv() returned: %d\r\n", result);
                return false;
            }

            received_bytes += result;
            remaining_bytes -= result;
        }

        /* Null-terminate the received data to safely work with it as a string */
        if (received_bytes < MAX_MESSAGE_RECEIVED_LENGTH) {
            buffer[received_bytes] = '\0';
        } else {
            buffer[MAX_MESSAGE_RECEIVED_LENGTH - 1] = '\0';
        }

        /* Find the start of the JSON payload by looking for the end of the headers */
        char *json_start = strstr(buffer, "\r\n\r\n");
        if (json_start) {
            json_start += 4; // Move past the "\r\n\r\n" to the start of the JSON body

            printf("Received JSON response:\r\n%s\r\n", json_start);

            // Optional: If you want to check the JSON content
            if (strstr(json_start, "\"message\":\"pong\"")) {
                printf("JSON contains message: pong\r\n");
            }
        } else {
            printf("Error: No JSON payload found.\r\n");
            printf("Full response:\r\n%s\r\n", buffer);  // Debug: print the full response for inspection
            return false;
        }

        return true;
    }

    bool send_http_post_request(const char* endpoint, const char* json_data)
    {
        // Construct the HTTP POST request with the JSON data
        char buffer[1024];
        snprintf(buffer, sizeof(buffer),
                "POST %s HTTP/1.1\r\n"
                "Host: iot-auth-project.rezo-rm.fr\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s", 
                endpoint, strlen(json_data), json_data);
                
        nsapi_size_t bytes_to_send = strlen(buffer);
        nsapi_size_or_error_t bytes_sent = 0;

        printf("\r\nSending POST request:\r\n%s", buffer);

        while (bytes_to_send) {
            bytes_sent = _socket.send(buffer + bytes_sent, bytes_to_send);
            if (bytes_sent < 0) {
                printf("Error! _socket.send() returned: %d\r\n", bytes_sent);
                return false;
            } else {
                printf("Sent %d bytes\r\n", bytes_sent);
            }

            bytes_to_send -= bytes_sent;
        }

        printf("Complete POST request sent\r\n");

        return true;
    }

    std::pair<int, char*> receive_http_post_response() {
        char buffer[MAX_MESSAGE_RECEIVED_LENGTH];
        int remaining_bytes = MAX_MESSAGE_RECEIVED_LENGTH;
        int received_bytes = 0;
        nsapi_size_or_error_t result = remaining_bytes;
        while (result > 0 && remaining_bytes > 0) {
            result = _socket.recv(buffer + received_bytes, remaining_bytes);
            if (result < 0) {
                printf("Error! _socket.recv() returned: %d\r\n", result);
                return std::make_pair(-1, nullptr);
            }
            received_bytes += result;
            remaining_bytes -= result;
        }

        // Null-terminate the received data to safely work with it as a string
        if (received_bytes < MAX_MESSAGE_RECEIVED_LENGTH) {
            buffer[received_bytes] = '\0';
        } else {
            buffer[MAX_MESSAGE_RECEIVED_LENGTH - 1] = '\0';
        }

        char* cloneBuffer = new char[strlen(buffer) + 1]; // Allocate memory for the clone
        std::strcpy(cloneBuffer, buffer); // Copy the content

        // Parse the HTTP response status code
        int status_code = -1;
        char* status_line = strtok(buffer, "\r\n");
        if (status_line) {
            char* token = strtok(status_line, " ");
            token = strtok(nullptr, " ");
            if (token) {
                status_code = atoi(token);
            }
        }

        // Find the start of the JSON payload by looking for the end of the headers
        char* json_start = strstr(cloneBuffer, "\r\n\r\n");
        if (json_start) {
            json_start += 4; // Move past the "\r\n\r\n" to the start of the JSON body
            printf("Received JSON response:\r\n%s\r\n", json_start);
            return std::make_pair(status_code, json_start);
        } else {
            printf("Error: No JSON payload found.\r\n");
            printf("Full response:\r\n%s\r\n", cloneBuffer); // Debug: print the full response for inspection
            return std::make_pair(status_code, nullptr);
        }
    }

#if MBED_CONF_APP_USE_TLS_SOCKET
    TLSSocket _socket;
#else
    TCPSocket _socket;
#endif // MBED_CONF_APP_USE_TLS_SOCKET
    NetworkInterface* _net;
};

//////////////////////////////
//////    KEYBOARD    ////////
//////////////////////////////


// Définition des broches
BusOut columns(D7, D6, D5, D4);  // Colonnes du clavier 4x4
BusIn rows(D11, D10, D9, D8);    // Lignes du clavier 4x4


// PIN led clavier (anti doublon) (y4 correspond au pin le plus à droite lorsque l'on regarde le clivier de face)
DigitalOut led_clavier(LED1);

// PINS clavier
DigitalIn y4(D11);
DigitalIn y3(D10);
DigitalIn y2(D9);
DigitalIn y1(D8);

DigitalOut x4(D7);
DigitalOut x3(D6);
DigitalOut x2(D5);
DigitalOut x1(D4);

/*
DigitalIn y4(D4);
DigitalIn y3(D5);
DigitalIn y2(D6);
DigitalIn y1(D7);

DigitalOut x4(D8);
DigitalOut x3(D9);
DigitalOut x2(D10);
DigitalOut x1(D11);
*/

DigitalIn lignes_in[] = {y1,y2,y3,y4};
DigitalOut colones_out[] = {x1,x2,x3,x4};

// Tableau des touches correspondant au clavier EOZ 4x4
char keys[4][4] = {
    {'1', '2', '3', 'F'},
    {'4', '5', '6', 'E'},
    {'7', '8', '9', 'D'},
    {'A', '0', 'B', 'C'}
};

void scan_keypad()
{

    for (int col = 0; col < 4; col++) {
        // Active une colonne à la fois
        columns = ~(1 << col); // Met la colonne courante à 0 et les autres à 1

        // Lecture des lignes
        for (int row = 0; row < 4; row++) {
            if (!(rows.read() & (1 << row))) { // Si la ligne est active (état bas)
                ThisThread::sleep_for(50ms);
                printf("Key pressed: %c\n\r", keys[row][col]);
                while (!(rows.read() & (1 << row))) {
                    // Attente que la touche soit relâchée
                }
                break;
            }
        }
    }
}


char scan_keypad_mine()
{
    int counter = 0;
    int last_col = -1;
    int last_row = -1;
    // on active une à une chaque colone
    for (int col = 0; col < 4; col++) {
        colones_out[col] = 0;
        //ThisThread::sleep_for(1000ms);
        // pour chaque ligne on regarde la valeur de la pin
        for (int row = 0; row < 4; row++) {
            // si la pin est active alors 
            //printf("%d", lignes_in[row].read() ? 0 : 1);
            if(!lignes_in[row].read()){
                counter +=1;
                last_col = col;
                last_row = row;
            }
            /*
            if (!lignes_in[row].read()) {
                ThisThread::sleep_for(50ms);
                // on affiche le caractère pressé
                printf("Key pressed: %c\n\r", keys[row][col]);
                // et on attend qu'il soit relaché
            }
            */
        }
        colones_out[col] = 1;
        //printf("\n");
    }
    //printf("\ncounter = %d\n", counter);
    //printf("\n");
    if (counter == 1){
        printf("Key pressed: %c\n\r", keys[last_row][last_col]);
        return keys[last_row][last_col];
    }
    return '\0';
}

char scan_keypad_mine_clean()
{
    // on s'assure que les pins soit initialement en état haut
    for (int i = 0; i < 4; i++) {colones_out[i] = 1;}

    // on initialise les variable d'états
    int counter = 0; // nombre de bouton lu comme activé
    int last_col = -1; // position en colone du dernier bouton activé lu
    int last_row = -1; // position en ligne du dernier bouton activé lu

    // on désactive une à une chaque colone
    for (int col = 0; col < 4; col++) {
        colones_out[col] = 0;

        // pour chaque ligne on regarde la valeur des pins d'entré
        for (int row = 0; row < 4; row++) {
            // si la pin est désactive alors c'est que le bouton à été appuié
            if(!lignes_in[row].read()){
                // on change les variables d'état en fonction de la position de la pin
                counter +=1;
                last_col = col;
                last_row = row;
            }
        }
        // on re-active la colone avant de passer à la suivante
        colones_out[col] = 1; 
    }
    // si le compteur à bien la valeur 1, alors on a qu'un seul bouton de pressé 
    if (counter == 1){
        return keys[last_row][last_col];
    }
    // TODO : gérer le cas où counter est supérieur à 1
    return '\0';
}

char scan_falling_char() {
    led_clavier = 0;
    char pressed = scan_keypad_mine_clean();
    if (pressed == '\0') {
        //rien n'a été pressé
    } else {
        while (scan_keypad_mine_clean() == pressed) {}
        //printf("Key pressed: %c\n\r", pressed);
    }
    return pressed;
}

void wait_for_key(char cara) {
    while (scan_keypad_mine_clean() != cara) {}
}

char wait_rising_edge() {
    char pressed = '\0';
    while (pressed == '\0') {pressed = scan_keypad_mine_clean();}
    return pressed;
}

char wait_falling_edge() {
    char pressed1 = '\0';
    while (pressed1 == '\0') {pressed1 = scan_keypad_mine_clean();}
    while (scan_keypad_mine_clean() == pressed1) {}
    return pressed1;
}

void wait_rising_edge_char(char car) {
    while (scan_keypad_mine_clean() != car) {}
}

void wait_falling_edge_char(char car) {
    while (scan_keypad_mine_clean() != car) {}
    while (scan_keypad_mine_clean() == car) {}
}

int main_clavier()
{
    printf("Starting keypadmine scanning\n\r");
    /*
    for (int i = 0; i < 4; i++) {colones_out[i] = 0;}
    ThisThread::sleep_for(1000ms);  // Petite pause pour éviter les rebonds
    for (int i = 0; i < 4; i++) {colones_out[i] = 1;}
    */
    while (true) {
        //scan_keypad_mine();                // Balayage du clavier
        scan_falling_char();
        ThisThread::sleep_for(100ms);  // Petite pause pour éviter les rebonds
    }
}


enum COLOR {
    WHITE = 'W',
    RED = 'R',
    BLUE = 'B',
    GREEN = 'G',
    PINK = 'P',
    YELLOW = 'Y',
    CYAN = 'C' // ORANGE
};

enum LIGHT {
    BLINK = 'B',
    SOLID = 'S'
};

#define TIMING_BLINK_KED 100ms

DigitalOut led_red(D1);
DigitalOut led_green(D2);
DigitalOut led_blue(D3);

//void led(enum COLOR color, enum LIGHT type, int num) { printf("led = %c | type = %c | num = %d\n", color, type, num);}
void led(enum COLOR color, enum LIGHT type, int num) { 
    printf("led = %c | type = %c | num = %d\n", color, type, num);
    int led_color[3] = {};
    switch (color) {
        case WHITE:
            led_color[0] = 1;
            led_color[1] = 1;
            led_color[2] = 1;
            break;
        case RED:
            led_color[0] = 1;
            led_color[1] = 0;
            led_color[2] = 0;
            break;
        case BLUE:
            led_color[0] = 0;
            led_color[1] = 0;
            led_color[2] = 1;
            break;
        case GREEN:
            led_color[0] = 0;
            led_color[1] = 1;
            led_color[2] = 0;
            break;
        case PINK:
            led_color[0] = 1;
            led_color[1] = 0;
            led_color[2] = 1;
            break;
        case YELLOW:
            led_color[0] = 1;
            led_color[1] = 1;
            led_color[2] = 0;
            break;
        case CYAN:
            led_color[0] = 0;
            led_color[1] = 1;
            led_color[2] = 1;
            break;
    }
    switch (type) {
        case BLINK:
            for (int i = 0; i < num; i ++) {
                led_red = led_color[0];
                led_green = led_color[1];
                led_blue = led_color[2];
                ThisThread::sleep_for(TIMING_BLINK_KED);
                led_red = 0;
                led_green = 0;
                led_blue = 0;
                ThisThread::sleep_for(TIMING_BLINK_KED);
            }
            break;
        case SOLID:
            led_red = led_color[0];
            led_green = led_color[1];
            led_blue = led_color[2];
            ThisThread::sleep_for(num * 1000ms);
            break;
    }
}

int reset = 0;

char wait_choice_key_falling(const char charsa[], size_t charsa_len){
    char waited_key = '\0';
    bool done = false;
    while(!done) {
        waited_key = wait_falling_edge();
        for( int i = 0 ; i < charsa_len; i ++){
            if (waited_key == charsa[i]) {
                done = true;
                break;
            }
        }
    }
    return waited_key;
}

const char INIT_KEYS[] = {'A','B'};



//////////////////////////////
//////   FINGERPINT   ////////
//////////////////////////////

DigitalOut ledV(LED1); // for various information without terminal

Fingerprint finger(PC_1,PC_0,0x0); // TX,TX,pass
InterruptIn fD(PB_0);               // IT from fingerprint detection (see datasheet WAKEUP)

uint8_t id=1;

bool fingerON=false;

DigitalIn btnBleu(PC_13);     // to start enroll (USER_BUTTON)



// interrupt funtion 
void fingerDetect(void)
{
    // ledV=1;
    // ThisThread::sleep_for(100ms); 
    // ledV=0;
    fingerON=true;
}



// original setup fonction on Arduino
void setup()
{
    fD.fall(&fingerDetect);
    printf("\nR503 Finger detect test\nSTM32 version with MBED compiler and library\n");

    // set the data rate for the sensor serial port
    finger.begin(57600);
    ThisThread::sleep_for(200ms);
    if (finger.verifyPassword()) {
        printf("\nFound fingerprint sensor!\n");
    } else {
        printf("\nDid not find fingerprint sensor -> STOP !!!!\n");
        while (1)
        {
            ledV=1;
            ThisThread::sleep_for(100ms); 
            ledV=0;
            ThisThread::sleep_for(100ms); 
        }
    }

    printf("Reading sensor parameters\n");
    finger.getParameters();
    printf("Status: 0x%X\n",finger.status_reg);
    printf("Sys ID: 0x%X\n",finger.system_id);
    printf("Capacity: %d\n",finger.capacity);
    printf("Security level: %d\n",finger.security_level);
    printf("Device address: 0x%X\n",finger.device_addr);
    printf("Packet len: %d\n",finger.packet_len);
    printf("Baud rate: %d\n",finger.baud_rate);

    finger.getTemplateCount();

    if (finger.templateCount == 0) {
        printf("Sensor doesn't contain any fingerprint data. Please run the 'enroll' example.\n");
    }
    else {
        printf("Waiting for valid finger...\n");
        printf("Sensor contains : %d templates\n",finger.templateCount);
    }

}

// --------------------------------------
uint8_t getFingerprintID() {
    uint8_t p = finger.getImage();
    switch (p) {
        case FINGERPRINT_OK:
            printf("Image taken\n");
            break;
        case FINGERPRINT_NOFINGER:
            printf("No finger detected\n");
            return p;
        case FINGERPRINT_PACKETRECIEVEERR:
            printf("Communication error\n");
            return p;
        case FINGERPRINT_IMAGEFAIL:
            printf("Imaging error\n");
            return p;
        default:
            printf("Unknown error\n");
            return p;
    }

  // OK success!

    p = finger.image2Tz();
    switch (p) {
        case FINGERPRINT_OK:
            printf("Image converted\n");
            break;
        case FINGERPRINT_IMAGEMESS:
            printf("Image too messy\n");
            return p;
        case FINGERPRINT_PACKETRECIEVEERR:
            printf("Communication error\n");
            return p;
        case FINGERPRINT_FEATUREFAIL:
            printf("Could not find fingerprint feature\n");
            return p;
        case FINGERPRINT_INVALIDIMAGE:
            printf("Could not find fingerprint features\n");
            return p;
        default:
            printf("Unknown error\n");
            return p;
    }

    // OK converted!
    p = finger.fingerSearch();
    if (p == FINGERPRINT_OK) {
        printf("Found a print match!\n");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
        printf("Communication error\n");
        return p;
    } else if (p == FINGERPRINT_NOTFOUND) {
        printf("Did not find a match\n");
        return p;
    } else {
        printf("Unknown error\n");
        return p;
    }

    // found a match!
    printf("Found ID #%d\n",finger.fingerID);
    printf(" with confidence of %d\n",finger.confidence);

    return finger.fingerID;
}

// returns -1 if failed, otherwise returns ID #
int getFingerprintIDez() {
    uint8_t p = finger.getImage();
    if (p != FINGERPRINT_OK)  return -1;

    p = finger.image2Tz();
    if (p != FINGERPRINT_OK)  return -1;

    p = finger.fingerFastSearch();
    if (p != FINGERPRINT_OK)  return -1;

    // found a match!
    printf("Found ID #%d\n",finger.fingerID);
    printf(" with confidence of %d\n",finger.confidence);
    return finger.fingerID;
}

void demoLED(void)
{
    // control (3 on)(4off), speed (0-255) , color (1 red, 2 blue, 3 purple), cycles (0 infinit,- 255)
    // LED fully on
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_RED);
    ThisThread::sleep_for(250ms);
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_BLUE);
    ThisThread::sleep_for(250ms);
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);
    ThisThread::sleep_for(250ms);

    // flash red LED
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
    ThisThread::sleep_for(2000ms);
}

void breathLED() {
    // Breathe blue LED till we say to stop
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE);
}

void breathLEDFast() {
    // Breathe blue LED till we say to stop faster
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 20, FINGERPRINT_LED_BLUE);
}

void purpleLED() {
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);
}

void redLED() {
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);
}

void blueLED() {
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_BLUE);
}

// enroll a fingerprint
uint8_t getFingerprintEnroll() 
{
    int p = -1;
    fD.fall(NULL); 
    printf("Waiting for valid finger to enroll as #%d\n",id);
    while (p != FINGERPRINT_OK) 
    {
        p = finger.getImage();
        switch (p) 
        {
            case FINGERPRINT_OK:
                printf("Image taken\n");
                purpleLED();
                ThisThread::sleep_for(250ms);
                breathLEDFast();
                break;
            case FINGERPRINT_NOFINGER:
                printf(".");
                blueLED();
                ThisThread::sleep_for(500ms);
                breathLEDFast();
                break;
            case FINGERPRINT_PACKETRECIEVEERR:
                printf("Communication error\n");
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                ThisThread::sleep_for(100ms);
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                break;
            case FINGERPRINT_IMAGEFAIL:
                printf("Imaging error\n");
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                ThisThread::sleep_for(100ms);
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                ThisThread::sleep_for(100ms);
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                break;
            default:
                printf("Unknown error\n");
                break;
        }
    }

    // OK success!

    p = finger.image2Tz(1);
    switch (p) {
        case FINGERPRINT_OK:
            printf("Image converted");
            break;
        case FINGERPRINT_IMAGEMESS:
            printf("Image too messy\n");
            return p;
        case FINGERPRINT_PACKETRECIEVEERR:
            printf("Communication error\n");
            return p;
        case FINGERPRINT_FEATUREFAIL:
            printf("Could not find fingerprint features\n");
            return p;
        case FINGERPRINT_INVALIDIMAGE:
            printf("Could not find fingerprint features\n");
            return p;
        default:
            printf("Unknown error\n");
            return p;
    }

    printf("Remove finger\n");
    ThisThread::sleep_for(200ms);
    p = 0;
    while (p != FINGERPRINT_NOFINGER) {
        p = finger.getImage();
    }
    printf("ID %d\n",id);
    p = -1;
    printf("Place same finger again\n");
    while (p != FINGERPRINT_OK) {
        p = finger.getImage();
        switch (p) {
            case FINGERPRINT_OK:
                printf("Image taken\n");
                purpleLED();
                ThisThread::sleep_for(250ms);
                breathLEDFast();
                break;
            case FINGERPRINT_NOFINGER:
                printf(".");
                blueLED();
                ThisThread::sleep_for(500ms);
                breathLEDFast();
                break;
            case FINGERPRINT_PACKETRECIEVEERR:
                printf("Communication error\n");
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                ThisThread::sleep_for(100ms);
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                break;
            case FINGERPRINT_IMAGEFAIL:
                printf("Imaging error\n");
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                ThisThread::sleep_for(100ms);
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                ThisThread::sleep_for(100ms);
                blueLED();
                ThisThread::sleep_for(200ms);
                breathLEDFast();
                break;
            default:
                printf("Unknown error\n");
                break;
        }
    }

    // OK success!

    p = finger.image2Tz(2);
    switch (p) {
        case FINGERPRINT_OK:
            printf("Image converted\n");
            break;
        case FINGERPRINT_IMAGEMESS:
            printf("Image too messy\n");
            return p;
        case FINGERPRINT_PACKETRECIEVEERR:
            printf("Communication error\n");
            return p;
        case FINGERPRINT_FEATUREFAIL:
            printf("Could not find fingerprint features\n");
            return p;
        case FINGERPRINT_INVALIDIMAGE:
            printf("Could not find fingerprint features\n");
            return p;
        default:
            printf("Unknown error\n");
            return p;
    }

    // OK converted!
    printf("Creating model for #%d\n",id); 

    p = finger.createModel();
    if (p == FINGERPRINT_OK) {
        printf("Prints matched!\n");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
        printf("Communication error\n");
        return p;
    } else if (p == FINGERPRINT_ENROLLMISMATCH) {
        printf("Fingerprints did not match\n");
        return p;
    } else {
        printf("Unknown error\n");
        return p;
    }

    printf("ID %d\n",id);
    p = finger.storeModel(id);
    if (p == FINGERPRINT_OK) {
        printf("Stored!\n");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
        printf("Communication error\n");
        return p;
    } else if (p == FINGERPRINT_BADLOCATION) {
        printf("Could not store in that location\n");
        return p;
    } else if (p == FINGERPRINT_FLASHERR) {
        printf("Error writing to flash\n");
        return p;
    } else {
        printf("Unknown error\n");
        return p;
    }
    fD.fall(&fingerDetect); 
    return id;
}

int main() {
    printf("\r\nStarting IOT-AUTH System\r\n\r\n");

#ifdef MBED_CONF_MBED_TRACE_ENABLE
    mbed_trace_init();
#endif

    setup();
    demoLED();
    finger.LEDcontrol(3,128,1,10);
    printf("\nPret ! \n");

    // Make a ping request to the serer
    Net net;
    net.preinit();
    SocketDemo *sckt = new SocketDemo(net.get_netif());
    MBED_ASSERT(sckt);
    sckt->apiPing();
    delete sckt;

    unsigned char c=1;
    breathLED();

    while (true) {
        reset = 0;

        //white led
        led(WHITE, SOLID, 0);
        //keycode A is pressed
        printf("waiting for A to be pressed\n");
        //char init_key = '\0';
        //while (init_key == 'A' || init_key == 'B') {init_key=wait_falling_edge();}
        char pressed_init_key = wait_choice_key_falling(INIT_KEYS, 2) ;
        if (pressed_init_key == 'A') {
            //Blue led 
            led(BLUE, BLINK, 1);
            char code[5] = {};
            int indice = 0;
            while (indice <= 5) {
                printf("waiting for number\n");
                char pressed = '\0';
                led(CYAN, BLINK, 1);
                // ces lettres sont cursed pour un raison inconnue
                while ( pressed == '\0' || pressed == 'C' || pressed == 'D' || pressed == 'E' || pressed == 'F') {
                    pressed = wait_falling_edge();
                }
                code[indice] = pressed;
                indice += 1;
                printf("indice %d | key = %c\n", indice, pressed);
                
                if (pressed == 'B') {
                    reset = 1;
                    break;
                }
            }
            if (reset == 1) {
                continue;
            }
            // check code
            // Make a POST request to the server
            net.preinit();
            sckt = new SocketDemo(net.get_netif());
            MBED_ASSERT(sckt);
            std::string json_payload = R"({
                "initcode": ")" + std::string(code) + R"(",
                "room": "Bouygues-sb123"
            })";
            std::pair<int, char*> response = sckt->apiPOST("/api/check", json_payload.c_str());
            delete sckt;

            int status_code = response.first;
            printf("Received %d", status_code);

            if (status_code != 200) {
                led(RED, BLINK, 1);
                continue;
            }

            char* json = response.second;
            printf("Received JSON response:\r\n%s\r\n", json);

            // Find the key "message"
            char* start = strstr(json, "\"message\":");
            if (start == NULL) {
                printf("Key 'message' not found!\n");
                return 1;
            }

            // Move the pointer past the "message": part to the value
            start = strchr(start, '\"') + 1; // Skip the first quote after "message"
            char* end = strchr(start, '\"');  // Find the closing quote of the value

            if (start && end) {
                // Extract the value (this assumes there's no extra spaces in the value)
                size_t length = end - start;
                char value[length + 1];
                strncpy(value, start, length);
                value[length] = '\0';  // Null-terminate the extracted value

                // Print the extracted value
                printf("Extracted value (remaining ident): %s\n", value);

                int intValue = atoi(value);

                // si le keycode a toujours des slot utilisable
                if (intValue > 0) {
                    printf("Fingerprint Enroll");
                    breathLEDFast();
                    c=getFingerprintEnroll();
                    breathLED();

                    // Make a POST request to the server
                    net.preinit();
                    sckt = new SocketDemo(net.get_netif());
                    MBED_ASSERT(sckt);
                    std::string json_payload = R"({
                        "initcode": ")" + std::string(code) + R"(",
                        "footprint": ")" + std::to_string(c) + R"(",
                        "room": "Bouygues-sb123"
                    })";
                    std::pair<int, char*> response = sckt->apiPOST("/api/ident", json_payload.c_str());
                    delete sckt;

                    int status_code = response.first;
                    printf("Received %d", status_code);

                    if (status_code == 200) {
                        printf("Registration finished\n");
                        led(GREEN, BLINK,3);
                        printf("waiting for A to be pressed\n");
                        while (wait_falling_edge() == 'A') {}
                    } else if (status_code == 401) {
                        printf("Unauthorized registration\n");
                        led(RED, BLINK,3);
                        continue;
                    } else {
                        printf("Server responded with status: %d", status_code);
                        led(RED, BLINK,3);
                        continue;
                    }
                }
            } else {
                printf("Failed to extract value.\n");
                continue;
            }

        } else if (pressed_init_key == 'B') {
            //left loop
            int res_statues = 0;
            //boucle pour s'assurer d'un bon scan
            while (scan_falling_char() != 'A') {
                led(CYAN, SOLID, 0);
                if(fingerON){
                    printf("Doigt detecte ! \n");     
                    purpleLED();
                    uint8_t id = getFingerprintID();
                    ThisThread::sleep_for(100ms); 
                    breathLED();
                    fingerON=false;

                    led(PINK, BLINK, 1);

                    // Make a POST request to the server
                    net.preinit();
                    sckt = new SocketDemo(net.get_netif());
                    MBED_ASSERT(sckt);
                    std::string json_payload = R"({
                        "footprint": ")" + std::to_string(id) + R"(",
                        "room": "Bouygues-sb123"
                    })";
                    std::pair<int, char*> response = sckt->apiPOST("/api/sign", json_payload.c_str());
                    delete sckt;

                    int status_code = response.first;
                    printf("Received %d", status_code);

                    if (status_code != 401) {
                        if (status_code != 200) {
                            return 404; // couldn't connect to server
                            
                            //printf("sending failed trying again");
                            //ThisThread::sleep_for(200ms);
                        }
                        led(GREEN, SOLID, 1);
                    }
                }
            }
        }
    }

    return 0;
}