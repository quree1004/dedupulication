/*
 * Copyright (C) 2001-2003 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "/usr/src/kernels/linux-2.6.32.27/drivers/md/dm.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "sim_dedup"

/*
 * Dedup
*/

struct sim_dudup_c {
	struct dm_dev *dev;
	sector_t start;
};

/*
 * Construct a sim_dedup mapping: <dev_path> <offset>
 */
static int sim_dedup_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct sim_dedup_c *sdc;
	unsigned long long tmp;

	if (argc != 2) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	sdc = kmalloc(sizeof(*sdc), GFP_KERNEL);
	if (sdc == NULL) {
		ti->error = "dm-sim_dedup: Cannot allocate sim_dedup context";
		return -ENOMEM;
	}

	if (sscanf(argv[1], "%llu", &tmp) != 1) {
		ti->error = "dm-sim_dedup: Invalid device sector";
		goto bad;
	}
	sdc->start = tmp;

	if (dm_get_device(ti, argv[0], sdc->start, ti->len,
			  dm_table_get_mode(ti->table), &sdc->dev)) {
		ti->error = "dm-sim_dedup: Device lookup failed";
		goto bad;
	}

	ti->num_flush_requests = 1;
	ti->private = sdc;

	return 0;

      bad:
	kfree(sdc);
	return -EINVAL;
}

static void sim_dedup_dtr(struct dm_target *ti)
{
	struct sim_dedup_c *sdc = (struct sim_dedup_c *) ti->private;

	dm_put_device(ti, sdc->dev);
	kfree(sdc);
}

static sector_t sim_dedup_map_sector(struct dm_target *ti, sector_t bi_sector)
{
	struct sim_dedup_c *sdc = ti->private;

	return sdc->start + (bi_sector - ti->begin);
}

static void sim_dedup_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct sim_dedup_c *sdc = ti->private;
	char buf[1024];

	strcpy(buf, "sim_dedup_map_bio is called\n");
	ksend(sdc->sock, buf, sizeof(buf), 0);
	krecv(sdc->sock, buf, sizeof(buf), 0);
	printk("User MSG : %s\n", buf);

	bio->bi_bdev = sdc->dev->bdev;
	if (bio_sectors(bio))
		bio->bi_sector = sim_dedup_map_sector(ti, bio->bi_sector);
}

static int sim_dedup_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	sim_dedup_map_bio(ti, bio);

	return DM_MAPIO_REMAPPED;
}

static int sim_dedup_status(struct dm_target *ti, status_type_t type,
			 char *result, unsigned int maxlen)
{
	struct sim_dedup_c *sdc = (struct sim_dedup_c *) ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, "%s %llu", sdc->dev->name,
				(unsigned long long)sdc->start);
		break;
	}
	return 0;
}

static int sim_dedup_ioctl(struct dm_target *ti, unsigned int cmd,
			unsigned long arg)
{
	struct sim_dedup_c *sdc = (struct sim_dedup_c *) ti->private;
	return __blkdev_driver_ioctl(sdc->dev->bdev, sdc->dev->mode, cmd, arg);
}

static int sim_dedup_merge(struct dm_target *ti, struct bvec_merge_data *bvm,
			struct bio_vec *biovec, int max_size)
{
	struct sim_dedup_c *sdc = ti->private;
	struct request_queue *q = bdev_get_queue(sdc->dev->bdev);

	if (!q->merge_bvec_fn)
		return max_size;

	bvm->bi_bdev = sdc->dev->bdev;
	bvm->bi_sector = sim_dedup_map_sector(ti, bvm->bi_sector);

	return min(max_size, q->merge_bvec_fn(q, bvm, biovec));
}

static int sim_dedup_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct sim_dedup_c *sdc = ti->private;

	return fn(ti, sdc->dev, sdc->start, ti->len, data);
}

static struct target_type sim_dedup_target = {
	.name   = "sim_dedup",
	.version = {0, 0, 1},
	.module = THIS_MODULE,
	.ctr    = sim_dedup_ctr,
	.dtr    = sim_dedup_dtr,
	.map    = sim_dedup_map,
	.status = sim_dedup_status,
	.ioctl  = sim_dedup_ioctl,
	.merge  = sim_dedup_merge,
	.iterate_devices = sim_dedup_iterate_devices,
};

int __init sim_dedup_init(void)
{
	int r = dm_register_target(&sim_dedup_target);

	if (r < 0 || s < 0)
		DMERR("register failed %d", r);
	else
		printk("register success\n");

	return r;
}

void sim_dedup_exit(void)
{
	dm_unregister_target(&sim_dedup_target);
}

module_init(sim_dedup_init);
module_exit(sim_dedup_exit);
MODULE_LICENSE("GPL");
