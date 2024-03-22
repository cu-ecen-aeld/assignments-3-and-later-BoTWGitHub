/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h>
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Bo Lin TW"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev* dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = (void *)dev;

    PDEBUG("open");

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    unsigned long res;
    size_t entry_offset = 0;
    struct aesd_dev *data;
    struct aesd_buffer_entry *entry;
    ssize_t retval = 0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    data = (struct aesd_dev *)filp->private_data;
    if(data == NULL) {
        return -EFAULT;
    }

    retval = mutex_lock_interruptible(&data->mu);
    if (retval != 0) {
        return -EINTR;
    }
    
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&data->cbuffer, *f_pos, &entry_offset);
    
    if(entry) {
        retval = entry->size - entry_offset;
        if(retval <= 0) {
            mutex_unlock(&data->mu);
            return -EFAULT;
        } else if (retval > count) {
            retval = count;
        }
        
        res = copy_to_user(buf, &entry->buffptr[entry_offset], retval);
        retval -= res;
        if(retval <= 0) {
            mutex_unlock(&data->mu);
            return -EFAULT;
        }
        *f_pos += retval;
    }

    mutex_unlock(&data->mu);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    unsigned long res = 0;
    char* working_buf;
    struct aesd_dev *data;
    struct aesd_buffer_entry *working_entry;
    ssize_t retval = -ENOMEM;
    
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    data = (struct aesd_dev *)filp->private_data;
    if(data == NULL) {
        return -EFAULT;
    }

    retval = mutex_lock_interruptible(&data->mu);
    if (retval != 0) {
        return -EINTR;
    }

    if(data->working_index != data->cbuffer.in_offs) {
        working_entry = &data->cbuffer.entry[data->working_index];
        working_buf = kmalloc((working_entry->size + count), GFP_KERNEL);
        if(working_buf == NULL) {
            mutex_unlock(&data->mu);
            return ENOMEM;
        }
        memset(working_buf, '\0', (working_entry->size + count));
        memcpy(working_buf, working_entry->buffptr, working_entry->size);
        kfree((char *)working_entry->buffptr);
        working_entry->buffptr = working_buf;
        res = copy_from_user((working_buf + working_entry->size), buf, count);
        working_entry->size += (count - res);

    } else {
        working_entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
        if(working_entry == NULL) {
            mutex_unlock(&data->mu);
            return ENOMEM;
        }
        working_entry->buffptr = kmalloc(count, GFP_KERNEL);
        if(working_entry->buffptr == NULL) {
            kfree(data);
            mutex_unlock(&data->mu);
            return ENOMEM;
        }
        res = copy_from_user((char *)working_entry->buffptr, buf, count);
        working_entry->size = count - res;

        working_buf = (char*)aesd_circular_buffer_add_entry(&data->cbuffer, working_entry);
        if(working_buf) {
            kfree(working_buf);
        }
        kfree(working_entry);
        working_entry = &data->cbuffer.entry[data->working_index];
    }

    if(working_entry->buffptr[working_entry->size-1] == '\n') {
        data->working_index = data->cbuffer.in_offs;
    }

    retval = count - res;
    mutex_unlock(&data->mu);

    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    printk(KERN_WARNING "Hello from aesdchar bolintw\n");
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.mu);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        mutex_destroy(&aesd_device.mu);
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    int index;
    struct aesd_buffer_entry *cur;
    printk(KERN_WARNING "Goodbye from aesdchar bolintw\n");

    cdev_del(&aesd_device.cdev);
    
    AESD_CIRCULAR_BUFFER_FOREACH(cur, &aesd_device.cbuffer, index) {
        if(cur->buffptr) {
            kfree(cur->buffptr);
            cur->buffptr = NULL;
        }
    }

    mutex_unlock(&aesd_device.mu);
    mutex_destroy(&aesd_device.mu);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
