// SPDX-License-Identifier: GPL-2.0-only
/*
 * A block device driver module by: Daniel Vlasenco
 */

#include <linux/blkdev.h>
#include <linux/module.h>

#define THIS_DEVICE_NAME "sdmy"
#define THIS_DEVICE_PATH "/dev/sdmy"

static struct block_device_handle {
	char *name;
	char *path;
};

static struct block_device_handle *base_handle;

static int __init blkdevm_init(void)
{
	pr_info("blkdev module init\n");

	return 0;
}

static void __exit blkdevm_exit(void)
{
	if (base_handle) {
		kfree(base_handle->name);
		kfree(base_handle->path);
	}
	kfree(base_handle);

	pr_info("blkdev module exit\n");
}

static int base_name_and_path_set(const char *arg, const struct kernel_param *kp)
{
	int len;
	char *name;
	char *path;

	if (!base_handle) {
		base_handle = kzalloc(sizeof(*base_handle), GFP_KERNEL);
		if (!base_handle)
			return -ENOMEM;
	}
	len = strlen(arg);

	name = kzalloc(sizeof(char) * (len + 1), GFP_KERNEL);
	path = kzalloc(sizeof(char) * len, GFP_KERNEL);
	if (!name || !path) {
		kfree(name);
		kfree(path);
		pr_err("failed to allocate base block device name or path\n");
		return -ENOMEM;
	}

	strncpy(name, arg, len);
	strncpy(path, arg, len - 1);

	base_handle->name = name;
	base_handle->path = path;

	return 0;
}

static int base_name_get(char *buf, const struct kernel_param *kp)
{
	ssize_t len;

	if (!base_handle || !base_handle->name) {
		pr_err("base device name was not set\n");
		return -EINVAL;
	}

	len = strlen(base_handle->name);
	strcpy(buf, base_handle->name);

	return len;
}

static const struct kernel_param_ops base_ops = {
	.set = base_name_and_path_set,
	.get = base_name_get,
};

MODULE_PARM_DESC(base, "Base block device name");
module_param_cb(base, &base_ops, NULL, S_IRUGO | S_IWUSR);

module_init(blkdevm_init);
module_exit(blkdevm_exit);

MODULE_AUTHOR("Vlasenko Daniil <vlasenko.daniil26@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Block device driver module");