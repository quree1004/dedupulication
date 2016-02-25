#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x5b4e00ef, "module_layout" },
	{ 0x6e9c0267, "dm_get_device" },
	{ 0xfa2e111f, "slab_buffer_size" },
	{ 0xd691cba2, "malloc_sizes" },
	{ 0x1d271f06, "dm_table_get_mode" },
	{ 0x105e2727, "__tracepoint_kmalloc" },
	{ 0x9ac18fc1, "dm_register_target" },
	{ 0x562760fb, "__blkdev_driver_ioctl" },
	{ 0xf85ccdae, "kmem_cache_alloc_notrace" },
	{ 0xea147363, "printk" },
	{ 0x42224298, "sscanf" },
	{ 0x259be03d, "dm_unregister_target" },
	{ 0xb4390f9a, "mcount" },
	{ 0x37a0cba, "kfree" },
	{ 0x50896cf2, "dm_put_device" },
	{ 0x9edbecae, "snprintf" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=dm-mod";


MODULE_INFO(srcversion, "C6F92645FA44CEC6D1C0E7D");