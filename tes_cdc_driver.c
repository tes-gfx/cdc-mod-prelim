#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "tes_cdc_module.h"
#include "tes_cdc_driver.h"
#include "cdc_base.h"

/* store class globally. So far, this module does not fully support multiple
 * cdc instances but still... */
struct class *cdc_class;

/* fops functions */
static int cdc_open(struct inode *ip, struct file *fp)
{
	struct cdc_dev *dev;

	/* extract the device structure and add it to the file pointer for easier
	 * access */
	dev = container_of(ip->i_cdev, struct cdc_dev, cdev);
	fp->private_data = dev;

	return 0;
}

static long cdc_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct cdc_dev *dev = fp->private_data;
	unsigned int cmd_nr;
	cdc_settings cset;
  static int cdc_reg;

	cmd_nr = _IOC_NR(cmd);
	if (_IOC_DIR(cmd) == _IOC_WRITE)
	{
    switch(cmd_nr)
    {
      case CDC_IOCTL_REG_WRITE:
        /* direct register write: Register value in argument */
        CDC_IO_WREG(CDC_IO_RADDR(dev->base_virt,
              cdc_reg),
            arg);
        break;
      case CDC_IOCTL_SET_WORKING_REG:
        if(arg > dev->span)
        {
          return -EINVAL;
        } else {
          cdc_reg = arg;
        }
        break;
      default:
        return -EINVAL;
    }
    return 0;
	}
	else if (_IOC_DIR(cmd) == _IOC_READ)
	{
    switch(cmd_nr)
    {
      case CDC_IOCTL_REG_READ:
        if(!put_user(CDC_IO_RREG(CDC_IO_RADDR(dev->base_virt,
                  cdc_reg)), (unsigned long*) arg))
          return -EFAULT;
        break;
      case CDC_IOCTL_NR_SETTINGS:
        cset.base_phys = dev->base_phys;
        cset.span = dev->span;

        if(copy_to_user((void*) arg, (void*) &cset,
              sizeof(cdc_settings)))
        {
          dev_err(fp->private_data,
              "error while copying settings to user space\n");
          return -EFAULT;
        }
        break;
      default:
        return -EINVAL;
    }
	}
	return 0;
}

ssize_t cdc_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	struct cdc_dev *dev = filp->private_data;
	unsigned long flags;
	int temp;

	wait_event_interruptible(dev->irq_waitq, dev->irq_stat);

	spin_lock_irqsave(&dev->irq_slck, flags);
	temp = dev->irq_stat;
	dev->irq_stat = 0;
	spin_unlock_irqrestore(&dev->irq_slck, flags);

	if(count==sizeof(temp))
	{
		put_user(temp, buff);
		return sizeof(temp);
	}

	return 0;
}

static struct file_operations cdc_fops = {
	.owner = THIS_MODULE,
	.open = cdc_open,
	.unlocked_ioctl = cdc_ioctl,
	.read = cdc_read,
};

static irqreturn_t std_irq_handler(int irq, void *dev_id)
{
	unsigned long flags;
	struct cdc_dev *cdcd = dev_id;
	int status;

	status = CDC_IO_RREG(CDC_IO_RADDR(cdcd->base_virt,CDC_REG_GLOBAL_IRQ_STATUS));
	CDC_IO_WREG(CDC_IO_RADDR(cdcd->base_virt, CDC_REG_GLOBAL_IRQ_CLEAR), status);

	spin_lock_irqsave(&cdcd->irq_slck, flags);
	cdcd->irq_stat |= status;
	spin_unlock_irqrestore(&cdcd->irq_slck, flags);

	wake_up_interruptible(&cdcd->irq_waitq);

	return IRQ_HANDLED;
}

static int register_irq(struct cdc_dev *dev)
{
	if(request_irq(dev->irq_no, std_irq_handler, 0, "TES CDC", (void*) dev))
	{
		dev_err(dev->device, "irq cannot be registered\n");
		return -EBUSY;
	}

	return 0;
}

static void unregister_irq(struct cdc_dev *dev)
{
	free_irq(dev->irq_no, (void*) dev);
}

/* create character class and devices */
static int cdc_setup_device(struct cdc_dev *dev)
{
	int result = 0;

	result = alloc_chrdev_region(&dev->dev, 0, CDC_DEVICE_CNT,
			CDC_DEVICE_NAME);

	if (result < 0) {
		dev_err(dev->device, "can't alloc_chrdev_region\n");
		goto CHR_FAILED;
	}

	cdc_class = class_create(THIS_MODULE, CDC_DEVICE_CLASS);
	if(!cdc_class)
	{
		dev_err(dev->device, "cannot create class %s\n", CDC_DEVICE_CLASS);
		result = -EBUSY;
		goto CLASS_FAILED;
	}

	dev->device = device_create(cdc_class, NULL, dev->dev, dev, CDC_DEVICE_NAME);
	if(!dev->device)
	{
		dev_err(dev->device, "cannot create device %s\n", CDC_DEVICE_NAME);
		result = -EBUSY;
		goto DEVICE_FAILED;
	}

	cdev_init(&dev->cdev, &cdc_fops);
	dev->cdev.owner = THIS_MODULE;

	result = cdev_add(&dev->cdev, dev->dev, CDC_DEVICE_CNT);
	if(result)
	{
		dev_err(dev->device, "can't register char device\n");
		goto DEV_FAILED;
	}

	return 0;

DEV_FAILED:
	device_destroy(cdc_class, dev->dev);
DEVICE_FAILED:
	class_destroy(cdc_class);
CLASS_FAILED:
	unregister_chrdev_region(dev->dev, CDC_DEVICE_CNT);
CHR_FAILED:

	return result;
}

static void cdc_log_params(struct cdc_dev *dev)
{
	dev_info(dev->device, "Base address:\t0x%08lx - 0x%08lx\n",
			dev->base_phys, dev->base_phys + dev->span);
	dev_info(dev->device, "IRQ:\t%d\n", dev->irq_no);
}

static void cdc_shutdown_device(struct cdc_dev *dev)
{
	device_destroy(cdc_class, dev->dev);
	class_destroy(cdc_class);
	unregister_chrdev_region(dev->dev, CDC_DEVICE_CNT);
	cdev_del(&dev->cdev);
}

/* platform device functions:
 * on probe (new device), copy all neccessary data from device tree description
 * to local data structure and initialize the driver part.
 * */
static int cdc_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct resource rsrc;
	int result = 0;
	struct cdc_dev *cdc;

	cdc = devm_kzalloc(&pdev->dev, sizeof(struct cdc_dev), GFP_KERNEL);
	if(!cdc)
	{
		dev_err(&pdev->dev,
			"Memory allocation for driver struct failed!\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, cdc);
	cdc->device = &pdev->dev;
	np = pdev->dev.of_node;
	if(!np)
	{
		dev_err(&pdev->dev,
			"driver should only be instanciated over device tree!\n");
		return -ENODEV;
	}

	of_address_to_resource(np, 0, &rsrc);
	cdc->base_phys = rsrc.start;
	cdc->span = rsrc.end - rsrc.start;
	cdc->irq_no = of_irq_to_resource(np, 0, &rsrc);

	cdc_log_params(cdc);

	spin_lock_init(&cdc->irq_slck);
	init_waitqueue_head(&cdc->irq_waitq);

	if (!request_mem_region(cdc->base_phys, cdc->span, "TES CDC"))
	{
		dev_err(&pdev->dev, "memory region already in use\n");
		return -EBUSY;
	}

	cdc->base_virt = ioremap_nocache(cdc->base_phys, cdc->span);
	if (!cdc->base_virt)
	{
		dev_err(&pdev->dev, "ioremap failed\n");
		result = -EBUSY;
		goto IO_FAILED;
	}

	result = CDC_IO_RREG(CDC_IO_RADDR(cdc->base_virt,
				CDC_REG_GLOBAL_HW_REVISION));
	if(result < 1)
	{
		dev_warn(&pdev->dev,
				"Unknown CDC found. Identifier: 0x%08X\n",
				result);
	}
	else
	{
		dev_info(&pdev->dev, "Found CDC rev. 0x%08X\n", result);
	}

	result = CDC_IO_RREG(CDC_IO_RADDR(cdc->base_virt,
				CDC_REG_GLOBAL_LAYER_COUNT));
	if(result < 1)
	{
		dev_warn(&pdev->dev,
				"CDC supports zero layers!\n");
	}
	else
	{
		dev_info(&pdev->dev, "CDC supports %d layers!\n", result);
	}

	result = cdc_setup_device(cdc);
	if(result)
	{
		goto DEV_FAILED;
	}

	/* register cdc IRQs */
	result = register_irq(cdc);
	if(result)
	{
		dev_err(&pdev->dev, "can't register irq %d\n", cdc->irq_no);
		goto IRQ_FAILED;
	}

	dev_warn(&pdev->dev, "This driver is PRELIMINARY. Do NOT use in production environment!\n");

	return 0;

IRQ_FAILED:
	cdc_shutdown_device(cdc);
DEV_FAILED:
	iounmap(cdc->base_virt);
IO_FAILED:
	release_mem_region(cdc->base_phys, cdc->span);

	return -EBUSY;
}

static int cdc_remove(struct platform_device *pdev)
{
	struct cdc_dev *cdc = platform_get_drvdata(pdev);
	unregister_irq(cdc);
	iounmap(cdc->base_virt);
	release_mem_region(cdc->base_phys, cdc->span);
	cdc_shutdown_device(cdc);
	devm_kfree(&pdev->dev, cdc);
	return 0;
}

static const struct of_device_id cdc_of_ids[] = {
	{
		.compatible = CDC_OF_COMPATIBLE,
	},
	{ }
};

static struct platform_driver cdc_driver = {
	.driver = {
		.name = CDC_DEVICE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cdc_of_ids),
	},
	.probe = cdc_probe,
	.remove = cdc_remove,
};

static int __init _cdc_init(void)
{
	int result = 0;

	result = platform_driver_register(&cdc_driver);
	if(result)
		printk(KERN_ALERT "%s: failed to register platform driver\n", __func__);

	return result;

}

static void __exit _cdc_exit(void)
{
	platform_driver_unregister(&cdc_driver);
}

module_init(_cdc_init);
module_exit(_cdc_exit);
MODULE_AUTHOR("TES Electronic Solutions GmbH");
MODULE_DESCRIPTION("Kernel driver for CDC display controller.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DEVICE_TABLE(of, cdc_of_ids);
