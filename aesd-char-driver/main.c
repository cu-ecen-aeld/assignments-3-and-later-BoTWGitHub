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
    ssize_t retval = 0;
    struct aesd_dev *data;
    size_t entry_offset = 0;
    struct aesd_buffer_entry *entry;
    int res;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    data = (struct aesd_dev *)filp->private_data;
    if(data == NULL) {
        return -EFAULT;
    }
    retval = mutex_lock_interruptible(&data->mu);
    if (retval != 0) {
        return -EINTR;
    }
    
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&data->cbuffer, *f_pos+data->seek_index, &entry_offset);
    
    if(entry) {
        retval = entry->size - entry_offset;
        if(retval > count) {
            retval = count;
        }
        
        res = copy_to_user(buf, &entry->buffptr[entry_offset], retval);
        if(res != 0) {
            retval-=res;
        }
        data->seek_index+=retval;
    }
    else {
        data->seek_index = 0;
    }

    mutex_unlock(&data->mu);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_buffer_entry *data;
    char* ptr;
    int res = 0;
    struct aesd_dev *pack;
    struct aesd_buffer_entry *tmp;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    pack = (struct aesd_dev *)filp->private_data;
    if(pack == NULL) {
        return -EFAULT;
    }

    retval = mutex_lock_interruptible(&pack->mu);
    if (retval != 0) {
        return -EINTR;
    }

    if(pack->working_index != pack->cbuffer.in_offs) {
        data = &pack->cbuffer.entry[pack->working_index];
        ptr = kmalloc(sizeof(char)*(data->size+count), GFP_KERNEL);
        if(ptr == NULL) {
            mutex_unlock(&pack->mu);
            return retval;
        }
        memcpy(ptr, data->buffptr, data->size);
        kfree(data->buffptr);
        data->buffptr = ptr;
        ptr+=data->size;
        res = copy_from_user(ptr, buf, count);
        data->size+=(count-res);
        
    } else {
        data = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
        if(data == NULL) {
            mutex_unlock(&pack->mu);
            return retval;
        }
        data->buffptr = kmalloc(count, GFP_KERNEL);
        if(data->buffptr == NULL) {
            kfree(data);
            mutex_unlock(&pack->mu);
            return retval;
        }
        ptr = (char*)data->buffptr;
        res = copy_from_user(ptr, buf, count);
        data->size = count - res;

        tmp = aesd_circular_buffer_add_entry(&pack->cbuffer, data);
        if(tmp) {
            kfree(tmp);
        }
    }
    if(data->buffptr[data->size-1] == '\n') {
        pack->working_index = pack->cbuffer.in_offs;
    }
    retval = count - res;
    mutex_unlock(&pack->mu);
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
