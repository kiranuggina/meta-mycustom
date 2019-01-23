#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>     /* kmalloc() */
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include "scull.h"

static unsigned int major, majorp; /* major number for device */
static struct class *scull_class;
static struct class *scullp_class;

struct  scull_dev *scull_devices;

int scull_nr_devs = SCULL_NR_DEVS;  /* number of bare scull devices */
int scull_quantum = SCULL_QUANTUM;
int scull_qset =    SCULL_QSET;

/*-----------------------------------------------------------------------------------------*/
static int scull_p_nr_devs = SCULL_P_NR_DEVS; 	/* number of pipe devices */
int scull_p_buffer = SCULL_P_BUFFER;		/* buffer size */
dev_t scull_p_devno;				/* our first device number */

static struct scull_pipe *scull_p_devices;
static int scull_p_fasync(int fd, struct file *filep, int mode);
static int spacefree(struct scull_pipe *dev);

static int scull_p_open(struct inode *inode, struct file *filep)
{
	struct scull_pipe *dev;
	
	dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
	filep->private_data = dev;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	if(!dev->buffer){
		/* allocate the buffer */
		dev->buffer = kmalloc(scull_p_buffer, GFP_KERNEL);
		if(!dev->buffer){
			up(&dev->sem);
			return -ENOMEM;
		}
	}

	dev->buffersize = scull_p_buffer;
	dev->end = dev->buffer + dev->buffersize;
	dev->rp = dev->wp = dev->buffer; /* rd and wr from the beginning */

	/* use f_mode, not f_flags: it's cleaner (fs/open.c tells why) */
	if(filep->f_mode & FMODE_READ)
		dev->nreaders++;
	if(filep->f_mode & FMODE_WRITE)
		dev->nwriters++;
	up(&dev->sem);

	return nonseekable_open(inode, filep);
}

static int scull_p_release(struct inode *inode, struct file *filep)
{
	struct scull_pipe *dev = filep->private_data;
	
	/* remove this filep from the asynchronously notified filp's */
	scull_p_fasync(-1,filep,0);
	down(&dev->sem);
	if(filep->f_mode & FMODE_READ)
		dev->nreaders--;
	if(filep->f_mode & FMODE_WRITE)
		dev->nwriters--;
	if(dev->nreaders + dev->nwriters == 0){
		kfree(dev->buffer);
		dev->buffer = NULL; /* the other fields are not checked on open */
	}
	up(&dev->sem);
	return 0;
}

static ssize_t scull_p_read(struct file *filep, char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_pipe *dev = filep->private_data;
	
	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	
	while(dev->rp == dev->wp){ /* nothing to read */
		up(&dev->sem);
		if(filep->f_flags & O_NONBLOCK)
			return -EAGAIN;
		pr_info("%s reading going to sleep",current->comm);
		if(wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
			return -ERESTARTSYS;
		/* otherwise loop, but first reacquire the lock */
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}

	/* ok, data is there, return something */
	if(dev->wp > dev->rp)
		count = min(count, (size_t)(dev->wp - dev->rp));
	else 	/*the write pointer has wrapped, return data up to dev->end */
		count = min(count, (size_t)(dev->end - dev->rp));
	if(copy_to_user(buf,dev->rp,count)){
		up(&dev->sem);
		return -EFAULT;
	}
	dev->rp += count;
	if(dev->rp == dev->end)
		dev->rp = dev->buffer; /* wrapped */
	up(&dev->sem);

	/* finaly, awake any writers and return */
	wake_up_interruptible(&dev->outq);
	pr_info("%s did read %li bytes",current->comm, (long)count);
	return count;
}

/*
wait for space for writing; caller must hold device semaphore.
on error the semaphore will be release before returning.
*/
static int scull_getwritespace(struct scull_pipe *dev, struct file *filep)
{
	while(spacefree(dev)==0) { /* full */
		DEFINE_WAIT(wait);
		
		up(&dev->sem);
		if(filep->f_flags & O_NONBLOCK)
			return -EAGAIN;
		pr_info("%s writing: gpidn to sleep", current->comm);
		prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
		if(spacefree(dev)==0)
			schedule();
		finish_wait(&dev->outq, &wait);
		if(signal_pending(current))
			return -ERESTARTSYS; /*signal: tell the fs layer to handle it*/
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	return 0;
}

/* how much space is free? */
static int spacefree(struct scull_pipe *dev)
{
	if(dev->rp == dev->wp)
		return dev->buffersize - 1;
	return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

static ssize_t scull_p_write(struct file *filep, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_pipe *dev = filep->private_data;
	int result;
	
	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	/* make sure there's space to write */
	result = scull_getwritespace(dev,filep);
	if(result)
		return result; /* scull_getwritespace called up(&dev->sem) */

	/* ok, space is there, accept something */
	count = min(count, (size_t)spacefree(dev));
	if(dev->wp >= dev->rp)
		count = min(count, (size_t)(dev->end - dev->wp)); /* to end-of-buf */
	else /* the write pointer has wrapped, fill up to rp-1 */
		count = min(count, (size_t)(dev->rp -dev->wp -1));
	pr_info("going to accept %li bytes to %p from %p",(long)count, dev->wp, buf);
	if(copy_from_user(dev->wp, buf,count)){
		up(&dev->sem);
		return -EFAULT;
	}
	dev->wp += count;
	if(dev->wp == dev->end)
		dev->wp = dev->buffer; /* wrapped */
	up(&dev->sem);

	/* finally, make any reader */
	wake_up_interruptible(&dev->inq); /* blocked in read() and select() */

	/* and signal asychronous readers, explained late in chapter 5 */
	if(dev->async_queue)
		kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
	pr_info("%s did write %li bytes",current->comm, (long)count);
	return count;
}

static unsigned int scull_p_poll(struct file *filep, poll_table *wait)
{
	struct scull_pipe *dev = filep->private_data;
	unsigned int mask = 0;

	/*
	The buffer is circular; it is considered full
	if "wp" is right behing "rp" and empty if the two are equal
	*/
	down(&dev->sem);
	poll_wait(filep, &dev->inq, wait);
	poll_wait(filep, &dev->outq, wait);
	if(dev->rp != dev->wp)
		mask |= POLLIN | POLLRDNORM; /* readable */
	if(spacefree(dev))
		mask |= POLLOUT | POLLWRNORM; /* writable */
	up(&dev->sem);
	return mask;
}

static int scull_p_fasync(int fd, struct file *filep, int mode)
{
	struct scull_pipe *dev = filep->private_data;
	
	return fasync_helper(fd,filep, mode, &dev->async_queue);
}

/*
The file operations for the pipe device
(soem are overlayed with bare scull)
*/

struct file_operations scull_pipe_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = scull_p_read,
	.write = scull_p_write,
	.poll = scull_p_poll,
	.open = scull_p_open,
	.release = scull_p_release,
	.fasync = scull_p_fasync,
};

/*-----------------------------------------------------------------------------------------*/
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
    unregister_chrdev_region(MKDEV(majorp,0), 1);

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

   if(scull_p_devices) {
	for(i=0;i < scull_p_nr_devs; i++)
	{
		device_destroy(scullp_class, MKDEV(majorp,i));
		cdev_del(&scull_p_devices[i].cdev);
		kfree(scull_p_devices[i].buffer);
	}
	kfree(scull_p_devices);
   }
   class_destroy(scullp_class);
    pr_info("scull char and pipe module Unloaded\n");
}

static int __init scull_char_init_module(void)
{
    int error,i,j;
    dev_t devt = 0;
    dev_t devp = 0;

    /* Get a range of minor numbers (starting with 0) to work with */
    error = alloc_chrdev_region(&devt, 0, 1, "scull_char");
    if (error < 0) {
        pr_err("Can't get major number\n");
        return error;
    }
    major = MAJOR(devt);
    pr_info("scull_char major number = %d\n",major);

    error = alloc_chrdev_region(&devp, 0, 1, "scullp");
    if(error < 0){
	pr_err("can't get major number\n");
	return error;
    }
    majorp = MAJOR(devp);
    pr_info("scullp major number = %d\n",MAJOR(devp));

    /* Create device class, visible in /sys/class */
    scull_class = class_create(THIS_MODULE, "scull_char_class");
    if (IS_ERR(scull_class)) {
        pr_err("Error creating scull char class.\n");
        unregister_chrdev_region(MKDEV(major, 0), 1);
        return PTR_ERR(scull_class);
    }
	
    scullp_class = class_create(THIS_MODULE, "scullp_class");
    if(IS_ERR(scull_class)){
	pr_err("Error creating scullp class.\n");
	unregister_chrdev_region(MKDEV(MAJOR(devp),0),1);
	return PTR_ERR(scullp_class);
    }

    scull_devices =  kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
    if (!scull_devices) {
        error = -ENOMEM;
        goto fail;  /* Make this more graceful */
    }

    scull_p_devices = kmalloc(scull_p_nr_devs * sizeof(struct scull_pipe), GFP_KERNEL);
    if(!scull_devices) {
	error = -ENOMEM;
	goto fail; /*make this more graceful */
    }

    memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));
    memset(scull_p_devices, 0, scull_p_nr_devs * sizeof(struct scull_pipe));

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

    for(i=0; i<scull_p_nr_devs; i++){
        /* Initialize the char device and tie a file_operations to it */
        init_waitqueue_head(&(scull_p_devices[i].inq));
	init_waitqueue_head(&(scull_p_devices[i].outq));
        sema_init(&scull_p_devices[i].sem, 1);

        cdev_init(&scull_p_devices[i].cdev, &scull_pipe_fops);
        scull_p_devices[i].cdev.owner = THIS_MODULE;
        scull_p_devices[i].cdev.ops = &scull_pipe_fops;
        /* Now make the device live for the users to access */
        cdev_add(&scull_devices[i].cdev, MKDEV(MAJOR(devp),MINOR(devp)+i), 1);

        if (IS_ERR(device_create(scullp_class,NULL,MKDEV(MAJOR(devp),MINOR(devp)+i),NULL,"scullpipe%d",i))) {
            pr_err("Error creating scull pipe device.\n");
            for(j=0;j<i;j++)
            {
                device_destroy(scull_class,MKDEV(MAJOR(devp),MINOR(devp)+j));
                cdev_del(&scull_p_devices[j].cdev);
            }
            class_destroy(scullp_class);
            unregister_chrdev_region(devp, 1);
            return -1;
        }
    }


    pr_info("scull char and pipe module loaded\n");
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
