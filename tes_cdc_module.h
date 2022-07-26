#ifndef TES_DAVE_MODULE_H_
#define TES_DAVE_MODULE_H_

#include <linux/wait.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>

/* Linux character device config */
#define CDC_DEVICE_NAME					"cdc"
#define CDC_DEVICE_CLASS				"cdc"
#define CDC_DEVICE_CNT					1u

/* device tree node */
#define CDC_OF_COMPATIBLE				"tes,cdc-2.1"

struct cdc_dev
{
	unsigned long base_phys; // fixme: use resource_size_t
	unsigned long span;// fixme: use resource_size_t
	void __iomem *mmio;
	unsigned int irq_no;
	unsigned int irq_stat;
	spinlock_t irq_slck;
	wait_queue_head_t irq_waitq;
	dev_t dev;
	struct cdev cdev;
	struct device *device;
};

/* Register access */
u32 cdc_read_reg (struct cdc_dev *cdc, u32 reg)
{
	return ioread32(cdc->mmio + (reg * 4));
}

void cdc_write_reg (struct cdc_dev *cdc, u32 reg, u32 val)
{
	iowrite32(val, cdc->mmio + (reg * 4));
}

#endif /* TES_DAVE_MODULE_H_ */
