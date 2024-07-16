// SPDX-License-Identifier: GPL-2.0-only
/*
 * A block device driver module by: Daniel Vlasenco
 */

#include <linux/blkdev.h>
#include <linux/module.h>

#define THIS_DEVICE_NAME "sdmy"
#define THIS_DEVICE_PATH "/dev/sdmy"

static struct block_device_handle {
	struct file *bdev_file;
	struct gendisk *assoc_disk;
	char *name;
	char *path;
};

static struct block_device_handle *base_handle;
static int major;

static struct gendisk *init_disk(sector_t capacity);
static const struct block_device_operations sdmy_fops;

static int __init blkdevm_init(void)
{
	major = register_blkdev(0, THIS_DEVICE_NAME);
	if (major < 0) {
		pr_err("failed to obtain major\n");
		return major;
	}
	pr_warn("blkdev module init\n");

	return 0;
}

static void __exit blkdevm_exit(void)
{
	if (base_handle && base_handle->assoc_disk)
		put_disk(base_handle->assoc_disk);
	if (base_handle && base_handle->bdev_file)
		fput(base_handle->bdev_file);
	if (base_handle) {
		kfree(base_handle->name);
		kfree(base_handle->path);
	}
	kfree(base_handle);

	pr_warn("blkdev module exit\n");
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
	if (base_handle->bdev_file) {
		pr_err("need to close device before setting new one\n");
		return -EBUSY;
	}

	len = strlen(arg);
	name = kzalloc(sizeof(char) * (len + 1), GFP_KERNEL);
	path = kzalloc(sizeof(char) * len, GFP_KERNEL);
	if (!name || !path) {
		kfree(name);
		kfree(path);
		pr_err("failed to allocate name or path\n");
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

	if (!base_handle || !base_handle->path) {
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

static int open_base(const char *arg, const struct kernel_param *kp)
{
	struct file *bdev_file;
	struct block_device *bdev;
	sector_t disk_capacity;
	struct gendisk *disk;

	if (!base_handle || !base_handle->path) {
		pr_err("nothing to open\n");
		return -EINVAL;
	}
	if (base_handle->bdev_file) {
		pr_err("base device is already opened\n");
		return -EBUSY;
	}

	bdev_file = bdev_file_open_by_path(base_handle->path,
		BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL);
	pr_err("bdev file: %p\n", bdev_file);
	
	if (IS_ERR(bdev_file)) {
		pr_err("cannot open block device %s, "
		"did you set a valid name?\n", base_handle->path);
		return PTR_ERR(bdev_file);
	}

	bdev = file_bdev(bdev_file);
	disk_capacity = get_capacity(bdev->bd_disk);

	disk = init_disk(disk_capacity);
	if (IS_ERR(disk))
		return PTR_ERR(disk);


	base_handle->bdev_file = bdev_file;
	base_handle->assoc_disk = disk;

	pr_warn("%s: open", base_handle->path);

	return 0;
}

static const struct kernel_param_ops open_ops = {
	.set = open_base,
	.get = NULL,
};

static struct gendisk *init_disk(sector_t capacity)
{
	struct gendisk *disk;
	int err;

	disk = blk_alloc_disk(NULL, NUMA_NO_NODE);
	if (IS_ERR(disk)) {
		pr_err("failed to allocate disk\n");
		return disk;
	}

	disk->major = major;
	disk->first_minor = 0;
	disk->minors = 1;
	strcpy(disk->disk_name, base_handle->path);
	disk->fops = &sdmy_fops;
	disk->private_data = NULL;

	err = add_disk(disk);
	if (err) {
		pr_err("failed to add disk\n");
		put_disk(disk);
		return ERR_PTR(err);
	}

	pr_warn("requested capacity: %llu", capacity);
	set_capacity(disk, capacity);
	pr_warn("actual capacity: %llu\n", get_capacity(disk));

	return disk;
}

static const struct block_device_operations sdmy_fops = {
	.owner = THIS_MODULE,
//	.submit_bio = sdmy_submit_bio,
};

static int close_base(const char *arg, const struct kernel_param *kp)
{
	if (!base_handle || !base_handle->bdev_file) {
		pr_err("nothing to close\n");
		return -EINVAL;
	}
	fput(base_handle->bdev_file);
	base_handle->bdev_file = NULL;

	return 0;
}

static const struct kernel_param_ops close_ops = {
	.set = close_base,
	.get = NULL,
};

MODULE_PARM_DESC(base, "Base block device name");
module_param_cb(base, &base_ops, NULL, S_IRUGO | S_IWUSR);

MODULE_PARM_DESC(open, "Open base block device");
module_param_cb(open, &open_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(close, "Close base block device");
module_param_cb(close, &close_ops, NULL, S_IWUSR);

module_init(blkdevm_init);
module_exit(blkdevm_exit);

MODULE_AUTHOR("Vlasenko Daniil <vlasenko.daniil26@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Block device driver module");