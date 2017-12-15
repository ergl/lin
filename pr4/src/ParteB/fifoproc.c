#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>
#include <asm-generic/uaccess.h>

MODULE_LICENSE("GPL");

#define MAX_FIFO_SIZE 64 // 64 bytes, fits 64 chars
#define MAX_BUFFER_SIZE 64

static int fifoproc_open(struct inode *, struct file *);
static int fifoproc_release(struct inode *, struct file *);

static ssize_t fifoproc_read(struct file *, char *, size_t, loff_t *);
static ssize_t fifoproc_write(struct file *, const char *, size_t, loff_t *);

// Circular buffer and associated lock
static struct kfifo cbuffer;
static struct semaphore mtx;

// Opened process count
int reader_opens, writer_opens;

// Read & write queues and associated sizes
int reader_waiting, writer_waiting;
static struct semaphore read_queue, write_queue;

static struct proc_dir_entry* proc_entry;
static const struct file_operations proc_entry_fops = {
    .read = fifoproc_read,
    .write = fifoproc_write,
    .open = fifoproc_open,
    .release = fifoproc_release
};

static int fifoproc_open(struct inode *inode, struct file *fd) {
    fmode_t mode = fd->f_mode;
    unsigned int flags = fd->f_flags;

    // In either mode, wait until we meet with the other side
    if (mode & FMODE_READ) {
        int private_writers;
        printk(KERN_INFO "fifoproc: Open reader mode\n");

        // Fenced atomic incr and fetch
        if (down_interruptible(&mtx)) return -EINTR;

        reader_opens++;
        private_writers = writer_opens;

        up(&mtx);

        // If file is opened in non-blocking mode and this call would block
        // return EAGAIN
        if ((flags & O_NONBLOCK) && (private_writers == 0)) {
            return -EAGAIN;
        }

        printk(
            KERN_INFO "fifoproc: cached writers on the other side: %i\n",
            private_writers
        );

        up(&write_queue);
        while (private_writers == 0) {
            printk(KERN_INFO "fifproc: Waiting for writers to meet...\n");
            if (down_interruptible(&read_queue)) {
                down(&mtx);
                reader_opens--;
                up(&mtx);
                return -EINTR;
            }

            // Refresh
            if (down_interruptible(&mtx)) return -EINTR;
            private_writers = writer_opens;
            up(&mtx);
        }

        printk(KERN_INFO "fifoproc: Reader matched with writer\n");

    } else {
        int private_readers;
        printk(KERN_INFO "fifoproc: Open writer mode\n");

        // Fenced atomic incr and fetch
        if (down_interruptible(&mtx)) return -EINTR;

        writer_opens++;
        private_readers = reader_opens;

        up(&mtx);

        // If file is opened in non-blocking mode and this call would block
        // return EAGAIN
        if ((flags & O_NONBLOCK) && (private_readers == 0)) {
            return -EAGAIN;
        }

        printk(
            KERN_INFO "fifoproc: cached readers on the other side: %i\n",
            private_readers
        );

        up(&read_queue);
        while (private_readers == 0) {
            printk(KERN_INFO "fifoproc: Waiting for readers to meet...\n");
            if (down_interruptible(&write_queue)) {
                down(&mtx);
                writer_opens--;
                up(&mtx);
                return -EINTR;
            }

            // Refresh
            if (down_interruptible(&mtx)) return -EINTR;
            private_readers = reader_opens;
            up(&mtx);
        }

        printk(KERN_INFO "fifoproc: Writer matched with reader\n");
    }

    return 0;
}

static int fifoproc_release(struct inode *inode, struct file *fd) {
    fmode_t mode = fd->f_mode;
    if (mode & FMODE_READ) {
        printk(KERN_INFO "fifoproc: Close reader mode\n");
        if (down_interruptible(&mtx)) return -EINTR;
        reader_opens--;
        // If we're the last one, flush fifo
        if (reader_opens == 0 && writer_opens == 0) {
            kfifo_reset(&cbuffer);
        }
        up(&mtx);
    } else {
        printk(KERN_INFO "fifoproc: Close writer mode\n");
        if (down_interruptible(&mtx)) return -EINTR;
        writer_opens--;
        // If we're the last one, flush fifo
        if (reader_opens == 0 && writer_opens == 0) {
            kfifo_reset(&cbuffer);
        }
        up(&mtx);
    }

    return 0;
}

static ssize_t fifoproc_read(struct file *fd, char __user *buf, size_t len, loff_t *off) {
    int bytes_extracted;
    char own_buffer[MAX_BUFFER_SIZE];

    printk(KERN_INFO "fifoproc: Reading file\n");

    // If trying to read with size larger than kfifo max size, return error
    if (len > MAX_FIFO_SIZE || len > MAX_BUFFER_SIZE) {
        return -ENOSPC;
    }

    // Just allow a single read
    if ((*off) > 0) {
        return 0;
    }

    if (down_interruptible(&mtx)) return -EINTR;

    // If trying to read from empty kfifo, and no writers are present,
    // return 0 (EOF)
    if (kfifo_is_empty(&cbuffer) && writer_opens == 0) {
        up(&mtx);
        return 0;
    }

    // If trying to read with size less than kfifo size,
    // block caller with read_queue
    //
    // we can do the comparison directly since we store chars
    while (kfifo_len(&cbuffer) < len) {
        reader_waiting++;
        up(&mtx);

        if (down_interruptible(&read_queue)) {
            down(&mtx);
            reader_waiting--;
            up(&mtx);
            return -EINTR;
        }

        if (down_interruptible(&mtx)) return -EINTR;
    }

    bytes_extracted = kfifo_out(&cbuffer, &own_buffer, len);

    if (writer_waiting > 0) {
        up(&write_queue);
        writer_waiting--;
    }

    up(&mtx);

    if (copy_to_user(buf, own_buffer, len)) {
        return -EFAULT;
    }

    *off += len;
    return len;
}

static ssize_t fifoproc_write(struct file *fd, const char __user *buf, size_t len, loff_t *off) {
    // If trying to write with size larger than kfifo max size, return error
    // If there's no room in kfifo to write the entire buffer, block the caller with write_queue
    // If writing to FIFO without readers, return error

    printk(KERN_INFO "fifoproc: Writing file\n");

    if ((*off) > 0) {
        return 0;
    }

    *off += len;
    return len;
}

int fifoproc_module_init(void) {
    if (kfifo_alloc(&cbuffer, MAX_FIFO_SIZE, GFP_KERNEL) != 0) {
        printk(KERN_INFO "fifoproc: Couldn't allocate kfifo\n");
        return -ENOMEM;
    }

    sema_init(&mtx, 1);

    sema_init(&read_queue, 0);
    sema_init(&write_queue, 0);

    reader_opens = 0;
    writer_opens = 0;

    reader_waiting = 0;
    writer_waiting = 0;

    proc_entry = proc_create("modfifo", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
        kfifo_free(&cbuffer);
        printk(KERN_INFO "fifproc: Can't create /proc entry\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "fifproc: module loaded\n");
    return 0;
}

void fifoproc_module_cleanup(void) {
    remove_proc_entry("modfifo", NULL);
    kfifo_free(&cbuffer);
    printk(KERN_INFO "fifoproc: module unloaded\n");
}

module_init(fifoproc_module_init);
module_exit(fifoproc_module_cleanup);
