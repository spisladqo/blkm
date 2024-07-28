#include <linux/blkdev.h>

struct skiplist_node;
struct skiplist;

struct skiplist *skiplist_init(void);
struct skiplist_node *skiplist_find_node(sector_t key, struct skiplist *sl);
int skiplist_add(sector_t key, sector_t data, struct skiplist *sl);
void skiplist_free(struct skiplist *sl);