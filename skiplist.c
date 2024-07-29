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
	free_node_full(curr);

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
	int lvls_passed;

	lvls_passed = 0;
	curr = sl->head;
	while (curr && lvls_passed <= lvl) {
		if (curr->next->key < key || curr->data != HEAD_DATA) {
			curr = curr->next;
		} else {
			buf[lvl-lvls_passed] = curr;
			pr_warn("buf[%d] is %p\n", lvl-lvls_passed, curr);
			++lvls_passed;
			curr = curr->lower;
		}
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

	pr_warn("need to move %d lvls up\n", lvls_up);
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

	pr_warn("initial head->next = %p\n", sl->head->next);
	curr->lower = sl->head;
	pr_warn("curr->lower = %p\n", curr->lower);
	temp->lower = skiplist_find_node(TAIL_KEY, sl);
	pr_warn("temp->lower = %p\n", temp->lower);
	sl->head = head_ext;
	pr_warn("new head = %p\n", sl->head);

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
		return 0;
	}

	pr_warn("head lvl was %d, got lvl %d, need to move up\n", sl->head_lvl, lvl);
	diff = lvl - sl->head_lvl;
	ret = move_head_and_tail_up(sl, diff);
	if (ret) {
		pr_err("failed to move head and tail up\n");
		return ret;
	}

	pr_warn("moved up successfully\n");
	sl->head_lvl = lvl;

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
	if (!sl) {
		pr_warn("cannot add, skiplist is not initialized\n");
		return NULL;
	}
	pr_warn("max lvl = %d\n", sl->max_lvl);

	struct skiplist_node *prev[sl->max_lvl+1];
	struct skiplist_node *old;
	struct skiplist_node *new;
	int lvl;
	int err;
	int i;

	pr_warn("finding old\n");
	old = skiplist_find_node(key, sl);
	if (old)
		return old;

	pr_warn("old not found\n");

	lvl = get_random_lvl(sl->max_lvl);
	pr_warn("got lvl %d, curr lvl %d\n", lvl, sl->head_lvl);

	err = move_up_if_lvl_nex(sl, lvl);
	if (err)
		goto fail;

	skiplist_print(sl);
	pr_warn("curr lvl = %d\n", sl->head_lvl);

	get_prev_nodes(key, sl, prev, lvl);
	for (i = 0; i <= lvl; ++i) {
		new = create_node(key, data);
		if (!new) {
			err = -ENOMEM;
			goto alloc_fail;
		}
		new->next = prev[i]->next;
		prev[i]->next = new;
	}
	pr_warn("added successfully\n");

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