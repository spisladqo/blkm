// SPDX-License-Identifier: GPL-2.0-only
/*
 * Skiplist implementation by: Daniel Vlasenco
 */

#include <linux/blkdev.h>
#include <include/vdso/limits.h>

#define HEAD_KEY 0UL
#define HEAD_DATA 0UL
#define TAIL_KEY ULONG_MAX
#define TAIL_DATA ULONG_MAX

static struct skiplist_node {
	struct skiplist_node *next;
	struct skiplist_node *lower;
	unsigned long key;
	unsigned long data;
};

static struct skiplist {
	struct skiplist_node *head;
	unsigned int head_lvl;
	unsigned int max_lvl;
};

static void skiplist_free_node_full(struct skiplist_node *node)
{
	struct skiplist_node *temp;
	while (node) {
		temp = node->lower;
		kfree(node);
		node = temp;
	}
}

static struct skiplist_node *skiplist_create_node_lvl(unsigned long key,
				unsigned long data, unsigned int lvl)
{
	struct skiplist_node *last;
	struct skiplist_node *curr;
	struct skiplist_node *temp;
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
	skiplist_free_node_full(curr);

	return NULL;
}

static struct skiplist_node *skiplist_create_node(unsigned long key,
						unsigned long data)
{
	return skiplist_create_node_lvl(key, data, 0);
}

static struct skiplist *skiplist_init(void)
{
	struct skiplist *sl;
	struct skiplist_node *head;
	struct skiplist_node *tail;

	sl = kzalloc(sizeof(*sl), GFP_KERNEL);
	head = skiplist_create_node(HEAD_KEY, HEAD_DATA);
	tail = skiplist_create_node(TAIL_KEY, TAIL_DATA);
	if (!sl || !head || !tail)
		goto alloc_fail;

	sl->head = head;
	sl->head_lvl = 0;
	sl->max_lvl = 24;
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

static struct skiplist_node *skiplist_find_first_before_key(unsigned long key,
					struct skiplist_node *seek_from)
{
	struct skiplist_node *curr;
	struct skiplist_node *found;

	found = NULL;
	curr = seek_from;
	while (curr->key < key && !found) {
		if (curr->next->key >= key)
			found = curr;
		else
			curr = curr->next;
	}

	return found;
}

static struct skiplist_node *skiplist_find_node(unsigned long key,
						struct skiplist *sl)
{
	struct skiplist_node *curr;
	struct skiplist_node *curr_seek_from;
	struct skiplist_node *found;

	found = NULL;
	curr = NULL;
	curr_seek_from = sl->head;
	while (!found) {
		curr = skiplist_find_first_before_key(key, curr_seek_from);
		if (curr && curr->next->key == key)
			found = curr->next;
		else if (curr && curr->lower)
			curr_seek_from = curr->lower;
		else
			return NULL;
	}

	return found;
}