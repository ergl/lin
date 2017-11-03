#include <linux/module.h> 
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>

#include <linux/proc_fs.h>
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
static struct proc_dir_entry* proc_entry;

static ssize_t ledctl_mod_write(struct file* fd, const char __user* buf, size_t len, loff_t* off){
    int data;
    char mod_buf[READ_BUF_LEN];

    if (copy_from_user(mod_buf, buf, len)) {
        return -EFAULT;
    }

    mod_buf[len] = '\0';
    printk(KERN_INFO "ledtcl-mod: Calling write with order %s", mod_buf);

    if (get_user_num(mod_buf, &data) != 1) {
        return len;
    }

    printk(KERN_INFO "ledtcl-mod: Parsed %i\n", data);
    if (data == 0) {
        printk(KERN_INFO "ledtcl-mod: ALL_LEDS_OFF\n");
        set_leds(kbd_driver, ALL_LEDS_OFF); 
    } else if (data == 1) {
        printk(KERN_INFO "ledtcl-mod: SCROLL_ON\n");
        set_leds(kbd_driver, SCROLL_ON); 
    } else if (data == 2) {
        printk(KERN_INFO "ledtcl-mod: CAPS_ON\n");
        set_leds(kbd_driver, CAPS_ON); 
    } else if (data == 3) {
        printk(KERN_INFO "ledtcl-mod: CAPSCROLL\n");
        set_leds(kbd_driver, CAPSCROLL); 
    } else if (data == 4) {
        printk(KERN_INFO "ledtcl-mod: NUM_ON\n");
        set_leds(kbd_driver, NUM_ON); 
    } else if (data == 5) {
        printk(KERN_INFO "ledtcl-mod: NUMSCROLL\n");
        set_leds(kbd_driver, NUMSCROLL); 
    } else if (data == 6) {
        printk(KERN_INFO "ledtcl-mod: NUMCAPS\n");
        set_leds(kbd_driver, NUMCAPS); 
    } else if (data == 7) {
        printk(KERN_INFO "ledtcl-mod: ALL_LEDS_ON\n");
        set_leds(kbd_driver, ALL_LEDS_ON); 
    }

    return len;
}

static const struct file_operations proc_entry_fops = {
    .write = ledctl_mod_write
};

static int __init ledctl_init(void) {
    proc_entry = proc_create("ledctl-mod", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
        printk(KERN_INFO "ledctl-mod: Can't create /proc entry\n");
        return -ENOMEM;
    } else {
        printk(KERN_INFO "ledct-mod: Module loaded\n");
        kbd_driver = get_kbd_driver_handler();
    }

    return 0;
}

static void __exit ledctl_exit(void){
    printk(KERN_INFO "ledct-mod: Module unloaded\n");
    remove_proc_entry("ledctl-mod", NULL);
    set_leds(kbd_driver, ALL_LEDS_OFF); 
}

// Utils

int get_user_num(const char* buffer, int* container) {
    const char* format = "0x%i";
    return sscanf(buffer, format, container);
}

/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void) {
    printk(KERN_INFO "ledtcl-mod: loading\n");
    printk(KERN_INFO "ledtcl-mod: fgconsole is %x\n", fg_console);
    return vc_cons[fg_console].d->port.tty->driver;
}

/* Set led state to that specified by mask */
static inline int set_leds(struct tty_driver* handler, unsigned int mask) {
    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED, mask);
}

module_init(ledctl_init);
module_exit(ledctl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ledctl-mod");
