/*
 * LAB-02: Simple Character Device Driver
 *
 * This driver demonstrates:
 *   - Character device registration
 *   - open(), read(), write(), release()
 *   - Kernel/User space data transfer
 *   - Mutex protection for shared data
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "asgn1_ioctl.h"
/* Module information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lam Quoc Trung - SE205235");
MODULE_DESCRIPTION("ASM1: Character Device Driver with ioctl Interface");
MODULE_VERSION("1.0");

/* Device configuration */
#define DEVICE_NAME  "asgn1"
#define CLASS_NAME   "asgn1_class"
#define BUFFER_SIZE  1024
#define DRIVER_MAJOR 241
#define ASGN1_VERSION "asgn1_driver v1.0 - SE205235"
/* Driver global variables */
static int major_number = DRIVER_MAJOR;

/*
 * Buffer used to store data written from userspace.
 * BUFFER_SIZE - 1 is used for data because the last byte
 * is reserved for the string terminating '\0'.
 */
static char device_buffer[BUFFER_SIZE];

static int buffer_len = 0;
static int open_count = 0;

/* Driver statistics */
static int write_count = 0;
static int read_count = 0;
static int last_write_size = 0;

/* Operating mode: 0 = Kernel, 1 = Echo */
static int current_mode = 1;

static struct class *asgn1_class = NULL;
static struct device *asgn1_device = NULL;

/*
 * Mutex protects shared data such as:
 *   - device_buffer
 *   - buffer_len
 *   - open_count
 *
 * This prevents race conditions when multiple processes
 * access the driver at the same time.
 */
static DEFINE_MUTEX(asgn1_mutex);
/* ===== PROCFS Implementation ===== */
static struct proc_dir_entry *proc_entry;

/*
 * Display driver information through /proc.
 *
 * Parameters:
 *   m - seq_file output stream
 *   v - iterator data (unused)
 *
 * Returns 0 on success.
 */
static int asgn1_proc_show(struct seq_file *m, void *v)
{
    /* Lock shared driver data */
    mutex_lock(&asgn1_mutex);

    seq_printf(m, "ASM1 Driver Statistics\n");
    seq_printf(m, "======================\n");
    seq_printf(m, "Driver name   : %s\n", DEVICE_NAME);
    seq_printf(m, "Major number  : %d\n", major_number);
    seq_printf(m, "Buffer size   : %d bytes\n", BUFFER_SIZE);
    seq_printf(m, "Data length   : %d bytes\n", buffer_len);
    seq_printf(m, "Open count    : %d\n", open_count);
    seq_printf(m, "Write count   : %d\n", write_count);
    seq_printf(m, "Read count    : %d\n", read_count);
    seq_printf(m, "Last write    : %d bytes\n", last_write_size);
    seq_printf(m, "Current mode  : %d\n", current_mode);

    if (buffer_len > 0)
        seq_printf(m, "Last data     : [%s]\n", device_buffer);
    else
        seq_printf(m, "Last data     : (empty)\n");

    /* Release mutex */
    mutex_unlock(&asgn1_mutex);

    return 0;
}

static int asgn1_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, asgn1_proc_show, NULL);
}

static const struct proc_ops asgn1_proc_fops = {
    .proc_open    = asgn1_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};
/* ===== SYSFS Implementation ===== */

/*
 * Display the current buffer length through sysfs.
 *
 * Returns the number of bytes written to buf.
 */
static ssize_t buffer_len_show(struct device *dev,
                               struct device_attribute *attr,
                               char *buf)
{
    ssize_t ret;

    mutex_lock(&asgn1_mutex);

    ret = sysfs_emit(buf, "%d\n", buffer_len);

    mutex_unlock(&asgn1_mutex);

    return ret;
}

/*
 * Display the number of device opens through sysfs.
 *
 * Returns the number of bytes written to buf.
 */
static ssize_t open_count_show(struct device *dev,
                               struct device_attribute *attr,
                               char *buf)
{
    ssize_t ret;

    mutex_lock(&asgn1_mutex);

    ret = sysfs_emit(buf, "%d\n", open_count);

    mutex_unlock(&asgn1_mutex);

    return ret;
}

/*
 * Display the last stored data through sysfs.
 *
 * Returns the number of bytes written to buf.
 */
static ssize_t last_data_show(struct device *dev,
                              struct device_attribute *attr,
                              char *buf)
{
    ssize_t ret;

    /* Protect shared device buffer */
    mutex_lock(&asgn1_mutex);

    if (buffer_len > 0)
        ret = sysfs_emit(buf, "%s\n", device_buffer);
    else
        ret = sysfs_emit(buf, "(empty)\n");

    /* Release mutex */
    mutex_unlock(&asgn1_mutex);

    return ret;
}

static DEVICE_ATTR_RO(buffer_len);
static DEVICE_ATTR_RO(open_count);
static DEVICE_ATTR_RO(last_data);

static struct attribute *asgn1_attrs[] = {
    &dev_attr_buffer_len.attr,
    &dev_attr_open_count.attr,
    &dev_attr_last_data.attr,
    NULL,
};

static struct attribute_group asgn1_attr_group = {
    .attrs = asgn1_attrs,
};
/*
 * ---------------------------------------------------------
 * asgn1_open()
 * ---------------------------------------------------------
 *
 * Called when userspace opens:
 *
 *     /dev/asgn1
 *
 * Example:
 *
 *     echo "Hello" > /dev/asgn1
 *
 * The open count is protected by a mutex because multiple
 * processes may open the device simultaneously.
 */
static int asgn1_open(struct inode *inode, struct file *file)
{
    mutex_lock(&asgn1_mutex);

    open_count++;

    mutex_unlock(&asgn1_mutex);

    pr_info("asgn1_driver: device opened (count=%d)\n",
            open_count);

    return 0;
}


/*
 * ---------------------------------------------------------
 * asgn1_release()
 * ---------------------------------------------------------
 *
 * Called when userspace closes the device.
 *
 * Example:
 *
 *     cat /dev/asgn1
 *
 * After cat finishes, the file is released.
 */
static int asgn1_release(struct inode *inode, struct file *file)
{
    pr_info("asgn1_driver: device closed\n");

    return 0;
}


/*
 * ---------------------------------------------------------
 * asgn1_read()
 * ---------------------------------------------------------
 *
 * Transfers data:
 *
 *     Kernel buffer -> Userspace buffer
 *
 * The userspace pointer cannot be accessed directly from
 * kernel space, therefore copy_to_user() must be used.
 *
 * read() returns:
 *   > 0 : number of bytes successfully read
 *     0 : end of data
 *    <0 : error
 */
static ssize_t asgn1_read(struct file *file,
                         char __user *buf,
                         size_t len,
                         loff_t *offset)
{
    size_t bytes_to_read;

    /* Protect shared buffer and statistics */
    mutex_lock(&asgn1_mutex);

    /* Return EOF if no more data is available */
    if (*offset >= buffer_len) {
        mutex_unlock(&asgn1_mutex);
        return 0;
    }

    /* Calculate the number of bytes to read */
    bytes_to_read = buffer_len - *offset;

    if (len > bytes_to_read)
        len = bytes_to_read;

    /* Return immediately for a zero-length read */
    if (len == 0) {
        mutex_unlock(&asgn1_mutex);
        return 0;
    }

    /* Copy data from kernel to userspace */
    if (copy_to_user(buf, device_buffer + *offset, len)) {

        mutex_unlock(&asgn1_mutex);

        pr_err("asgn1_driver: copy_to_user failed\n");

        return -EFAULT;
    }

    /* Update file position */
    *offset += len;

    /* Count successful read operations */
    read_count++;

    mutex_unlock(&asgn1_mutex);

    pr_info("asgn1_driver: read %zu bytes\n", len);

    return len;
}


/*
 * ---------------------------------------------------------
 * asgn1_write()
 * ---------------------------------------------------------
 *
 * Transfers data:
 *
 *     Userspace buffer -> Kernel buffer
 *
 * copy_from_user() must be used instead of memcpy()
 * because the source pointer belongs to userspace.
 *
 * The mutex protects the shared device buffer from
 * concurrent writes.
 */

/*
 * Write data from userspace into the driver buffer.
 *
 * Mode 0: Prefix data with "[KERNEL]".
 * Mode 1: Store original data (Echo mode).
 *
 * Returns the number of input bytes written,
 * or a negative error code on failure.
 */
static ssize_t asgn1_write(struct file *file,
                          const char __user *buf,
                          size_t len,
                          loff_t *offset)
{
    const char prefix[] = "[KERNEL]";
    size_t prefix_len = sizeof(prefix) - 1;
    size_t max_data;
    size_t bytes_to_copy;

    /* A zero-length write does not modify device state */
    if (len == 0)
        return 0;

    /* Lock shared buffer, mode and statistics */
    mutex_lock(&asgn1_mutex);

    /* Reserve space for prefix and string terminator */
    if (current_mode == 0)
        max_data = BUFFER_SIZE - prefix_len - 1;
    else
        max_data = BUFFER_SIZE - 1;

    bytes_to_copy = len;

    if (bytes_to_copy > max_data) {
        pr_warn("asgn1_driver: write too large, truncating\n");
        bytes_to_copy = max_data;
    }

    /* Handle Kernel Mode */
    if (current_mode == 0) {

        memcpy(device_buffer, prefix, prefix_len);

        if (copy_from_user(device_buffer + prefix_len,
                           buf,
                           bytes_to_copy)) {

            mutex_unlock(&asgn1_mutex);
            return -EFAULT;
        }

        buffer_len = prefix_len + bytes_to_copy;
    }
    /* Handle Echo Mode */
    else {

        if (copy_from_user(device_buffer,
                           buf,
                           bytes_to_copy)) {

            mutex_unlock(&asgn1_mutex);
            return -EFAULT;
        }

        buffer_len = bytes_to_copy;
    }

    /* Add string terminator */
    device_buffer[buffer_len] = '\0';

    /* Update statistics */
    write_count++;
    last_write_size = bytes_to_copy;

    /* Release mutex */
    mutex_unlock(&asgn1_mutex);

    pr_info("asgn1_driver: write completed, size=%zu\n",
            bytes_to_copy);

    return bytes_to_copy;
}

/*
 * ---------------------------------------------------------
 * File operations structure
 * ---------------------------------------------------------
 *
 * This structure connects userspace system calls with
 * the corresponding driver functions.
 *
 * open()    -> asgn1_open()
 * read()    -> asgn1_read()
 * write()   -> asgn1_write()
 * close()   -> asgn1_release()
 */

/*
 * Process device control commands from userspace.
 *
 * cmd: ioctl command identifier
 * arg: optional userspace argument
 *
 * Returns 0 on success or a negative error code.
 */
static long asgn1_ioctl(struct file *file,
                        unsigned int cmd,
                        unsigned long arg)
{
    struct asgn1_stats stats;
    int new_mode;

    switch (cmd) {

    case ASGN1_RESET_BUFFER:

        mutex_lock(&asgn1_mutex);

        memset(device_buffer, 0, BUFFER_SIZE);

        buffer_len = 0;
        write_count = 0;
        read_count = 0;
        last_write_size = 0;

        mutex_unlock(&asgn1_mutex);

        pr_info("asgn1_driver: RESET_BUFFER executed\n");

        return 0;

    case ASGN1_GET_STATS:

        /* Lock shared statistics */
        mutex_lock(&asgn1_mutex);

        /* Collect driver statistics */
        stats.open_count = open_count;
        stats.write_count = write_count;
        stats.read_count = read_count;
        stats.buffer_len = buffer_len;
        stats.last_write_size = last_write_size;

        /* Unlock shared statistics */
        mutex_unlock(&asgn1_mutex);

        /* Copy statistics from kernel to userspace */
        if (copy_to_user((void __user *)arg,
                         &stats,
                         sizeof(stats))) {

            pr_err("asgn1_driver: GET_STATS copy_to_user failed\n");

            return -EFAULT;
        }

        pr_info("asgn1_driver: GET_STATS executed\n");

        return 0;

    case ASGN1_SET_MODE:

        /* Copy new mode value from userspace */
        if (copy_from_user(&new_mode,
                           (int __user *)arg,
                           sizeof(new_mode))) {

            pr_err("asgn1_driver: SET_MODE copy_from_user failed\n");

            return -EFAULT;
        }

        /* Only modes 0 and 1 are supported */
        if (new_mode != 0 && new_mode != 1) {

            pr_err("asgn1_driver: invalid mode %d\n", new_mode);

            return -EINVAL;
        }

        /* Protect shared mode variable */
        mutex_lock(&asgn1_mutex);

        current_mode = new_mode;

        mutex_unlock(&asgn1_mutex);

        pr_info("asgn1_driver: SET_MODE executed, mode=%d\n",
                new_mode);

        return 0;


    case ASGN1_GET_VERSION:

        /* Copy version string to userspace */
        if (copy_to_user((void __user *)arg,
                         ASGN1_VERSION,
                         sizeof(ASGN1_VERSION))) {

            pr_err("asgn1_driver: GET_VERSION copy_to_user failed\n");

            return -EFAULT;
        }

        pr_info("asgn1_driver: GET_VERSION executed\n");

        return 0;

    default:
        return -ENOTTY;
    }
}

static const struct file_operations asgn1_fops = {
    .owner   = THIS_MODULE,
    .open    = asgn1_open,
    .release = asgn1_release,
    .read    = asgn1_read,
    .write   = asgn1_write,
    .unlocked_ioctl = asgn1_ioctl,
};


/*
 * ---------------------------------------------------------
 * asgn1_init()
 * ---------------------------------------------------------
 *
 * Executed when the kernel loads the module.
 *
 * Responsibilities:
 *   1. Register character device
 *   2. Create device class
 *   3. Create device
 */
static int __init asgn1_init(void)
{
    int ret;

    pr_info("asgn1_driver: initializing module\n");

    /*
     * Register character device using fixed major number 240.
     */
    ret = register_chrdev(major_number,
                          DEVICE_NAME,
                          &asgn1_fops);

    if (ret < 0) {

        pr_err("asgn1_driver: register_chrdev failed: %d\n",
               ret);

        return ret;
    }

    /*
     * Create device class.
     *
     * This creates:
     *
     *     /sys/class/asgn1_class/
     */
    asgn1_class = class_create(THIS_MODULE, CLASS_NAME);

    if (IS_ERR(asgn1_class)) {

        pr_err("asgn1_driver: class_create failed\n");

        unregister_chrdev(major_number, DEVICE_NAME);

        return PTR_ERR(asgn1_class);
    }

    /*
     * Create device.
     *
     * The kernel will expose the device through sysfs.
     */
    asgn1_device = device_create(asgn1_class,
                                NULL,
                                MKDEV(major_number, 0),
                                NULL,
                                DEVICE_NAME);

    if (IS_ERR(asgn1_device)) {

        pr_err("asgn1_driver: device_create failed\n");

        class_destroy(asgn1_class);

        unregister_chrdev(major_number, DEVICE_NAME);

        return PTR_ERR(asgn1_device);
    }

    pr_info("asgn1_driver: loaded, major=%d\n",
            major_number);

    proc_entry = proc_create("asgn1_info", 0444, NULL, &asgn1_proc_fops);

    if (!proc_entry)
        pr_warn("asgn1_driver: failed to create /proc/asgn1_info\n");
    else
        pr_info("asgn1_driver: /proc/asgn1_info created\n");

    /* Create sysfs attributes */
    ret = sysfs_create_group(&asgn1_device->kobj, &asgn1_attr_group);
    if (ret)
        pr_warn("asgn1_driver: sysfs_create_group failed\n");
    else
        pr_info("asgn1_driver: sysfs attributes created\n");

    return 0;
}

/*
 * ---------------------------------------------------------
 * asgn1_exit()
 * ---------------------------------------------------------
 *
 * Executed when the kernel unloads the module.
 *
 * It removes all resources allocated by asgn1_init().
 */
static void __exit asgn1_exit(void)
{
if (proc_entry)
    proc_remove(proc_entry);

  sysfs_remove_group(&asgn1_device->kobj, &asgn1_attr_group);

    /*
     * Remove device from sysfs.
     */
    device_destroy(asgn1_class,
                   MKDEV(major_number, 0));

    /*
     * Remove device class.
     */
    class_destroy(asgn1_class);

    /*
     * Unregister character device.
     */
    unregister_chrdev(major_number,
                     DEVICE_NAME);

    pr_info("asgn1_driver: module unloaded\n");
}


/*
 * Tell the kernel which functions are the module
 * initialization and cleanup functions.
 */
module_init(asgn1_init);
module_exit(asgn1_exit);
