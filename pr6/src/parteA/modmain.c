#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <asm-generic/uaccess.h>

#include "modlist.h"

#define LIST_LEN 25
#define CONFIG_BUFFER 50

static atomic_t current_entries = ATOMIC_INIT(0);

static unsigned int max_entries = 4;
static unsigned int max_size = 10;

module_param(max_entries, uint, 0000);
MODULE_PARM_DESC(max_entries, "Maximum number of dynamic lists");

module_param(max_size, uint, 0000);
MODULE_PARM_DESC(max_size, "Maximum number of elements inside the dynamic lists");

struct list_head* main_list;
typedef struct list_item_t {
    char* list_name;
    spinlock_t private_lock;
    struct list_head* private_list;
    struct proc_dir_entry* proc_entry;

    struct list_head links;
} list_item_t;

DEFINE_SPINLOCK(list_lock);

struct list_head* proc_list_init(void);

int add_proc_entry(struct list_head *, char *);
void remove_matching_proc_entry(struct list_head *, char *);

bool proc_match_item(list_item_t *, char *);
int contains(struct list_head *, char *);

void proc_cleanup(struct list_head *);
void proc_free_item(list_item_t *);

static ssize_t config_proc_write(struct file *, const char *, size_t, loff_t *);

static struct proc_dir_entry* proc_dir = NULL;
static struct proc_dir_entry* config_entry;

static const struct file_operations config_entry_fops = {
    .write = config_proc_write
};

static ssize_t config_proc_write(struct file *filp, const char __user *buf,
                                 size_t len, loff_t *off) {

    char list_name[LIST_LEN];
    char own_buffer[CONFIG_BUFFER];

    if ((*off) > 0) {
        return 0;
    }

    printk(KERN_INFO "modmain: Writing config\n");

    if (copy_from_user(own_buffer, buf, len)) {
        return -EFAULT;
    }

    own_buffer[len] = '\0';
    *off += len;

    if (sscanf(own_buffer, "create %s", list_name)) {
        if (contains(main_list, list_name)) {
            printk(KERN_INFO "modmain: Entry %s already exists\n", list_name);
            return -EINVAL;
        }

        if (add_proc_entry(main_list, list_name) != 0) {
            return -ENOSPC;
        }

        return len;
    }
    
    if (sscanf(own_buffer, "delete %s", list_name)) {
        if (!contains(main_list, list_name)) {
            printk(KERN_INFO "modmain: Entry %s doesn't exist\n", list_name);
            return -EINVAL;
        }

        remove_matching_proc_entry(main_list, list_name);
        return len;
    }

    return -EINVAL;
}

int init_modmain(void) {
    proc_dir = proc_mkdir("list", NULL);
    if (!proc_dir) {
        printk(KERN_INFO "modmain: Can't create /proc directory\n");
        return -ENOMEM;
    }

    config_entry = proc_create("control", 0666, proc_dir, &config_entry_fops);
    if (config_entry == NULL) {
        remove_proc_entry("list", NULL);
        return -ENOMEM;
    }

    main_list = proc_list_init();
    INIT_LIST_HEAD(main_list);

    add_proc_entry(main_list, "default");

    printk(KERN_INFO "modmain: module loaded\n");

    return 0;
}

void exit_modmain(void) {
    proc_cleanup(main_list);
    vfree(main_list);
    remove_proc_entry("control", proc_dir);
    remove_proc_entry("list", NULL);
    printk(KERN_INFO "modmain: module undloaded\n");
}

// Util

struct list_head* proc_list_init(void) {
    struct list_head* head;

    head = (struct list_head*) vmalloc((sizeof(struct list_head)));
    memset(head, 0, sizeof(struct list_head));

    return (struct list_head*) head;
}

struct list_item_t* __litem_alloc(void) {
    list_item_t* item;
    item = vmalloc((sizeof(list_item_t)));
    memset(item, 0, sizeof(list_item_t));
    return item;
}

char* __lname_alloc(char* list_name) {
    char* new_name;
    int list_name_size = sizeof(char) * strlen(list_name) + 1;
    new_name = vmalloc(list_name_size);
    memset(new_name, 0, list_name_size);
    memcpy(new_name, list_name, list_name_size);
    return new_name;
}

void __add_proc_entry(struct list_head* list, list_item_t* item) {
    spin_lock(&list_lock);
    list_add_tail(&item->links, list);
    spin_unlock(&list_lock);
}

int add_proc_entry(struct list_head* list, char* stack_list_name) {
    list_item_t* obj;

    char* d_list_name;
    struct list_head* d_list;
    struct proc_dir_entry* d_entry;

    // Respect global max
    if (atomic_add_unless(&current_entries, 1, max_entries) == 0) {
        return -1;
    }

    obj = __litem_alloc();

    d_list = list_alloc();
    d_list_name = __lname_alloc(stack_list_name);
    d_entry = proc_create_data(stack_list_name, 0666, proc_dir, get_fops(), d_list);

    obj->list_name = d_list_name;
    obj->private_list = d_list;
    obj->proc_entry = d_entry;
    spin_lock_init(&obj->private_lock);

    __add_proc_entry(list, obj);

    return 0;
}

int contains(struct list_head* list, char* list_name) {
    int found = 0;
    struct list_head* cur_node = NULL;
    list_item_t* item = NULL;

    spin_lock(&list_lock);
    list_for_each(cur_node, list) {
        item = list_entry(cur_node, list_item_t, links);
        if (proc_match_item(item, list_name)) {
            found = 1;
            break;
        }
    }
    spin_unlock(&list_lock);

    return found;
}

void proc_cleanup(struct list_head* list) {
    struct list_head tmp;
    struct list_head* cur_node = NULL;
    struct list_head* aux_storage = NULL;
    list_item_t* item = NULL;

    INIT_LIST_HEAD(&tmp);

    spin_lock(&list_lock);
    list_for_each_safe(cur_node, aux_storage, list) {
        item = list_entry(cur_node, list_item_t, links);
        list_move(cur_node, &tmp);
    }
    spin_unlock(&list_lock);

    list_for_each_safe(cur_node, aux_storage, &tmp) {
        item = list_entry(cur_node, list_item_t, links);
        list_del(cur_node);
        proc_free_item(item);
    }
}

void remove_matching_proc_entry(struct list_head* list, char* data) {
    struct list_head tmp;
    struct list_head* cur_node = NULL;
    struct list_head* aux_storage = NULL;
    list_item_t* item = NULL;

    INIT_LIST_HEAD(&tmp);

    spin_lock(&list_lock);
    list_for_each_safe(cur_node, aux_storage, list) {
        item = list_entry(cur_node, list_item_t, links);
        if (proc_match_item(item, data)) {
            list_move(cur_node, &tmp);
        }
    }
    spin_unlock(&list_lock);

    list_for_each_safe(cur_node, aux_storage, &tmp) {
        item = list_entry(cur_node, list_item_t, links);
        list_del(cur_node);
        atomic_dec_if_positive(&current_entries);
        proc_free_item(item);
    }
}

bool proc_match_item(list_item_t* item, char* name) {
    return (0 == strncmp(item->list_name, name, strlen(name)));
}

void __free_item_contents(list_item_t* node) {
    remove_proc_entry(node->list_name, proc_dir);
    vfree(node->list_name);
    list_dealloc(node->private_list);
}

void proc_free_item(list_item_t* item) {
    __free_item_contents(item);
    vfree(item);
}

MODULE_LICENSE("GPL");

module_init(init_modmain);
module_exit(exit_modmain);
