#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define BLINK_PATH "/dev/usb/blinkstick0"

#define YELLOW 0x111100
#define GREEN 0x001100
#define RED 0x110000

typedef FILE blink_dev_t;
typedef struct {
    unsigned int led;
    unsigned int color;
} blink_msg_t;

blink_dev_t* blink_init();
int blink_deinit(blink_dev_t* dev);
int send_to_driver(blink_dev_t* dev, blink_msg_t** msg_l, size_t len);

int main(int argc, char** argv) {
    int ret;
    blink_dev_t* blink_device = blink_init();
    if (blink_device == NULL) {
        return -1;
    }

    blink_msg_t* messages[] = {
        [0] = &(blink_msg_t){.led = 1, .color = GREEN},
        [1] = &(blink_msg_t){.led = 2, .color = GREEN},
        [2] = &(blink_msg_t){.led = 3, .color = GREEN},
        [3] = &(blink_msg_t){.led = 4, .color = GREEN},
        [4] = &(blink_msg_t){.led = 5, .color = YELLOW},
        [5] = &(blink_msg_t){.led = 6, .color = YELLOW},
        [6] = &(blink_msg_t){.led = 7, .color = RED},
        [7] = &(blink_msg_t){.led = 8, .color = RED}
    };

    ret = send_to_driver(blink_device, messages, 8);
    blink_deinit(blink_device);
    return ret;
}

// Blinkstick API

blink_dev_t* blink_init() {
    blink_dev_t* dev = fopen(BLINK_PATH, "w");
    if (dev == NULL) {
        perror("Can't open blinkstick file");
        return NULL;
    } else {
        return dev;
    }
}

int blink_deinit(blink_dev_t* dev) {
    return fclose(dev);
}

int blink_send(blink_dev_t* dev, const char* buf) {
    size_t written;
    size_t orig_size = strlen(buf);
    written = fwrite(buf, sizeof(char), strlen(buf), dev);
    return orig_size == written ? 0 : -1;
}

#define READ_BUF_LEN 256
#define s_end(s) ((s) + strlen(s))

int send_to_driver(blink_dev_t* dev, blink_msg_t** msg_l, size_t len) {
    unsigned int i;
    int size;
    char dev_msg[READ_BUF_LEN];

    size = sprintf(dev_msg, "%u:0x%X", msg_l[0]->led, msg_l[0]->color);
    if (size == -1) {
        return -1;
    }

    for (i = 1; i < len; i++) {
        size = sprintf(s_end(dev_msg), ",%u:0x%X", msg_l[i]->led, msg_l[i]->color);
        if (size == -1) {
            return -1;
        }
    }

    return blink_send(dev, dev_msg);
}
