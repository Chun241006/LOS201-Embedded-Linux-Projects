#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define DEVICE_NAME "counter"
#define PROC_NAME   "counter_info"

static atomic_t counter = ATOMIC_INIT(0);

static atomic_t read_count = ATOMIC_INIT(0);
static atomic_t write_count = ATOMIC_INIT(0);

static DEFINE_MUTEX(counter_mutex);

static struct proc_dir_entry *proc_entry;

static ssize_t counter_read(struct file *file,
                            char __user *buf,
                            size_t count,
                            loff_t *ppos)
{
    char temp[32];
    int value;
    int len;
    ssize_t ret;

    if (*ppos != 0)
        return 0;

    if (count == 0)
        return 0;

    if (mutex_lock_interruptible(&counter_mutex))
        return -ERESTARTSYS;

    value = atomic_read(&counter);
    len = scnprintf(temp, sizeof(temp), "%d\n", value);

    if (count < len) {
        mutex_unlock(&counter_mutex);
        return -EINVAL;
    }

    ret = simple_read_from_buffer(buf, count, ppos, temp, len);

    if (ret == len) {
        atomic_inc(&counter);
        atomic_inc(&read_count);

        pr_info("counter_driver: read value=%d\n", value);
    }

    mutex_unlock(&counter_mutex);

    return ret;
}

static ssize_t counter_write(struct file *file,
                             const char __user *buf,
                             size_t count,
                             loff_t *ppos)
{
    char temp[32];
    int value;
    int ret;

    if (count == 0)
        return 0;

    if (count >= sizeof(temp))
        return -EINVAL;

    if (copy_from_user(temp, buf, count))
        return -EFAULT;

    temp[count] = '\0';

    ret = kstrtoint(temp, 10, &value);
    if (ret)
        return ret;

    if (value < 0) {
        pr_info("counter_driver: negative value rejected\n");
        return -EINVAL;
    }

    if (mutex_lock_interruptible(&counter_mutex))
        return -ERESTARTSYS;

    atomic_set(&counter, value);
    atomic_inc(&write_count);

    mutex_unlock(&counter_mutex);

    pr_info("counter_driver: counter set to %d\n", value);

    return count;
}

static int counter_proc_show(struct seq_file *m, void *v)
{
    int current_value;
    int total_reads;
    int total_writes;

    mutex_lock(&counter_mutex);

    current_value = atomic_read(&counter);
    total_reads = atomic_read(&read_count);
    total_writes = atomic_read(&write_count);

    mutex_unlock(&counter_mutex);

    seq_printf(m, "Counter Driver Information\n");
    seq_printf(m, "==========================\n");
    seq_printf(m, "Current counter : %d\n", current_value);
    seq_printf(m, "Total reads     : %d\n", total_reads);
    seq_printf(m, "Total writes    : %d\n", total_writes);

    return 0;
}

static int counter_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, counter_proc_show, NULL);
}

static const struct proc_ops counter_proc_ops = {
    .proc_open    = counter_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static const struct file_operations counter_fops = {
    .owner = THIS_MODULE,
    .read  = counter_read,
    .write = counter_write,
    .llseek = no_llseek,
};

static struct miscdevice counter_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &counter_fops,
    .mode  = 0666,
};

static int __init counter_init(void)
{
    int ret;

    pr_info("counter_driver: initializing module\n");

    ret = misc_register(&counter_misc_device);
    if (ret) {
        pr_err("counter_driver: misc_register failed\n");
        return ret;
    }

    proc_entry = proc_create(PROC_NAME, 0444, NULL,
                             &counter_proc_ops);

    if (!proc_entry) {
        pr_err("counter_driver: proc_create failed\n");
        misc_deregister(&counter_misc_device);
        return -ENOMEM;
    }

    pr_info("counter_driver: /dev/counter registered\n");
    pr_info("counter_driver: /proc/counter_info created\n");

    return 0;
}

static void __exit counter_exit(void)
{
    proc_remove(proc_entry);
    misc_deregister(&counter_misc_device);

    pr_info("counter_driver: module unloaded\n");
}

module_init(counter_init);
module_exit(counter_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SE205235");
MODULE_DESCRIPTION("ASM1 Misc Counter Character Device Driver");
MODULE_VERSION("1.0");
