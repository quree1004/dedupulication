#include <linux/errno.h>
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-btree.h"
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-space-map.h"
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-space-map-disk.h"
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-block-manager.h"
#include "/usr/src/kernels/linux-3.18.26/drivers/md/persistent-data/dm-transaction-manager.h"

#include "dm-ssddup-meta.h"

#define	MEATDATA_SUPERBLOCK_LOCATION	0
#define	METADATA_BSIZE				4096
#define METADATA_CACHESIZE			64
#define METADATA_MAXLOCKS			5

struct btree_store {
	uint32_t key_size;
	uint32_t value_size;
	uint32_t entry_size;

	struct dm_btree_info tree_info;
	uint64_t root;

	struct btree_ops bops;
};

static int __begin_transaction(struct metadata *md)
{
	int r;
	struct metadata_superblock *disk_super;
	struct dm_block *sblock;

	r = dm_bm_read_lock(md->meta_bm, METADATA_SUPERBLOCK_LOCATION,
				NULL, &sblock);

	if(r)
		return r;

	disk_super = dm_block_data(sblock);

	if (md->bs_hash_pbn)
		md->bs_hash_pbn->root = le64_to_cpu(disk_super->hash_pbn_root);
	if (md->bs_lbn_pbn)
		md->bs_lbn_pbn->root = le64_to_cpu(disk_super->lbn_pbn_root);

	dm_bm_unlock(sblock);

	return r;
}

static int __commit_transaction(struct metadata *md)
{
	int r = 0;
	size_t meta_len, data_len;
	struct metadata_superblock *disk_super;
	struct dm_block *sblock;

	BUILD_BUG_ON(sizeof(struct metadata_superblock) > 512);

	r = dm_sm_commit(md->data_sm);
	if(r < 0) 
		return r;

	r= dm_tm_pre_commit(md->tm);
	if(r < 0) 
		return r;

	r = dm_sm_root_size(md->meta_sm, &meta_len);
	if(r < 0) 
		return r;

	r = dm_sm_root_size(md->data_sm, &data_len);
	if(r < 0) 
		return r;

	r = dm_bm_write_lock(md->meta_bm, METADATA_SUPERBLOCK_LOCATION, 
				NULL, &sblock);
	if(r) 
		return r;

	disk_super = dm_block_data(sblock);
	if( md->bs_lbn_pbn)
		disk_super->lbn_pbn_root = cpu_to_le64(md->bs_lbn_pbn->root);
	if( md->bs_hash_pbn)
		disk_super->hash_pbn_root = cpu_to_le64(md->bs_hash_pbn->root);

	r = dm_sm_copy_root(md->meta_sm, &disk_super->metadata_space_map_root, meta_len);
	if(r < 0) 
		goto unlock;

	r = dm_sm_copy_root(md->data_sm, &disk_super->data_space_map_root, data_len);
	if(r < 0) 
		goto unlock;

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
	struct metadata_superblock *disk_super;

	r = dm_sm_root_size(md->meta_sm, &meta_len);
	if(r < 0) 
		return r;

	r = dm_sm_root_size(md->data_sm, &data_len);
	if(r < 0) 
		return r;

	r = dm_sm_commit(md->data_sm);
	if(r < 0) 
		return r;

	r= dm_tm_pre_commit(md->tm);
	if(r < 0) 
		return r;

	r = dm_bm_write_lock_zero(md->meta_bm, METADATA_SUPERBLOCK_LOCATION, NULL, &sblock);
	if(r < 0) 
		return r;

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

static int __superblock_all_zeros(struct dm_block_manager *bm, int *result)
{
	int r;
	unsigned i;
	struct dm_block *b;
	__le64 *data_le, zero = cpu_to_le64(0);
	unsigned block_size = dm_bm_block_size(bm) / sizeof(__le64);

	/*
	 *	 * We can't use a validator here - it may be all zeroes.
	 *		 */
	r = dm_bm_read_lock(bm, METADATA_SUPERBLOCK_LOCATION, NULL, &b);
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

static struct metadata *init_btree(void * param, int *unformatted)
{
	int r;
	struct metadata *md;
	struct dm_block_manager *meta_bm;
	struct dm_space_map *meta_sm;
	struct dm_space_map *data_sm = NULL;
	struct init_btree_param *ibp = (struct init_btree_param *)param;
	struct dm_transaction_manager *tm;
	
	DMINFO("Init btree");

	md = kzalloc(sizeof(*md), GFP_NOIO);
	if(!md)
		return ERR_PTR(-ENOMEM);

	meta_bm = dm_block_manager_create(ibp->meta_bdev, METADATA_BSIZE,
						METADATA_CACHESIZE, METADATA_MAXLOCKS);
	if( IS_ERR(meta_bm)){
		md = (struct metadata *)meta_bm;
		goto badbm;
	}

	r = __superblock_all_zeros(meta_bm, unformatted);
	if(r) {
		md = ERR_PTR(r);
		goto badtm;
	}
	
	if(!*unformatted) {
		struct dm_block *sblock;
		struct metadata_superblock *disk_super;

		md->meta_bm = meta_bm;

		r = dm_bm_read_lock(meta_bm, METADATA_SUPERBLOCK_LOCATION,
					NULL, &sblock);
		if(r < 0) 
			DMERR("read_lock superblock failed");

		disk_super = dm_block_data(sblock);

		r = dm_tm_open_with_sm(meta_bm, METADATA_SUPERBLOCK_LOCATION,
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

	r = dm_tm_create_with_sm(meta_bm, METADATA_SUPERBLOCK_LOCATION, &tm, &meta_sm);
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

	r = __write_initial_superblock(md);
	if(r < 0){
		md = ERR_PTR(r);
		goto badwritesuper;
	}

begin_trans :
	r = __begin_transaction(md);
	if(r < 0) {
		md = ERR_PTR(r);
		goto badwritesuper;
	}

	md->bs_hash_pbn = NULL;
	md->bs_lbn_pbn = NULL;

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
//----------------------------------------------------------------
static void exit_btree(struct metadata *md)
{
	int r;

	r = __commit_transaction(md);
	if(r < 0)
		DMWARN("%s: __commit_transaction() failed, error = %d", __func__, r);

	dm_sm_destroy(md->data_sm);
	dm_tm_destroy(md->tm);
	dm_sm_destroy(md->meta_sm);
	dm_block_manager_destroy(md->meta_bm);

	kfree(md->bs_hash_pbn);
	kfree(md->bs_lbn_pbn);

	kfree(md);
}

static int flush_btree(struct metadata *md)
{
	int r;

	r = __commit_transaction(md);

	if (r < 0) {
		DMWARN("%s: __commit_transaction() failed, error = %d", __func__, r);
		return r;
	}

	r = __begin_transaction(md);

	return r;
}

//----------------------------------------------------------------
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
	if(r < 0) 
		return r;

	return (int)refcnt;
}
//----------------------------------------------------------------
/////////////////////////////////
// lbn -> pbn btree functions
/////////////////////////////////
static int lbn_pbn_delete_btree(struct btree_store *bs, void *key, int32_t ksize)
{
	int r;

	if(ksize != bs->key_size) 
		return -EINVAL;

	r = dm_btree_remove(&(bs->tree_info), bs->root, key, &(bs->root));

	if(r == -ENODATA) 
		return -ENODEV;
	else if (r >= 0) 
		return 0;
	else 
		return r;
}

static int lbn_pbn_search_btree(struct btree_store *bs, void *key, int32_t kszie,
			void *value, int32_t *vsize)
{
	int r;

	if(ksize != (int32_t)(bs->key_size)) 
		 return -EINVAL;

	r = dm_btree_lookup(&(bs->tree_info), bs->root, key, value);

	if(r == -ENODATA) 
		return 0; // not found
	else if(r >= 0) 
		return 1; // found
	else 
		return 0; // error
}

static int lbn_pbn_insert_btree(struct btree_store *bs, void *key, int32_t kszie,
		void *value, int32_t vsize)
{
	if (ksize != (int32_t)bs->key_size ||
		vsize != (int32_t)bs->value_size)
		return -EINVAL;

	return dm_btree_insert(&(bs->tree_info), bs->root, key, value, &(bs->root));
}

static struct btree_store *lbn_pbn_create_btree(struct metadata *md, uint32_t ksize,
	uint32_t vsize, uint32_t kmax, int unformatted)
{
	struct btree_store *bs;

	int r;

	if (!vsize || !ksize) 
		return ERR_PTR(-ENOTSUPP);

	if (ksize != 8) 
		return ERR_PTR(-ENOTSUPP); // persistent data support until 64bit key
	
	if (md->bs_lbn_pbn) 
		return ERR_PTR(-EBUSY);

	bs = kmalloc(sizeof(*bs), GFP_NOIO);
	if (!bs) 
		return ERR_PTR(-ENOMEM);

	bs->key_size = ksize;
	bs->value_size = vsize;
	
	bs->tree_info.tm = md->tm;
	bs->tree_info.levels = 1;
	bs->tree_info.value_type.context = NULL;
	bs->tree_info.value_type.size = vsize;
	bs->tree_info.value_type.inc = NULL;
	bs->tree_info.value_type.dec = NULL;
	bs->tree_info.value_type.equal = NULL;

	bs->bops.btree_delete = lbn_pbn_delete_btree;
	bs->bops.btree_search = lbn_pbn_search_btree;
	bs->bops.btree_insert = lbn_pbn_insert_btree;


	if(!unformatted){
		md->bs_lbn_pbn = bs;
		__begin_transaction(md);
	}
	else
	{
		r = dm_btree_empty(&(bs->tree_info), &(bs->root));
		if(r < 0){
			bs = ERR_PTR(r);
			kfree(bs);
			return bs;
		}
		md->bs_lbn_pbn = bs;
		flush_btree(md);
	}

	return bs;
}

//----------------------------------------------------------------
//////////////////////////////////////////////////////////////////////////////////
// hash -> pbn btree functions
// hash value(md5, sha256, sha512 ..) cannot be a key 
// for btree because of 64bit limit
// cut value of hash value is the key of btree and 
// duplicated key is just handled by increasing the key
// value of the btree node is consist of concated all of the hash value and data
//////////////////////////////////////////////////////////////////////////////////
static int hash_pbn_delete_btree(struct btree_store *bs, void *key, int32_t ksize)
{
	char *entry;
	uint64_t cut_key;
	int r;
	
	if (ksize != bs->key_size)
		return -EINVAL;

	entry = kmalloc(bs->entry_size, GFP_NOIO);
	if (!entry)
		return -ENOMEM;

	cut_key = (*(uint64_t *)key);

	do{
		r = dm_btree_lookup(&(bs->tree_info), bs->root, &cut_key, entry);
		if (r == -ENODATA)
			return -ENODEV;
		else if (r >= 0){
			if (!memcmp(entry, key, ksize)){
				r = dm_btree_remove(&(bs->tree_info), bs->root, &cut_key, &(bs->root));
				kfree(entry);
				return r;
			}
			cut_key++;
		}
		else{
			kfree(entry);
			return r;
		}
	} while (r>=0);

	return 0;
}
static int hash_pbn_search_btree(struct btree_store *bs, void *key, int32_t ksize,
	void *value, int32_t *vsize)
{
	char *entry;
	uint64_t cut_key;
	int r;
	
	if (ksize != bs->key_size)
		return -EINVAL;

	entry = kmalloc(bs->entry_size, GFP_NOIO);
	if (!entry)
		return -ENOMEM;

	cut_key = (*(uint64_t *)key);

	do{
		r = dm_btree_lookup(&(bs->tree_info), bs->root, &cut_key, entry);
		if (r == -ENODATA){
			kfree(entry);
			return 0;
		}
		else if (r >= 0){
			if (!memcmp(entry, key, ksize)){
				memcpy(value, entry + ksize, bs->value_size);
				kfree(entry);
				return 1;
			}
			cut_key++;
		}
		else{
			kfree(entry);
			return r;
		}
	} while (r >= 0);

	return 0;
}
static int hash_pbn_insert_btree(struct btree_store *bs, void *key, int32_t ksize,
	void *value, int32_t vsize)
{
	char *entry;
	uint64_t cut_key;
	int r;

	if (ksize != bs->key_size)
		return -EINVAL;

	entry = kmalloc(bs->entry_size, GFP_NOIO);
	if (!entry)
		return -ENOMEM;

	cut_key = (*(uint64_t *)key);

	do{
		r = dm_btree_lookup(&(bs->tree_info), bs->root, &cut_key, entry);
		if (r == -ENODATA){
			memcpy(entry, key, ksize);
			memcpy(entry + ksize, value, vsize);
			__dm_bless_for_disk(&cut_key); // ????
			r = dm_btree_insert(&(bs->tree_info), bs->root, 
						&cut_key, entry, &(bs->root));
			kfree(entry);
			return r;
		}
		else if (r >= 0){
			cut_key++;
		}
		else{
			kfree(entry);
			return r;
		}
	} while (r >= 0);

	return 0;
}
static struct btree_store *hash_pbn_create_btree(struct metadata *md, uint32_t ksize,
	uint32_t vsize, uint32_t kmax, int unformatted)
{
	struct btree_store *bs;

	int r;

	if (!vsize || !ksize)
		return ERR_PTR(-ENOTSUPP);

	if (md->bs_hash_pbn)
			return ERR_PTR(-EBUSY);

	bs = kmalloc(sizeof(*bs), GFP_NOIO);
	if (!bs)
		return ERR_PTR(-ENOMEM);

	bs->key_size = ksize;
	bs->value_size = vsize;
	bs->entry_size = ksize + vsize;

	bs->tree_info.tm = md->tm;
	bs->tree_info.levels = 1;
	bs->tree_info.value_type.context = NULL;
	bs->tree_info.value_type.size = bs->entry_size;
	bs->tree_info.value_type.inc = NULL;
	bs->tree_info.value_type.dec = NULL;
	bs->tree_info.value_type.equal = NULL;

	bs->bops.btree_delete = hash_pbn_delete_btree;
	bs->bops.btree_search = hash_pbn_search_btree;
	bs->bops.btree_insert = hash_pbn_insert_btree;


	if (!unformatted){
		md->bs_lbn_pbn = bs;
		__begin_transaction(md);
	}
	else
	{
		r = dm_btree_empty(&(bs->tree_info), &(bs->root));
		if (r < 0){
			bs = ERR_PTR(r);
			kfree(bs);
			return bs;
		}
		md->bs_lbn_pbn = bs;
		flush_btree(md);
	}

	return bs;
}

struct metadata_ops metadata_ops_btree = {
	.init_meta = init_btree,
	.exit_meta = exit_btree,
	.bs_create_lbn_pbn = lbn_pbn_create_btree,
	.bs_create_hash_pbn = hash_pbn_create_btree,
	.alloc_data_block = alloc_data_block,
	.inc_refcount = inc_refcnt,
	.dec_refcount = dec_refcnt, 
	.get_refcount = get_refcnt, 

	.flush_meta = flush_btree,
};




