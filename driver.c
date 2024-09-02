// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bio-based block device driver for log-structured storage by: Daniel Vlasenco
 */

#include "blkm.h"
#include <linux/blkdev.h>

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
} *base_handle;

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
		kfree(base_handle->path);
		if (base_handle->assoc_disk) {
			del_gendisk(base_handle->assoc_disk);
			put_disk(base_handle->assoc_disk);
		}
		if (base_handle->bh)
			bdev_release(base_handle->bh);
	}
	kfree(base_handle);
	bioset_exit(bio_pool);
	kfree(bio_pool);
	unregister_blkdev(major, THIS_DEVICE_NAME);
	skiplist_free(skiplist);

	pr_warn("blkdev module exit\n");
}

/*
 * sets base block device name if it does not exceed MAX_PATH_LEN
 */
static int base_path_set(const char *arg, const struct kernel_param *kp)
{
	int len;
	char *path;

	if (!base_handle) {
		base_handle = kzalloc(sizeof(*base_handle), GFP_KERNEL);
		if (!base_handle) {
			pr_err("failed to allocate base block device handle\n");
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

/*
 * tries to open base block device, on success creates virtual block device.
 */
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
	if (!skiplist){
		skiplist = skiplist_init();
		if (!skiplist) {
			pr_warn("failed to initialize skiplist\n");
			return -ENOMEM;
		}
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
		pr_err("failed to add disk\n");
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
	set_capacity(disk, capacity);

	return disk;
}

static sector_t get_bi_size_sectors(struct bio *bio)
{
	return (bio->bi_iter.bi_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
}

/*
 * changes bio read destination sector according to mapping stored in skiplist,
 * or creates a new mapping
 */
static int redirect_read(struct bio *bio)
{
	struct skiplist_node *node;
	sector_t virt_disk_sector;
	sector_t base_disk_sector;

	virt_disk_sector = bio->bi_iter.bi_sector;
	pr_warn("read request: sector %llu\n", virt_disk_sector);
	node = skiplist_find_node(virt_disk_sector, skiplist);
	if (!node) {
		base_disk_sector = virt_disk_sector;
		pr_warn("successful read from %llu, which is unmapped\n", virt_disk_sector);
	} else {
		base_disk_sector = node->data;
		pr_warn("successful read from %llu, which is mapped to %llu\n",
			virt_disk_sector, base_disk_sector);
	}
	bio->bi_iter.bi_sector = base_disk_sector;

	return 0;
}

/*
 * changes bio write destination sector according to mapping stored in skiplist
 * or creates a new mapping.
 */
static int redirect_write(struct bio *bio)
{
	struct skiplist_node *node;
	sector_t virt_disk_sector;
	sector_t base_disk_sector;

	virt_disk_sector = bio->bi_iter.bi_sector;
	base_disk_sector = next_free_sector;
	pr_warn("write request: sector %llu, next free base sector is %llu\n",
		virt_disk_sector, next_free_sector);

	node = skiplist_add(virt_disk_sector, base_disk_sector, skiplist);
	if (IS_ERR(node)) {
		pr_err("failed to map %llu to %llu\n", virt_disk_sector, base_disk_sector);
		return PTR_ERR(node);
	}

	if (base_disk_sector != node->data) {
		base_disk_sector = node->data;
		pr_warn("successful write to %llu, which was already mapped to %llu\n",
			virt_disk_sector, base_disk_sector);
	} else {
		pr_warn("successful write to %llu, it is now mapped to %llu\n",
			virt_disk_sector, base_disk_sector);
		next_free_sector += get_bi_size_sectors(bio);
	}
	bio->bi_iter.bi_sector = base_disk_sector;

	pr_warn("next free base sector is %llu\n", next_free_sector);
	skiplist_print(skiplist);

	return 0;
}

/*
 * function to handle bio sector mapping according to skiplist
 */
static int map_bio_sector(struct bio *bio)
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

/*
 * function that handles bio redirect. creates a clone with mapped bio sector and
 * sends it to the base block device.
 */
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
	err = map_bio_sector(new_bio);
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

/*
 * closes opened block device and destroyes created virtual disk.
 */
static int close_base(const char *arg, const struct kernel_param *kp)
{
	if (!base_handle || !base_handle->bh) {
		pr_err("nothing to close\n");
		return -EINVAL;
	}
	if (!base_handle->assoc_disk) {
		pr_err("disk wasn't allocated, cannot close\n");
		return -EINVAL;
	}

	skiplist_free(skiplist);
	skiplist = NULL;
	next_free_sector = 0;

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

MODULE_PARM_DESC(blkm_base, "Base block device name");
module_param_cb(blkm_base, &base_ops, NULL, S_IRUGO | S_IWUSR);

MODULE_PARM_DESC(blkm_open, "Open base block device");
module_param_cb(blkm_open, &open_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(blkm_close, "Close base block device");
module_param_cb(blkm_close, &close_ops, NULL, S_IWUSR);

module_init(blkm_init);
module_exit(blkm_exit);

MODULE_AUTHOR("Daniel Vlasenco <vlasenko.daniil26@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bio-based block device driver for log-structured storage");