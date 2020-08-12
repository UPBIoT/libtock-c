#include "esp_serial.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>

typedef struct {
  int error;
  int data1;
  int data2;
  bool done;
} CallbackReturn;

static user_callback *receive_buffer_callback = NULL;

static uint8_t *tx_buffer = NULL;
static uint8_t *rx_buffer = NULL;
static size_t tx_buffer_len = 0;
static size_t rx_buffer_len = 0;
static uint8_t *user_buffer = NULL;

static void write_callback(int error,
                           int data1,
                           __attribute__ ((unused)) int data2,
                           void* ud) {
  CallbackReturn *ret = (CallbackReturn*)ud;
  ret->error = error;
  ret->data1 = data1;
  ret->done  = true;
}

static void read_callback(int error,
                          int data1,
                          __attribute__ ((unused)) int data2,
                          void* ud) {
  CallbackReturn *ret = (CallbackReturn*)ud;
  ret->error = error;
  ret->data1 = data1;
  ret->done  = true;
  if (error == 0 && data1 != 0) {
    memcpy(user_buffer, rx_buffer, data1);
    memset(rx_buffer, 0, 64);
    if (receive_buffer_callback) receive_buffer_callback (user_buffer, data1);
  }
}


int esp_subscribe(subscribe_cb cb, void *userdata, size_t cb_type)
{
    return subscribe(DRIVER_NUM_ESP_SERIAL, cb_type, cb, userdata);
}

int esp_allow(void* ptr, size_t buffer_type, size_t size)
{
    return allow(DRIVER_NUM_ESP_SERIAL, buffer_type, ptr, size);
}

int esp_command(int command_num, size_t data1, size_t data2)
{
    return command(DRIVER_NUM_ESP_SERIAL, command_num, data1, data2);
}

int check_response(void)
{
    // todo
    return 0;
}

int subscribe_user_callback (user_callback* cb, void* ud)
{
    receive_buffer_callback = cb;
    return esp_subscribe(cb != NULL ? read_callback : NULL, ud, 2);
}

int send_command (int command_num, int wait_for_response, int link_id)
{
    CallbackReturn cbret;
    cbret.done = false;
    // register the write_callback to be called
    esp_subscribe (write_callback, &cbret, 1);
    // execute command
    cbret.error = esp_command(command_num, strlen((char*)tx_buffer), link_id);
    // wait for the callback to be called
    if (cbret.error == TOCK_SUCCESS) yield_for(&cbret.done);
    // unsubscribe the callback
    esp_subscribe (NULL, NULL, 1);
    // are we waiting for sth special? (link_id for example)
    if (wait_for_response) {
        return cbret.data1;
    } else return cbret.error;
}

int receive_command (int type, int wait_yield)
{
    CallbackReturn cbret;
    cbret.done = false;
    // subscribe the read callback
    esp_subscribe (read_callback, &cbret, 2);
    // wait for the callback to be called
    if (wait_yield) {
        yield_for (&cbret.done);
    }

    if (cbret.error == TOCK_SUCCESS) {
        // are we waiting for some special string? (like ip_address)
        if (type) {
            // get ip and save it in rx_buffer
            return 0;
            // else - we wait for data in rx_buffer, we check rx_buffer then
        } else return check_response();
        // else we were waiting for confirmation (OK, EROOR, etc);
    } else return cbret.error;
}

int connect_to_wifi (char* ssid, char* password, int link_id)
{
    // share tx_buffer, we assume we connect to the internet first
    if (tx_buffer != NULL) {
        return TOCK_EALREADY;
    } else {
        tx_buffer = (uint8_t*) calloc (64, sizeof(uint_fast8_t));
        if (tx_buffer != NULL) {
            tx_buffer_len = 64;
            int ret = esp_allow(tx_buffer, 1, 64);
            if (ret != TOCK_SUCCESS) return ret;
        } else {
            return TOCK_FAIL;
        }
    }
    // send the connection to wifi command
    memset(tx_buffer, 0, 64);
    snprintf((char*) tx_buffer, 14, "%s\n\r", SET_WIFI_COMMAND);
    int ret = send_command(GENERAL_COMMAND_NUMBER, 0, link_id);
    
    if (ret == TOCK_SUCCESS) {
        memset(tx_buffer, 0, 64);
        // send the actual command with ssid and password
        snprintf((char*) tx_buffer, 17 + strlen(ssid) + strlen(password), "%s\"%s\",\"%s\"\n\r", WF_COMMAND, ssid, password);
        ret = send_command(GENERAL_COMMAND_NUMBER, 0, link_id);
    }
    return ret;
}

int bind (const char* ip_address, int port_dest, int* port_src, int* link_id)
{
    int ret;
    if (tx_buffer != NULL) {
        return TOCK_EALREADY;
    } else {
        tx_buffer = (uint8_t*) calloc (64, sizeof(uint_fast8_t));
        if (tx_buffer != NULL) {
            tx_buffer_len = 64;
            ret = esp_allow(tx_buffer, 1, 64);
            if (ret != TOCK_SUCCESS) return ret;
        } else {
            return TOCK_FAIL;
        }
    }

    if (rx_buffer != NULL) {
        return TOCK_EALREADY;
    } else {
        rx_buffer = (uint8_t*) calloc (64, sizeof(uint_fast8_t));
        user_buffer = (uint8_t*) calloc (64, sizeof(uint_fast8_t));
        if (rx_buffer != NULL) {
            rx_buffer_len = 64;
            ret = esp_allow(rx_buffer, 2, 64);
            if (ret != TOCK_SUCCESS) return ret;
        } else {
            return TOCK_FAIL;
        }
    }

    // get first available link_id from capsule (successWithValue)
    *link_id = esp_command(LINK_ID_COMMAND_NUMBER, 0, 0); 
    // send CIPMUX=1 command to enable multiple connections
    memset(tx_buffer, 0, 64);
    snprintf((char*) tx_buffer, 14, "%s\r\n", CONNECTION_TYPE_COMMAND);
    ret = send_command(BIND_COMMAND_NUMBER, 0, *link_id);

    if (ret == TOCK_SUCCESS) {
        // send CIPSTART_COMMAND
        memset(tx_buffer, 0, 64);
        if (*port_src == 0) {
            *port_src = rand() % 5000;
        }
        snprintf((char*) tx_buffer, 35 + strlen(ip_address), "%s%d,\"UDP\",\"%s\",%d,%d\r\n", START_COMMAND, *link_id, ip_address, port_dest, *port_src);
        // still dunno what we're waiting for here?
        ret = send_command(BIND_COMMAND_NUMBER, 0, *link_id);
        if (ret == 0) {
            return receive_command(0, 0);
        }
    }
    return ret;
}

int send_UDP_payload (size_t len, const char* str, int link_id)
{
    memset(tx_buffer, 0, 64);
    // save the payload and send the command
    snprintf((char*) tx_buffer, 20 + len, "%s%d,%d\r\n%s\r\n", SEND_COMMAND, link_id, len, str);
    return send_command(GENERAL_COMMAND_NUMBER, 0, link_id);
}

int get_esp_ip (void)
{
    memset(tx_buffer, 0, 64);
    snprintf((char*) tx_buffer, 11, "%s\r\n", GET_IP_COMMAND);
    int ret = send_command (GENERAL_COMMAND_NUMBER, 1, 0);
    if (ret == TOCK_SUCCESS) {
        // return receive_command(1);
    }
    return ret;
}

int close_socket (int link_id)
{
    memset(tx_buffer, 0, 64);
    snprintf((char*) tx_buffer, 16, "%s\r\n", CLOSE_COMMAND);
    return esp_command(CLOSE_COMMAND_NUMBER, 64, link_id);
}

uint8_t* get_tx_buffer (void)
{
    return tx_buffer;
}

uint8_t* get_rx_buffer (void)
{
    return rx_buffer;
}