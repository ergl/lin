#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

// CPU API
#define CPU_PATH "/proc/stat"

#define CPU_POLL_N 100 // TODO: Pass this as argument
#define CPU_POLL_INT 1

#define CPU_N sysconf(_SC_NPROCESSORS_ONLN)

typedef struct {
    int current_idle;
    int prev_idle;
} cpu_info_t;

const cpu_info_t cpu_info_default = {
    .current_idle = 0,
    .prev_idle = 0
};

void sleep_wait();
int fill_cpu_info(cpu_info_t* last_info);
int get_cpu_idle(cpu_info_t* last_info);

// Blinkstick API
#define BLINK_PATH "/dev/usb/blinkstick0"

#define GREEN 0x001100
#define RED 0x110000

typedef FILE blink_dev_t;
typedef struct {
    unsigned int msg[8];
} blink_msg_t;

const blink_msg_t blink_msg_default = {
    .msg = { 0x000000 }
};

blink_dev_t* blink_init();
int blink_deinit(blink_dev_t* dev);
int send_to_driver(blink_dev_t* dev, blink_msg_t* msg_c);

int display_cpu_load(int idle) {
    int i = 0;
    int leds_to_red = 8 - (idle * 8) / 100;

    printf("CPU Load: %3d%% user, %3d%% idle\r", 100 - idle, idle);
    // print inline
    fflush(stdout);

    blink_dev_t* dev = blink_init();
    blink_msg_t led_message = blink_msg_default;
    for (i = 0; i < 8; i++) {
        led_message.msg[i] = (i + 1 <= leds_to_red) ? RED : GREEN;
    }

    if (send_to_driver(dev, &led_message) == -1) {
        printf("Something went wrong while sending to usb device\n");
    }

    return blink_deinit(dev);
}

int send_all_off() {
    blink_dev_t* dev = blink_init();
    blink_msg_t led_message = blink_msg_default;
    if (send_to_driver(dev, &led_message) == -1) {
        printf("Something went wrong while sending to usb device\n");
    }
    return blink_deinit(dev);
}


int main(int argc, char** argv) {
    int times;
    cpu_info_t last = cpu_info_default;

    for (times = 0; times <= CPU_POLL_N; times++) {
        if (fill_cpu_info(&last) == -1) {
            perror("Couldn't get CPU usage");
            return -1;
        }

        if (display_cpu_load(get_cpu_idle(&last)) == -1) {
            printf("Couldn't properly close usb device\n");
        }

        sleep_wait();
    }

    if (send_all_off() == -1) {
        printf("Couldn't properly close usb device\n");
    }

    return 0;
}


// CPU API

void sleep_wait() {
    // Don't really care that we sleep less time, approx
    sleep(CPU_POLL_INT);
}

int fill_cpu_info(cpu_info_t* last_info) {
    int read;

    FILE* cpu_handler = fopen(CPU_PATH, "r");
    if (cpu_handler == NULL) {
        perror("Can't open stat file");
        return -1;
    }

    // TODO: Find a better way
    read = fscanf(
        cpu_handler,
        "cpu %*d %*d %*d %d %*d %*d %*d %*d %*d",
        &last_info->current_idle
    );

    if (read != 1) {
        perror("Error while scanning stat");
        return -1;
    }

    return fclose(cpu_handler);
}

void save_current(cpu_info_t* info) {
    info->prev_idle = info->current_idle;
}

int get_cpu_idle(cpu_info_t* last_info) {
    float idle_usage;
    int idle_delta;

    if (last_info->prev_idle == 0) {
        save_current(last_info);
        return 0;
    }

    idle_delta = last_info->current_idle - last_info->prev_idle;
    idle_usage = (float) idle_delta / CPU_N;

    save_current(last_info);
    return (idle_usage >= 100) ? 100 : (int) idle_usage;
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

int send_to_driver(blink_dev_t* dev, blink_msg_t* msg_c) {
    int i;
    int size;
    char dev_msg[READ_BUF_LEN];

    size = sprintf(dev_msg, "%u:0x%X", 1, msg_c->msg[0]);
    if (size == -1) {
        return -1;
    }

    for (i = 1; i < 8; i++) {
        size = sprintf(s_end(dev_msg), ",%u:0x%X", i + 1, msg_c->msg[i]);
    }

    return blink_send(dev, dev_msg);
}
