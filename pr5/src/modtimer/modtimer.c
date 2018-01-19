#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/semaphore.h>
#include <asm-generic/uaccess.h>

MODULE_LICENSE("GPL");

// 64 bytes, fits 16 uints
#define MAX_FIFO_SIZE 64
#define COPY_BUFFER_SIZE 64

#define DEFAULT_RANDOM 100
#define DEFAULT_PERIOD_MS 1000
#define DEFAULT_THRESHOLD 75

// Only one client might open
int client_opens;
static struct semaphore open_lock;

// Linked list holding the generated numbers
// by the timer, copied from the circular buffer
struct list_head* client_list;
typedef struct list_item_t {
    unsigned int num;
    struct list_head links;
} list_item_t;

// list_lock locks both the client list
// and the clients_waiting var
// client_queue represents the client
// waiting for the list to become non-empty
DEFINE_SPINLOCK(list_lock);
int clients_waiting;
static struct semaphore client_queue;

struct list_head* list_head_init(void);
void add_item(struct list_head* list, int data);
int print_flush_list(struct list_head* list, char* buf);
void cleanup(struct list_head* list);

// Used by open / release to keep track of clients open
int open_client(void);
int close_client(void);

void init_gen_timer(void);
int resched_timer(void);
static void insert_random_int(unsigned long);

// Returns 1 if kfifo needs to be flushed, 0 otherwise
int reached_threshold(void);

static struct workqueue_struct* mod_workq;
struct work_struct transfer_task;
static void copy_items_into_list(struct work_struct *);

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
// and associated spinlock
static struct kfifo cbuffer;
DEFINE_SPINLOCK(buffer_lock);

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

struct list_head* list_head_init(void) {
    struct list_head* head;

    head = (struct list_head*)vmalloc((sizeof(struct list_head)));
    memset(head, 0, sizeof(struct list_head));

    return (struct list_head*)head;
}

struct list_item_t* list_item_init(struct list_item_t data) {
    struct list_item_t* item;
    item = (struct list_item_t*) vmalloc((sizeof(struct list_item_t)));
    memset(item, 0, sizeof(struct list_item_t));
    memcpy(item, &data, sizeof(struct list_item_t));
    return (struct list_item_t*) item;
}

void add_item(struct list_head* list, int data) {
    struct list_item_t* new_item;
    new_item = list_item_init((struct list_item_t){.num = data});

    spin_lock(&list_lock);
    list_add_tail(&new_item->links, list);
    if (clients_waiting > 0) {
        up(&client_queue);
        clients_waiting = 0;
    }
    spin_unlock(&list_lock);
}

int print_flush_list(struct list_head* list, char* buf) {
    int buf_len = 0;
    int read_bytes = 0;

    struct list_head* cur_node = NULL;
    struct list_head* aux_storage = NULL;
    struct list_item_t* item = NULL;

    spin_lock(&list_lock);
    list_for_each_safe(cur_node, aux_storage, list) {
        item = list_entry(cur_node, struct list_item_t, links);
        read_bytes = sprintf(buf, "%i\n", item->num);

        list_del(cur_node);
        vfree(item);

        buf += read_bytes;
        buf_len += read_bytes;
    }
    spin_unlock(&list_lock);

    return buf_len;
}

void cleanup(struct list_head* list) {
    struct list_head* cur_node = NULL;
    struct list_head* aux_storage = NULL;
    struct list_item_t* item = NULL;

    spin_lock(&list_lock);
    list_for_each_safe(cur_node, aux_storage, list) {
        item = list_entry(cur_node, struct list_item_t, links);
        list_del(cur_node);
        vfree(item);
    }
    spin_unlock(&list_lock);
}

void init_gen_timer(void) {
    init_timer(&gen_timer);
    gen_timer.data = 0;
    gen_timer.function = insert_random_int;
}

// If gen_timer is inactive (not added), this will activate it
// Returns 0 if gen_timer was inactive, 1 otherwise
int resched_timer(void) {
    return mod_timer(&gen_timer, jiffies + msecs_to_jiffies(timer_period_ms));
}

// Generate a random int and insert in the kfifo
static void insert_random_int(unsigned long _data) {
    unsigned int ret;
    unsigned int current_cpu, target_cpu;
    unsigned int gen = get_random_int() % max_random;
    printk(KERN_INFO "modtimer: Generated %u\n", gen);

    // New API for kfifo_in with spin_lock_irqsave underneath
    ret = kfifo_in_spinlocked(&cbuffer, &gen, sizeof(unsigned int), &buffer_lock);

    if (reached_threshold() == 1) {
        current_cpu = smp_processor_id();
        target_cpu = (current_cpu == 0) ? 1 : 0;

        printk(
            KERN_INFO "modtimer: Migrating task from CPU %u to CPU %u\n",
            current_cpu,
            target_cpu
        );

        if (work_pending(&transfer_task)) {
            printk(KERN_INFO "modtimer: There's still some work on the workqueue");
        } else {
            printk(KERN_INFO "modtimer: Will schedule flush");
            queue_work_on(target_cpu, mod_workq, &transfer_task);
        }
    }

    resched_timer();
}

int reached_threshold(void) {
    float used;
    unsigned int bytes;
    unsigned long flags;

    spin_lock_irqsave(&buffer_lock, flags);
    bytes = kfifo_len(&cbuffer);
    spin_unlock_irqrestore(&buffer_lock, flags);

    used = (float) bytes / (float) MAX_FIFO_SIZE * 100.0;
    printk(KERN_INFO "modtimer: kfifo usage %d%%\n", (int) used);
    return (int) used >= emergency_threshold;
}

static void copy_items_into_list(struct work_struct* _work) {
    unsigned int retry = 1;
    unsigned int num;
    unsigned int bytes_extracted;

    int idx;
    int total_items = 0;
    unsigned int nums[COPY_BUFFER_SIZE / sizeof(unsigned int)];

    printk(KERN_INFO "modtimer: Transfering numbers to linked list\n");
    do {
        bytes_extracted = kfifo_out_spinlocked(&cbuffer, &num, sizeof(unsigned int), &buffer_lock);
        if (bytes_extracted != sizeof(unsigned int)) {
            retry = 0;
        } else {
            nums[total_items] = num;
            total_items++;
        }
    } while (retry != 0);

    printk(KERN_INFO "modtimer: Extracted %d items from buffer\n", total_items);
    for (idx = 0; idx < total_items; idx++) {
        printk(KERN_INFO "modtimer: Extracted %u from buffer\n", nums[idx]);
        add_item(client_list, nums[idx]);
    }
}

int open_client(void) {
    int open = 0;
    if (down_interruptible(&open_lock)) {
        return -1;
    }

    open = client_opens;
    if (open == 0) {
        client_opens++;
    } else {
        open = 1;
    }

    up(&open_lock);

    return open;
}

int close_client(void) {
    int close;
    if (down_interruptible(&open_lock)) {
        return -1;
    }

    close = client_opens;
    if (close == 1) {
        client_opens--;
    }

    up(&open_lock);
    return 0;
}

static int mod_proc_open(struct inode *inode, struct file *filp) {
    int open;
    try_module_get(THIS_MODULE);

    open = open_client();
    if (open == -1) {
        return -EINTR;
    }

    if (open == 1) {
        return -EBUSY;
    }

    sema_init(&client_queue, 0);

    clients_waiting = 0;

    client_list = list_head_init();
    INIT_LIST_HEAD(client_list);

    // Opening the file starts the timer to generate numbers
    resched_timer();

    printk(KERN_INFO "modtimer: Opening /proc mod entry\n");
    return 0;
}

static ssize_t mod_proc_read(struct file *filp, char __user *buf, size_t len,
                             loff_t *off) {

    int ret;
    char own_buffer[COPY_BUFFER_SIZE];

    spin_lock(&list_lock);

    while (list_empty(client_list)) {
        clients_waiting = 1;
        spin_unlock(&list_lock);

        printk(KERN_INFO "modtimer: List is empty, sleeping\n");

        if (down_interruptible(&client_queue)) {
            spin_lock(&list_lock);
            clients_waiting = 0;
            spin_unlock(&list_lock);
            return -EINTR;
        }

        printk(KERN_INFO "modtimer: Client awoke\n");
    }

    ret = print_flush_list(client_list, own_buffer);
    if (copy_to_user(buf, own_buffer, ret)) {
        return -EFAULT;
    }

    *off += ret;
    return ret;
}

static int mod_proc_release(struct inode *inode, struct file *filp) {
    unsigned long flags;

    // Deactivates the timer.
    // If it was already inactive does nothing
    del_timer_sync(&gen_timer);

    // Wait for the workqueue to be finished
    flush_workqueue(mod_workq);

    // Flush the buffer
    spin_lock_irqsave(&buffer_lock, flags);
    kfifo_reset(&cbuffer);
    spin_unlock_irqrestore(&buffer_lock, flags);

    // Flush and free linked list
    cleanup(client_list);
    vfree(client_list);

    // Let other client open the file
    if (close_client() == -1) {
        return -EINTR;
    }

    module_put(THIS_MODULE);

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

    mod_workq = create_workqueue("modtimer_queue");
    if (!mod_workq) goto procclean;

    INIT_WORK(&transfer_task, copy_items_into_list);

    max_random = DEFAULT_RANDOM;
    timer_period_ms = DEFAULT_PERIOD_MS;
    emergency_threshold = DEFAULT_THRESHOLD;

    sema_init(&open_lock, 1);
    client_opens = 0;

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
    destroy_workqueue(mod_workq);
    kfifo_free(&cbuffer);
    printk(KERN_INFO "modtimer: module unloaded\n");
}

module_init(modtimer_init);
module_exit(modtimer_cleanup);
