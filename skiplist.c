// SPDX-License-Identifier: GPL-2.0-only
/*
 * Skiplist implementation by: Daniel Vlasenco
 */

#include "blkm.h"

#define HEAD_KEY ((sector_t)0)
#define HEAD_DATA ((sector_t)U64_MAX)
#define TAIL_KEY ((sector_t)U64_MAX)
#define TAIL_DATA ((sector_t)0)
#define MAX_LVL 20

static void free_node_full(struct skiplist_node *node)
{
	struct skiplist_node *temp;

	while (node) {
		temp = node->lower;
		kfree(node);
		node = temp;
	}
}

static struct skiplist_node *create_node_tall(sector_t key, sector_t data,
								int h)
{
	struct skiplist_node *last;
	struct skiplist_node *curr;
	int curr_h;

	last = NULL;
	for (curr_h = 0; curr_h < h; ++curr_h) {
		curr = kzalloc(sizeof(*curr), GFP_KERNEL);
		if (!curr)
			goto alloc_fail;

		curr->key = key;
		curr->data = data;
		curr->lower = last;
		last = curr;
	}

	return curr;

alloc_fail:
	free_node_full(last);

	return NULL;
}

static struct skiplist_node *create_node(sector_t key, sector_t data)
{
	return create_node_tall(key, data, 1);
}

struct skiplist *skiplist_init(void)
{
	struct skiplist *sl;
	struct skiplist_node *head;
	struct skiplist_node *tail;

	sl = kzalloc(sizeof(*sl), GFP_KERNEL);
	head = create_node(HEAD_KEY, HEAD_DATA);
	tail = create_node(TAIL_KEY, TAIL_DATA);
	if (!sl || !head || !tail)
		goto alloc_fail;

	sl->head = head;
	sl->head_lvl = 0;
	sl->max_lvl = MAX_LVL;
	head->next = tail;

	return sl;

alloc_fail:
	kfree(sl);
	kfree(head);
	kfree(tail);
	return NULL;
}

static void get_prev_nodes(sector_t key, struct skiplist *sl,
			struct skiplist_node **buf, int lvl)
{
	struct skiplist_node *curr;
	int curr_lvl;

	curr = sl->head;
	curr_lvl = sl->head_lvl;
	pr_warn("need to get prev nodes for key %llu\n", key);

	while (curr && curr_lvl >= 0) {
		pr_warn("curr key %llu, next key %llu, curr lvl %d, max pred lvl %d\n",
			curr->key, curr->next->key, curr_lvl, lvl);
		if (curr->next->key < key) {
			pr_warn("moving forward, next is %p\n", curr->next);
			curr = curr->next;
		} else {
			if (curr_lvl <= lvl) {
				buf[curr_lvl] = curr;
				pr_warn("buf[%d] is %p with key %llu\n",
					curr_lvl, buf[curr_lvl], buf[curr_lvl]->key);
			}
			pr_warn("moving down, lower is %p\n", curr->lower);
			--curr_lvl;
			curr = curr->lower;
		}
	}

	for (int i = 0; i <= lvl; ++i) {
		pr_warn("buf[%d] is %p with key %llu\n",
			i, buf[i], buf[i]->key);
	}
}

struct skiplist_node *skiplist_find_node(sector_t key, struct skiplist *sl)
{
	struct skiplist_node *curr = sl->head;

	while (curr) {
		if (curr->next->key == key)
			return curr->next;
		else if (curr->next->key < key)
			curr = curr->next;
		else
			curr = curr->lower;
	}

	return NULL;
}

static int move_head_and_tail_up(struct skiplist *sl, int lvls_up)
{
	struct skiplist_node *head_ext;
	struct skiplist_node *tail_ext;
	struct skiplist_node *curr;
	struct skiplist_node *temp;

	head_ext = create_node_tall(HEAD_KEY, HEAD_DATA, lvls_up);
	tail_ext = create_node_tall(TAIL_KEY, TAIL_DATA, lvls_up);
	if (!head_ext || !tail_ext)
		goto alloc_fail;

	curr = head_ext;
	temp = tail_ext;
	while (curr && temp) {
		curr->next = temp;
		if (!curr->lower || !temp->lower)
			break;

		curr = curr->lower;
		temp = temp->lower;
	}

	curr->lower = sl->head;
	temp->lower = skiplist_find_node(TAIL_KEY, sl);
	sl->head = head_ext;

	return 0;

alloc_fail:
	free_node_full(head_ext);
	free_node_full(tail_ext);
	
	return -ENOMEM;
}

static int move_up_if_lvl_nex(struct skiplist *sl, int lvl)
{
	unsigned int diff;
	int ret;

	if (lvl <= sl->head_lvl || lvl > sl->max_lvl) {
		pr_warn("no need to move up\n");
		return 0;
	}

	diff = lvl - sl->head_lvl;
	ret = move_head_and_tail_up(sl, diff);
	if (ret) {
		pr_err("failed to move head and tail up\n");
		return ret;
	}

	sl->head_lvl = lvl;
	pr_warn("moved up successfully, now sl->head is at lvl %d\n", sl->head_lvl);

	return 0;
}

static int flip_coin(void)
{
	return get_random_u8() % 2;
}

static int get_random_lvl(int max) {
	int lvl = 0;

	while ((lvl < max) && flip_coin())
		lvl++;

	return lvl;
}

struct skiplist_node *skiplist_add(sector_t key, sector_t data,
					struct skiplist *sl)
{
	struct skiplist_node *prev[sl->max_lvl+1];
	struct skiplist_node *old;
	struct skiplist_node *new;
	struct skiplist_node *temp;
	int lvl;
	int err;
	int i;

	old = skiplist_find_node(key, sl);
	if (old)
		return old;

	lvl = get_random_lvl(sl->max_lvl);
	err = move_up_if_lvl_nex(sl, lvl);
	if (err)
		goto fail;

	get_prev_nodes(key, sl, prev, lvl);

	temp = NULL;
	for (i = 0; i <= lvl; ++i) {
		new = create_node(key, data);
		if (!new) {
			err = -ENOMEM;
			goto alloc_fail;
		}
		pr_warn("created node %p with key %llu and data %llu\n",
			new, new->key, new->data);
		if (temp) {
			new->lower = temp;
			pr_warn("connected node %p with key %llu from lvl %d to node %p with key %llu from lvl %d\n",
				new, new->key, i, temp, temp->key, i-1);
		}
		new->next = prev[i]->next ;
		prev[i]->next = new;
		temp = new;
	}
	skiplist_print(sl);

	return new;

alloc_fail:
	for (i = i - 1; i >= 0; --i) {
		new = prev[i]->next;
		prev[i]->next = new->next;
		kfree(new);
	}
fail:
	return ERR_PTR(err);
}

void skiplist_free(struct skiplist *sl)
{
	struct skiplist_node *curr;
	struct skiplist_node *next;
	struct skiplist_node *tofree;
	struct skiplist_node *tofree_stack[MAX_LVL + 1];
	int stack_i;

	stack_i = 0;
	tofree_stack[stack_i++] = sl->head;

	while (stack_i > 0 && tofree_stack[stack_i]) {
		tofree = tofree_stack[stack_i];

		curr = tofree;
		while (curr) {
			next = curr->next;
			if (!next)
				break;

			if (next->key < tofree_stack[stack_i]->key)
				tofree_stack[++stack_i] = next;

			curr = curr->lower;
		}

		free_node_full(tofree);
		tofree_stack[stack_i--] = NULL;
	}

	kfree(sl);
}

void skiplist_print(struct skiplist *sl) {
	struct skiplist_node *curr;
	struct skiplist_node *head;

	head = sl->head;
	while (head) {
		curr = head;
		while (curr) {
			if (curr->key == HEAD_KEY && curr->data == HEAD_DATA)
				printk("head->");
			else if (curr->key == TAIL_KEY && curr->data == TAIL_DATA)
				printk("tail->");
			else
				printk("(%llu-%llu)->", curr->key, curr->data);

			curr = curr->next;
		}
		printk("\n");
		head = head->lower;
	}
}