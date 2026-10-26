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
#include <linux/random.h>
#include <linux/timekeeping.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include "sms_sensor.h"

static atomic_t read_count = ATOMIC_INIT(0);

static DEFINE_MUTEX(sensor_mutex);

static int last_temperature_x10;
static int last_humidity_x10;

static u32 interval_ms = SMS_DEFAULT_INTERVAL_MS;

static u64 driver_start_ms;
static u64 last_read_ms;

static struct proc_dir_entry *proc_entry;

static ssize_t sensor_read(struct file *file,
                           char __user *buf,
                           size_t count,
                           loff_t *ppos)
{
    char temp[64];
    int temperature_x10;
    int humidity_x10;
    int len;
    ssize_t ret;
    u64 now_ms;

        if (*ppos != 0) {
        return 0;
    }

    if (count == 0) {
        return 0;
    }

    if (mutex_lock_interruptible(&sensor_mutex)) {
        return -ERESTARTSYS;
    }

    now_ms = div_u64(ktime_get_ns(), 1000000);

    /* Enforce the minimum interval between successful reads. */
    if (last_read_ms != 0 &&
        now_ms - last_read_ms < interval_ms) {
        mutex_unlock(&sensor_mutex);
        return -EAGAIN;
    }

    /* Generate values with one decimal place. */
    temperature_x10 = 150 + get_random_u32() % 301;
    humidity_x10 = 300 + get_random_u32() % 601;

    len = scnprintf(temp, sizeof(temp),
                    "%llu,%d.%01d,%d.%01d\n",
                    (unsigned long long)now_ms,
                    temperature_x10 / 10,
                    temperature_x10 % 10,
                    humidity_x10 / 10,
                    humidity_x10 % 10);

    if (count < len) {
        mutex_unlock(&sensor_mutex);
        return -EINVAL;
    }

    ret = simple_read_from_buffer(buf, count, ppos,
                                  temp, len);

    if (ret == len) {
        last_temperature_x10 = temperature_x10;
        last_humidity_x10 = humidity_x10;
        last_read_ms = now_ms;
        atomic_inc(&read_count);
    }

    mutex_unlock(&sensor_mutex);

    return ret;
}


static long sensor_ioctl(struct file *file,
                         unsigned int cmd,
                         unsigned long arg)
{
    u32 new_interval;

    if (cmd != SMS_SET_INTERVAL)
        return -ENOTTY;

    if (copy_from_user(&new_interval,
                       (void __user *)arg,
                       sizeof(new_interval)))
        return -EFAULT;

    if (new_interval < 1 || new_interval > 60000)
        return -EINVAL;

    if (mutex_lock_interruptible(&sensor_mutex))
        return -ERESTARTSYS;

    interval_ms = new_interval;

    mutex_unlock(&sensor_mutex);

    pr_info("sms_sensor: interval set to %u ms\n",
            new_interval);

    return 0;
}

static int sensor_proc_show(struct seq_file *m, void *v)
{
    int total_reads;
    int temperature;
    int humidity;
    u32 current_interval;
    u64 uptime_seconds;
    u64 now_ms;

    mutex_lock(&sensor_mutex);

    total_reads = atomic_read(&read_count);
    temperature = last_temperature_x10;
    humidity = last_humidity_x10;
    current_interval = interval_ms;

    now_ms = div_u64(ktime_get_ns(), 1000000);
    uptime_seconds = div_u64(now_ms - driver_start_ms, 1000);
    mutex_unlock(&sensor_mutex);

    seq_printf(m, "SMS Sensor Driver Statistics\n");
    seq_printf(m, "============================\n");

    seq_printf(m, "read_count: %d\n", total_reads);

    seq_printf(m, "last_temperature: %d.%01d C\n",
               temperature / 10,
               temperature % 10);

    seq_printf(m, "last_humidity: %d.%01d %%\n",
               humidity / 10,
               humidity % 10);

    seq_printf(m, "interval_ms: %u\n", current_interval);

    seq_printf(m, "uptime_seconds: %llu\n",
               (unsigned long long)uptime_seconds);

    return 0;
}

static int sensor_proc_open(struct inode *inode,
                            struct file *file)
{
    return single_open(file, sensor_proc_show, NULL);
}

static const struct proc_ops sensor_proc_ops = {
    .proc_open    = sensor_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static const struct file_operations sensor_fops = {
    .owner          = THIS_MODULE,
    .read           = sensor_read,
    .unlocked_ioctl = sensor_ioctl,
    .llseek         = no_llseek,
};

static struct miscdevice sensor_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = SMS_DEVICE_NAME,
    .fops  = &sensor_fops,
    .mode  = 0666,
};

static int __init sensor_init(void)
{
    int ret;

    pr_info("sms_sensor: initializing module\n");

    atomic_set(&read_count, 0);

    last_temperature_x10 = 0;
    last_humidity_x10 = 0;

    interval_ms = SMS_DEFAULT_INTERVAL_MS;

    driver_start_ms = div_u64(ktime_get_ns(), 1000000);
    last_read_ms = 0;

    ret = misc_register(&sensor_misc_device);
    if (ret) {
        pr_err("sms_sensor: misc_register failed: %d\n", ret);
        return ret;
    }

    proc_entry = proc_create(SMS_PROC_NAME, 0444, NULL,
                             &sensor_proc_ops);

    if (!proc_entry) {
        pr_err("sms_sensor: proc_create failed\n");
        misc_deregister(&sensor_misc_device);
        return -ENOMEM;
    }

    pr_info("sms_sensor: /dev/sms_sensor registered\n");
    pr_info("sms_sensor: /proc/sms_stats created\n");

    return 0;
}

static void __exit sensor_exit(void)
{
    proc_remove(proc_entry);
    misc_deregister(&sensor_misc_device);

    pr_info("sms_sensor: module unloaded\n");
}

module_init(sensor_init);
module_exit(sensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SE205235");
MODULE_DESCRIPTION("ASM2 Sensor Monitoring Character Device Driver");
MODULE_VERSION("1.0");
