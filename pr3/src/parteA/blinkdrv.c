/*
 * Simple driver for the Blinkstick Strip USB device
 *
 * Copyright (C) 2015 Juan Carlos Saez (jcsaezal@ucm.es)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2.
 *
 * This driver is based on the sample driver found in the
 * Linux kernel sources  (drivers/usb/usb-skeleton.c) 
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");

// Get a minor range for your devices from the usb maintainer
#define USB_BLINK_MINOR_BASE    0

// Structure to hold all of our device specific stuff
struct usb_blink {
    // the usb device for this device
    struct usb_device *udev;
    // the interface for this device
    struct usb_interface *interface;
    struct kref kref;
};

#define to_blink_dev(d) container_of(d, struct usb_blink, kref)

static struct usb_driver blink_driver;

/*
 * Free up the usb_blink structure and
 * decrement the usage count associated with the usb device 
 */
static void blink_delete(struct kref *kref) {
    struct usb_blink *dev = to_blink_dev(kref);
    usb_put_dev(dev->udev);
    vfree(dev);
}

// Called when a user program invokes the open() system call on the device
static int blink_open(struct inode *inode, struct file *file) {

    struct usb_blink *dev;
    struct usb_interface *interface;
    int subminor;
    int retval = 0;

    subminor = iminor(inode);

    // Obtain reference to USB interface from minor number
    interface = usb_find_interface(&blink_driver, subminor);
    if (!interface) {
        pr_err("%s - error, can't find device for minor %d\n", __func__, subminor);
        return -ENODEV;
    }

    // Obtain driver data associated with the USB interface
    dev = usb_get_intfdata(interface);
    if (!dev) {
        return -ENODEV;
    }

    // increment our usage count for the device
    kref_get(&dev->kref);

    // save our object in the file's private structure
    file->private_data = dev;

    return retval;
}

// Called when a user program invokes the close() system call on the device
static int blink_release(struct inode *inode, struct file *file) {
    struct usb_blink *dev;

    dev = file->private_data;
    if (dev == NULL) {
        return -ENODEV;
    }

    // decrement the count on our device
    kref_put(&dev->kref, blink_delete);
    return 0;
}

#define NR_LEDS 8
#define NR_BYTES_BLINK_MSG 6

typedef struct {
    unsigned int led;
    unsigned int color;
} blink_msg_t;

void to_usb_control_msg(blink_msg_t* msg, unsigned char* container) {
    unsigned int color;

    container[0] = '\x05';
    container[1] = 0x00;

    container[2] = msg->led;

    color = msg->color;
    container[3] = ((color>>16) & 0xff);
    container[4] = ((color>>8) & 0xff);
    container[5] = (color & 0xff);
}

int send_usb_message(struct usb_blink *device, blink_msg_t* message) {
    unsigned char message_holder[NR_BYTES_BLINK_MSG];
    memset(message_holder, 0, NR_BYTES_BLINK_MSG);
    to_usb_control_msg(message, message_holder);
    return usb_control_msg(
        device->udev,
        // specify endpoint #0
        usb_sndctrlpipe(device->udev, 00),
        USB_REQ_SET_CONFIGURATION,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE,
        // wValue
        0x5,
        // wIndex=Endpoint
        0,
        message_holder,
        // message's size in bytes
        NR_BYTES_BLINK_MSG,
        0
    );
}

#define READ_BUF_LEN 256

void all_off(unsigned int* command) {
    int i;
    for (i = 0; i < NR_LEDS; i++) {
        command[i] = 0x000000;
    }
}

void parse_user_message(char* buf, unsigned int* command) {
    int ret;
    int led;
    char* token;
    unsigned int color;

    all_off(command);
    while ((token = strsep(&buf, ","))) {
        if (!(*token)) {
            continue;
        }

        ret = sscanf(token, "%i:%X", &led, &color);
        if (ret == 2 && led > 0 && led <= NR_LEDS) {
            command[led - 1] = color;
        }
    }
}

// Called when a user program invokes the write() system call on the device
static ssize_t blink_write(
    struct file *file,
    const char __user* user_buffer,
    size_t len,
    loff_t *off
) {

    struct usb_blink *dev = file->private_data;

    char own_buffer[READ_BUF_LEN];
    unsigned int user_command[NR_LEDS];

    int i = 0;
    int retval = 0;
    blink_msg_t message;

    if (copy_from_user(own_buffer, user_buffer, len)) {
        return -EFAULT;
    }

    own_buffer[len] = '\0';
    memset(user_command, 0, NR_LEDS);
    parse_user_message(own_buffer, user_command);

    message = (blink_msg_t) {
        .led = 0,
        .color = 0
    };

    for (i = 0; i < NR_LEDS; i++) {
        message.led = i;
        message.color = user_command[i];

        retval = send_usb_message(dev, &message);
        if (retval < 0) {
            printk(KERN_ALERT "Executed with retval=%d\n", retval);
            return retval;
        }
    }

    (*off) += len;
    return len;
}

/*
 * Operations associated with the character device 
 * exposed by driver
 */
static const struct file_operations blink_fops = {
    .owner   =  THIS_MODULE,
    .write   =  blink_write,    // write() operation on the file
    .open    =  blink_open,     // open() operation on the file
    .release =  blink_release,  // close() operation on the file
};

/*
 * Return permissions and pattern enabling udev 
 * to create device file names under /dev
 *
 * For each blinkstick connected device a character device file
 * named /dev/usb/blinkstick<N> will be created automatically  
 */
char* set_device_permissions(struct device *dev, umode_t *mode) {
    if (mode) {
        // RW permissions
        (*mode)=0666;
    }

    // Return formatted string
    return kasprintf(GFP_KERNEL, "usb/%s", dev_name(dev));
}


/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver blink_class = {
    .name       =  "blinkstick%d",  // Pattern used to create device files
    .devnode    =  set_device_permissions,
    .fops       =  &blink_fops,
    .minor_base =  USB_BLINK_MINOR_BASE,
};

/*
 * Invoked when the USB core detects a new
 * blinkstick device connected to the system.
 */
static int blink_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_blink *dev;
    int retval = -ENOMEM;

    /*
     * Allocate memory for a usb_blink structure.
     * This structure represents the device state.
     * The driver assigns a separate structure to each blinkstick device
     */
    dev = vmalloc(sizeof(struct usb_blink));

    if (!dev) {
        dev_err(&interface->dev, "Out of memory\n");
        goto error;
    }

    // Initialize the various fields in the usb_blink structure
    kref_init(&dev->kref);
    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;

    // save our data pointer in this interface device
    usb_set_intfdata(interface, dev);

    // we can register the device now, as it is ready
    retval = usb_register_dev(interface, &blink_class);
    if (retval) {
        // something prevented us from registering this driver
        dev_err(&interface->dev, "Not able to get a minor for this device.\n");
        usb_set_intfdata(interface, NULL);
        goto error;
    }

    // let the user know what node this device is now attached to
    dev_info(
        &interface->dev,
        "Blinkstick device now attached to blinkstick-%d",
        interface->minor
    );

    return 0;

error:
    if (dev) {
        // this frees up allocated memory
        kref_put(&dev->kref, blink_delete);
    }

    return retval;
}

/*
 * Invoked when a blinkstick device is
 * disconnected from the system.
 */
static void blink_disconnect(struct usb_interface *interface) {
    struct usb_blink *dev;
    int minor = interface->minor;

    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);

    // give back our minor
    usb_deregister_dev(interface, &blink_class);

    // prevent more I/O from starting
    dev->interface = NULL;

    // decrement our usage count
    kref_put(&dev->kref, blink_delete);

    dev_info(&interface->dev, "Blinkstick device #%d has been disconnected", minor);
}

// Define these values to match your devices
#define BLINKSTICK_VENDOR_ID    0X20A0
#define BLINKSTICK_PRODUCT_ID   0X41E5

// table of devices that work with this driver
static const struct usb_device_id blink_table[] = {
    { USB_DEVICE(BLINKSTICK_VENDOR_ID,  BLINKSTICK_PRODUCT_ID) },
    { }     // Terminating entry
};

MODULE_DEVICE_TABLE(usb, blink_table);

static struct usb_driver blink_driver = {
    .name       =  "blinkstick",
    .probe      =  blink_probe,
    .disconnect =  blink_disconnect,
    .id_table   =  blink_table,
};

// Module initialization
int blinkdrv_module_init(void) {
   return usb_register(&blink_driver);
}

// Module cleanup function
void blinkdrv_module_cleanup(void) {
  usb_deregister(&blink_driver);
}

module_init(blinkdrv_module_init);
module_exit(blinkdrv_module_cleanup);
