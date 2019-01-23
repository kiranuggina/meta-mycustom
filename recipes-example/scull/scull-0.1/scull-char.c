#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>     /* kmalloc() */
#include <linux/uaccess.h>
#include "scull.h"

static unsigned int major; /* major number for device */
static struct class *scull_class;

struct  scull_dev *scull_devices;

int scull_nr_devs = SCULL_NR_DEVS;  /* number of bare scull devices */
int scull_quantum = SCULL_QUANTUM;
int scull_qset =    SCULL_QSET;

/*
 * Empty out the scull device; must be called with the device
 * semaphore held.
 */
int scull_trim(struct scull_dev *dev)
{
    struct scull_qset *next, *dptr;
    int qset = dev->qset;   /* "dev" is not-null */
    int i;

    for (dptr = dev->data; dptr; dptr = next) { /* all the list items */
        if (dptr->data) {
            for (i = 0; i < qset; i++)
                kfree(dptr->data[i]);
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        kfree(dptr);
    }
    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;
    return 0;
}

/*
 * Follow the list
 */
struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
    struct scull_qset *qs = dev->data;

        /* Allocate first qset explicitly if need be */
    if (! qs) {
        qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
        if (qs == NULL)
            return NULL;  /* Never mind */
        memset(qs, 0, sizeof(struct scull_qset));
    }

    /* Then follow the list */
    while (n--) {
        if (!qs->next) {
            qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
            if (qs->next == NULL)
                return NULL;  /* Never mind */
            memset(qs->next, 0, sizeof(struct scull_qset));
        }
        qs = qs->next;
        continue;
    }
    return qs;
}

int scull_open(struct inode * inode, struct file * filp)
{
    struct scull_dev *dev; /* device information */

    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev; /* for other methods */
    pr_info("Someone tried to open me\n");
    return 0;
}

int scull_release(struct inode * inode, struct file * filp)
{
    pr_info("Someone closed me\n");
    return 0;
}

ssize_t scull_read (struct file *filp, char __user * buf, size_t count, loff_t * f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;    /* the first listitem */
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset; /* how many bytes in the listitem */
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    if (*f_pos >= dev->size)
        goto out;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;

    /* find listitem, qset index, and offset in the quantum */
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum; q_pos = rest % quantum;

    pr_info("Read item: %d\trest: %d\t s_pos: %d\tq_pos: %d\tf_pos: %llu",item,rest,s_pos,q_pos,*f_pos);

    /* follow the list up to the right position (defined elsewhere) */
    dptr = scull_follow(dev, item);

    if (dptr == NULL || !dptr->data || ! dptr->data[s_pos])
        goto out; /* don't fill holes */

    /* read only up to the end of this quantum */
    if (count > quantum - q_pos)
        count = quantum - q_pos;

    if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
        retval = -EFAULT;
        goto out;
    }

    pr_info("read %lu chars",count);
    *f_pos += count;
    retval = count;

  out:
    up(&dev->sem);
    return retval;
}


ssize_t scull_write(struct file * filp, const char __user * buf, size_t count, loff_t * f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM; /* value used in "goto out" statements */

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    /* find listitem, qset index and offset in the quantum */
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum; q_pos = rest % quantum;

    pr_info("Write item: %d\trest: %d\t s_pos: %d\tq_pos: %d\tf_pos: %llu",item,rest,s_pos,q_pos,*f_pos);

    /* follow the list up to the right position */
    dptr = scull_follow(dev, item);
    if (dptr == NULL)
        goto out;
    if (!dptr->data) {
        dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if (!dptr->data)
            goto out;
        memset(dptr->data, 0, qset * sizeof(char *));
    }
    if (!dptr->data[s_pos]) {
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if (!dptr->data[s_pos])
            goto out;
    }
    /* write only up to the end of this quantum */
    if (count > quantum - q_pos)
        count = quantum - q_pos;

    if (copy_from_user(dptr->data[s_pos]+q_pos, buf, count)) {
        retval = -EFAULT;
        goto out;
    }

    pr_info("wrote %lu characters",count);
    *f_pos += count;
    retval = count;

        /* update the size */
    if (dev->size < *f_pos)
        dev->size = *f_pos;

  out:
    up(&dev->sem);
    return retval;
}

/*
 * The "extended" operations -- only seek
 */

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
    struct scull_dev *dev = filp->private_data;
    loff_t newpos;

    switch(whence) {
      case 0: /* SEEK_SET */
        newpos = off;
        break;

      case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

      case 2: /* SEEK_END */
        newpos = dev->size + off;
        break;

      default: /* can't happen */
        return -EINVAL;
    }
    if (newpos < 0) return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}

struct file_operations scull_fops = {
    owner:      THIS_MODULE,
    open:       scull_open,
    release:    scull_release,
    read:       scull_read,
    write:      scull_write,
    llseek:     scull_llseek,
};

static void __exit scull_char_cleanup_module(void)
{
    int i;
    unregister_chrdev_region(MKDEV(major, 0), 1);

        /* Get rid of our char dev entries */
    if (scull_devices) {
        for (i = 0; i < scull_nr_devs; i++) {
            scull_trim(scull_devices + i);
            device_destroy(scull_class, MKDEV(major, i));
            cdev_del(&scull_devices[i].cdev);
        }
        kfree(scull_devices);
    }
    class_destroy(scull_class);

    pr_info("scull char module Unloaded\n");
}

static int __init scull_char_init_module(void)
{
    int error,i,j;
    dev_t devt = 0;

    /* Get a range of minor numbers (starting with 0) to work with */
    error = alloc_chrdev_region(&devt, 0, 1, "scull_char");
    if (error < 0) {
        pr_err("Can't get major number\n");
        return error;
    }
    major = MAJOR(devt);
    pr_info("scull_char major number = %d\n",major);

    /* Create device class, visible in /sys/class */
    scull_class = class_create(THIS_MODULE, "scull_char_class");
    if (IS_ERR(scull_class)) {
        pr_err("Error creating scull char class.\n");
        unregister_chrdev_region(MKDEV(major, 0), 1);
        return PTR_ERR(scull_class);
    }

    scull_devices =  kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
    if (!scull_devices) {
        error = -ENOMEM;
        goto fail;  /* Make this more graceful */
    }

    memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

    for(i=0; i<scull_nr_devs; i++){
        /* Initialize the char device and tie a file_operations to it */
        scull_devices[i].quantum = scull_quantum;
        scull_devices[i].qset = scull_qset;
        sema_init(&scull_devices[i].sem, 1);

        cdev_init(&scull_devices[i].cdev, &scull_fops);
        scull_devices[i].cdev.owner = THIS_MODULE;
        scull_devices[i].cdev.ops = &scull_fops;
        /* Now make the device live for the users to access */
        cdev_add(&scull_devices[i].cdev, MKDEV(MAJOR(devt),MINOR(devt)+i), 1);

        if (IS_ERR(device_create(scull_class,NULL,MKDEV(MAJOR(devt),MINOR(devt)+i),NULL,"scull_char%d",i))) {
            pr_err("Error creating scull char device.\n");
            for(j=0;j<i;j++)
            {
                scull_trim(scull_devices + j);
                device_destroy(scull_class,MKDEV(MAJOR(devt),MINOR(devt)+j));
                cdev_del(&scull_devices[j].cdev);
            }
            class_destroy(scull_class);
            unregister_chrdev_region(devt, 1);
            return -1;
        }    
    }

    pr_info("scull char module loaded\n");
    return 0;

fail:
    scull_char_cleanup_module();
    return error;
}

module_init(scull_char_init_module);
module_exit(scull_char_cleanup_module);

MODULE_AUTHOR("Kiran Kumar Uggina <suryakiran104@gmail.com>");
MODULE_DESCRIPTION("scull device driver");
MODULE_LICENSE("GPL");
