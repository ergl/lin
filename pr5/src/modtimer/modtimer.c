#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/random.h>
#include <asm-generic/uaccess.h>

MODULE_LICENSE("GPL");

// 64 bytes, fits 16 uints
#define MAX_FIFO_SIZE 64
#define COPY_BUFFER_SIZE 32

#define DEFAULT_RANDOM 100
#define DEFAULT_PERIOD_MS 500
#define DEFAULT_THRESHOLD 75

static int mod_proc_open(struct inode *, struct file *);
static int mod_proc_release(struct inode *, struct file *);
static ssize_t mod_proc_read(struct file *, char *, size_t, loff_t *);

static ssize_t config_proc_read(struct file *, char *, size_t, loff_t *);
static ssize_t config_proc_write(struct file *, const char *, size_t, loff_t *);

// Upper bound for random numbers
static int max_random;
// Random number gen period
static int timer_period_ms;
// % of cbuffer usage before sched copy
// When it's reached, we schedule a flush
// on a different CPU core (smp_processor_id)
static int emergency_threshold;

// Circular buffer that holds generated numbers
static struct kfifo cbuffer;

static struct proc_dir_entry* mod_entry;
static struct proc_dir_entry* config_entry;

static const struct file_operations mod_entry_fops = {
    .open = mod_proc_open,
    .read = mod_proc_read,
    .release = mod_proc_release
};

static const struct file_operations config_entry_fops = {
    .read = config_proc_read,
    .write = config_proc_write
};

static int mod_proc_open(struct inode *inode, struct file *filp) {
    // TODO: Complete stub
    printk(KERN_INFO "modtimer: Opening /proc mod entry\n");
    return 0;
}

static ssize_t mod_proc_read(struct file *filp, char __user *buf, size_t len,
                             loff_t *off) {

    // TODO: Complete stub
    printk(KERN_INFO "modtimer: Reading /proc mod entry\n");
    return 0;
}

static int mod_proc_release(struct inode *inode, struct file *filp) {
    // TODO: Complete stub
    printk(KERN_INFO "modtimer: Closing /proc mod entry\n");
    return 0;
}

static ssize_t config_proc_read(struct file *filp, char __user *buf, size_t len,
                                loff_t *off) {
    int sz;

    // Application can only read once
    if ((*off) > 0) {
        return 0;
    }

    printk(KERN_INFO "modtimer: Reading config\n");

    // Figure out the size of the buffer
    sz = snprintf(
        NULL,
        0,
        "timer_period_ms=%d\nemergency_threshold=%d\nmax_random=%d\n",
        timer_period_ms,
        emergency_threshold,
        max_random
    );

    char own_buffer[sz + 1];
    snprintf(
        own_buffer,
        sizeof(own_buffer),
        "timer_period_ms=%d\nemergency_threshold=%d\nmax_random=%d\n",
        timer_period_ms,
        emergency_threshold,
        max_random
    );

    if (copy_to_user(buf, own_buffer, sz + 1)) {
        return -EFAULT;
    }

    *off += sz + 1;
    return len;
}

static ssize_t config_proc_write(struct file *filp, const char __user *buf,
                                 size_t len, loff_t *off) {

    int data;
    char own_buffer[33];

    // The application can only write to this entry once
    if ((*off) > 0) {
        return 0;
    }

    printk(KERN_INFO "modtimer: Writing config\n");

    if (copy_from_user(own_buffer, buf, len)) {
        return -EFAULT;
    }

    own_buffer[len] = '\0';
    *off += len;

    if (sscanf(own_buffer, "timer_period_ms %i", &data)) {
        printk(KERN_INFO "modtimer: Setting timer_period_ms to %d\n", data);
        timer_period_ms = data;
    } else if (sscanf(own_buffer, "emergency_threshold %i", &data)) {
        printk(KERN_INFO "modtimer: Setting emergency_threshold to %d\n", data);
        emergency_threshold = data;
    } else if (sscanf(own_buffer, "max_random %i", &data)) {
        printk(KERN_INFO "modtimer: Setting max_random to %d\n", data);
        max_random = data;
    } else {
        printk(KERN_INFO "modtimer: Couldn't recognize config option\n");
        return -EINVAL;
    }

    return len;
}

int modtimer_init(void) {
    if (kfifo_alloc(&cbuffer, MAX_FIFO_SIZE, GFP_KERNEL) != 0) {
        printk(KERN_INFO "moditmer: Couldn't allocate kfifo\n");
        return -ENOMEM;
    }

    mod_entry = proc_create("modtimer", 0666, NULL, &mod_entry_fops);
    if (mod_entry == NULL) goto procclean;

    config_entry = proc_create("modconfig", 0666, NULL, &config_entry_fops);
    if (config_entry == NULL) goto procclean;

    max_random = DEFAULT_RANDOM;
    timer_period_ms = DEFAULT_PERIOD_MS;
    emergency_threshold = DEFAULT_THRESHOLD;

    printk(KERN_INFO "modtimer: module loaded\n");
    return 0;

procclean:;
    kfifo_free(&cbuffer);
    printk(KERN_INFO "modtimer: Can't create /proc entry\n");
    return -ENOMEM;
}

void modtimer_cleanup(void) {
    remove_proc_entry("modtimer", NULL);
    remove_proc_entry("modconfig", NULL);
    kfifo_free(&cbuffer);
    printk(KERN_INFO "modtimer: module unloaded\n");
}

module_init(modtimer_init);
module_exit(modtimer_cleanup);
