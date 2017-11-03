#include <linux/syscalls.h>
#include <linux/kernel.h>

#include <asm-generic/errno.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */

#include <linux/string.h>

#define ALL_LEDS_OFF 0
#define SCROLL_ON 0x1
#define CAPS_ON 0x2
#define CAPSCROLL 0x3
#define NUM_ON 0x4
#define NUMSCROLL 0x5
#define NUMCAPS 0x6
#define ALL_LEDS_ON 0x7

#define READ_BUF_LEN 256

struct tty_driver* get_kbd_driver_handler(void);
int get_user_num(const char* buffer, int* container);
static inline int set_leds(struct tty_driver* handler, unsigned int mask);

struct tty_driver* kbd_driver = NULL;

SYSCALL_DEFINE1(ledctl, const char __user *, buf, size_t, count) {

    int data;
    char mod_buf[READ_BUF_LEN];

    if (copy_from_user(mod_buf, buf, len)) {
        return -EFAULT;
    }

    mod_buf[len] = '\0';
    printk(KERN_INFO "ledtclsys: Calling write with order %s", mod_buf);

    if (get_user_num(mod_buf, &data) != 1) {
        return 0;
    }

    printk(KERN_INFO "ledctlsys: Parsed %i\n", data);
    if (data == 0) {
        printk(KERN_INFO "ledctlsys: ALL_LEDS_OFF\n");
        set_leds(kbd_driver, ALL_LEDS_OFF); 
    } else if (data == 1) {
        printk(KERN_INFO "ledctlsys: SCROLL_ON\n");
        set_leds(kbd_driver, SCROLL_ON); 
    } else if (data == 2) {
        printk(KERN_INFO "ledctlsys: CAPS_ON\n");
        set_leds(kbd_driver, CAPS_ON); 
    } else if (data == 3) {
        printk(KERN_INFO "ledctlsys: CAPSCROLL\n");
        set_leds(kbd_driver, CAPSCROLL); 
    } else if (data == 4) {
        printk(KERN_INFO "ledctlsys: NUM_ON\n");
        set_leds(kbd_driver, NUM_ON); 
    } else if (data == 5) {
        printk(KERN_INFO "ledctlsys: NUMSCROLL\n");
        set_leds(kbd_driver, NUMSCROLL); 
    } else if (data == 6) {
        printk(KERN_INFO "ledctlsys: NUMCAPS\n");
        set_leds(kbd_driver, NUMCAPS); 
    } else if (data == 7) {
        printk(KERN_INFO "ledctlsys: ALL_LEDS_ON\n");
        set_leds(kbd_driver, ALL_LEDS_ON); 
    }

    return 0;
}

// Utils

int get_user_num(const char* buffer, int* container) {
    const char* format = "0x%i";
    return sscanf(buffer, format, container);
}

/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void) {
    printk(KERN_INFO "ledctlsys: fgconsole is %x\n", fg_console);
    return vc_cons[fg_console].d->port.tty->driver;
}

/* Set led state to that specified by mask */
static inline int set_leds(struct tty_driver* handler, unsigned int mask) {
    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED, mask);
}
