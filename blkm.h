struct skiplist_node;
struct skiplist;

struct skiplist *skiplist_init(void);
struct skiplist_node *skiplist_find_node(unsigned long key, struct skiplist *sl);
int skiplist_add(unsigned long key, unsigned long data, struct skiplist *sl);
int skiplist_del(unsigned long key, struct skiplist *sl);
