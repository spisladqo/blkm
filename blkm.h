#include <linux/blkdev.h>
#include <linux/limits.h>

struct skiplist_node {
	struct skiplist_node *next;
	struct skiplist_node *lower;
	sector_t key;
	sector_t data;
};

struct skiplist {
	struct skiplist_node *head;
	int head_lvl;
	int max_lvl;
};

struct skiplist *skiplist_init(void);
struct skiplist_node *skiplist_find_node(sector_t key, struct skiplist *sl);
struct skiplist_node *skiplist_add(sector_t key, sector_t data,
					struct skiplist *sl);
void skiplist_free(struct skiplist *sl);
void skiplist_print(struct skiplist *sl);