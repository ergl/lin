#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>

MODULE_LICENSE("GPL");

static int fifoproc_open(struct inode *, struct file *);
static int fifoproc_release(struct inode *, struct file *);

static ssize_t fifoproc_read(struct file *, char *, size_t, loff_t *);
static ssize_t fifoproc_write(struct file *, const char *, size_t, loff_t *);

// Circular buffer and associated lock
static struct kfifo cbuffer;
static struct semaphore mtx;

// Opened process count
int reader_opens = 0, writer_opens = 0;

// Read & write queues and associated sizes
int reader_waiting = 0, writer_waiting = 0;
static struct semaphore read_queue, write_queue;

static struct proc_dir_entry* proc_entry;
static const struct file_operations proc_entry_fops = {
    .read = fifoproc_read,
    .write = fifoproc_write,
    .open = fifoproc_open,
    .release = fifoproc_release
};

// TODO: During any of the previous ops, if a semaphore is waked by an interrupt
// or signal, raise error (EINTR).

// When all processes are done (both readers and writes), flush kfifo

static int fifoproc_open(struct inode *inode, struct file *fd) {
    fmode_t mode = fd->f_mode;
    unsigned int flags = fd->f_flags;
    

    try_module_get(THIS_MODULE);
    if (mode & FMODE_READ) {
        printk(KERN_INFO "fifoproc: New reader attempt\n");
        // TODO: Change this
        // If opening in read mode, block until there's someone on the other
        // side (write mode)
        //
        // If file is opened in non-blocking mode and this call would block
        // return EAGAIN (change this part for appropiate condition)
        //                           VVVVVVVVVVVVVV
        if ((flags & O_NONBLOCK) && reader_opens > 0) {
            return -EAGAIN;
        }
    } else if (mode & FMODE_WRITE) {
        printk(KERN_INFO "fifoproc: New writer attempt\n");
        // If opening in write mode, block until there's someone on the other
        // side (write mode)
    } else {
        printk(KERN_INFO "fifproc: New ??? with mode %o\n", mode);
    }

    return 0;
}

static int fifoproc_release(struct inode *inode, struct file *fd) {
    module_put(THIS_MODULE);
    printk(KERN_INFO "fifoproc: Closing file\n");
    return 0;
}

static ssize_t fifoproc_read(struct file *fd, char __user *buf, size_t len, loff_t *off) {
    // If trying to read with size larger than kfifo max size, return error
    // If trying to read with size less than kfifo size, block caller with read_queue
    // If trying to read from empty kfifo, and no writers are present, return 0 (EOF)
    printk(KERN_INFO "fifoproc: Reading file\n");
    return 0;
}

static ssize_t fifoproc_write(struct file *fd, const char __user *buf, size_t len, loff_t *off) {
    // If trying to write with size larger than kfifo max size, return error
    // If there's no room in kfifo to write the entire buffer, block the caller with write_queue
    // If writing to FIFO without readers, return error

    printk(KERN_INFO "fifoproc: Writinf file\n");

    if ((*off) > 0) {
        return 0;
    }

    *off += len;
    return len;
}

int fifoproc_module_init(void) {
    proc_entry = proc_create("modfifo", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
        printk(KERN_INFO "fifproc: Can't create /proc entry\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "fifproc: module loaded\n");
    return 0;
}

void fifoproc_module_cleanup(void) {
    remove_proc_entry("modfifo", NULL);
    printk(KERN_INFO "fifoproc: module unloaded\n");
}

module_init(fifoproc_module_init);
module_exit(fifoproc_module_cleanup);
