#ifndef _MODLIST_H
#define _MODLIST_H

#include <asm-generic/uaccess.h>
#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

struct callback_data {
    // TODO: Maybe another atomic to mark for deletion?

    // Current number of elements
    atomic_t elts;
    // Max number of elements allowed
    int max_elts;
    // Per-list spinlock
    spinlock_t c_lock;
    // Private list
    struct list_head* c_list;
};

struct list_head* list_alloc(void);
void list_dealloc(struct list_head *, spinlock_t *);

struct callback_data* call_alloc(void);
void call_dealloc(struct callback_data *);

const struct file_operations* get_fops(void);

#endif /* _MODLIST_H */
