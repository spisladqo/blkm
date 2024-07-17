// SPDX-License-Identifier: GPL-2.0-only
/*
 * A block device driver module by: Daniel Vlasenco
 */

#include <linux/blkdev.h>
#include <linux/module.h>

#define THIS_DEVICE_NAME "sdmy"
#define THIS_DEVICE_PATH "/dev/sdmy"

static struct gendisk *init_disk(sector_t capacity);
static void sdmy_submit_bio(struct bio *bio);
static const struct block_device_operations sdmy_fops;

static struct block_device_handle {
	struct bdev_handle *bh;
	struct gendisk *assoc_disk;
	char *name;
	char *path;
};

static struct block_device_handle *base_handle;
static struct bio_set *bio_pool;
static int major;

static int __init blkdevm_init(void)
{
	int err;

	major = register_blkdev(0, THIS_DEVICE_NAME);
	if (major < 0) {
		pr_err("failed to obtain major\n");
		return major;
	}
	bio_pool = kzalloc(sizeof(*bio_pool), GFP_KERNEL);
	if (!bio_pool) {
		pr_err("failed to allocate bioset\n");
		unregister_blkdev(major, THIS_DEVICE_NAME);
		return -ENOMEM;
	}
	err = bioset_init(bio_pool, BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (err) {
		pr_err("failed to initialize bioset\n");
		bioset_exit(bio_pool);
		kfree(bio_pool);
		unregister_blkdev(major, THIS_DEVICE_NAME);
		return err;
	}

	pr_warn("blkdev module init\n");

	return 0;
}

static void __exit blkdevm_exit(void)
{
	if (base_handle) {
		kfree(base_handle->name);
		kfree(base_handle->path);
	}
	if (base_handle && base_handle->assoc_disk) {
		del_gendisk(base_handle->assoc_disk);
		put_disk(base_handle->assoc_disk);
	}
	if (base_handle && base_handle->bh) {
		bdev_release(base_handle->bh);
	}
	kfree(base_handle);
	bioset_exit(bio_pool);
	kfree(bio_pool);
	unregister_blkdev(major, THIS_DEVICE_NAME);

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
	if (base_handle->bh || base_handle->assoc_disk) {
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

static int open_base_and_create_disk(const char *arg, const struct kernel_param *kp)
{
	struct bdev_handle *bh;
	sector_t base_disk_cap;
	struct gendisk *new_disk;
	int err;

	if (!base_handle || !base_handle->path) {
		pr_err("nothing to open\n");
		return -EINVAL;
	}
	if (base_handle->bh || base_handle->assoc_disk) {
		pr_err("base device is already opened\n");
		return -EBUSY;
	}

	bh = bdev_open_by_path(base_handle->path, BLK_OPEN_READ |
				BLK_OPEN_WRITE, NULL, NULL);
	if (IS_ERR(bh)) {
		pr_err("cannot open block device %s\n", base_handle->path);
		return PTR_ERR(bh);
	}

	base_disk_cap = get_capacity(bh->bdev->bd_disk);
	new_disk = init_disk(base_disk_cap);
	if (IS_ERR_OR_NULL(new_disk)) {
		pr_err("failed to initialize disk\n");
		bdev_release(bh);
		return PTR_ERR(new_disk);
	}
	new_disk->private_data = base_handle;
	base_handle->bh = bh;
	base_handle->assoc_disk = new_disk;

	err = add_disk(new_disk);
	if (err) {
		pr_err("failed to add disk after initialization\n");
		bdev_release(bh);
		put_disk(new_disk);
		base_handle->bh = NULL;
		base_handle->assoc_disk = NULL;
		return err;
	}

	pr_warn("opened device '%s' and created disk '%s' based on it\n",
			base_handle->path, new_disk->disk_name);

	return 0;
}

static const struct kernel_param_ops open_ops = {
	.set = open_base_and_create_disk,
	.get = NULL,
};

static struct gendisk *init_disk(sector_t capacity)
{
	struct gendisk *disk;

	disk = blk_alloc_disk(NUMA_NO_NODE);
	if (IS_ERR_OR_NULL(disk)) {
		pr_err("failed to allocate disk\n");
		return disk;
	}

	disk->major = major;
	disk->first_minor = 1;
	disk->minors = 1;
	strcpy(disk->disk_name, THIS_DEVICE_NAME);
	disk->fops = &sdmy_fops;

	pr_warn("requested capacity: %llu\n", capacity);
	set_capacity(disk, capacity);
	pr_warn("actual capacity: %llu\n", get_capacity(disk));

	return disk;
}

static void sdmy_submit_bio(struct bio *bio)
{
	struct bio *new_bio;
	struct block_device *base_dev;

	// when we get here, we should have device opened and disk created
	BUG_ON(!base_handle);
	BUG_ON(!base_handle->bh);
	BUG_ON(!base_handle->assoc_disk);

	base_dev = base_handle->bh->bdev;
	new_bio = bio_alloc_clone(base_dev, bio, GFP_KERNEL, bio_pool);
	if (!new_bio) {
		pr_err("failed to allocate new bio based on incoming bio\n");
		return;
	}

	bio_chain(new_bio, bio);
	submit_bio(new_bio);
	bio_endio(bio);
}

static const struct block_device_operations sdmy_fops = {
	.owner = THIS_MODULE,
	.submit_bio = sdmy_submit_bio,
};

static int close_base(const char *arg, const struct kernel_param *kp)
{
	char *disk_name;

	if (!base_handle || !base_handle->bh) {
		pr_err("nothing to close\n");
		return -EINVAL;
	}
	if (!base_handle->assoc_disk) {
		pr_err("disk wasn't allocated, cannot close\n");
		return -EINVAL;
	}

	disk_name = base_handle->assoc_disk->disk_name;
	pr_warn("closing device '%s' and destroying disk %s based on it\n",
			base_handle->path, disk_name);

	bdev_release(base_handle->bh);
	base_handle->bh = NULL;
	del_gendisk(base_handle->assoc_disk);
	put_disk(base_handle->assoc_disk);
	base_handle->assoc_disk = NULL;

	pr_warn("closed device and destroyed disk successfully\n");

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