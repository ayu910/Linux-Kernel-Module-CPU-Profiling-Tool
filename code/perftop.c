#include <linux/module.h>
#include <linux/seq_file.h>
#include<linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include<linux/init.h>
#include<linux/kprobes.h>
#include<linux/version.h>
#include <linux/spinlock.h>
#include <linux/limits.h>
#include <linux/sched.h>
#include <linux/hashtable.h>
#include <linux/rbtree.h>
#include <linux/slab.h>


static unsigned long prev_val;
static unsigned long post_val;

static unsigned long long hash_final;

static unsigned long long initial_time;
static unsigned long long net_time;
static unsigned long long curr_time;

static spinlock_t spinlock1;

/*struct rbtree{
	struct rb_node list1;
	unsigned long long cumulative_tsc_val;
};*/

struct rbtree_1{
	struct rb_node list1;
	int pid_val;
	unsigned long long cumulative_tsc_val;
};

struct rb_int_node{
	struct rb_root root_node;
};
struct rb_int_node *rb_int_node;

struct h_table_node{
	struct hlist_node h_table;
	int pid_val;
	unsigned long long tsc_val;
};
/*
struct header
{
        DECLARE_HASHTABLE(num_hasher, 14);
};*/
static DEFINE_HASHTABLE(num_hasher, 14);

int rbtree_initialize(void)
{
	rb_int_node = kmalloc(sizeof *rb_int_node, GFP_ATOMIC);
	rb_int_node->root_node = RB_ROOT;
	return 0;
}

//int insert_val, count;

//struct item *currItem, *insert_item, *read_item;
//struct rb_node **curr = &num_tree.rb_node;
//struct rb_node *parent = NULL;

unsigned long long get_value_from_rbtree(int pid){
	unsigned long long ret_val;
    struct rbtree_1 *current_node;
	struct rb_node *parent = NULL;
	for(parent= rb_first(&rb_int_node->root_node); parent; parent= rb_next(parent)){
		current_node = rb_entry(parent, struct rbtree_1, list1);
		if(current_node->pid_val == pid){
			ret_val = current_node->cumulative_tsc_val;
			rb_erase(&current_node->list1, &rb_int_node->root_node);
	        kfree(current_node);
			return ret_val;
		}
	}
    return 0;
}

int set_value_in_rbtree(int pid, unsigned long long tsc_val){
	struct rbtree_1 *node, *current_node;
	struct rb_node **temp_val = &rb_int_node->root_node.rb_node;
	struct rb_node *parent = NULL;
	node = kmalloc(sizeof(struct rbtree_1), GFP_ATOMIC);
	node->cumulative_tsc_val = tsc_val;
	node->pid_val = pid;
	
	while(*temp_val){
		parent = *temp_val;
		current_node = rb_entry(parent, struct rbtree_1, list1);
		if(node->cumulative_tsc_val < current_node->cumulative_tsc_val){
			temp_val = &parent->rb_left;
		}
		else{
			temp_val = &parent->rb_right;
		}
	}
	rb_link_node(&node->list1, parent, temp_val);
	rb_insert_color(&node->list1, &rb_int_node->root_node);
	return 0;
}

static unsigned long long find_value_in_hash(int pid){
	struct h_table_node *finder_node;
	hash_for_each_possible(num_hasher, finder_node, h_table, pid){
		if(finder_node->pid_val == pid){
			return finder_node->tsc_val;
		}
	}
	return 0;
}

static int replace_value_in_hash(int pid, unsigned long long tsc_val){
	struct h_table_node *hash_curr;
	hash_for_each_possible(num_hasher, hash_curr, h_table, pid){
		if(hash_curr->pid_val == pid){
			hash_curr->tsc_val = tsc_val;
		}
		return 1;
	}
	return 0;
}


//static int query_hash_fn(int pid)
//{
//        hash_for_each_possible_safe(num_hasher_ptr->num_hasher, my_num2, temp_node, list1, pid)
//        {
//printk("UPDATE");
//printk("Value is = %Ld for PID = %d. Removing this value.", my_num2->tsc_value, my_num2->pid_value);
//               return my_num2->val;
//        }
//        return 0;
//}

static int set_value_in_hash(int pid, unsigned long long tsc_val){
	if(replace_value_in_hash(pid, tsc_val) == 0){
		struct h_table_node *hash_temp;
		hash_temp = kmalloc(sizeof *hash_temp, GFP_ATOMIC);
		hash_temp->pid_val = pid;
		hash_temp->tsc_val = tsc_val;
		hash_add(num_hasher, &hash_temp->h_table, pid);
	}
	return 0;
}

static int perftop_show(struct seq_file *m, void *v) {
    int count = 0;
    struct rb_node *parent = NULL;
    struct rbtree_1 *temp_node;
    seq_printf(m, "Printing top 10 values :\n");
    for(parent= rb_last(&rb_int_node->root_node); parent; parent= rb_prev(parent)){
		if(count >= 10){
				break;
		}
		temp_node = rb_entry(parent, struct rbtree_1, list1);
		seq_printf(m, "%d.TSC = %llu for PID = %d.\n", count, temp_node->cumulative_tsc_val, temp_node->pid_val);
		count++;
    }
  	return 0;
}

static int perftop_open(struct inode *inode, struct  file *file) {
  return single_open(file, perftop_show, NULL);
}

static const struct proc_ops perftop_fops = {
  .proc_open = perftop_open,
  .proc_read = seq_read,
  .proc_lseek = seq_lseek,
  .proc_release = single_release,
};

static int entry_pick_next_fair(struct kretprobe_instance *ri, struct pt_regs *regs){
  prev_val = regs->si;
  return 0;
}
NOKPROBE_SYMBOL(entry_pick_next_fair);

static int ret_pick_next_fair(struct kretprobe_instance *ri, struct pt_regs *regs){
          struct task_struct *next_task, *prev_task;
  spin_lock(&spinlock1);
  post_val = regs_return_value(regs);
  if(prev_val != 0 && post_val != 0 && prev_val != post_val){
	curr_time = rdtsc();
        next_task = (struct task_struct *)post_val;
        prev_task = (struct task_struct *)prev_val;
	hash_final = find_value_in_hash(prev_task->pid);
	set_value_in_hash(next_task->pid, curr_time);
	initial_time = hash_final;
	net_time = get_value_from_rbtree(prev_task->pid);
	net_time = net_time + curr_time - initial_time;
	set_value_in_rbtree(prev_task->pid, net_time);
  }
  spin_unlock(&spinlock1);
  return 0;
}
NOKPROBE_SYMBOL(ret_pick_next_fair);

static struct kretprobe my_kretprobe = {
	.handler		= ret_pick_next_fair,
	.entry_handler		= entry_pick_next_fair,
	.maxactive		= 20,
};

static int __init perftop_init(void) {
  int return_value;
  spin_lock_init(&spinlock1);
  rbtree_initialize();
  proc_create("perftop", 0, NULL, &perftop_fops);
  my_kretprobe.kp.symbol_name = "pick_next_task_fair";
  return_value = register_kretprobe(&my_kretprobe);
  if (return_value < 0) {
  	printk("couldn't register kprobe");
	return -1;
  }
  return 0;
}

static void __exit perftop_exit(void) {
  remove_proc_entry("perftop", NULL);
  unregister_kretprobe(&my_kretprobe);
}

MODULE_LICENSE("GPL");
module_init(perftop_init);
module_exit(perftop_exit);

