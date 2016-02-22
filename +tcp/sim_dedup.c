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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <net/sock.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <linux/in.h>

#include <net/ksocket.h>

#define DM_MSG_PREFIX "sim_dedup"

/*
 * Dedup
*/
struct sim_dedup_c {
	ksocket_t sock;
	struct sockaddr_in addr_srv;
	struct dm_dev *dev;
	sector_t start;
};

/*
 * Init kernel client socket
 */
static int sim_dedup_ksocket_init(struct dm_target *ti)
{
	struct sim_dedup_c *sdc = (struct sim_dedup_c *) ti->private;

	int addr_len;
	char * srv_ip_addr;

<<<<<<< HEAD
	struct sockaddr_in addr;

=======
>>>>>>> 3ad11c0565c5cfeec047165131f6815c0fb67db8
#ifdef KSOCKET_ADDR_SAFE
	mm_segment_t old_fs;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	sprintf(current->comm, "ksktcli");

	memset(&(sdc->addr_srv), 0, sizeof(struct sockaddr_in));
	sdc->addr_srv.sin_family = AF_INET;
	sdc->addr_srv.sin_port = htons(5555);
	sdc->addr_srv.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr_len = sizeof(struct sockaddr_in);

	sdc->sock = ksocket(AF_INET, SOCK_STREAM, 0);

	if(sdc->sock == NULL)
	{
		printk("socket func failed\n");
		return -1;
	}
	if(kconnect(sdc->sock, (struct sockaddr*)&(sdc->addr_srv), addr_len) < 0)
	{
		printk("connect func failed\n");
		return -1;
	}

	/*debug*/
	srv_ip_addr = inet_ntoa(&(sdc->addr_srv));
	printk("connected to : %s:%d\n", srv_ip_addr, ntohs(sdc->addr_srv.sin_port));
	kfree(srv_ip_addr);
	////////////////////////////////////////////
	return 0;
}

static void sim_dedup_ksocket_free(struct dm_target *ti){
	struct sim_dedup_c *sdc = (struct sim_dedup_c *) ti->private;
	char buf[1024] = "End of Dedup";

	ksend(sdc->sock, buf, sizeof(buf), 0);
	kclose(sdc->sock);
#ifdef KSOCKET_ADDR_SAFE
	set_fs(old_fs);
#endif
}

/*
 * Construct a sim_dedup mapping: <dev_path> <offset>
 */
static int sim_dedup_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct sim_dedup_c *sdc;
	unsigned long long tmp;

	int s;
<<<<<<< HEAD
=======
	char buf[1024];
>>>>>>> 3ad11c0565c5cfeec047165131f6815c0fb67db8

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

	s =	sim_dedup_ksocket_init(ti);

<<<<<<< HEAD
=======
	strcpy(buf, "sim_dedup_ctr is called\n");
	ksend(sdc->sock, buf, sizeof(buf), 0);
//	krecv(sdc->sock, buf, sizeof(buf), 0);
//	printk("User MSG : %s\n", buf);
	
>>>>>>> 3ad11c0565c5cfeec047165131f6815c0fb67db8
	if(s < 0){
		ti->error = "ksocket error";
		return -EINVAL;
	}
	return 0;

bad:
	kfree(sdc);
	return -EINVAL;
}

static void sim_dedup_dtr(struct dm_target *ti)
{
	struct sim_dedup_c *sdc = (struct sim_dedup_c *) ti->private;

	sim_dedup_ksocket_free(ti);
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
<<<<<<< HEAD
	char buf[1024];

	strcpy(buf, "sim_dedup_map_bio is called\n");
	ksend(sdc->sock, buf, sizeof(buf), 0);
	krecv(sdc->sock, buf, sizeof(buf), 0);
	printk("User MSG : %s\n", buf);
=======

>>>>>>> 3ad11c0565c5cfeec047165131f6815c0fb67db8

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

	if (r < 0)
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
