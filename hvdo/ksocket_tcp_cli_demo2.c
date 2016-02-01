/* 
 * ksocket project test sample - tcp client
 * BSD-style socket APIs for kernel 2.6 developers
 * 
 * @2007-2008, China
 * @song.xian-guang@hotmail.com (MSN Accounts)
 * 
 * This code is licenced under the GPL
 * Feel free to contact me if any questions
 * 
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <net/sock.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <linux/in.h>

#include <net/ksocket.h>

int tcp_cli(void *arg)
{
	ksocket_t sockfd_cli;
	struct sockaddr_in addr_srv;
	char buf[1024], *tmp;
	int addr_len;

#ifdef KSOCKET_ADDR_SAFE
	mm_segment_t old_fs;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	sprintf(current->comm, "ksktcli");	

	memset(&addr_srv, 0, sizeof(addr_srv));
	addr_srv.sin_family = AF_INET;
	addr_srv.sin_port = htons(5555);
	addr_srv.sin_addr.s_addr = inet_addr("127.0.0.1");;
	addr_len = sizeof(struct sockaddr_in);
	
	sockfd_cli = ksocket(AF_INET, SOCK_STREAM, 0);
	printk("sockfd_cli = 0x%p\n", sockfd_cli);
	if (sockfd_cli == NULL)
	{
		printk("socket failed\n");
		return -1;
	}
	if (kconnect(sockfd_cli, (struct sockaddr*)&addr_srv, addr_len) < 0)
	{
		printk("connect failed\n");
		return -1;
	}

	tmp = inet_ntoa(&addr_srv.sin_addr);
	printk("connected to : %s %d\n", tmp, ntohs(addr_srv.sin_port));
	kfree(tmp);
	
	krecv(sockfd_cli, buf, 1024, 0);
	printk("got message : %s\n", buf);

	kclose(sockfd_cli);
#ifdef KSOCKET_ADDR_SAFE
		set_fs(old_fs);
#endif
	
	return 0;
}


static int dm_dedup_ctr_fn(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct dedup_args *da;
	struct dedup_config *dc;
	struct workqueue_struct *wq;
	struct metadata *md = NULL;
	uint64_t data_size;
	int r;
	int crypto_key_size;
	struct on_disk_stats *data = NULL;
	uint64_t logical_block_counter = 0;
	uint64_t physical_block_counter = 0;

	if (argc != 2) {
		ti->error = "Invalid argument count";
		 return -EINVAL;
	}


	da = kzalloc(sizeof(*da), GFP_KERNEL);
	if (!da) {
		ti->error = "Error allocating memory for dedup arguments";
		return -ENOMEM;
	}
	da->ti = ti;

	dc = kmalloc(sizeof(*dc), GFP_NOIO);
	if (!dc) {
		ti->error = "Error allocating memory for dedup config";
		r = -ENOMEM; 
		goto out;
	}
	wq = create_singlethread_workqueue("dm-dedup");
	if (!wq) {
		ti->error = "failed to create workqueue";
		r = -ENOMEM;
		goto bad_wq;
	}
	dedup_work_pool = mempool_create_kmalloc_pool(MIN_DEDUP_WORK_IO,
		sizeof(struct dedup_work));
	if (!dedup_work_pool) {
		r = -ENOMEM;
		ti->error = "failed to create mempool";
		goto bad_mempool;
	}
	dc->io_client = dm_io_client_create();
	
	dc->block_size = da->block_size;
	dc->sectors_per_block = to_sector(da->block_size);
	dc->lblocks = ti->len / dc->sectors_per_block;
	data_size = i_size_read(da->data_dev->bdev->bd_inode);
	dc->pblocks = data_size / da->block_size;
	/* Meta-data backend specific part */


	dc->kvs_lbn_pbn = dc->mdops->kvs_create_linear(md, 8,
		sizeof(struct lbn_pbn_value), dc->lblocks, unformatted);
	
		logical_block_counter = data->logical_block_counter;
		physical_block_counter = data->physical_block_counter;
	}
	dc->data_dev = da->data_dev;
	dc->metadata_dev = da->meta_dev;
	dc->workqueue = wq;
	dc->dedup_work_pool = dedup_work_pool;
	dc->bmd = md;
	dc->logical_block_counter = logical_block_counter;
	dc->physical_block_counter = physical_block_counter;
	dc->writes = 0;
	dc->dupwrites = 0;

	strcpy(dc->crypto_alg, da->hash_algo);
	dc->crypto_key_size = crypto_key_size;
	dc->flushrq = flushrq;
	dc->writes_after_flush = 0;
	dm_set_target_max_io_len(ti, dc->sectors_per_block);
	ti->private = dc;
	da->meta_dev = da->data_dev = NULL;
	r = 0;
	goto out;

out:
	destroy_dedup_args(da);
	return r;
}
static void dm_dedup_dtr_fn(struct dm_target *ti)
{
	struct dedup_config *dc = ti->private;
	struct on_disk_stats data;
	int ret;
	if (dc->mdops->set_private_data) {
		data.physical_block_counter = dc->physical_block_counter;
		data.logical_block_counter = dc->logical_block_counter;
		ret = dc->mdops->set_private_data(dc->bmd, &data,
			sizeof(struct on_disk_stats));
		if (ret < 0)
			BUG();
	}
	ret = dc->mdops->flush_meta(dc->bmd);
	if (ret < 0)
		BUG();

	destroy_workqueue(dc->workqueue);
	dm_put_device(ti, dc->data_dev);
	dm_put_device(ti, dc->metadata_dev);
	desc_table_deinit(dc->desc_table);
	kfree(dc);
}


static struct target_type dm_dedup_target = {
	.name = "dedup",
	.module = THIS_MODULE,
	.ctr = dm_dedup_ctr_fn,
	.dtr = dm_dedup_dtr_fn,
	.map = dm_dedup_map_fn,
	.end_io = dm_dedup_endio_fn,
};


static int ksocket_tcp_cli_init(void)
{
	kernel_thread(tcp_cli, NULL, 0);
	
	printk("ksocket tcp cli init ok\n");

	int r;
	r = dm_register_target(&dm_dedup_target);
	if (r < 0)
		DMERR("target registration failed: %d.", r);
	else
		DMINFO("target registered succesfully.");
	return r;

}

static void ksocket_tcp_cli_exit(void)
{
	printk("ksocket tcp cli exit\n");
	dm_unregister_target(&dm_dedup_target);


}

module_init(ksocket_tcp_cli_init);
module_exit(ksocket_tcp_cli_exit);

MODULE_LICENSE("Dual BSD/GPL");
