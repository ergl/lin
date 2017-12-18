#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>
#include <asm-generic/uaccess.h>

MODULE_LICENSE("GPL");

#define DEVICE_NAME "fifodev"
#define MAX_FIFO_SIZE 64 // 64 bytes, fits 64 chars
#define MAX_BUFFER_SIZE 64

static int major;

static int fifodev_open(struct inode *, struct file *);
static int fifodev_release(struct inode *, struct file *);

static ssize_t fifodev_read(struct file *, char *, size_t, loff_t *);
static ssize_t fifodev_write(struct file *, const char *, size_t, loff_t *);

// Circular buffer and associated lock
static struct kfifo cbuffer;
static struct semaphore mtx;

// Opened process count
int reader_opens, writer_opens;

// Read & write queues and associated sizes
int reader_waiting, writer_waiting;
static struct semaphore read_queue, write_queue;

static const struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .read = fifodev_read,
    .write = fifodev_write,
    .open = fifodev_open,
    .release = fifodev_release,
};

static int fifodev_open(struct inode *inode, struct file *filp) {
    fmode_t mode = filp->f_mode;
    unsigned int flags = filp->f_flags;

    // In either mode, wait until we meet with the other side
    if (mode & FMODE_READ) {
        int private_writers;
        printk(KERN_INFO "fifodev: Open reader mode\n");

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
            KERN_INFO "fifodev: cached writers on the other side: %i\n",
            private_writers
        );

        up(&write_queue);
        while (private_writers == 0) {
            printk(KERN_INFO "fifodev: Waiting for writers to meet...\n");
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

        printk(KERN_INFO "fifodev: Reader matched with writer\n");

    } else {
        int private_readers;
        printk(KERN_INFO "fifodev: Open writer mode\n");

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
            KERN_INFO "fifodev: cached readers on the other side: %i\n",
            private_readers
        );

        up(&read_queue);
        while (private_readers == 0) {
            printk(KERN_INFO "fifodev: Waiting for readers to meet...\n");
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

        printk(KERN_INFO "fifodev: Writer matched with reader\n");
    }

    return 0;
}

static int fifodev_release(struct inode *inode, struct file *filp) {
    fmode_t mode = filp->f_mode;
    if (mode & FMODE_READ) {
        printk(KERN_INFO "fifodev: Close reader mode\n");
        if (down_interruptible(&mtx)) return -EINTR;
        reader_opens--;

        // Signal writers that we're leaving
        up(&write_queue);

        // If we're the last one, flush fifo
        if (reader_opens == 0 && writer_opens == 0) {
            kfifo_reset(&cbuffer);
        }
        up(&mtx);
    } else {
        printk(KERN_INFO "fifodev: Close writer mode\n");
        if (down_interruptible(&mtx)) return -EINTR;
        writer_opens--;

        // Signal writers that we're leaving
        up(&read_queue);

        // If we're the last one, flush fifo
        if (reader_opens == 0 && writer_opens == 0) {
            kfifo_reset(&cbuffer);
        }
        up(&mtx);
    }

    return 0;
}

static ssize_t fifodev_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    int bytes_extracted;
    char own_buffer[MAX_BUFFER_SIZE];

    printk(KERN_INFO "fifodev: Reading file\n");

    // If trying to read with size larger than kfifo max size, return error
    if (len > MAX_FIFO_SIZE || len > MAX_BUFFER_SIZE) {
        return -ENOSPC;
    }

    if (down_interruptible(&mtx)) return -EINTR;

    // If trying to read with size less than kfifo size,
    // block caller with read_queue
    //
    // we can do the comparison directly since we store chars
    while (kfifo_len(&cbuffer) < len && writer_opens > 0) {
        reader_waiting++;
        up(&mtx);

        printk(
            KERN_INFO "fifodev: Buffer not full enough, will wait until there's enough\n"
        );

        if (down_interruptible(&read_queue)) {
            down(&mtx);
            reader_waiting--;
            up(&mtx);
            return -EINTR;
        }

        printk(KERN_INFO "fifodev: Reader awoke, will check buffer again\n");

        if (down_interruptible(&mtx)) return -EINTR;
    }

    // If trying to read from empty kfifo, and no writers are present,
    // return 0 (EOF)
    if (kfifo_is_empty(&cbuffer) && writer_opens == 0) {
        up(&mtx);
        return 0;
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

static ssize_t fifodev_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    int bytes_written;
    char own_buffer[MAX_BUFFER_SIZE + 1];

    printk(KERN_INFO "fifodev: Writing file\n");

    // If trying to write with size larger than kfifo max size, return error
    if (len > MAX_FIFO_SIZE || len > MAX_BUFFER_SIZE) {
        return -ENOSPC;
    }

    if (copy_from_user(own_buffer, buf, len)) {
        return -EFAULT;
    }

    own_buffer[len] = '\0';
    *off += len;

    if (down_interruptible(&mtx)) return -EINTR;

    // If there's no room in kfifo to write the entire buffer,
    // block the caller with write_queue
    while (kfifo_avail(&cbuffer) < len && reader_opens > 0) {
        writer_waiting++;
        up(&mtx);

        printk(KERN_INFO "fifodev: Reader present, waiting for signal\n");

        if (down_interruptible(&write_queue)) {
            down(&mtx);
            writer_waiting--;
            up(&mtx);
            return -EINTR;
        }

        printk(KERN_INFO "fifodev: Writer awoke, will check buffer again\n");

        if (down_interruptible(&mtx)) return -EINTR;
    }

    // If writing to FIFO without readers, return error
    if (reader_opens == 0) {
        up(&mtx);
        printk(KERN_INFO "fifodev: Reader exited while we waited\n");
        // TODO: What kind of error do we return here?
        return -1;
    }

    bytes_written = kfifo_in(&cbuffer, own_buffer, len);

    if (reader_waiting > 0) {
        up(&read_queue);
        reader_waiting--;
    }

    up(&mtx);

    if (bytes_written < len) {
        // TODO: What to do here?
        printk(KERN_INFO "fifodev: Couldn't write everything\n");
        return -ENOSPC;
    }

    return len;
}

int fifoproc_module_init(void) {
    if (kfifo_alloc(&cbuffer, MAX_FIFO_SIZE, GFP_KERNEL) != 0) {
        printk(KERN_INFO "fifodev: Couldn't allocate kfifo\n");
        return -ENOMEM;
    }

    sema_init(&mtx, 1);

    sema_init(&read_queue, 0);
    sema_init(&write_queue, 0);

    reader_opens = 0;
    writer_opens = 0;

    reader_waiting = 0;
    writer_waiting = 0;

    major = register_chrdev(0, DEVICE_NAME, &dev_fops);
    if (major < 0) {
        kfifo_free(&cbuffer);
        printk(KERN_ALERT "fifodev: Can't register device: %d\n", major);
        return major;
    }

    printk(KERN_INFO "fifodev: module loaded with major %d, minor: 0\n", major);
    return 0;
}

void fifoproc_module_cleanup(void) {
    kfifo_free(&cbuffer);
    unregister_chrdev(major, DEVICE_NAME);
    printk(KERN_INFO "fifodev: module unloaded\n");
}

module_init(fifoproc_module_init);
module_exit(fifoproc_module_cleanup);
