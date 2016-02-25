#ifndef _DM_PERS_TEST_H
#define _DM_PERS_TEST_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device-mapper.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/gfp.h>
#include <linux/blk_types.h>

#endif

struct dssddup_c{
	struct dm_dev *meta_dev;
	struct dm_dev *data_dev;

	uint32_t blk_size;

	uint32_t sector_num_per_blk;
	uint32_t logical_blk_cnt;
	uint32_t physical_blk_cnt;

	struct dm_io_client *io_client;

	char hash_algo[16];
	struct data_hash_ops * hops;

	struct metadata *meta_l_p;
	struct metadata *meta_d_p;


	uint32_t flush_limit;
	uint64_t writes_cnt_for_flush;

	mempool_t * ssddup_mempool;
};
