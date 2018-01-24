#ifndef _MODLIST_H
#define _MODLIST_H

#include <asm-generic/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

struct list_head* list_alloc(void);
void list_dealloc(struct list_head *);

const struct file_operations* get_fops(void);

#endif /* _MODLIST_H */
