#include <asm-generic/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#define READ_BUF_LEN 256

MODULE_LICENSE("GPL");

static struct proc_dir_entry* proc_entry;

struct list_head* llist;
typedef struct list_item_t {
  int data;
  struct list_head links;
} list_item_t;

int to_c_str(char* buf, char* out, size_t len) {
  memcpy(out, buf, len);
  out[len + 1] = '\0';
  return len + 1;
}

// int to_c_str(char* buf, size_t len) {
//   out[len + 1] = '\0';
//   return len + 1;
// }

int print_list(struct list_head* list, char* buf) {
  int buf_len = 0;
  int read_bytes = 0;

  struct list_head* cur_node = NULL;
  struct list_item_t* item = NULL;

  list_for_each(cur_node, list) {
    item = list_entry(cur_node, struct list_item_t, links);
    read_bytes = sprintf(buf, "%i\n", item->data);
    if (read_bytes == -1) {
      return read_bytes;
    }

    buf += read_bytes;
    buf_len += read_bytes;
  }

  return buf_len;
}

static ssize_t modlist_read(struct file* fd, char __user* buf, size_t len,
                            loff_t* off) {
  int size;
  int to_copy;
  char own_buffer[READ_BUF_LEN];

  printk(KERN_ALERT "Modlist: Calling read\n");

  if (len == 0) {
    return 0;
  }

  if ((*off) > 0) {
    return 0;
  }

  size = print_list(llist, own_buffer);
  if (size <= 0) {
    printk(KERN_INFO "Modlist: Empty list\n");
    return 0;
  }

  printk(KERN_INFO "Modlist: Read %i bytes from list\n", size);
  to_copy = size <= len ? size : len;
  if (copy_to_user(buf, &own_buffer, to_copy)) {
    return -EFAULT;
  }
  (*off) += to_copy;
  return to_copy;
}

struct list_item_t* list_item_init(struct list_item_t data) {
  struct list_item_t* item;
  item = (struct list_item_t*)vmalloc((sizeof(struct list_item_t)));
  memset(item, 0, sizeof(struct list_item_t));
  memcpy(item, &data, sizeof(struct list_item_t));
  return (struct list_item_t*)item;
}

struct list_head* list_head_init(void) {
  struct list_head* head;

  head = (struct list_head*)vmalloc((sizeof(struct list_head)));
  memset(head, 0, sizeof(struct list_head));

  return (struct list_head*)head;
}

static ssize_t modlist_write(struct file* fd, const char __user* buf,
                             size_t len, loff_t* off) {
  struct list_item_t* new_item;
  printk(KERN_ALERT "Modlist: Calling write\n");

  new_item = list_item_init((struct list_item_t){.data = 10});
  list_add_tail(&new_item->links, llist);
  return len;
}

static const struct file_operations proc_entry_fops = {.read = modlist_read,
                                                       .write = modlist_write};

int init_modlist_module(void) {
  llist = list_head_init();
  INIT_LIST_HEAD(llist);

  proc_entry = proc_create("modlist", 0666, NULL, &proc_entry_fops);
  if (proc_entry == NULL) {
    printk(KERN_INFO "Modlist: Can't create /proc entry\n");
    return -ENOMEM;
  } else {
    printk(KERN_INFO "Modlist: Module loaded\n");
  }

  return 0;
}

void cleanup(struct list_head* list) {
  struct list_head* cur_node = NULL;
  struct list_head* aux_storage = NULL;
  struct list_item_t* item = NULL;

  list_for_each_safe(cur_node, aux_storage, llist) {
    item = list_entry(cur_node, struct list_item_t, links);
    list_del(cur_node);
    vfree(item);
  }
}

void exit_modlist_module(void) {
  cleanup(llist);
  vfree(llist);

  remove_proc_entry("modlist", NULL);

  printk(KERN_INFO "Modlist: Module unloaded.\n");
}

module_init(init_modlist_module);
module_exit(exit_modlist_module);
