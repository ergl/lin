#include <asm-generic/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#define READ_BUF_LEN 256

MODULE_LICENSE("GPL");

struct list_head* llist;
typedef struct list_item_t {
#ifdef CHARLIST
  char* data;
  size_t data_len;
#else
  int data;
#endif
  struct list_head links;
} list_item_t;

static struct proc_dir_entry* proc_entry;

struct list_head* list_head_init(void);
struct list_item_t* list_item_init(struct list_item_t data);

#ifdef CHARLIST
char* list_item_data_init(char* data, size_t len);
void add_item(struct list_head* list, char* data, size_t len);
bool match_item(list_item_t* item, char* data, size_t len);
void remove_item(struct list_head* list, char* data, size_t len);
#else
void add_item(struct list_head* list, int data);
bool match_item(list_item_t* item, int data);
void remove_item(struct list_head* list, int data);
#endif
void free_item(list_item_t* item);
void cleanup(struct list_head* list);

int print_list(struct list_head* list, char* buf);

int scancleanup(const char* buffer);
#ifdef CHARLIST
int scanadd(const char* buffer, char* dst);
int scanremove(const char* buffer, char* dst);
#else
int scanadd(const char* buffer, int* container);
int scanremove(const char* buffer, int* container);
#endif

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

  to_copy = size <= len ? size : len;
  if (copy_to_user(buf, &own_buffer, to_copy)) {
    return -EFAULT;
  }
  (*off) += to_copy;
  return to_copy;
}

static ssize_t modlist_write(struct file* fd, const char __user* buf,
                             size_t len, loff_t* off) {

#ifdef CHARLIST
  char data[READ_BUF_LEN];
  size_t data_len;
#else
  int data;
#endif
  char own_buffer[READ_BUF_LEN];

  if (copy_from_user(own_buffer, buf, len)) {
    return -EFAULT;
  }

  printk(KERN_ALERT "Modlist: Calling write\n");

  own_buffer[len] = '\0';

#ifdef CHARLIST

  data_len = scanadd(own_buffer, data);
  if (data_len) {
    add_item(llist, data, data_len);
    return len;
  }

  data_len = scanremove(own_buffer, data);
  if (data_len) {
    remove_item(llist, data, data_len);
    return len;
  }

  if (scancleanup(own_buffer)) {
    cleanup(llist);
  }

#else

  if (scanadd(own_buffer, &data)) {
    add_item(llist, data);
  } else if (scanremove(own_buffer, &data)) {
    remove_item(llist, data);
  } else if (scancleanup(own_buffer)) {
    cleanup(llist);
  }

#endif

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

void exit_modlist_module(void) {
  cleanup(llist);
  vfree(llist);

  remove_proc_entry("modlist", NULL);

  printk(KERN_INFO "Modlist: Module unloaded.\n");
}

//////////////////////
//  UTIL FUNCTIONS  //
//////////////////////

struct list_head* list_head_init(void) {
  struct list_head* head;

  head = (struct list_head*)vmalloc((sizeof(struct list_head)));
  memset(head, 0, sizeof(struct list_head));

  return (struct list_head*)head;
}

struct list_item_t* list_item_init(struct list_item_t data) {
  struct list_item_t* item;
  item = (struct list_item_t*)vmalloc((sizeof(struct list_item_t)));
  memset(item, 0, sizeof(struct list_item_t));
  memcpy(item, &data, sizeof(struct list_item_t));
  return (struct list_item_t*)item;
}

int print_list(struct list_head* list, char* buf) {
  int buf_len = 0;
  int read_bytes = 0;

  struct list_head* cur_node = NULL;
  struct list_item_t* item = NULL;

  list_for_each(cur_node, list) {
    item = list_entry(cur_node, struct list_item_t, links);
#ifdef CHARLIST
    read_bytes = snprintf(buf, (item->data_len + 1), "%s\n", item->data);
#else
    read_bytes = sprintf(buf, "%i\n", item->data);
#endif
    if (read_bytes == -1) {
      return read_bytes;
    }

    buf += read_bytes;
    buf_len += read_bytes;
  }

  return buf_len;
}

int scancleanup(const char* buffer) {
  return (0 == strncmp(buffer, "cleanup", 7));
}

#ifdef CHARLIST
int scanadd(const char* buffer, char* dst) {
  int i;
  if (0 == strncmp(buffer, "add ", 4)) {
    for (i = 0; i < strlen(buffer); i++) {
      dst[i] = buffer[i+4];
    }
    return strlen(dst);
  }

  return 0;
}
#else
int scanadd(const char* buffer, int* container) {
  const char* format = "add %i";
  return sscanf(buffer, format, container);
}
#endif

#ifdef CHARLIST
int scanremove(const char* buffer, char* dst) {
  int i;
  if (0 == strncmp(buffer, "remove ", 7)) {
    for (i = 0; i < strlen(buffer); i++) {
      dst[i] = buffer[i+7];
    }
    return strlen(dst);
  }

  return 0;
}
#else
int scanremove(const char* buffer, int* container) {
  const char* format = "remove %i";
  return sscanf(buffer, format, container);
}
#endif


#ifdef CHARLIST

char* list_item_data_init(char* data, size_t len) {
  char* dyn_data;
  dyn_data = (char *)vmalloc(len);
  memset(dyn_data, 0, len);
  memcpy(dyn_data, data, len);
  return (char *) dyn_data;
}

void add_item(struct list_head* list, char* data, size_t len) {
  char* dyn_data;
  struct list_item_t* new_item;
  dyn_data = list_item_data_init(data, len);
  new_item = list_item_init((struct list_item_t){.data = dyn_data, .data_len = len});
  list_add_tail(&new_item->links, list);
}

#else
void add_item(struct list_head* list, int data) {
  struct list_item_t* new_item;
  new_item = list_item_init((struct list_item_t){.data = data});
  list_add_tail(&new_item->links, list);
}

#endif

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

#ifdef CHARLIST
void remove_item(struct list_head* list, char* data, size_t len) {
#else
void remove_item(struct list_head* list, int data) {
#endif
  struct list_head* cur_node = NULL;
  struct list_head* aux_storage = NULL;
  struct list_item_t* item = NULL;

  list_for_each_safe(cur_node, aux_storage, llist) {
    item = list_entry(cur_node, struct list_item_t, links);
#ifdef CHARLIST
    if (match_item(item, data, len)) {
#else
    if (match_item(item, data)) {
#endif
      list_del(cur_node);
      free_item(item);
    }
  }
}

#ifdef CHARLIST
bool match_item(list_item_t* item, char* data, size_t len) {
  if (item->data_len == len) {
    return (0 == strncmp(item->data, data, item->data_len));
  }

  return false;
}
#else
bool match_item(list_item_t* item, int data) {
  return item->data == data;
}
#endif

void free_item(list_item_t* item) {
#ifdef CHARLIST
  vfree(item->data);
#endif
  vfree(item);
}

module_init(init_modlist_module);
module_exit(exit_modlist_module);
