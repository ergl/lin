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

unsigned long to_jiffies(int);
void init_gen_timer(void);
int resched_timer(void);
static void insert_random_int(unsigned long);

// Returns 1 if kfifo needs to be flushed, 0 otherwise
int reached_threshold(void);

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

// Random number timer handle
struct timer_list gen_timer;

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

unsigned long to_jiffies(int ms) {
    int s = ms / 1000;
    return (s * HZ);
}

void init_gen_timer(void) {
    init_timer(&gen_timer);
    gen_timer.data = 0;
    gen_timer.function = insert_random_int;
}

// If gen_timer is inactive (not added), this will activate it
// Returns 0 if gen_timer was inactive, 1 otherwise
int resched_timer(void) {
    return mod_timer(&gen_timer, jiffies + to_jiffies(timer_period_ms));
}

// Generate a random int and insert in the kfifo
static void insert_random_int(unsigned long _data) {
    unsigned int ret;
    unsigned int gen = get_random_int();
    printk(KERN_INFO "modtimer: Generated %u\n", gen);
    // TODO: Check this earlier
    if (reached_threshold() == 0) {
        // Enqueue does not need locking (see docs)
        ret = kfifo_in(&cbuffer, &gen, sizeof(unsigned int));
        printk(KERN_INFO "modtimer: Inserted int with ret %d\n", ret);
    }

    resched_timer();
}

int reached_threshold(void) {
    unsigned int bytes = kfifo_len(&cbuffer);
    printk(KERN_INFO "modtimer: kfifo usage (bytes): %u\n", bytes);
    float used = (float) bytes / (float) MAX_FIFO_SIZE * 100.0;
    printk(KERN_INFO "modtimer: kfifo usage (%%): %d\n", (int) used);
    return (int) used >= emergency_threshold;
}

static int mod_proc_open(struct inode *inode, struct file *filp) {
    // TODO: Complete stub
    // FIXME: Only one process might open the file
    printk(KERN_INFO "modtimer: Opening /proc mod entry\n");

    // Opening the file starts the timer to generate numbers
    resched_timer();
    return 0;
}

static ssize_t mod_proc_read(struct file *filp, char __user *buf, size_t len,
                             loff_t *off) {

    // TODO: Complete stub
    // Block while the list if the linked list is empty

    printk(KERN_INFO "modtimer: Reading /proc mod entry\n");
    return 0;
}

static int mod_proc_release(struct inode *inode, struct file *filp) {
    // TODO: Complete stub
    // If there is an active timer, cancel it
    printk(KERN_INFO "modtimer: Closing /proc mod entry\n");

    // Deactivates the timer.
    // If it was already inactive does nothing
    del_timer_sync(&gen_timer);
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

    init_gen_timer();

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
