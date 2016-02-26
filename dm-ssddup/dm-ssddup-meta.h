#ifndef _SSDDUP_META_H
#define _SSDDUP_META_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device-mapper.h>
#include <linux/dm-io.h>
#include <linux/dm-kcopyd.h>
#include <linux/list.h>
#include <linux/err.h>
#include <asm/current.h>
#include <linux/string.h>
#include <linux/gfp.h>

#include <linux/scatterlist.h>
#include <asm/page.h>
#include <asm/unaligned.h>
#include <crypto/hash.h>
#include <crypto/md5.h>
#include <crypto/algapi.h>

#include "dm-ssddup-target.h"

#define SPACE_MAP_ROOT_SIZE			128


struct metadata_ops {
	struct metadata * (*init_meta)(void *init_param, int *unformatted);
	void(*exit_meta)(struct metadata *md);
	
	struct btree_store * (*bs_create_lbn_pbn)(struct metadata *md,
		uint32_t ksize, uint32_t vsize, uint32_t kmax,
		int unformatted);
	struct btree_store * (*bs_create_hash_pbn)(struct metadata *md,
		uint32_t ksize, uint32_t vsize, uint32_t knummax,
		int unformatted);

	int(*alloc_data_block)(struct metadata *md, uint64_t *blockn);
	int(*inc_refcount)(struct metadata *md, uint64_t blockn);
	int(*dec_refcount)(struct metadata *md, uint64_t blockn);
	int(*get_refcount)(struct metadata *md, uint64_t blockn);
	
	int(*flush_meta)(struct metadata *md);

	int(*get_private_data)(struct metadata *md, void **data,
		uint32_t size);
	int(*set_private_data)(struct metadata *md, void *data, uint32_t size);

};
extern struct metadata_ops metadata_ops_btree;

struct btree_ops {
	int(*btree_delete)(struct btree_store *bs, void *key, int32_t ksize);
	int(*btree_search)(struct btree_store *bs, void *key, int32_t ksize,
		void *value, int32_t *vsize);
	int(*btree_insert)(struct btree_store *bs, void *key, int32_t ksize,
		void *value, int32_t vsize);
};



struct metadata {
	struct dm_block_manager *meta_bm;
	struct dm_transaction_manager *tm;
	struct dm_space_map *data_sm;
	struct dm_space_map *meta_sm;

	struct btree_store *bs_hash_pbn;
	struct btree_store *bs_lbn_pbn;
};

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

	__le64 hash_pbn_root;
	__le64 lbn_pbn_root;
};

struct init_btree_param{
	struct block_device *meta_bdev;
	uint64_t blocks;
};



#endif
