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
		atomic_set((atomic_t *)&sdev->write_reqs_for_gc, 0);
	}

}

static int init_symbol_addr(void)
{
	struct module *shannon = NULL;
	symbol.src_fun_addr = NULL;
	symbol.my_text_mutex = NULL;
	shannon = find_module(DRIVER_NAME);
	/* this is necessary */
	symbol.find = (void *)kallsyms_lookup_name("mod_find_symname");
	if ((!shannon) || (!symbol.find)) {
		printk(KERN_WARNING "unexpected error!!!, "
				"shannon_module:%p, symbol.find:%p.\n",
				shannon, symbol.find);
		return -ENOENT;
	}

	if (!shannon->version ||	\
			(strcmp(SHANNON_VERSION_330, shannon->version) &&	\
			(strcmp(SHANNON_VERSION_331, shannon->version)))) {
		printk(KERN_WARNING "current driver: %s don't match, "
				"please make sure the driver version is %s or %s.\n",
				shannon->version, SHANNON_VERSION_330,	\
				SHANNON_VERSION_331);
		return -1;
	}

	symbol.src_fun_addr = (char *)symbol.find(shannon, "add_sb_to_wait_erase_list");
	symbol.mem_remap = (void *)kallsyms_lookup_name("text_poke_smp");
	symbol.my_text_mutex = (struct mutex *)kallsyms_lookup_name("text_mutex");
	if ((!symbol.mem_remap) || (!symbol.src_fun_addr) || (!symbol.my_text_mutex)) {
		printk(KERN_WARNING "unexpected error!!!, "
				"mem_remap:%p, src_fun_addr: %p, my_text_mutex: %p.\n",
				symbol.mem_remap, symbol.src_fun_addr, symbol.my_text_mutex);
		return -ENOENT;
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
	/* start to prepare */
	if (!init_symbol_addr()) {
		hot_patch_register();
		printk(KERN_INFO "Registered shannon hot patch.\n");
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
		printk(KERN_INFO "shannon already removed!!!.\n");
		return;
	}
	addr = (char *)symbol.find(shannon, "add_sb_to_wait_erase_list");
	if (addr != symbol.src_fun_addr) {
		printk(KERN_WARNING "src_fun_addr dont't match!!!, "
				"pre: %p, cur: %p!!!.\n",	\
				symbol.src_fun_addr, addr);
		return;
	}
	get_online_cpus();
	mutex_lock(symbol.my_text_mutex);
	symbol.mem_remap(symbol.src_fun_addr, symbol.restore_inst, INST_SIZE);
	mutex_unlock(symbol.my_text_mutex);
	put_online_cpus();
	printk(KERN_INFO "unloading shannon hot_patch module.\n");
}

module_init(hot_patch_init);
module_exit(hot_patch_exit);


MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");