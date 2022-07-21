#ifndef __TES_CDC_H__
#define __TES_CDC_H__

/* IOCTL Commands */
#define CDC_IOCTL_TYPE 'E'
#define CDC_IOCTL_REG_PREFIX 	(0x80)
#define CDC_IOCTL_NR_SETTINGS	(0x01)
#define CDC_IOCTL_SET_WORKING_REG (0x02)
/* Read and write share same IOCTL number as the IOW and IOR allow distinction */
#define CDC_IOCTL_REG_WRITE (0x03)
#define CDC_IOCTL_REG_READ (0x03)
#define CDC_IOCTL_SET_REG (_IOW(CDC_IOCTL_TYPE,CDC_IOCTL_SET_WORKING_REG,unsigned int))
#define CDC_IOCTL_W (_IOW(CDC_IOCTL_TYPE,CDC_IOCTL_REG_WRITE,unsigned int))
#define CDC_IOCTL_R (_IOR(CDC_IOCTL_TYPE,CDC_IOCTL_REG_READ,unsigned int))
#define CDC_IOCTL_GET_SETTINGS (_IOR(CDC_IOCTL_TYPE,CDC_IOCTL_NR_SETTINGS,CDC_SETTINGS))

/* CDC resource information */
typedef struct
{
	unsigned long base_phys;
	unsigned long span;
} cdc_settings;

#endif
