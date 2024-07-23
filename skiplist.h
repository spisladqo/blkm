// SPDX-License-Identifier: GPL-2.0-only
/*
 * Skiplist implementation by: Daniel Vlasenco
 */

#include <linux/blkdev.h>
#include <include/vdso/limits.h>

#define INF LONG_MAX
#define NEG_INF LONG_MIN

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


static struct skiplist skiplist_init(void)
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