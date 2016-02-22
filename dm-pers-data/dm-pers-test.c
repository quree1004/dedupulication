#include <linux/module.h>
#include <linux/init.h>
#include <linux/device-mapper.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/gfp.h>
#include <linux/blk_types.h>

int __init dm_pers_test_init(void)
{
	int r;
}

void dm_pers_test_exit(void)
{
}

module_init(dm_pers_test_init);
module_exit(dm_pers_test_exit);

MODULE_LICENSE("GPL");
