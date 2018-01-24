#include "modlist.h"

#define READ_BUF_LEN 256

typedef struct list_item_t {
  int data;
  struct list_head links;
} list_item_t;

DEFINE_SPINLOCK(sp);

struct list_head* __list_head_init(void);
struct list_item_t* __list_item_init(struct list_item_t data);

void __add_item(struct list_head* list, int data);
bool __match_item(list_item_t* item, int data);
void __remove_item(struct list_head* list, int data);
void __free_item(list_item_t* item);
void __cleanup(struct list_head* list);

int __print_list(struct list_head* list, char* buf);

int __scancleanup(const char* buffer);
int __scanadd(const char* buffer, void* container);
int __scanremove(const char* buffer, void* container);

static ssize_t modlist_read(struct file* filp, char __user* buf, size_t len,
                            loff_t* off) {
    int size;
    int to_copy;
    char own_buffer[READ_BUF_LEN];
    struct list_head* private_list;

    if (len == 0) {
        return 0;
    }

    if ((*off) > 0) {
        return 0;
    }

    printk(KERN_ALERT "multilist->modlist: calling read");

    private_list = (struct list_head *) PDE_DATA(filp->f_inode);
    size = __print_list(private_list, own_buffer);
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
    struct list_head* private_list;

    if (copy_from_user(own_buffer, buf, len)) {
        return -EFAULT;
    }

    own_buffer[len] = '\0';

    printk(KERN_ALERT "multilist->modlist: calling write");

    private_list = (struct list_head *) PDE_DATA(filp->f_inode);
    if (__scanadd(own_buffer, &data)) {
        __add_item(private_list, data);
    } else if (__scanremove(own_buffer, &data)) {
        __remove_item(private_list, data);
    } else if (__scancleanup(own_buffer)) {
        __cleanup(private_list);
    }

    return len;
}

static const struct file_operations proc_entry_fops = {
    .read = modlist_read,
    .write = modlist_write
};

const struct file_operations* get_fops(void) {
    return &proc_entry_fops;
}

struct list_head* list_alloc(void) {
    struct list_head* private_list = __list_head_init();
    INIT_LIST_HEAD(private_list);
    return private_list;
}

void cleanup_own_list(struct list_head* private_list) {
    __cleanup(private_list);
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

struct list_item_t* __list_item_init(list_item_t data) {
  list_item_t* item;
  item = vmalloc((sizeof(list_item_t)));
  memset(item, 0, sizeof(list_item_t));
  memcpy(item, &data, sizeof(list_item_t));
  return item;
}

int __print_list(struct list_head* list, char* buf) {
    int buf_len = 0;
    int read_bytes = 0;

    struct list_head* cur_node = NULL;
    list_item_t* item = NULL;

    spin_lock(&sp);
    list_for_each(cur_node, list) {
        item = list_entry(cur_node, list_item_t, links);
        read_bytes = sprintf(buf, "%i\n", item->data);
        if (read_bytes == -1) {
            return read_bytes;
        }

        buf += read_bytes;
        buf_len += read_bytes;
    }
    spin_unlock(&sp);

    return buf_len;
}

int __scancleanup(const char* buffer) {
    return (0 == strncmp(buffer, "__cleanup", 7));
}

int __scanadd(const char* buffer, void* container) {
    const char* format = "add %i";
    return sscanf(buffer, format, container);
}

int __scanremove(const char* buffer, void* container) {
    const char* format = "remove %i";
    return sscanf(buffer, format, container);
}

void __add_item(struct list_head* list, int data) {
    list_item_t* new_item;
    new_item = __list_item_init((list_item_t){.data = data});
    spin_lock(&sp);
    list_add_tail(&new_item->links, list);
    spin_unlock(&sp);
}

void __cleanup(struct list_head* list) {
    struct list_head* cur_node = NULL;
    struct list_head* aux_storage = NULL;
    list_item_t* item = NULL;

    spin_lock(&sp);
    list_for_each_safe(cur_node, aux_storage, list) {
      item = list_entry(cur_node, list_item_t, links);
      list_del(cur_node);
      vfree(item);
    }
    spin_unlock(&sp);
}

void __remove_item(struct list_head* list, int data) {
    struct list_head* cur_node = NULL;
    struct list_head* aux_storage = NULL;
    list_item_t* item = NULL;

    spin_lock(&sp);
    list_for_each_safe(cur_node, aux_storage, list) {
        item = list_entry(cur_node, list_item_t, links);
        if (__match_item(item, data)) {
            list_del(cur_node);
            __free_item(item);
        }
    }
    spin_unlock(&sp);
}

bool __match_item(list_item_t* item, int data) {
    return item->data == data;
}

void __free_item(list_item_t* item) {
  vfree(item);
}

MODULE_LICENSE("GPL");
