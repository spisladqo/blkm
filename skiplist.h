// SPDX-License-Identifier: GPL-2.0-only
/*
 * Skiplist implementation by: Daniel Vlasenco
 */

#include <linux/blkdev.h>
#include <include/vdso/limits.h>

#define INF ULONG_MAX

static struct skiplist_node {
	struct skiplist_node *next;
	struct skiplist_node *lower;
	long key;
	long data;
};

static struct skiplist {
	struct skiplist_node *head;
	int size;
	int max_lvl;
};

static struct skiplist_node *skiplist_create_node(long key, long data)
{
	struct skiplist_node *node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;

	node->key = key;
	node->data = data;

	return node;
}


static struct skiplist *skiplist_init(void)
{
	struct skiplist *sl;
	struct skiplist_node *head;

	sl = kzalloc(sizeof(*sl), GFP_KERNEL);
	head = skiplist_create_node(NEG_INF, NULL);
	if (!sl || !head)
		goto fail;
	sl->head = head;
	sl->size = 0;
	sl->max_lvl = 1;

	return sl;

fail:
	kfree(sl);
	kfree(head);
	return NULL;
}


static int flip_coin(void)
{
	return get_random_u8() % 2;
}

static int get_random_lvl(int limit) {
	int lvl = 0;

	while ((lvl < limit) && flip_coin())
		lvl++;

	return lvl;
}

static struct skiplist_node *skiplist_find_first_before(long key,
				struct skiplist_node *curr_seek_from)
{
	struct skiplist_node *curr;
	struct skiplist_node *pred;

	pred = NULL;
	curr = curr_seek_from;
	while (curr && curr->key < key) {
		if (!curr->next || curr->next->key >= key)
			pred = curr;

		curr = curr->next;
	}

	return pred;
}

static struct skiplist_node *skiplist_find_node(long key,
					struct skiplist *sl)
{
	struct skiplist_node *curr_seek_from;
	struct skiplist_node *curr_pred;
	struct skiplist_node *ret;

	ret = NULL;
	curr_seek_from = sl->head;
	while (curr_seek_from) {
		curr_pred = skiplist_find_first_before(key, curr_seek_from);
		WARN_ON(!curr_pred);
		if (curr_pred->next && curr_pred->next->key == key)
			ret = curr_pred->next;

		curr_seek_from = curr_pred->lower;
	}

	return ret;
}