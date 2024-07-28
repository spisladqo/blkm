// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bio-based block device driver module by: Daniel Vlasenco
 */

#include "blkm.h"
#include <linux/module.h>

#define THIS_DEVICE_NAME "sdblk"
#define THIS_DEVICE_PATH "/dev/sdblk"
#define MAX_PATH_LEN 20

static struct gendisk *init_disk(sector_t capacity);
static void blkm_submit_bio(struct bio *bio);
static const struct block_device_operations blkm_fops;

static struct blkm_dev {
	struct bdev_handle *bh;
	struct gendisk *assoc_disk;
	char *path;
};

static struct blkm_dev *base_handle;
static struct bio_set *bio_pool;
static int major;

static sector_t next_free_sector = 0;
static struct skiplist *skiplist;

static int __init blkm_init(void)
{
	int err;

	major = register_blkdev(0, THIS_DEVICE_NAME);
	if (major < 0) {
		pr_warn("failed to register block device\n");
		err = major;
		goto reg_fail;
	}
	bio_pool = kzalloc(sizeof(*bio_pool), GFP_KERNEL);
	if (!bio_pool) {
		pr_warn("failed to allocate bioset\n");
		err = -ENOMEM;
		goto bioset_alloc_fail;
	}
	err = bioset_init(bio_pool, BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (err) {
		pr_warn("failed to initialize bioset\n");
		goto init_fail;
	}
	skiplist = skiplist_init();
	if (!skiplist) {
		pr_warn("failed to initialize skiplist\n");
		err = -ENOMEM;
		goto init_fail;
	}

	pr_warn("blkdev module init\n");
	return 0;

init_fail:
	bioset_exit(bio_pool);
	kfree(bio_pool);
bioset_alloc_fail:
	unregister_blkdev(major, THIS_DEVICE_NAME);
reg_fail:
	return err;
}

static void __exit blkm_exit(void)
{
	if (base_handle) {
		if (base_handle->assoc_disk) {
			del_gendisk(base_handle->assoc_disk);
			put_disk(base_handle->assoc_disk);
		}
		if (base_handle->bh)
			bdev_release(base_handle->bh);
		kfree(base_handle->path);
	}
	kfree(base_handle);
	bioset_exit(bio_pool);
	kfree(bio_pool);
	unregister_blkdev(major, THIS_DEVICE_NAME);
	skiplist_free(skiplist);

	pr_warn("blkdev module exit\n");
}

static int base_path_set(const char *arg, const struct kernel_param *kp)
{
	int len;
	char *path;

	if (!base_handle) {
		base_handle = kzalloc(sizeof(*base_handle), GFP_KERNEL);
		if (!base_handle) {
			pr_err("failed to allocate base blkm_dev handle\n");
			return -ENOMEM;
		}
	}
	if (base_handle->bh || base_handle->assoc_disk) {
		pr_err("need to close device before setting new one\n");
		return -EBUSY;
	}

	len = strcspn(arg, "\n");
	if (len >= MAX_PATH_LEN)
		return -ENAMETOOLONG;

	path = kzalloc(sizeof(char) * (len + 1), GFP_KERNEL);
	if (!path) {
		pr_err("failed to allocate path to block device\n");
		return -ENOMEM;
	}
	strncpy(path, arg, len);
	base_handle->path = path;

	return 0;
}

static int base_path_get(char *buf, const struct kernel_param *kp)
{
	int len;

	if (!base_handle || !base_handle->path) {
		pr_err("path to base device was not set\n");
		return -EINVAL;
	}
	len = snprintf(buf, MAX_PATH_LEN, "%s\n", base_handle->path);

	return len;
}

static const struct kernel_param_ops base_ops = {
	.set = base_path_set,
	.get = base_path_get,
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
		pr_err("cannot open block device '%s'\n", base_handle->path);
		return PTR_ERR(bh);
	}

	base_disk_cap = get_capacity(bh->bdev->bd_disk);
	new_disk = init_disk(base_disk_cap);
	if (IS_ERR_OR_NULL(new_disk)) {
		pr_err("failed to initialize disk\n");
		err = PTR_ERR(new_disk);
		goto disk_init_fail;
	}
	new_disk->private_data = base_handle;
	base_handle->bh = bh;
	base_handle->assoc_disk = new_disk;

	err = add_disk(new_disk);
	if (err) {
		pr_err("failed to add disk after initialization\n");
		goto disk_add_fail;
	}

	pr_warn("opened device '%s' and created disk '%s' based on it\n",
			base_handle->path, new_disk->disk_name);
	return 0;

disk_add_fail:
	put_disk(new_disk);
	base_handle->bh = NULL;
	base_handle->assoc_disk = NULL;
disk_init_fail:
	bdev_release(bh);
	return err;
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
	disk->fops = &blkm_fops;

	pr_warn("requested capacity: %llu\n", capacity);
	set_capacity(disk, capacity);
	pr_warn("actual capacity: %llu\n", get_capacity(disk));

	return disk;
}

int redirect_read(struct bio *bio)
{
	struct skiplist_node *node;
	sector_t orig_address;
	sector_t redir_address;

	orig_address = bio->bi_iter.bi_sector;
	node = skiplist_find_node(orig_address, skiplist);
	if (!node) {
		pr_err("failed to read: address %llu is not mapped\n",
			orig_address);
		return -EINVAL;
	}
	redir_address = node->data;
	pr_warn("successful read: address %llu is mapped to %llu\n",
			orig_address, redir_address);

	bio->bi_iter.bi_sector = redir_address;

	return 0;
}

/*
 * TODO: add write lenght check, and if rewrite request exceedes it,
 * do not allow it.
 */
static int redirect_write(struct bio *bio)
{
	struct skiplist_node *node;
	sector_t orig_address;
	sector_t redir_address;
	sector_t op_size;

	orig_address = bio->bi_iter.bi_sector;
	redir_address = next_free_sector;
	op_size = (bio->bi_iter.bi_size + SECTOR_SIZE - 1) / SECTOR_SIZE;

	node = skiplist_add(orig_address, redir_address, skiplist);
	if (IS_ERR(node)) {
		pr_err("failed to add mapping %llu to %llu to skiplist\n",
			orig_address, redir_address);
		return PTR_ERR(node);
	}

	if (redir_address == node->data) {
		pr_warn("successful write: address %llu is already mapped to %llu\n",
			orig_address, redir_address);
		next_free_sector += op_size;
		pr_warn("next free sector is now %llu\n", next_free_sector);
	} else {
		pr_warn("successful write: address %llu is now mapped to %llu\n",
			orig_address, redir_address);
		redir_address = node->data;
	}
	bio->bi_iter.bi_sector = redir_address;

	return 0;
}

static int map_bio_address(struct bio *bio)
{
	enum req_op bio_oper = bio_op(bio);
	int err;

	switch (bio_oper) {
	case REQ_OP_READ:
		err = redirect_read(bio);
		break;
	case REQ_OP_WRITE:
		err = redirect_write(bio);
		break;
	default:
		pr_err("operation %d is not supported\n", bio_oper);
		err = -EINVAL;
	}
	if (err)
		return err;

	return 0;
}

static void blkm_bio_end_io(struct bio *bio)
{
	bio_endio(bio->bi_private);
	bio_put(bio);
}

static void blkm_submit_bio(struct bio *bio)
{
	struct bio *new_bio;
	struct block_device *base_dev;
	int err;

	base_dev = base_handle->bh->bdev;
	new_bio = bio_alloc_clone(base_dev, bio, GFP_KERNEL, bio_pool);
	if (!new_bio)
		goto fail;

	new_bio->bi_private = bio;
	new_bio->bi_end_io = blkm_bio_end_io;
	err = map_bio_address(new_bio);
	if (err)
		goto fail;

	submit_bio(new_bio);
	return;
fail:
	if (new_bio)
		bio_put(new_bio);
	bio_io_error(bio);
}

static const struct block_device_operations blkm_fops = {
	.owner = THIS_MODULE,
	.submit_bio = blkm_submit_bio,
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
	pr_warn("closing device '%s' and destroying disk '%s' based on it\n",
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

module_init(blkm_init);
module_exit(blkm_exit);

MODULE_AUTHOR("Vlasenco Daniel <vlasenko.daniil26@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bio-based block device driver module");