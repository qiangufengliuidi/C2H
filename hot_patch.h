#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/cpu.h>
#include <linux/spinlock.h>

#define DRIVER_VERSION	"0.1.1"
#define DRIVER_AUTHOR	"Shannon_Softdept_Group1"
#define DRIVER_NAME		"shannon"
#define DRIVER_DESC		"Hot Patch Beta for #4412"
#define SHANNON_VERSION_330	"3.3.0"
#define SHANNON_VERSION_331	"3.3.1"
#define GC_THRESHOLD_N1L	23
#define INST_SIZE			5
#define JMP_INSTRUCT		0xe9
#define WRITE_REQS_FOR_GC_THRESHOLD	(200000)
#define RESERVE_MEM(bytes) char mem[bytes] __attribute__ ((aligned(8)))
/*
 * text_poke_smp - Update instructions on a live kernel on SMP
 * @addr: address to modify
 * @opcode: source of the copy
 * @len: length to copy
 * Modify multi-byte instruction by using stop_machine() on SMP
 * Note: Must be called under get_online_cpus() and text_mutex.
 */
typedef void *(*my_text_poke_smp)(void *addr, const void *opcode, size_t len);
typedef unsigned long *(*my_mod_find_symname)(struct module *mod, const char *name);
typedef struct {
	RESERVE_MEM(sizeof(long int));
} shannon_atomic_t;

struct shannon_list_head {
	struct shannon_list_head *next, *prev;
};

struct shannon_dev {
	char reserved0[2856];
	shannon_atomic_t write_reqs_for_gc; //2856
	char reserved1[7008];
	int free_blkcnt; //9872
	char reserved2[588];
	int wait_erased_blkcnt; //10464
	char reserved3[2140];
	char cdev_name[16]; //12608
};

struct shannon_sb {
	struct shannon_list_head list; //size:16
	char reserved0[744];
	struct shannon_dev *sdev; //760
	char reserved1[4];
	int erase_counter; //772
};

struct kallsyms_lookup_symbol {
	unsigned char restore_inst[INST_SIZE]; //instruction
	struct mutex *my_text_mutex;
	char *src_fun_addr;
	my_text_poke_smp mem_remap;
	my_mod_find_symname find;
};

static inline int shannon_list_empty(const struct shannon_list_head *head)
{
	return head->next == head;
}

static inline void __shannon_list_add(struct shannon_list_head *new,
			      struct shannon_list_head *prev,
			      struct shannon_list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void shannon_list_add_tail(struct shannon_list_head *new, struct shannon_list_head *head)
{
	__shannon_list_add(new, head->prev, head);
}
