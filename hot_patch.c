#include "hot_patch.h"
static struct kallsyms_lookup_symbol symbol;

void add_sb_to_wait_erase_list(struct shannon_sb *sb, struct shannon_list_head *sb_list)
{
	struct shannon_sb *tmp;
	struct shannon_list_head *list;
	struct shannon_dev *sdev = sb->sdev;
	int empty_blkcnt = sdev->wait_erased_blkcnt + sdev->free_blkcnt;

	if (shannon_list_empty(sb_list)) {
		shannon_list_add_tail(&sb->list, sb_list);
		return;
	}

	list = sb_list->next;
	while (list != sb_list) {
		tmp = list_entry(list, struct shannon_sb, list);
		if (tmp->erase_counter > sb->erase_counter)
			break;
		list = list->next;
	}
	shannon_list_add_tail(&sb->list, list);

	if ((empty_blkcnt > GC_THRESHOLD_N1L) &&	\
			(atomic_read((atomic_t *)&sdev->write_reqs_for_gc) >=	\
			WRITE_REQS_FOR_GC_THRESHOLD)) {
		shannon_warn("%s: empty_blkcnt: %d, write_reqs_for_gc: %d, "	\
				"and it will be set to 0.\n",
				sdev->cdev_name, empty_blkcnt,
				atomic_read((atomic_t *)&sdev->write_reqs_for_gc));
		atomic_set((atomic_t *)&sdev->write_reqs_for_gc, 0);
	}

}

static void ready_to_check_dev_status(void)
{
	struct shannon_dev *sdev = NULL;
	list_for_each_entry(sdev, symbol.dev_addr, list) {
		if (sdev && (sdev->gc_thread_state == 0) &&	\
				(atomic_read((atomic_t *)&sdev->write_reqs_for_gc) >=	\
				WRITE_REQS_FOR_GC_THRESHOLD)) {
			shannon_warn("%s: gc_state: %d, write_reqs_for_gc: %d, "	\
					"and it will be set to 0.\n",
					sdev->cdev_name, sdev->gc_thread_state,
					atomic_read((atomic_t *)&sdev->write_reqs_for_gc));
			atomic_set((atomic_t *)&sdev->write_reqs_for_gc, 0);
		}
	}
}

/* Before link, we must ensure that the dest mod already exists. */
static int link_hot_patch_to_shannon(struct module *target)
{
	if (!target)
		return 0;
	return try_module_get(target);
}

static void unlink_hot_patch_to_shannon(struct module *target)
{
	module_put(target);
}

static int match_driver_version(struct module *mod)
{
	if ((!mod->version) ||	\
			(strcmp(SHANNON_VERSION_330, mod->version) &&	\
			(strcmp(SHANNON_VERSION_331, mod->version)))) {
		return 0;
	}
	return 1;
}

static int fill_symbol_struct(struct module *mod)
{
	struct module *shannon = mod;
	symbol.dev_addr = NULL;
	symbol.src_fun_addr = NULL;
	symbol.my_text_mutex = NULL;
	symbol.dev_addr = (struct shannon_list_head *)	\
						symbol.find(shannon, "shannon_dev_list");
	symbol.src_fun_addr = (char *)	\
						symbol.find(shannon, "add_sb_to_wait_erase_list");
	symbol.mem_remap = (void *)	\
						kallsyms_lookup_name("text_poke_smp");
	symbol.my_text_mutex = (struct mutex *)	\
						kallsyms_lookup_name("text_mutex");

	if ((!symbol.dev_addr) || (!symbol.mem_remap) ||	\
			(!symbol.src_fun_addr) || (!symbol.my_text_mutex)) {
		return -ENOENT;
	}
	return 0;
}

static int init_symbol_addr(void)
{
	struct module *shannon = NULL;
	shannon = find_module(DRIVER_NAME);
	/* this is necessary */
	symbol.find = (void *)kallsyms_lookup_name("mod_find_symname");
	if ((!shannon) || (!symbol.find)) {
		shannon_err("unexpected error!!!, "	\
				"shannon_module: %p, symbol.find: %p.\n",
				shannon, symbol.find);
		return -ENOENT;
	}

	if (!match_driver_version(shannon)) {
		shannon_err("current driver: %s don't match, "	\
				"please make sure the driver version is %s or %s.\n",
				shannon->version, SHANNON_VERSION_330,	\
				SHANNON_VERSION_331);
		return -1;
	}

	if (!link_hot_patch_to_shannon(shannon)) {
		shannon_err("link hot patch to shannon(%p) failed.\n",shannon);
		return -ENODEV;
	}

	if (fill_symbol_struct(shannon)) {
		shannon_err("unexpected error!!!, "	\
				"dev_addr: %p, mem_remap: %p, "	\
				"src_fun_addr: %p, my_text_mutex: %p.\n",
				symbol.dev_addr, symbol.mem_remap,	\
				symbol.src_fun_addr, symbol.my_text_mutex);
		unlink_hot_patch_to_shannon(shannon);
		return -1;
	}

	return 0;
}

static void hot_patch_register(void)
{
	unsigned char call_inst[INST_SIZE];
	int offset;
	memcpy(symbol.restore_inst, symbol.src_fun_addr, INST_SIZE);
	offset = (int)((long)add_sb_to_wait_erase_list -	\
			((long)symbol.src_fun_addr + INST_SIZE));

	call_inst[0] = JMP_INSTRUCT;
	(*(int *)(&call_inst[1])) = offset;
	get_online_cpus();
	mutex_lock(symbol.my_text_mutex);
	symbol.mem_remap(symbol.src_fun_addr, call_inst, INST_SIZE);
	mutex_unlock(symbol.my_text_mutex);
	put_online_cpus();
}

static int __init hot_patch_init(void)
{
	if (!init_symbol_addr()) {
		hot_patch_register();
		ready_to_check_dev_status();
		shannon_info("Registered shannon hot patch.\n");
		return 0;
	} else
		return -ENOENT;
}

static void __exit hot_patch_exit(void)
{
	struct module *shannon = NULL;
	char *addr = NULL;
	shannon = find_module(DRIVER_NAME);
	if (!shannon) {
		shannon_err("shannon already removed!!!.\n");
		return;
	}
	addr = (char *)symbol.find(shannon, "add_sb_to_wait_erase_list");
	if (addr != symbol.src_fun_addr) {
		shannon_err("unexpected error!!!, "	\
				"src_fun_addr dont't match!!!, "	\
				"pre: %p, cur: %p!!!.\n",	\
				symbol.src_fun_addr, addr);
		unlink_hot_patch_to_shannon(shannon);
		return;
	}
	get_online_cpus();
	mutex_lock(symbol.my_text_mutex);
	symbol.mem_remap(symbol.src_fun_addr, symbol.restore_inst, INST_SIZE);
	mutex_unlock(symbol.my_text_mutex);
	put_online_cpus();
	unlink_hot_patch_to_shannon(shannon);
	shannon_info("Unloading shannon hot_patch module.\n");
}

module_init(hot_patch_init);
module_exit(hot_patch_exit);


MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");