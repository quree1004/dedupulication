
#include <linux/vmalloc.h>

#include "dm-ssddup-target.h"
#include "dm-ssddup-meta.h"
#include "dm-ssddup-hash.h"

#define MAX_DEV_NAME_LEN (64)
#define MAX_BACKEND_NAME_LEN (64)

#define MIN_DATA_DEV_BLOCK_SIZE (4 * 1024)
#define MAX_DATA_DEV_BLOCK_SIZE (1024 * 1024)

struct on_disk_stats {
	uint64_t physical_block_counter;
	uint64_t logical_block_counter;
};

struct ssddup_work {
	struct work_struct worker;
	struct ssddup_c *sc;
	struct bio *bio;
};

void handle_read(struct ssddup_c *sc, struct bio *bio);
void handle_write(struct ssddup_c *sc, struct bio *bio);


static void process_bio(struct work_struct *ws)
{
	struct ssddup_work *data = container_of(ws, struct ssddup_work, worker);
	struct ssddup_c *sc = (struct ssddup_c *)data->sc;
	struct bio *bio = (struct bio *)data->bio;

	mempool_free(data, sc->ssddup_mempool);

	switch (bio_data_dir(bio)){
	case READ :
		handle_read(sc, bio);
	case WRITE :
		handle_write(sc, bio);
	default:
		DMERR("NO READ NO WRITE");
		bio_endio(bio, -EINVAL);
	}
}

static int dm_ssddup_map(struct dm_target *ti, struct bio *bio)
{
//	struct ssddup_work *data;
//	
//	data = mempool_alloc(sc->ssddup_mempool, GFP_NOIO);
//	if (!data) {
//		bio_endio(bio, -ENOMEM);
//		goto out;
//	}
//
//	data->bio = bio;
//	data->sc = sc;
//
//	INIT_WORK(&(data->worker), process_bio);
//
//	queue_work(sc->workqueue, &(data->worker));
//
//out :
//	return DM_MAPIO_SUBMITTED;
	bio_endio(bio, 0);
	return DM_MAPIO_SUBMITTED;
}

struct ssddup_args{
	struct dm_target *ti;
	struct dm_dev *meta_dev;
	struct dm_dev *data_dev;

	char hash_algo[CRYPTO_ALG_NAME_LEN];
};

static void destroy_ssddup_args(struct ssddup_args *sa)
{
	if (sa->meta_dev)
		dm_put_device(sa->ti, sa->meta_dev);
	if (sa->data_dev)
		dm_put_device(sa->ti, sa->data_dev);
}

static int parse_ssddup_args(struct ssddup_args *sa, int argc, char **argv, char **err)
{
	struct dm_arg_set as;
	int r;

	if (argc != 3){
		*err = "args are not 3";
		return -EINVAL;
	}

	as.argc = argc;
	as.argv = argv;

	r = dm_get_device(sa->ti, dm_shift_arg(&as),
		dm_table_get_mode(sa->ti->table), &sa->meta_dev);
	if (r){
		*err = "ERR Open meta dev";
		return r;
	}

	r = dm_get_device(sa->ti, dm_shift_arg(&as),
		dm_table_get_mode(sa->ti->table), &sa->data_dev);
	if (r){
		*err = "ERR Open data dev";
		return r;
	}

	strlcpy(sa->hash_algo, dm_shift_arg(&as), CRYPTO_ALG_NAME_LEN);
	if (!crypto_has_hash(sa->hash_algo, 0, CRYPTO_ALG_ASYNC)){
		*err = "ERR Unsupported hash algorithm";
		return -EINVAL;
	}
	return 0;
}

static int dm_ssddup_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct ssddup_args sa;
	struct ssddup_c *sc;
	struct workqueue_struct *wq;

	struct init_btree_param iparam;
	struct metadata *md = NULL;

	sector_t data_size;
	int r;
	int crypto_key_size;

	struct on_disk_stats *data = NULL;
	uint64_t logical_block_counter = 0;
	uint64_t physical_block_counter = 0;

	uint32_t flushrq = 100;
	mempool_t *ssddup_mempool = NULL;

	int unformatted;

	memset(&sa, 0, sizeof(struct ssddup_args));
	sa.ti = ti;

	r = parse_ssddup_args(&sa, argc, argv, &ti->error);
	if (r)
		goto out;
	
	sc = kzalloc(sizeof(*sc), GFP_KERNEL);
	if (!sc){
		ti->error = "ERR kzalloc for ssddup_c";
		r = -ENOMEM;
		goto out;
	}

	wq = create_singlethread_workqueue("dm-ssddup");
	if (!wq){
		ti->error = "ERR create work queue";
		r = -ENOMEM;
		goto bad_wq;
	}

	ssddup_mempool = mempool_create_kmalloc_pool(MIN_DEDUP_WORK_IO,
		sizeof(struct ssddup_work));
	if (!ssddup_mempool){
		r = -ENOMEM;
		ti->error = "ERR create mempool";
		goto bad_mempool;
	}

	sc->io_client = dm_io_client_create();
	if (IS_ERR(sc->io_client)) {
		r = PTR_ERR(sc->io_client);
		ti->error = "ERR create dm_io_client";
		goto bad_io_client;
	}

	sc->blk_size = 4096;
	sc->sector_num_per_blk = to_sector(sc->blk_size);
	data_size = ti->len;
	(void)sector_div(data_size, sc->sector_num_per_blk);
	sc->logical_blks = data_size;

	data_size = to_sector(i_size_read(sa.data_dev->bdev->bd_inode));
	(void)sector_div(data_size, sc->sector_num_per_blk);
	sc->physical_blks = data_size;

	sc->mdops = &metadata_ops_btree;
	iparam.meta_bdev = sa.meta_dev->bdev;
	iparam.blocks = sc->physical_blks;

	md = sc->mdops->init_meta((void *)&iparam, &unformatted);
	if (IS_ERR(md)){
		ti->error = "ERR init metadata";
		r = PTR_ERR(md);
		goto bad_meta_init;
	}

	sc->desc_table = desc_table_init(sa.hash_algo);
	if (!sc->desc_table || IS_ERR(sc->desc_table)) {
		ti->error = "ERR init crypto API";
		r = PTR_ERR(sc->desc_table);
		goto bad_meta_init;
	}

	crypto_key_size = get_hash_digestsize(sc->desc_table);

	sc->hash_pbn_store = sc->mdops->bs_create_hash_pbn(md, crypto_key_size,
		sizeof(uint64_t), sc->physical_blks, unformatted); // pbn nums are 64bit
	if (IS_ERR(sc->hash_pbn_store)) {
		r = PTR_ERR(sc->hash_pbn_store);
		ti->error = "ERR create hash_pbn_store";
		goto bad_metastore_init;
	}

	sc->lbn_pbn_store = sc->mdops->bs_create_lbn_pbn(md, 8,
			sizeof(uint64_t), sc->logical_blks, unformatted);
	if (IS_ERR(sc->lbn_pbn_store)) {
		ti->error = "ERR create lbn_pbn_store";
		r = PTR_ERR(sc->lbn_pbn_store);
		goto bad_metastore_init;
	}

	r = sc->mdops->flush_meta(md);
	if (r < 0)
		BUG();

	if (!unformatted && sc->mdops->get_private_data) {
		r = sc->mdops->get_private_data(md, (void **)&data, sizeof(struct on_disk_stats));
		if (r < 0)
			BUG();

		logical_block_counter = data->logical_block_counter;
		physical_block_counter = data->physical_block_counter;
	}

	sc->data_dev = sa.data_dev;
	sc->meta_dev = sa.meta_dev;

	sc->workqueue = wq;
	sc->ssddup_mempool = ssddup_mempool;
	sc->md = md;

	sc->logical_blk_counter = logical_block_counter;
	sc->physical_blk_counter = physical_block_counter;

	strcpy(sc->crypto_alg, sa.hash_algo);
	sc->crypto_key_size = crypto_key_size;

	sc->flush_limit = flushrq;
	sc->writes_cnt_for_flush = 0;

	r = dm_set_target_max_io_len(ti, sc->sector_num_per_blk);
	if (r)
		goto bad_metastore_init;

	ti->private = sc;

	DMINFO("dm_ssddup_ctr is normally ended");

	return 0;

bad_metastore_init:
	desc_table_deinit(sc->desc_table);
bad_meta_init:
	if (md && !IS_ERR(md))
		sc->mdops->exit_meta(md);
	dm_io_client_destroy(sc->io_client);
bad_io_client :
	mempool_destroy(ssddup_mempool);
bad_mempool:
	destroy_workqueue(wq);
bad_wq :
	kfree(sc);
out : 
	destroy_ssddup_args(&sa);
	return r;
}

static void dm_ssddup_dtr(struct dm_target *ti)
{
	struct ssddup_c *sc = ti->private;
	struct on_disk_stats data;
	int r;

	if (sc->mdops->set_private_data){
		data.physical_block_counter = sc->physical_blk_counter;
		data.logical_block_counter = sc->logical_blk_counter;

		r = sc->mdops->set_private_data(sc->md, &data,
			sizeof(struct on_disk_stats));
		if (r < 0)
			BUG();
	}

	r = sc->mdops->flush_meta(sc->md);
	if (r < 0)
		BUG();

	flush_workqueue(sc->workqueue);
	destroy_workqueue(sc->workqueue);

	mempool_destroy(sc->ssddup_mempool);

	sc->mdops->exit_meta(sc->md);

	dm_io_client_destroy(sc->io_client);

	dm_put_device(ti, sc->data_dev);
	dm_put_device(ti, sc->meta_dev);
	desc_table_deinit(sc->desc_table);

	kfree(sc);
}

static struct target_type dm_ssddup_target = {
	.name = "ssddup",
	.version = {1, 0, 0},
	.module = THIS_MODULE,
	.ctr = dm_ssddup_ctr,
	.dtr = dm_ssddup_dtr,
	.map = dm_ssddup_map,
};

int __init dm_ssddup_init(void)
{
	int r;

	r = dm_register_target(&dm_ssddup_target);
	if (r)
		return r;

	return 0;
}

void __exit dm_ssddup_exit(void)
{
	dm_unregister_target(&dm_ssddup_target);
}

module_init(dm_ssddup_init);
module_exit(dm_ssddup_exit);

MODULE_LICENSE("GPL");
