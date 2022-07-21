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
#define CDC_OF_COMPATIBLE				"tes,cdc-1.0"

/* Register access macros */
#define CDC_IO_WREG(addr,data) 			iowrite32(data,addr)
#define CDC_IO_RREG(addr) 				ioread32(addr)
#define CDC_IO_RADDR(base,reg)			((void*)((unsigned long)base|((unsigned long)reg)<<2))

struct cdc_dev
{
	unsigned long base_phys;
	unsigned long span;
	void *base_virt;
	unsigned int irq_no;
	unsigned int irq_stat;
	spinlock_t irq_slck;
	wait_queue_head_t irq_waitq;
	dev_t dev;
	struct cdev cdev;
	struct device *device;
};

#endif /* TES_DAVE_MODULE_H_ */
