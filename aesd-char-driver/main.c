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
    PDEBUG("open");
    
    filp->private_data = (void *)&aesd_device;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    filp->private_data = NULL;
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    struct aesd_dev *data = (struct aesd_dev *)filp->private_data;
    if(data == NULL) {
        return retval;
    }
    read_lock(&data->lock);
    size_t entry_offset = 0;
    struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(&data->cbuffer, *f_pos+data->seek_index, &entry_offset);
    
    if(entry) {
        retval = entry->size - entry_offset;
        if(retval > count) {
            retval = count;
        }
        
        int res = copy_to_user(buf, &entry->buffptr[entry_offset], retval);
        if(res != 0) {
            retval-=res;
        }
        data->seek_index+=retval;
    }
    else {
        data->seek_index = 0;
    }
    read_unlock(&data->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    struct aesd_buffer_entry *data;
    char* ptr;
    int res = 0;

    struct aesd_dev* pack = (struct aesd_dev *)filp->private_data;
    if(pack == NULL) {
        return retval;
    }
    write_lock(&pack->lock);
    if(pack->working_index != pack->cbuffer.in_offs) {
        data = &pack->cbuffer.entry[pack->working_index];
        ptr = kmalloc(sizeof(char)*(data->size+count), GFP_KERNEL);
        if(ptr == NULL) {
            write_unlock(&pack->lock);
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
            write_unlock(&pack->lock);
            return retval;
        }
        data->buffptr = kmalloc(count, GFP_KERNEL);
        if(data->buffptr == NULL) {
            kfree(data);
            write_unlock(&pack->lock);
            return retval;
        }
        ptr = data->buffptr;
        res = copy_from_user(ptr, buf, count);
        data->size = count - res;

        struct aesd_buffer_entry *tmp = aesd_circular_buffer_add_entry(&pack->cbuffer, data);
        if(tmp) {
            kfree(tmp);
        }
    }
    if(data->buffptr[data->size-1] == '\n') {
        pack->working_index = pack->cbuffer.in_offs;
    }
    retval = count - res;
    write_unlock(&pack->lock);
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
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    rwlock_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    int index;
    struct aesd_buffer_entry *cur;
    AESD_CIRCULAR_BUFFER_FOREACH(cur, &aesd_device.cbuffer, index) {
        if(cur->buffptr) {
            kfree(cur->buffptr);
            cur->buffptr = NULL;
        }
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
