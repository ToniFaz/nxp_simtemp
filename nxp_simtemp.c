#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/random.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/stringify.h>

#include "nxp_simtemp.h"

// Device private data
struct simtemp_dev {
    struct platform_device *pdev;
    struct device *dev;
    struct cdev cdev;
    dev_t devt;
    struct class *class;
    struct device *device;
    
    // Configuration
    __u32 sampling_ms;
    __s32 threshold_mC;
    enum simtemp_mode mode;
    
    // Data buffer
    struct simtemp_sample buffer[SIMTEMP_MAX_SAMPLES];
    __u32 read_idx;
    __u32 write_idx;
    __u32 count;
    
    // Synchronization
    struct mutex lock;
    wait_queue_head_t read_queue;
    wait_queue_head_t poll_queue;
    
    // Timers and state
    struct timer_list sample_timer;
    __s32 current_temp_mC;
    bool threshold_crossed;
    
    // Statistics
    struct simtemp_stats stats;
    
    // Simulation state
    __s32 ramp_base;
    bool ramp_direction;
};

static struct simtemp_dev *simtemp_device;

// Sysfs show/store functions
static ssize_t sampling_ms_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct simtemp_dev *priv = dev_get_drvdata(dev);
    return sprintf(buf, "%u\n", priv->sampling_ms);
}

static ssize_t sampling_ms_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
    struct simtemp_dev *priv = dev_get_drvdata(dev);
    unsigned long val;
    int ret;
    
    ret = kstrtoul(buf, 10, &val);
    if (ret)
        return ret;
    
    if (val < 10 || val > 10000)
        return -EINVAL;
    
    mutex_lock(&priv->lock);
    priv->sampling_ms = val;
    
    // Restart timer with new interval
    mod_timer(&priv->sample_timer, jiffies + msecs_to_jiffies(priv->sampling_ms));
    mutex_unlock(&priv->lock);
    
    return count;
}

static ssize_t threshold_mC_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct simtemp_dev *priv = dev_get_drvdata(dev);
    return sprintf(buf, "%d\n", priv->threshold_mC);
}

static ssize_t threshold_mC_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count)
{
    struct simtemp_dev *priv = dev_get_drvdata(dev);
    long val;
    int ret;
    
    ret = kstrtol(buf, 10, &val);
    if (ret)
        return ret;
    
    mutex_lock(&priv->lock);
    priv->threshold_mC = val;
    priv->threshold_crossed = false;
    mutex_unlock(&priv->lock);
    
    return count;
}

static ssize_t mode_show(struct device *dev,
                        struct device_attribute *attr, char *buf)
{
    struct simtemp_dev *priv = dev_get_drvdata(dev);
    const char *mode_str;
    
    switch (priv->mode) {
    case MODE_NORMAL: mode_str = "normal"; break;
    case MODE_NOISY: mode_str = "noisy"; break;
    case MODE_RAMP: mode_str = "ramp"; break;
    default: mode_str = "unknown"; break;
    }
    
    return sprintf(buf, "%s\n", mode_str);
}

static ssize_t mode_store(struct device *dev,
                         struct device_attribute *attr,
                         const char *buf, size_t count)
{
    struct simtemp_dev *priv = dev_get_drvdata(dev);
    
    mutex_lock(&priv->lock);
    if (sysfs_streq(buf, "normal")) {
        priv->mode = MODE_NORMAL;
    } else if (sysfs_streq(buf, "noisy")) {
        priv->mode = MODE_NOISY;
    } else if (sysfs_streq(buf, "ramp")) {
        priv->mode = MODE_RAMP;
        priv->ramp_base = 25000; // Start at 25°C
        priv->ramp_direction = true;
    } else {
        mutex_unlock(&priv->lock);
        return -EINVAL;
    }
    mutex_unlock(&priv->lock);
    
    return count;
}

static ssize_t stats_show(struct device *dev,
                         struct device_attribute *attr, char *buf)
{
    struct simtemp_dev *priv = dev_get_drvdata(dev);
    
    mutex_lock(&priv->lock);
    ssize_t len = sprintf(buf, "samples_produced: %u\nalerts_triggered: %u\n"
                          "read_errors: %u\nlast_error: %u\n",
                          priv->stats.samples_produced,
                          priv->stats.alerts_triggered,
                          priv->stats.read_errors,
                          priv->stats.last_error);
    mutex_unlock(&priv->lock);
    
    return len;
}

// Define device attributes
static DEVICE_ATTR_RW(sampling_ms);
static DEVICE_ATTR_RW(threshold_mC);
static DEVICE_ATTR_RW(mode);
static DEVICE_ATTR_RO(stats);

static struct attribute *simtemp_attrs[] = {
    &dev_attr_sampling_ms.attr,
    &dev_attr_threshold_mC.attr,
    &dev_attr_mode.attr,
    &dev_attr_stats.attr,
    NULL,
};

static const struct attribute_group simtemp_attr_group = {
    .attrs = simtemp_attrs,
};

// Temperature simulation functions
static __s32 simulate_temperature_normal(struct simtemp_dev *priv)
{
    // Base temperature around 40°C with small variations
    return 40000 + (get_random_u32() % 2000 - 1000);
}

static __s32 simulate_temperature_noisy(struct simtemp_dev *priv)
{
    // Larger variations for noisy mode
    return 40000 + (get_random_u32() % 10000 - 5000);
}

static __s32 simulate_temperature_ramp(struct simtemp_dev *priv)
{
    // Ramp up and down between 20°C and 60°C
    if (priv->ramp_direction) {
        priv->ramp_base += 1000;
        if (priv->ramp_base >= 60000) {
            priv->ramp_base = 60000;
            priv->ramp_direction = false;
        }
    } else {
        priv->ramp_base -= 1000;
        if (priv->ramp_base <= 20000) {
            priv->ramp_base = 20000;
            priv->ramp_direction = true;
        }
    }
    
    return priv->ramp_base;
}

static __s32 simulate_temperature(struct simtemp_dev *priv)
{
    switch (priv->mode) {
    case MODE_NORMAL:
        return simulate_temperature_normal(priv);
    case MODE_NOISY:
        return simulate_temperature_noisy(priv);
    case MODE_RAMP:
        return simulate_temperature_ramp(priv);
    default:
        return 40000;
    }
}

// Timer function to generate samples
static void sample_timer_callback(struct timer_list *t)
{
    struct simtemp_dev *priv = from_timer(priv, t, sample_timer);
    struct simtemp_sample sample;
    bool new_alert = false;
    
    mutex_lock(&priv->lock);
    
    // Generate new sample
    sample.timestamp_ns = ktime_get_ns();
    sample.temp_mC = simulate_temperature(priv);
    sample.flags = FLAG_NEW_SAMPLE;
    
    // Check threshold
    if (priv->current_temp_mC > priv->threshold_mC && 
         sample.temp_mC >= priv->threshold_mC)
	{
        sample.flags |= FLAG_THRESHOLD_CROSSED;
        new_alert = true;
        priv->stats.alerts_triggered++;
    }
    
    priv->current_temp_mC = sample.temp_mC;
    
    // Add to buffer
    if (priv->count < SIMTEMP_MAX_SAMPLES) {
        priv->buffer[priv->write_idx] = sample;
        priv->write_idx = (priv->write_idx + 1) % SIMTEMP_MAX_SAMPLES;
        priv->count++;
        priv->stats.samples_produced++;
    } else {
        priv->stats.read_errors++;
    }
    
    mutex_unlock(&priv->lock);
    
    // Wake up readers
    wake_up_interruptible(&priv->read_queue);
    if (new_alert) {
        wake_up_interruptible(&priv->poll_queue);
    }
    
    // Reschedule timer
    mod_timer(&priv->sample_timer, jiffies + msecs_to_jiffies(priv->sampling_ms));
}

// File operations
static int simtemp_open(struct inode *inode, struct file *file)
{
    struct simtemp_dev *priv = container_of(inode->i_cdev, struct simtemp_dev, cdev);
    file->private_data = priv;
    return 0;
}

static int simtemp_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t simtemp_read(struct file *file, char __user *buf,
                           size_t count, loff_t *ppos)
{
    struct simtemp_dev *priv = file->private_data;
    struct simtemp_sample sample;
    ssize_t ret = 0;
    
    if (count < sizeof(sample))
        return -EINVAL;
    
    mutex_lock(&priv->lock);
    
    while (priv->count == 0) {
        mutex_unlock(&priv->lock);
        
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
            
        if (wait_event_interruptible(priv->read_queue, priv->count > 0))
            return -ERESTARTSYS;
            
        mutex_lock(&priv->lock);
    }
    
    // Get sample from buffer
    sample = priv->buffer[priv->read_idx];
    priv->read_idx = (priv->read_idx + 1) % SIMTEMP_MAX_SAMPLES;
    priv->count--;
    
    mutex_unlock(&priv->lock);
    
    if (copy_to_user(buf, &sample, sizeof(sample))) {
        ret = -EFAULT;
    } else {
        ret = sizeof(sample);
    }
    
    return ret;
}

static __poll_t simtemp_poll(struct file *file, poll_table *wait)
{
    struct simtemp_dev *priv = file->private_data;
    __poll_t mask = 0;
    
    poll_wait(file, &priv->read_queue, wait);
    poll_wait(file, &priv->poll_queue, wait);
    
    mutex_lock(&priv->lock);
    if (priv->count > 0)
        mask |= EPOLLIN | EPOLLRDNORM;

    mask |= EPOLLPRI;
    mutex_unlock(&priv->lock);
    
    return mask;
}

static long simtemp_ioctl(struct file *file, unsigned int cmd,
                         unsigned long arg)
{
    struct simtemp_dev *priv = file->private_data;
    void __user *uarg = (void __user *)arg;
    int ret = 0;
    
    switch (cmd) {
    case SIMTEMP_SET_SAMPLING: {
        __u32 sampling_ms;
        if (copy_from_user(&sampling_ms, uarg, sizeof(sampling_ms)))
            return -EFAULT;
        
        if (sampling_ms < 10 || sampling_ms > 10000)
            return -EINVAL;
        
        mutex_lock(&priv->lock);
        priv->sampling_ms = sampling_ms;
        mod_timer(&priv->sample_timer, jiffies + msecs_to_jiffies(priv->sampling_ms));
        mutex_unlock(&priv->lock);
        break;
    }
    
    case SIMTEMP_SET_THRESHOLD: {
        __s32 threshold_mC;
        if (copy_from_user(&threshold_mC, uarg, sizeof(threshold_mC)))
            return -EFAULT;
        
        mutex_lock(&priv->lock);
        priv->threshold_mC = threshold_mC;
        priv->threshold_crossed = false;
        mutex_unlock(&priv->lock);
        break;
    }
    
    case SIMTEMP_SET_MODE: {
        __u32 mode;
        if (copy_from_user(&mode, uarg, sizeof(mode)))
            return -EFAULT;
        
        if (mode >= MODE_MAX)
            return -EINVAL;
        
        mutex_lock(&priv->lock);
        priv->mode = mode;
        if (mode == MODE_RAMP) {
            priv->ramp_base = 25000;
            priv->ramp_direction = true;
        }
        mutex_unlock(&priv->lock);
        break;
    }
    
    case SIMTEMP_GET_STATS: {
        struct simtemp_stats stats;
        mutex_lock(&priv->lock);
        stats = priv->stats;
        mutex_unlock(&priv->lock);
        
        if (copy_to_user(uarg, &stats, sizeof(stats)))
            return -EFAULT;
        break;
    }
    
    default:
        ret = -ENOTTY;
        break;
    }
    
    return ret;
}

static const struct file_operations simtemp_fops = {
    .owner = THIS_MODULE,
    .open = simtemp_open,
    .release = simtemp_release,
    .read = simtemp_read,
    .poll = simtemp_poll,
    .unlocked_ioctl = simtemp_ioctl,
    .compat_ioctl = simtemp_ioctl,
};

// Platform driver functions
static int simtemp_probe(struct platform_device *pdev)
{
    struct simtemp_dev *priv;
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    int ret;
    
    printk(KERN_INFO "nxp_simtemp: PROBE FUNCTION CALLED!\n");
    
    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;
    
    priv->pdev = pdev;
    priv->dev = dev;
    platform_set_drvdata(pdev, priv);
    simtemp_device = priv;
    
    // ==========DEVICE TREE ==========
    // Default settings
    priv->sampling_ms = 50;
    priv->threshold_mC = 41000;
    priv->mode = MODE_NORMAL;
    priv->current_temp_mC = 40000;
    
    // Device Tree
    if (np) {
        // sampling_ms
        if (of_property_read_u32(np, "sampling-ms", &priv->sampling_ms)) {
            printk(KERN_INFO "nxp_simtemp: Using default sampling_ms: %u\n", priv->sampling_ms);
        } else {
            printk(KERN_INFO "nxp_simtemp: DT sampling_ms: %u\n", priv->sampling_ms);
        }
        
        //threshold-mC
        if (of_property_read_u32(np, "threshold-mC", (u32*)&priv->threshold_mC)) {
            printk(KERN_INFO "nxp_simtemp: Using default threshold_mC: %d\n", priv->threshold_mC);
        } else {
            printk(KERN_INFO "nxp_simtemp: DT threshold_mC: %d\n", priv->threshold_mC);
        }
    } else {
        printk(KERN_INFO "nxp_simtemp: No Device Tree node, using defaults\n");
    }
    // ========== DEVICE TREE ==========
    
    printk(KERN_INFO "nxp_simtemp: Final config - sampling:%ums threshold:%dmC mode:%d\n",
           priv->sampling_ms, priv->threshold_mC, priv->mode);
    
    // Initialize synchronization
    mutex_init(&priv->lock);
    init_waitqueue_head(&priv->read_queue);
    init_waitqueue_head(&priv->poll_queue);
    
    // Initialize character device
    ret = alloc_chrdev_region(&priv->devt, 0, 1, DEVICE_NAME);
    if (ret) {
        dev_err(dev, "Failed to allocate device number\n");
        goto err_alloc;
    }
    
    printk(KERN_INFO "nxp_simtemp: Allocated major:%d minor:%d\n", 
           MAJOR(priv->devt), MINOR(priv->devt));
    
    cdev_init(&priv->cdev, &simtemp_fops);
    priv->cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&priv->cdev, priv->devt, 1);
    if (ret) {
        dev_err(dev, "Failed to add character device\n");
        goto err_cdev;
    }
    
    // Create device class and device
    priv->class = class_create(DRIVER_NAME);
    if (IS_ERR(priv->class)) {
        ret = PTR_ERR(priv->class);
        dev_err(dev, "Failed to create device class\n");
        goto err_class;
    }
   
    priv->device = device_create(priv->class, dev, priv->devt,
                                priv, DEVICE_NAME);

    if (IS_ERR(priv->device)) {
        ret = PTR_ERR(priv->device);
        dev_err(dev, "Failed to create device\n");
        goto err_device;
    }
    
    // Create sysfs attributes
    ret = sysfs_create_group(&dev->kobj, &simtemp_attr_group);
    if (ret) {
        dev_err(dev, "Failed to create sysfs group\n");
        goto err_sysfs;
    }
    
    dev_set_drvdata(priv->device, priv);
    
    // Initialize timer
    timer_setup(&priv->sample_timer, sample_timer_callback, 0);
    mod_timer(&priv->sample_timer, jiffies + msecs_to_jiffies(priv->sampling_ms));
    
    printk(KERN_INFO "NXP Virtual Temperature Sensor initialized\n");
    printk(KERN_INFO "Device created: /dev/simtemp\n");
    
    return 0;
    
err_sysfs:
    device_destroy(priv->class, priv->devt);
err_device:
    class_destroy(priv->class);
err_class:
    cdev_del(&priv->cdev);
err_cdev:
    unregister_chrdev_region(priv->devt, 1);
err_alloc:
    return ret;
}

static void simtemp_remove(struct platform_device *pdev)
{
    struct simtemp_dev *priv = platform_get_drvdata(pdev);
    
    printk(KERN_INFO "nxp_simtemp: REMOVE FUNCTION CALLED\n");
    
    del_timer_sync(&priv->sample_timer);
    sysfs_remove_group(&pdev->dev.kobj, &simtemp_attr_group);
    device_destroy(priv->class, priv->devt);
    class_destroy(priv->class);
    cdev_del(&priv->cdev);
    unregister_chrdev_region(priv->devt, 1);
    
    printk(KERN_INFO "NXP Virtual Temperature Sensor removed\n");
   
}

static const struct of_device_id simtemp_of_match[] = {
    { .compatible = "nxp,simtemp" },
    { },
};
MODULE_DEVICE_TABLE(of, simtemp_of_match);

static struct platform_driver simtemp_driver = {
    .probe = simtemp_probe,
    .remove = simtemp_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = simtemp_of_match,
        .owner = THIS_MODULE,
    },
};

// ================================================
static struct platform_device *simtemp_pdev;

static int __init nxp_simtemp_init(void)
{
    int ret;
    
    printk(KERN_INFO "nxp_simtemp: INIT FUNCTION CALLED\n");
    
    ret = platform_driver_register(&simtemp_driver);
    if (ret) {
        printk(KERN_ERR "nxp_simtemp: Failed to register platform driver: %d\n", ret);
        return ret;
    }
    
    printk(KERN_INFO "nxp_simtemp: Platform driver registered\n");

    simtemp_pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
    if (IS_ERR(simtemp_pdev)) {
        ret = PTR_ERR(simtemp_pdev);
        printk(KERN_ERR "nxp_simtemp: Failed to register platform device: %d\n", ret);
        platform_driver_unregister(&simtemp_driver);
        return ret;
    }
    
    printk(KERN_INFO "nxp_simtemp: Platform device registered\n");
    
    return 0;
}

static void __exit nxp_simtemp_exit(void)
{
    printk(KERN_INFO "nxp_simtemp: EXIT FUNCTION CALLED\n");
    
    if (simtemp_pdev)
        platform_device_unregister(simtemp_pdev);
    
    platform_driver_unregister(&simtemp_driver);
    
    printk(KERN_INFO "nxp_simtemp: Unloaded\n");
}

module_init(nxp_simtemp_init);
module_exit(nxp_simtemp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Juan Antonio Coronado Eguia, <jcoro.eguia@gmail.com>");
MODULE_DESCRIPTION("NXP Virtual Temperature Sensor Driver");
MODULE_VERSION("1.0");
