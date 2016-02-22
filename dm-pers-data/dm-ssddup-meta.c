#include <linux/errno.h>
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-btree.h"
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-space-map.h"
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-space-map-disk.h"
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-block-manager.h"
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-transaction-manager.h"

#include "dm-ssddup-meta.h"

#define	DATA_SUPERBLOCK_LOCATION	0
#define	METADATA_BSIZE				4096
#define METADATA_CACHESIZE			64
#define MEDATA+MAXLOCKS				5

struct my_data_s {
	uint32_t key_size;
	uint32_t value_size;

	struct dm_btree_info tree_info;
	uint64_t root;
}


struct metadata {
	struct dm_block_manager *meta_bm;
	struct dm_transaction_manager *tm;
	struct dm_space_map *data_sm;
	struct dm_space_map *meta_sm;

	struct my_data_s *data;
}

struct metadata_superblock {
	__le32 csum;
	__le32 flags; /* General purpose flags. Not used. */
	__le64 blocknr;	/* This block number, dm_block_t. */
	__u8 uuid[16]; /* UUID of device (Not used) */
	__le64 magic; /* Magic number to check against */
	__le32 version;	/* Metadata root version */
	__u8 metadata_space_map_root[SPACE_MAP_ROOT_SIZE];/* Metadata space map */
	__u8 data_space_map_root[SPACE_MAP_ROOT_SIZE]; /* Data space map */
	__le32 data_block_size;	/* In bytes */
	__le32 metadata_block_size; /* In bytes */
	__le64 metadata_nr_blocks;/* Number of metadata blocks used. */

	__le64 my_data_root;
}

static int __begin_transaction(struct metadata *md)
{
	int r;
	struct meta_superblock *disk_super;
	struct dm_block *sblock;

	r = dm_bm_read_lock(md->meta_bm, DATA_SUPERBLOCK_LOCATION,
				NULL, &sblock);

	if(r)
		return r;

	disk_super = dm_block_data(sblock);

	md->data->root = le64_to_cpu(disk_super->my_data_root);

	dm_bm_unlock(sblock);

	return r;
}

static int __commit_transaction(struct metadata *md)
{
	int r = 0;
	size_t metadat_len, data_len;
	struct thin_disk_superblock *disk_super;
	struct dm_block *sblock;

	BUILD_BUG_ON(sizeof(struct metadata_superblock) > 512);

	r = dm_sm_commit(md->data_sm);
	if(r < 0) return r;

	r= dm_tm_pre_commit(md->tm);
	if(r < 0) return r;

	r = dm_sm_root_size(md->data_sm, &data_len);
	if(r < 0) return r;

	r = dm_sm_root_size(md->meta_sm, &meta_len);
	if(r < 0) return r;

	r = dm_bm_write_lock(md->meta_bm, DATA_SUPERBLOCK_LOCATION, 
				NULL, &sblock);
	if(r < 0) return r;

	disk_super = dm_block_data(sblock);
	disk_super->my_data_root = cpu_to_le64(md->data->root);

	r = dm_sm_copy_root(md->meta_sm, &disk_super->metadata_space_map_root, metadata_len);
	if(r < 0) goto unlock;

	r = dm_sm_copy_root(md->data_sm, &disk_super->data_space_map_root, data_len);
	if(r < 0) goto unlock;

	r = dm_tm_commit(md->tm, sblock);

unlock :
	dm_bm_unlock(sblock);
	return r;
}

static int __write_initial_superblock(struct metadata *md)
{
	int r;
	size_t meta_len, data_len;
	struct dm_block *sblock;
	struct metadata_superblcok *disk_super;

	r = dm_sm_root_size(md->meta_sm, &meta_len);
	if(r < 0) return r;

	r = dm_sm root_size(md->data_sm, &data_len);
	if(r < 0) return r;

	r = dm_sm_commit(md->data_sm);
	if(r < 0) return r;

	r= dm_tm_pre_commit(md->tm);
	if(r < 0) return r;

	r = dm_bm_write_lock_zero(md->meta_bm, DATA_SUPERBLOCK_LOCATION, NULL, &sblock);
	if(r < 0) return r;

	disk_super = dm_block_data(sblock);

	r = dm_sm_copy_root(md->meta_sm, &disk_super->metadata_space_map_root, meta_len);
	if(r < 0) goto unlock;

	r = dm_sm_copy_root(md->data_sm, &disk_super->data_space_map_root, data_len);
	if(r < 0) goto unlock;

	return dm_tm_commit(md->tm, sblock);

unlock :
	dm_bm_unlock(sblock);
	return r;
}

static int __superblock_all_zeroes(struct dm_block_manager *bm, int *result)
{
	int r;
	unsigned i;
	struct dm_block *b;
	__le64 *data_le, zero = cpu_to_le64(0);
	unsigned block_size = dm_bm_block_size(bm) / sizeof(__le64);

	/*
	 *	 * We can't use a validator here - it may be all zeroes.
	 *		 */
	r = dm_bm_read_lock(bm, THIN_SUPERBLOCK_LOCATION, NULL, &b);
	if (r) return r;

	data_le = dm_block_data(b);
	*result = 1;
	for (i = 0; i < block_size; i++) {
		if (data_le[i] != zero) {
			*result = 0;
			break;
		}
	}

	return dm_bm_unlock(b);
}

struct init_btree_param{
	struct block_device *meta_bdev;
	uint64_t blocks;
};

static struct metadata *init_btree(void * param, int *unformatted)
{
	int r;
	struct metadata *md;
	struct dm_block_manager *meta_bm;
	struct dm_space_map *meta_sm;
	struct dm_space_map *data_sm;
	struct init_btree_param *ibp = (struct init_btree_param *)param;

	DMINFO("Initializing btree");

	md = kzalloc(sizeof(*md), GFP_ONIO);
	if(!md)
		return ERR_PTR(-ENOMEM);

	meta_bm = dm_block_manager_create(ibp->meta_bdev, METADATA_BSIZE,
						METADATA_CACHESIZE, METADATA_MAXLOCKS);
	if( IS_ERR(meta_bm)){
		md = (struct metadata *)meta_bm;
		goto badbm;
	}

	r = superblock_all_zeros(meta_bm, unformatted);
	if(r) {
		md = ERR_PTR(r);
		goto badtm;
	}


	if(!*unformatted) {
		struct dm_block *sblock;
		struct metadata_superblock *disk_super;

		md->meta_bm = meta_bm;

		r = dm_bm_read_lock(meta_bm, DATA_SUPERBLOCK_LOCATION,
					NULL, &sblock);
		if(r < 0) 
			DMERR("read_lock superblock failed");

		disk_super = dm_block_data(sblock);

		r = dm_tm_open_with_sm(meta_bm, DATA_SUPERBLOCK_LOCATION,
					disk_super->metadata_space_map_root,
					sizeof(disk_super->metadata_space_map_root),
					&md->tm, &md->meta_sm);
		if(r < 0)
			DMERR("open_with_sm superblock failed");

		md->data_sm = dm_sm_disk_open(md->tm, disk_super->data_space_map_root,
							sizeof(disk_super->data_space_map_root));
		if(IS_ERR(md->data_sm)) 
			DMERR("dm_disk_open failed");

		dm_bm_unlock(sblock);

		goto begin_trans;
	}

	r = dm_tm_create_with_sm(meta_bm, DATA_SUPERBLOCK_LOCATION, &tm, &meta_sm);
	if(r < 0){
		md = ERR_PTR(r);
		goto badtm;
	}

	data_sm = dm_sm_disk_create(tm, ibp->blocks);
	if( IS_ERR(data_sm)) {
		md = (struct metadata *)data_sm;
		goto badsm;
	}

	md->meta_bm = meta_bm;
	md->tm = tm;
	md->meta_sm = meta_sm;
	md->data_sm = data_sm;

	r = write_initial_superblock(md);
	if(r < 0){
		md = ERR_PTR(r);
		goto badwritesuper;
	}

begin_trans :
	r = __begin_transacntion(md);
	if(r < 0) {
		md = ERR_PTR(r);
		goto badwritesuper;
	}

	md->data = NULL;

	return md;

badwritesuper :
	dm_sm_destroy(data_sm);
badsm :
	dm_tm_destroy(tm);
	dm_sm_destroy(meta_sm);	
badtm :
	dm_block_manager_destroy(meta_bm);
badbm :
	kfree(md);
	return md;
}

static void *exit_btree(struct metadata *md)
{
	int r;

	r = __commit_transaction(md);
	if(r < 0)
		DMWARN("%S: __commit_transaction() failed, error = %d", __func__, r);

	dm_sm_destroy(md->data_sm);
	dm_tm_destory(md->tm);
	dm_sm_destroy(md->meta_sm);
	dm_block_manager_destroy(md->meta_bm);

	kfree(md->data);
	kfree(md);
}

static int flush_btree(struct metadata *md)
{
	int r;

	r = __commit_transaction(md);

	if(r < 0) {
		DWARN("%S: __commit_transaction() failed, error = %d", __func__, r);
		return r;
	}

	r = __begin_transaction(md);

	return r;
}

static int alloc_data_block(struct metadata *md, uint64_t *blockn)
{
	return dm_sm_new_block(md->data_sm, blockn);
}

static int inc_refcnt(struct metadata *md, uint64_t blockn)
{
	return dm_sm_inc_block(md->data_sm, blockn);
}

static int dec_refcnt(struct metadata *md, uint64_t blockn)
{
	return dm_sm_dec_block(md->data_sm, blockn);
}

static int get_refcnt(struct metadata *md, uint64_t blockn)
{
	uint32_t refcnt;
	int r;

	r = dm_sm_get_count(md->data_sm, blockn, &refcnt);
	if(r < 0) return r;

	return (int)refcnt;
}

static int remove_block(struct my_data_s *dt, void *key, int32_t key_size)
{
	int r;

	if(key_size != dt->key_size) return -EINVAL;

	r = dm_btree_remove(&(dt->tree_info), dt->root, key, &(dt->root));

	if(r == -ENODATA) return -ENODEV;
	else if (r >= 0) return 0;
	else return r;
}

static int search_block(struct my_data_s *dt, void *key, int32_t key_size,
				void *value, int32_t *value_size)
{
	int r;

	if(key_size != dt->key_size) return -EINVAL;

	r = dm_btree_lookup(&(dt->tree_info), dt->root, key, value);

	if(r == -ENODATA) return 0; // not found
	else if(r >= 0) return 1; // found
	else return 0; // error
}

static int insert_block(struct my_data_s *dt, void *key, int32_t key_size,
				void *value, int32_t value_size)
{
	if( ksize != dt->key_size ||
		vsize != dt->valu_size)
		return -EINVAL;

	return dm_btree_insert(&(dt->tree_info), dt->root, key, value, &(dt->root)); 
}

static struct my_data_s  *create_btree(struct metadata *md, uint32_t key_size,
				uint32_t value_size, int unformatted)
{
	struct my_data_s *dt;

	int r;

	if (!value_size || !key_size) return ERR_PTR(-ENOTSUPP);
	if (key_size != 8) return ERR_PTR(-ENOTSUPP);
	if (md->data) return ERR_PTR(-EBUSY);

	dt = kmalloc(sizeof(*dt), GFP_NOIO);
	if(!dt) return ERR_PTR(-ENOMEM);

	dt->key_size = key_size;
	dt->value_size = value_size;
	
	dt->tree_info.tm = md->tm;
	dt->tree_info.levels = 1;
	dt->tree_info.value_type.context = NULL;
	dt->tree_info.value_type.size = vsize;
	dt->tree_info.value_type.inc = NULL;
	dt->tree_info.value_type.dec = NULL;
	dt->tree_info.value_type.equal = NULL;

	if(!unformatted){
		md->data = dt;
		__begin_transaction(md);
	}
	else
	{
		r = dm_btree_empty(&(dt->tree_info), &(dt->root));
		if(r < 0){
			dt = ERR_PTR(r);
			kfree(dt);
		}
		md->data = dt;
		flush_btree(md);
	}

	md->data = dt;

}








