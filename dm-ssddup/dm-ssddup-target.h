#ifndef _DM_SSDDUP_H
#define _DM_SSDDUP_H

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
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/parser.h>
#include <linux/blk_types.h>
#include <linux/mempool.h>

#include <linux/scatterlist.h>
#include <asm/page.h>
#include <asm/unaligned.h>
#include <crypto/hash.h>
#include <crypto/md5.h>
#include <crypto/sha.h>
#include <crypto/algapi.h>

#define CRYPTO_ALG_NAME_LEN     16
#define MAX_DIGEST_SIZE	SHA256_DIGEST_SIZE

#define MIN_DEDUP_WORK_IO	16
#define DM_MSG_PREFIX "ssddup-mod"

struct ssddup_c{
	struct dm_dev *meta_dev;
	struct dm_dev *data_dev;

	uint32_t blk_size;

	uint32_t sector_num_per_blk;
	uint32_t logical_blks;
	uint32_t physical_blks;

	uint64_t logical_blk_counter;
	uint64_t physical_blk_counter;
	
	struct workqueue_struct *workqueue;
	struct dm_io_client *io_client;

	char crypto_alg[CRYPTO_ALG_NAME_LEN];
	int crypto_key_size;

	struct metadata_ops *mdops;
	struct metadata *md;
	struct btree_store *lbn_pbn_store;
	struct btree_store *hash_pbn_store;

	uint32_t flush_limit;
	uint64_t writes_cnt_for_flush;

	struct hash_desc_table *desc_table;

	mempool_t * ssddup_mempool;
};
#endif