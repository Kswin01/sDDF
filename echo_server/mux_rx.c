/* We need to determine direction of chars to get*/

#include <stdbool.h>
#include <stdint.h>
#include <sel4cp.h>
#include <sel4/sel4.h>
#include "serial.h"
#include "shared_ringbuffer.h"
#include <string.h>
#include "util.h"

#define CLI_CH 1
#define DRV_CH 11

#define NUM_CLIENTS 1

/* Memory regions as defined in the system file */

// Transmit rings with the driver
uintptr_t rx_avail_drv;
uintptr_t rx_used_drv;

// Transmit rings with the client
uintptr_t rx_avail_cli;
uintptr_t rx_used_cli;

uintptr_t shared_dma_rx_drv;
uintptr_t shared_dma_rx_cli;

// Have an array of client rings. 
ring_handle_t rx_ring;
ring_handle_t drv_rx_ring;

/* We need to do some processing of the input stream to determine when we need 
to change direction. */

/* To switch input direction, type the "@" symbol followed immediately by a number.
Otherwise, can put "\" before "@" to escape this.*/

int escape_character;
int client;
int num_to_get_chars;

int give_char(int curr_client, char got_char) {
    if (num_to_get_chars <= 0) {
        return 1;
    }
    sel4cp_dbg_puts("In the give char function\n");
    // Address that we will pass to dequeue to store the buffer address
    uintptr_t buffer = 0;
    // Integer to store the length of the buffer
    unsigned int buffer_len = 0; 

    void *cookie = 0;

    sel4cp_dbg_puts("This is the char we have in give_char: ");
    sel4cp_dbg_puts(&got_char);
    sel4cp_dbg_puts("\n");

    sel4cp_dbg_puts("Attempting to dequeue from rx avail ring\n");
    int ret = dequeue_avail(&rx_ring, &buffer, &buffer_len, &cookie);

    if (ret != 0) {
        // sel4cp_dbg_puts(sel4cp_name);
        sel4cp_dbg_puts(": unable to dequeue from the rx available ring\n");
        return;
    }

    sel4cp_dbg_puts("Attempting to copy char to buffer\n");

    ((char *) buffer)[0] = (char) got_char;

    sel4cp_dbg_puts("Finsihed copying to buffer\n");

    // Now place in the rx used ring
    ret = enqueue_used(&rx_ring, buffer, 1, &cookie);

    if (ret != 0) {
        // sel4cp_dbg_puts(sel4cp_name);
        sel4cp_dbg_puts(": unable to enqueue to the tx available ring\n");
        return 1;
    }

    num_to_get_chars -= 1;
    sel4cp_dbg_puts("Finished the give char function\n");
}


/* We will check for escape characters in here, as well as dealing with switching direction*/
void handle_rx(int curr_client) {
    sel4cp_dbg_puts("MUX rx we have recieved a request to get a character\n");
    // We want to request a character here, then busy wait until we get anything back

    // Address that we will pass to dequeue to store the buffer address
    uintptr_t buffer = 0;
    // Integer to store the length of the buffer
    unsigned int buffer_len = 0; 

    void *cookie = 0;

    // We can only be here if we have been notified by the driver
    int ret = dequeue_used(&drv_rx_ring, &buffer, &buffer_len, &cookie) != 0;
    if (ret != 0) {
        sel4cp_dbg_puts(sel4cp_name);
        sel4cp_dbg_puts(": getchar - unable to dequeue used buffer\n");
    }

    // We are only getting one character at a time, so we just need to cast the buffer to an int

    char got_char = *((char *) buffer);

    sel4cp_dbg_puts("MUX RX THIS IS THE CHARACTER WE GOT FROM THE DRIVER: ");
    sel4cp_dbg_puts(&got_char);
    sel4cp_dbg_puts("\n");

    /* Now that we are finished with the used buffer, we can add it back to the available ring*/

    ret = enqueue_avail(&drv_rx_ring, buffer, buffer_len, NULL);

    if (ret != 0) {
        sel4cp_dbg_puts(sel4cp_name);
        sel4cp_dbg_puts(": getchar - unable to enqueue used buffer back into available ring\n");
    }

    /* TO DO: Need to add in handling of white space and EOF */

    // We have now gotten a character, deal with the input direction switch
    if (escape_character == 1) {
        sel4cp_dbg_puts("Escape character is 1\n");
        // The previous character was an escape character
        give_char(curr_client, got_char);
        escape_character = 0;
    }  else if (escape_character == 2) {
        sel4cp_dbg_puts("CASE OF SWITCHING INPUT DIRECTION\n");
        // We are now switching input direction
        int new_client = atoi(&got_char);
        if (new_client < 1 || new_client > NUM_CLIENTS) {
            sel4cp_dbg_puts("Attempted to switch to invalid client number\n");
        } else {
            sel4cp_dbg_puts("Switching to different client\n");
            curr_client = new_client;
            client = curr_client;
        }
        escape_character = 0;
    } else if (escape_character == 0) {
        sel4cp_dbg_puts("Escape character is 0\n");
        // No escape character has been set
        if (got_char == '\\') {
            sel4cp_dbg_puts("WE GOT AN ESCAPE CHARACTER\n");
            escape_character = 1;
            // The next character is going to be escaped
        } else if (got_char == '@') {
            // We are changing input direction
            sel4cp_dbg_puts("We are switching input direction\n");
            sel4cp_dbg_puts("We need to get another character maybe?\n");
            escape_character = 2;
        } else {
            give_char(curr_client, got_char);
        }
    }

    sel4cp_dbg_puts("Finsihed the handle rx function\n");
}

void init (void) {
    // We want to init the client rings here. Currently this only inits one client
    ring_init(&rx_ring, (ring_buffer_t *)rx_avail_cli, (ring_buffer_t *)rx_used_cli, NULL, 0);
    ring_init(&drv_rx_ring, (ring_buffer_t *)rx_avail_drv, (ring_buffer_t *)rx_used_drv, NULL, 0);

    for (int i = 0; i < NUM_BUFFERS - 1; i++) {
        int ret = enqueue_avail(&drv_rx_ring, shared_dma_rx_drv + (i * BUFFER_SIZE), BUFFER_SIZE, NULL);

        if (ret != 0) {
            sel4cp_dbg_puts(sel4cp_name);
            sel4cp_dbg_puts(": mux rx buffer population, unable to enqueue buffer\n");
            return;
        }
    }

    // We initialise the current client to 1
    client = 1;
    // Set the current escape character to 0, we can't have recieved an escape character yet
    escape_character = 0;
    // No chars have been requested yet
    num_to_get_chars = 0;
    sel4cp_dbg_puts("mux rx init finished\n");  
}

void notified(sel4cp_channel ch) {
    sel4cp_dbg_puts("In the mux RX notified channel: ");
    // puthex64(ch);
    sel4cp_dbg_puts("\n");
    // We should only ever recieve notifications from the client
    // Sanity check the client
    if (ch == DRV_CH) {
        handle_rx(client);
    } else if (ch < 1 || ch > NUM_CLIENTS) {
        sel4cp_dbg_puts("Received a bad client channel\n");
        return;
    }  else {
        // This was recieved on a client channel. Index the number of characters to get
        num_to_get_chars += 1;
    }

}
