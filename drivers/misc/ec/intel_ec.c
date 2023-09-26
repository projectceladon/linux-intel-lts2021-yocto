// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Intel Embedded Controller.
 *
 * Copyright (C) 2023 Intel Corporation.
 *	Zhenlong Ji <zhenlong.z.ji@intel.com>
 */
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "intel_ec"

#define EC_IOCTL_MAGIC 'E'
#define EC_ERASE _IOW(EC_IOCTL_MAGIC, 1, char *)
#define EC_VERIFY_ERASE_STATUS _IOR(EC_IOCTL_MAGIC, 2, char *)

#define CFG_PORT1            0x2E
#define CFG_PORT2            0x4E

#define EC_STATUS            0x66
#define EC_CMD               0x66
#define EC_DATA              0x62

#define CMD_AAI_BYTE_PRO     0xAF
#define CMD_AAI_WORD_PRO     0xAD
#define CMD_CHIP_ERASE       0xC7
#define CMD_DEV_ID1          0xAB
#define CMD_DEV_ID2          0x90
#define CMD_EWSR             0x50
#define CMD_FAST_READ        0x0B
#define CMD_JEDEC_ID         0x9F
#define CMD_PAGE_PROGRAM     0x02
#define CMD_RDSR             0x05
#define CMD_READ             0x03
#define CMD_WRDI             0x04
#define CMD_WREN             0x06
#define CMD_WRSR             0x01
#define CMD_1K_SEC_ERASE     0xD7

#define EC_IBF               0x02
#define EC_OBF               0x01
#define ENTER_FOLLOW_MODE    0x01
#define ENTER_FLASH_MODE     0xDC
#define EXIT_FOLLOW_MODE     0x05
#define EXIT_FLASH_MODE      0xFC
#define READ_BYTE            0x04
#define SEND_CMD             0x02
#define SEND_BYTE            0x03

#define BLOCK_SIZE           256
#define FLASH_SIZE           0x20000
#define HALF_FLASH_BLOCKS    256
#define HALF_FLASH_SIZE      0x10000
#define MAX_DATA_SIZE        4096

static char *ec_buffer = NULL;
static unsigned char flash_id[3];
static int total_read = 0;
static int total_write = 0;

static void wait_ec_obf(void) {
	unsigned char status = inb(EC_STATUS);
	while(!(status & EC_OBF)) {
		status = inb(EC_STATUS);
	}
}

static void wait_ec_ibe(void) {
	unsigned char status = inb(EC_STATUS);
	while(status & EC_IBF) {
		status = inb(EC_STATUS);
	}
}

static void send_cmd_to_pm(unsigned char cmd) {
	wait_ec_ibe();
	outb(cmd, EC_CMD);
	wait_ec_ibe();
}

static unsigned char read_data_from_pm(void) {
	unsigned char data = 0;
	wait_ec_obf();
	data = inb(EC_DATA);
	return data;
}

static void follow_mode(unsigned char mode) {
	send_cmd_to_pm(mode);
}

static void send_cmd_to_ec(unsigned char cmd) {
	send_cmd_to_pm(SEND_CMD);
	send_cmd_to_pm(cmd);
}

static void send_byte_to_ec(unsigned char data) {
	send_cmd_to_pm(SEND_BYTE);
	send_cmd_to_pm(data);
}

static unsigned char read_byte_from_ec(void) {
	send_cmd_to_pm(READ_BYTE);
	return read_data_from_pm();
}

static void wait_for_ec_free(void) {
	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_RDSR);
	while(read_byte_from_ec() & 0x01);
	follow_mode(EXIT_FOLLOW_MODE);
}

static void enable_flash_write(void) {
	wait_for_ec_free();
	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_WRSR);
	send_byte_to_ec(0x00);

	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_WREN);

	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_RDSR);
	while(!(read_byte_from_ec() & 0x02));
	follow_mode(EXIT_FOLLOW_MODE);
}

static void disable_flash_write(void) {
	wait_for_ec_free();
	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_WRDI);

	wait_for_ec_free();
	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_RDSR);

	while(read_byte_from_ec() & 0x02);
	follow_mode(EXIT_FOLLOW_MODE);
}

static void read_flash_jedec_id(void) {
	int index = 0;
	wait_for_ec_free();
	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_JEDEC_ID);
	for (; index < 3; index++) {
		flash_id[index] = read_byte_from_ec();
	}

	follow_mode(EXIT_FOLLOW_MODE);
}

static void enable_ec_status_reg_write(void) {
	wait_for_ec_free();
	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_WREN);

	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_EWSR);
}

static void block_1k_erase(unsigned char addr2, unsigned char addr1, unsigned char addr0) {
	enable_ec_status_reg_write();
	enable_flash_write();
	wait_for_ec_free();
	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_1K_SEC_ERASE);
	send_byte_to_ec(addr2);
	send_byte_to_ec(addr1);
	send_byte_to_ec(addr0);
	wait_for_ec_free();
}

static void ec_erase(void) {
	unsigned char i, j;
	for (i = 0; i < 0x02; i++) {
		for (j = 0; j < 0x100; j += 0x04) {
			block_1k_erase(i, j, 0x00);
		}
	}
}

static int ec_erase_verify(void) {
	unsigned int counter, data, i;
	disable_flash_write();
	wait_for_ec_free();
	follow_mode(ENTER_FOLLOW_MODE);
	send_cmd_to_ec(CMD_FAST_READ);
	send_byte_to_ec(0x00);
	send_byte_to_ec(0x00);
	send_byte_to_ec(0x00);
	send_byte_to_ec(0x00);

	for(i = 0; i < 0x04; i++) {
		for(counter = 0; counter < 0x8000; counter++) {
			data = read_byte_from_ec();
			if (data != 0xFF) {
				wait_for_ec_free();
				return -1;
			}
		}
	}
	wait_for_ec_free();
	return 0;
}

static int intel_ec_open(struct inode *inode, struct file *file) {
	int mode = file->f_mode & (FMODE_READ | FMODE_WRITE);

	if (mode != FMODE_READ && mode != FMODE_WRITE) {
		pr_err("Invalid access mode\n");
		return -EINVAL;
	}

	ec_buffer = (unsigned char *)kmalloc(MAX_DATA_SIZE, GFP_KERNEL);
	if (!ec_buffer) {
		pr_info("EC: buffer allocation failed\n");
		return -ENOMEM;
	}

	send_cmd_to_pm(ENTER_FLASH_MODE);
	while(0x33 != read_data_from_pm());

	if (mode == FMODE_READ) {
		pr_info("Device opened with read-only access\n");
		wait_for_ec_free();
		follow_mode(ENTER_FOLLOW_MODE);
		send_cmd_to_ec(CMD_FAST_READ);
		send_byte_to_ec(0x00);
		send_byte_to_ec(0x00);
		send_byte_to_ec(0x00);
		send_byte_to_ec(0x00);
	} else if (mode == FMODE_WRITE) {
		pr_info("Device opened with write-only access\n");
	}

	return 0;
}

static int intel_ec_close(struct inode *inode, struct file *file) {
	pr_info("intel_ec_close\n");
	int mode = file->f_mode & (FMODE_READ | FMODE_WRITE);
	follow_mode(EXIT_FOLLOW_MODE);
	if (mode == FMODE_WRITE) {
		msleep(5000);
	}
	send_cmd_to_pm(EXIT_FLASH_MODE);
	if (ec_buffer)
		kfree(ec_buffer);
	ec_buffer = NULL;
	total_read = 0;
	total_write = 0;
	return 0;
}

static ssize_t intel_ec_read(struct file *filp, char __user *buf,
			size_t count, loff_t *ppos) {
	ssize_t bytes_read = 0;
	int index, read_count;

	if (ppos == NULL || *ppos >= FLASH_SIZE)
		return 0;

	read_count = min(count, MAX_DATA_SIZE);
	bytes_read = min(FLASH_SIZE - total_read, read_count);
	if (bytes_read == 0) {
		pr_info("EC: finished the read\n");
		return 0;
	}

	if (ec_buffer == NULL)
		return -EFAULT;

	for (index = 0; index < bytes_read; index++) {
		ec_buffer[index] = read_byte_from_ec();
	}

	if (copy_to_user(buf, ec_buffer, bytes_read) != 0) {
		pr_info("EC: copy_to_user failed\n");
		return -EFAULT;
	}

	*ppos += bytes_read;
	total_read += bytes_read;

	return bytes_read;
}

static ssize_t intel_ec_write(struct file *filp, const char __user *buf,
			size_t count, loff_t *ppos) {
	ssize_t bytes_write = 0;
	int write_count, i;
	int block, cur_pos;
	int temp1, temp2;

	if (ppos == NULL || *ppos >= FLASH_SIZE)
		return -EINVAL;

	write_count = min(count, MAX_DATA_SIZE);
	bytes_write = min(FLASH_SIZE - total_write, write_count);
	if (bytes_write == 0) {
		pr_info("EC: bytes write is zero\n");
		return 0;
	}

	if (bytes_write % BLOCK_SIZE != 0) {
		pr_info("EC: bytes count should be an integer multiple of block size(%d)\n",
		  BLOCK_SIZE);
		return -EINVAL;
	}

	if (ec_buffer == NULL)
		return -EFAULT;

	if (copy_from_user(ec_buffer, buf, bytes_write)) {
		return -EFAULT;
	}

	for (block = 0; block < (bytes_write / BLOCK_SIZE); block++) {
		cur_pos = block * BLOCK_SIZE;
		enable_flash_write();
		wait_for_ec_free();
		follow_mode(ENTER_FOLLOW_MODE);
		send_cmd_to_ec(CMD_PAGE_PROGRAM);
		send_byte_to_ec((total_write < HALF_FLASH_SIZE) ? 0 : 1);
		send_byte_to_ec((total_write / BLOCK_SIZE) % HALF_FLASH_BLOCKS);
		send_byte_to_ec(0x00);
		for (i = 0; i < BLOCK_SIZE; i++) {
			send_byte_to_ec(ec_buffer[cur_pos + i]);
		}
		total_write += BLOCK_SIZE;
		*ppos += BLOCK_SIZE;
		wait_for_ec_free();
	}

	return bytes_write;
}

static long ec_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	int ret = 0, status = 0;

	switch (cmd) {
		case EC_ERASE:
			ec_erase();
			break;

		case EC_VERIFY_ERASE_STATUS:
			status = ec_erase_verify();
			if (copy_to_user((char *)arg, &status, sizeof(status))) {
				return -EFAULT;
			}
			break;

		default:
			return -EINVAL;
	}

	return ret;
}

static const struct file_operations intel_ec_fops = {
	.owner = THIS_MODULE,
	.open = intel_ec_open,
	.release = intel_ec_close,
	.read = intel_ec_read,
	.write = intel_ec_write,
	.unlocked_ioctl = ec_ioctl,
};

static struct miscdevice intel_ec_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &intel_ec_fops,
};

static int __init intel_ec_init(void) {
	int ret;

	ret = misc_register(&intel_ec_device);
	if (ret) {
		pr_err("Failed to register misc device\n");
		return ret;
	}

	pr_info("Misc device registered: %s\n", DEVICE_NAME);
	return 0;
}

static void __exit intel_ec_exit(void) {
	misc_deregister(&intel_ec_device);
	pr_info("Misc device unregistered: %s\n", DEVICE_NAME);
}

module_init(intel_ec_init);
module_exit(intel_ec_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhenlong Ji");
MODULE_DESCRIPTION("Driver for Embedded Controller");

