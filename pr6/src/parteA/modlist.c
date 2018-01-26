#include "modlist.h"

#define READ_BUF_LEN 256

typedef struct list_item_t {
  int data;
  struct list_head links;
} list_item_t;

struct list_head* __list_head_init(void);
struct list_item_t* __list_item_init(struct list_item_t* data);

void __add_item(struct list_head *, spinlock_t *, int);
bool __match_item(list_item_t *, int);
void __remove_item(struct list_head *, spinlock_t *, int);
void __free_item(list_item_t *);
void __cleanup(struct list_head *, spinlock_t *);

int __print_list(struct list_head *, spinlock_t *, char *, int);

int __scancleanup(const char *);
int __scanadd(const char *, void *);
int __scanremove(const char *, void *);

static int modlist_open(struct inode* inode, struct file* filp) {
    struct callback_data* c_data = (struct callback_data *) PDE_DATA(inode);
    if (atomic_read(&c_data->will_delete) == 1) {
        return -ENOENT;
    }
    return 0;
}

static ssize_t modlist_read(struct file* filp, char __user* buf, size_t len,
                            loff_t* off) {
    int size;
    int to_copy;
    char own_buffer[READ_BUF_LEN];

    struct callback_data* c_data;
    struct list_head* private_list;

    if (len == 0) {
        return 0;
    }

    if ((*off) > 0) {
        return 0;
    }

    printk(KERN_ALERT "multilist->modlist: calling read\n");

    c_data = (struct callback_data *) PDE_DATA(filp->f_inode);
    private_list = c_data->c_list;
    size = __print_list(private_list, &c_data->c_lock, own_buffer, READ_BUF_LEN);
    if (size <= 0) {
        printk(KERN_INFO "multilist->modlist: Empty list\n");
        return 0;
    }

    to_copy = size <= len ? size : len;
    if (copy_to_user(buf, &own_buffer, to_copy)) {
        return -EFAULT;
    }

    (*off) += to_copy;
    return to_copy;
}

static ssize_t modlist_write(struct file* filp, const char __user* buf,
                             size_t len, loff_t* off) {

    int data;
    char own_buffer[READ_BUF_LEN];

    struct callback_data* c_data;

    int max_elts;
    struct list_head* private_list;

    if (copy_from_user(own_buffer, buf, len)) {
        return -EFAULT;
    }

    own_buffer[len] = '\0';

    printk(KERN_ALERT "multilist->modlist: calling write\n");

    c_data = (struct callback_data *) PDE_DATA(filp->f_inode);

    max_elts = c_data->max_elts;
    private_list = c_data->c_list;

    if (__scanadd(own_buffer, &data)) {
        if (atomic_add_unless(&c_data->elts, 1, max_elts) == 0) {
            return -ENOSPC;
        }

        __add_item(private_list, &c_data->c_lock, data);
    } else if (__scanremove(own_buffer, &data)) {
        atomic_dec_if_positive(&c_data->elts);
        __remove_item(private_list, &c_data->c_lock, data);
    } else if (__scancleanup(own_buffer)) {
        atomic_set(&c_data->elts, 0);
        __cleanup(private_list, &c_data->c_lock);
    } else {
        return -EINVAL;
    }

    return len;
}

static const struct file_operations proc_entry_fops = {
    .open = modlist_open,
    .read = modlist_read,
    .write = modlist_write
};

struct callback_data* call_alloc(void) {
    struct callback_data* data;

    data = vmalloc(sizeof(struct callback_data));
    memset(data, 0, sizeof(struct callback_data));

    return data;
}

void call_dealloc(struct callback_data* data) {
    vfree(data);
}

const struct file_operations* get_fops(void) {
    return &proc_entry_fops;
}

struct list_head* list_alloc(void) {
    struct list_head* private_list = __list_head_init();
    INIT_LIST_HEAD(private_list);
    return private_list;
}

void list_dealloc(struct list_head* private_list, spinlock_t* lock) {
    __cleanup(private_list, lock);
    vfree(private_list);
}

//////////////////////
//  UTIL FUNCTIONS  //
//////////////////////

struct list_head* __list_head_init(void) {
    struct list_head* head;

    head = vmalloc((sizeof(struct list_head)));
    memset(head, 0, sizeof(struct list_head));

    return head;
}

struct list_item_t* __list_item_init(list_item_t* data) {
  list_item_t* item;
  item = vmalloc((sizeof(list_item_t)));
  memset(item, 0, sizeof(list_item_t));
  memcpy(item, data, sizeof(list_item_t));
  return item;
}

// Attempt to copy the entire list contents into a `len`-sized `buf`.
int __print_list(struct list_head* list, spinlock_t* lock, char* buf, int len) {
    int sz;
    int buf_len = 0;
    int read_bytes = 0;

    struct list_head* cur_node = NULL;
    list_item_t* item = NULL;

    spin_lock(lock);
    list_for_each(cur_node, list) {
        item = list_entry(cur_node, list_item_t, links);
        // Figure out the size we need
        sz = snprintf(NULL, 0, "%i\n", item->data);

        // If we'd go over the size of the buffer,
        // discard next entries and return
        if ((buf_len + sz) > len) {
            printk(KERN_INFO "__print_list read over %d (max), returing %d\n", len, buf_len);
            break;
        }

        // If an error is detected, discard entry and abort scan
        read_bytes = sprintf(buf, "%i\n", item->data);
        if (read_bytes == -1) {
            printk(KERN_INFO "__print_list read -1\n");
            break;
        }

        buf += read_bytes;
        buf_len += read_bytes;
    }
    spin_unlock(lock);

    return buf_len;
}

int __scancleanup(const char* buffer) {
    return (0 == strncmp(buffer, "cleanup", 7));
}

int __scanadd(const char* buffer, void* container) {
    const char* format = "add %i";
    return sscanf(buffer, format, container);
}

int __scanremove(const char* buffer, void* container) {
    const char* format = "remove %i";
    return sscanf(buffer, format, container);
}

void __add_item(struct list_head* list, spinlock_t* lock, int data) {
    list_item_t* new_item;
    new_item = __list_item_init(&(list_item_t) {
        .data = data
    });

    spin_lock(lock);
    list_add_tail(&new_item->links, list);
    spin_unlock(lock);
}

void __cleanup(struct list_head* list, spinlock_t* lock) {
    struct list_head* cur_node = NULL;
    struct list_head* aux_storage = NULL;
    list_item_t* item = NULL;

    spin_lock(lock);
    list_for_each_safe(cur_node, aux_storage, list) {
      item = list_entry(cur_node, list_item_t, links);
      list_del(cur_node);
      vfree(item);
    }
    spin_unlock(lock);
}

void __remove_item(struct list_head* list, spinlock_t* lock, int data) {
    struct list_head* cur_node = NULL;
    struct list_head* aux_storage = NULL;
    list_item_t* item = NULL;

    spin_lock(lock);
    list_for_each_safe(cur_node, aux_storage, list) {
        item = list_entry(cur_node, list_item_t, links);
        if (__match_item(item, data)) {
            list_del(cur_node);
            __free_item(item);
        }
    }
    spin_unlock(lock);
}

bool __match_item(list_item_t* item, int data) {
    return item->data == data;
}

void __free_item(list_item_t* item) {
  vfree(item);
}

MODULE_LICENSE("GPL");
