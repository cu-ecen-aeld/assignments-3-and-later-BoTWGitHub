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
#include "aesd_ioctl.h"
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

static long aesd_move_the_pos(struct file *filp, struct aesd_seekto *seekto)
{
    int count = 0;
    int cmd;
    int tmp_out;
    struct aesd_dev *data;

    PDEBUG("aesd_move_the_pos: %d %d", seekto->write_cmd, seekto->write_cmd_offset);

    data = (struct aesd_dev *)filp->private_data;
    if(data == NULL) {
        printk(KERN_ERR "data retrieve error");
        return -EFAULT;
    }
    if(data->cbuffer.in_offs == data->cbuffer.out_offs && !data->cbuffer.full) {
        printk(KERN_ERR "ring buffer empty");
        return -EINVAL;
    }

    tmp_out = data->cbuffer.out_offs;
    for(cmd = 0; cmd < seekto->write_cmd; cmd++) {
        count += data->cbuffer.entry[tmp_out].size;
        tmp_out = (tmp_out+1)%AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        if(tmp_out == data->cbuffer.in_offs) {
            printk(KERN_ERR "not enough data 1");
            return -EINVAL;
        }
    }
    if(data->cbuffer.entry[tmp_out].size < seekto->write_cmd_offset) {
        printk(KERN_ERR "not enough data 2");
        return -EINVAL;
    }
    count += seekto->write_cmd_offset;
    filp->f_pos = count;
    return 0;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
    struct aesd_seekto seekto;

	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    switch(cmd) {
        case AESDCHAR_IOCSEEKTO:
            if( copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0) {
                printk(KERN_ERR "AESDCHAR_IOCSEEKTO: copy_from_user failed");
                retval = EFAULT;
            } else {
                retval = aesd_move_the_pos(filp, &seekto);
            }
            break;

        default:
            return -ENOTTY;
    }
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .unlocked_ioctl = aesd_ioctl,
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
