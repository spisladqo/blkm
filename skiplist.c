// SPDX-License-Identifier: GPL-2.0-only
/*
 * Skiplist implementation by: Daniel Vlasenco
 */

#include "blkm.h"
#include <vdso/limits.h>

#define HEAD_KEY ((sector_t)0)
#define HEAD_DATA ((sector_t)ULONG_MAX)
#define TAIL_KEY ((sector_t)ULONG_MAX)
#define TAIL_DATA ((sector_t)0)
#define MAX_LVL 20

struct skiplist_node {
	struct skiplist_node *next;
	struct skiplist_node *lower;
	sector_t key;
	sector_t data;
};

struct skiplist {
	struct skiplist_node *head;
	unsigned int head_lvl;
	unsigned int max_lvl;
};

static void free_node_full(struct skiplist_node *node)
{
	struct skiplist_node *temp;

	while (node) {
		temp = node->lower;
		kfree(node);
		node = temp;
	}
}

static struct skiplist_node *create_node_of_lvl(sector_t key, sector_t data,
							unsigned int lvl)
{
	struct skiplist_node *last;
	struct skiplist_node *curr;
	unsigned int curr_lvl;

	last = NULL;
	for (curr_lvl = 0; curr_lvl <= lvl; ++curr_lvl) {
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
	return create_node_of_lvl(key, data, 0);
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

static int flip_coin(void)
{
	return get_random_u8() % 2;
}

static unsigned int get_random_lvl(unsigned int max) {
	unsigned int lvl = 0;

	while ((lvl < max) && flip_coin())
		lvl++;

	return lvl;
}

static struct skiplist_node *find_same_lvl_pred_soft(sector_t key,
				struct skiplist_node *seek_from)
{
	struct skiplist_node *curr;
	struct skiplist_node *found;

	found = NULL;
	curr = seek_from;
	while (curr && !found) {
		if (curr->next->key >= key)
			found = curr;
		else
			curr = curr->next;
	}

	return found;
}

static struct skiplist_node *find_pred_soft(sector_t key, struct skiplist *sl)
{
	struct skiplist_node *curr;
	struct skiplist_node *curr_seek_from;
	struct skiplist_node *found;

	found = NULL;
	curr = NULL;
	curr_seek_from = sl->head;
	while (!found) {
		curr = find_same_lvl_pred_soft(key, curr_seek_from);
		if (curr && curr->next->key >= key)
			found = curr;
		else if (curr && curr->lower)
			curr_seek_from = curr->lower;
		else
			return NULL;
	}

	return found;
}

static struct skiplist_node *find_pred_strict(sector_t key, struct skiplist *sl)
{
	struct skiplist_node *found;

	found = find_pred_soft(key, sl);
	if (!found || found->next->key != key)
		return NULL;

	return found;
}

struct skiplist_node *skiplist_find_node(sector_t key, struct skiplist *sl)
{
	struct skiplist_node *found;

	found = find_pred_strict(key, sl);
	if (!found)
		return NULL;
	
	return found->next;
}

static int move_head_and_tail_up(struct skiplist *sl, unsigned int lvls_up)
{
	struct skiplist_node *head_ext;
	struct skiplist_node *tail_ext;
	struct skiplist_node *curr;
	struct skiplist_node *temp;

	head_ext = create_node_of_lvl(HEAD_KEY, HEAD_DATA, lvls_up);
	tail_ext = create_node_of_lvl(TAIL_KEY, TAIL_DATA, lvls_up);
	if (!head_ext || !tail_ext)
		goto alloc_fail;

	curr = head_ext;
	temp = tail_ext;
	while (curr->lower && temp->lower) {
		curr->next = temp;
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

static int move_up_if_lvl_nex(struct skiplist *sl, unsigned int lvl)
{
	unsigned int diff;
	int ret;

	if (lvl <= sl->head_lvl || lvl > sl->max_lvl) {
		return 0;
	}

	diff = lvl - sl->head_lvl;
	ret = move_head_and_tail_up(sl, diff);
	if (ret)
		return ret;
	sl->head_lvl = lvl;

	return 0;
}

/*
 * does not work when trying to add existing key yet
 */
int skiplist_add(sector_t key, sector_t data, struct skiplist *sl)
{
	struct skiplist_node *curr;
	struct skiplist_node *new;
	unsigned int new_lvl;
	unsigned int curr_lvl;
	int ret;

	new_lvl = get_random_lvl(sl->max_lvl);
	new = create_node_of_lvl(key, data, new_lvl);
	if (!new)
		return -ENOMEM;

	ret = move_up_if_lvl_nex(sl, new_lvl);
	if (ret) {
		free_node_full(new);
		return ret;
	}
	curr = sl->head;
	curr_lvl = sl->head_lvl;

	while (curr) {
		curr = find_same_lvl_pred_soft(key, curr);
		if (!curr)
			return -EINVAL;

		if (curr_lvl <= new_lvl) {
			new->next = curr->next;
			curr->next = new;
			new = new->lower;
		}

		curr = curr->lower;
		curr_lvl--;
	}

	return 0;
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