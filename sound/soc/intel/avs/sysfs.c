// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/sysfs.h>
#include "avs.h"

static ssize_t fw_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct avs_dev *adev = to_avs_dev(dev);
	struct avs_fw_version *fw_version = &adev->fw_cfg.fw_version;

	return sysfs_emit(buf, "%d.%d.%d.%d\n", fw_version->major, fw_version->minor,
			  fw_version->hotfix, fw_version->build);
}
static DEVICE_ATTR_RO(fw_version);

static struct attribute *avs_fw_attrs[] = {
	&dev_attr_fw_version.attr,
	NULL
};

struct avs_notify_voice_data *avs_keyphrase_data = NULL;
DEFINE_MUTEX(keyphrase_notify_mutex);

static ssize_t keyphrase_notify_read(struct file *file, struct kobject *kobj,
				     struct bin_attribute *attr, char *buf,
				     loff_t pos, size_t count)
{
	ssize_t size = 0;
	/* sanity check if userspace does what we expect it to do */
	if (pos != 0 || count != sizeof(*avs_keyphrase_data))
		return -EINVAL;

	mutex_lock(&keyphrase_notify_mutex);
	if (!avs_keyphrase_data)
		goto exit; /* no data to read */

	size = sizeof(*avs_keyphrase_data);
	memcpy(buf, avs_keyphrase_data, size);
	kfree(avs_keyphrase_data);
	avs_keyphrase_data = NULL;
exit:
	mutex_unlock(&keyphrase_notify_mutex);
	return size;
}
static BIN_ATTR_RO(keyphrase_notify, 0);

static struct bin_attribute *avs_bin_attrs[] = {
	&bin_attr_keyphrase_notify,
	NULL
};

static const struct attribute_group avs_attr_group = {
	.name = "avs",
	.attrs = avs_fw_attrs,
	.bin_attrs = avs_bin_attrs,
};

const struct attribute_group *avs_attr_groups[] = {
	&avs_attr_group,
	NULL
};
