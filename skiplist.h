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
	struct skiplist_node *tail;

	sl = kzalloc(sizeof(*sl), GFP_KERNEL);
	head = skiplist_create_node(0l, NULL);
	tail = skiplist_create_node(INF, NULL);
	if (!sl || !head || !tail)
		goto fail;

	sl->head = head;
	sl->size = 0;
	sl->max_lvl = 1;
	head->next = tail;

	return sl;

fail:
	kfree(sl);
	kfree(head);
	kfree(tail);
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

static struct skiplist_node *skiplist_find_node(long key,
					struct skiplist *sl)
{
	struct skiplist_node *curr;
	struct skiplist_node *curr_seek_from;
	struct skiplist_node *found;

	found = NULL;
	curr = NULL;
	curr_seek_from = sl->head;
	while (!found) {
		curr = skiplist_find_first_before(key, curr_seek_from);
		if (curr && curr->next->key == key)
			found = curr->next;
		else if (curr && curr->lower)
			curr_seek_from = curr->lower;
		else
			return NULL;
	}

	return found;
}