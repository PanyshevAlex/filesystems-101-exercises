#include <solution.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define ISLEAF(n) ((n)->subs == NULL)
#define ARRAY_MOVE(a,i,b,l) memmove((a)+(i),(a)+(b),sizeof(*a)*((l)-(b)))
#define ARRAY_INSERT(a,b,l,n) do {ARRAY_MOVE(a,(b)+1,b,l); (a)[b]=(n);} while(0)
#define TYPE_CMP(a, b) (*(a) > *(b) ? 1 : (*(a) == *(b)) ? 0 : -1)
#define BTREE_ITER_MAX_DEPTH	(sizeof(long) * 8)


struct btree_node {
    int size;
    int min_degree;
    struct btree_node **subs;
    int vals[];
};

struct btree
{
    /* implement me */
    int min_degree;
    struct btree_node *root;
};

static inline struct btree_node *btree_node_create(unsigned int min_degree, int is_leaf)
{
    struct btree_node **subs = NULL;
    struct btree_node *node = calloc(1, sizeof(struct btree_node) + (2 * min_degree - 1) * sizeof(int));
    if (!is_leaf)
        subs = (struct btree_node **) calloc(2 * min_degree, sizeof(struct btree_node *));

    if (!node || !(is_leaf || subs)) {
        free(node);
        free(subs);
        return NULL;
    }

    node->subs = subs;
    node->min_degree = min_degree;
    node->size = 0;

    return node;
}

struct btree* btree_alloc(unsigned int L)
{
    struct btree *btree = calloc(1, sizeof(struct btree));
    if (btree) 
    {
        btree->min_degree = L+1;
        btree->root = btree_node_create(btree->min_degree, 1);
    }
    return btree;
}

static inline void btree_node_destroy(struct btree_node *node)
{
	free(node->subs);
	free(node);
}

static void __btree_destroy(struct btree_node *n)
{
    if (n) 
    {
        if (!ISLEAF(n)) 
        {
            int i = 0;
            for (i = 0; i < (int)n->size + 1; i++)
                __btree_destroy(n->subs[i]);
        }
        btree_node_destroy(n);
    }
}

void btree_free(struct btree *t)
{
    if (t) {
        __btree_destroy(t->root);
        free(t);
    }
}

static int btree_node_search(int *arr, int len, int *n, int *ret)
{
	int m = 0, d = 0, cmp = 0;
	if (len > 16) {
		while (m < len) {
			d = (len + m) / 2;
			if ((cmp = TYPE_CMP(arr + d, n)) == 0) {
				*ret = d;
				return 1;
			}
			if (cmp < 0)
				m = d + 1;
			else
				len = d;
		}
	} else {
		for (m = 0; m < len; m++) {
			if ((cmp = TYPE_CMP(arr + m, n)) >= 0)
				break;
		}

		if (m < len && cmp == 0) {
			*ret = m;
			return 1;
		}
	}
	*ret = m;
	return 0;
}

static int btree_split_child(int t, struct btree_node *parent, int idx, struct btree_node *child)
{
	struct btree_node *n = btree_node_create(t, ISLEAF(child));
	if (!n)
		return -1;


	memcpy(n->vals, child->vals + t, sizeof(*n->vals) * (t - 1));
	if (n->subs)
		memcpy(n->subs, child->subs + t, sizeof(*n->subs) * t);
	n->size = t - 1;
	child->size = t - 1;
	ARRAY_INSERT(parent->vals, idx, parent->size, child->vals[t - 1]);
	ARRAY_INSERT(parent->subs, idx + 1, parent->size + 1, n);
	parent->size++;

	return 0;
}

void btree_insert(struct btree *t, int x)
{
    struct btree_node *child = NULL, *node = t->root;
	int tmd = t->min_degree, idx;

	if ((int)node->size == (2 * tmd - 1)) {
		if ((child = btree_node_create(tmd, 0)) == NULL)
			return;

		child->subs[0] = node, node = t->root = child;
		if (btree_split_child(tmd, child, 0, child->subs[0]) < 0)
			return;
	}
	for (;;) {
		if (btree_node_search(node->vals, node->size, &x, &idx))
			return;
		if (ISLEAF(node)) {
			ARRAY_INSERT(node->vals, idx, node->size, x);
			node->size++;
			break;
		}

		if ((int)node->subs[idx]->size == (2 * tmd - 1)) {
			if (btree_split_child(tmd, node, idx, node->subs[idx]) < 0)
				return;
			if (TYPE_CMP(&x, node->vals + idx) > 0)
				idx++;
		}

		node = node->subs[idx];
	}
}

static inline int *__btree_max(struct btree_node *subtree)
{
	assert(subtree && subtree->size > 0);
	for (;;) {
		if(ISLEAF(subtree) || !subtree->subs[subtree->size])
			return &subtree->vals[subtree->size - 1];
		subtree = subtree->subs[subtree->size];
	}
}

static inline int *__btree_min(struct btree_node *subtree)
{
	assert(subtree && subtree->size > 0);
	for (;;) {
		if(ISLEAF(subtree) || !subtree->subs[0])
			return subtree->vals;
		subtree = subtree->subs[0];
	}
}

static inline struct btree_node *btree_merge_siblings(struct btree *btree, struct btree_node *parent, int idx)
{
	int t = btree->min_degree;
	struct btree_node *n1, *n2;

	if (idx == (int)parent->size)
		idx--;
	n1 = parent->subs[idx];
	n2 = parent->subs[idx + 1];

	assert((int)n1->size + (int)n2->size + 1 < 2 * t);

	memcpy(n1->vals + t, n2->vals, sizeof(*n1->vals) * (t - 1));
	if (n1->subs)
		memcpy(n1->subs + t, n2->subs, sizeof(*n1->subs) * t);
	n1->vals[t - 1] = parent->vals[idx];
	n1->size += n2->size + 1;

	ARRAY_MOVE(parent->vals, idx, idx + 1, parent->size);
	ARRAY_MOVE(parent->subs, idx + 1, idx + 2, parent->size + 1);
	parent->subs[idx] = n1;
	parent->size--;

	if (parent->size == 0 && btree->root == parent) {
		btree_node_destroy(parent);
		btree->root = n1;
	}

	btree_node_destroy(n2);
	return n1;
}

static void move_left_to_right(struct btree_node *parent, int idx)
{
	struct btree_node *left = parent->subs[idx], *right = parent->subs[idx + 1];
	ARRAY_MOVE(right->vals, 1, 0, right->size);
	right->vals[0] = parent->vals[idx];
	parent->vals[idx] = left->vals[left->size - 1];

	if (right->subs) {
		ARRAY_MOVE(right->subs, 1, 0, right->size + 1);
		right->subs[0] = left->subs[left->size];
	}

	left->size--;
	right->size++;
}

static void move_right_to_left(struct btree_node *parent, int idx)
{
	struct btree_node *left = parent->subs[idx], *right = parent->subs[idx + 1];
	left->vals[left->size] = parent->vals[idx];
	parent->vals[idx] = right->vals[0];
	ARRAY_MOVE(right->vals, 0, 1, right->size);

	if (right->subs) {
		left->subs[left->size + 1] = right->subs[0];
		ARRAY_MOVE(right->subs, 0, 1, right->size + 1);
	}

	right->size--;
	left->size++;
}

static int __btree_delete(struct btree *btree, struct btree_node *sub, int *key)
{
	int idx, t = btree->min_degree;

	for (;;) {
		struct btree_node *parent;
		if (btree_node_search(sub->vals, sub->size, key, &idx))
			break;
		if (ISLEAF(sub))
			return -1;

		parent = sub, sub = sub->subs[idx];
		assert(sub != NULL);
		if ((int)sub->size > t - 1)
			continue;

		if (idx < (int)parent->size && (int)parent->subs[idx + 1]->size > t - 1)
			move_right_to_left(parent, idx);
		else if (idx > 0 && (int)parent->subs[idx - 1]->size > t - 1)
			move_left_to_right(parent, idx - 1);
		else
			sub = btree_merge_siblings(btree, parent, idx);
	}
LOOP:
	if (ISLEAF(sub)) {
		assert(sub == btree->root || (int)sub->size > t - 1);
		ARRAY_MOVE(sub->vals, idx, idx + 1, sub->size);
		sub->size--;
	} else {
		if (sub->subs[idx]->size > t - 1) {
			sub->vals[idx] = *__btree_max(sub->subs[idx]);
			__btree_delete(btree, sub->subs[idx], sub->vals + idx);
		} else if (sub->subs[idx + 1]->size > t - 1) {
			sub->vals[idx] = *__btree_min(sub->subs[idx + 1]);
			__btree_delete(btree, sub->subs[idx + 1], sub->vals + idx);
		} else {
			assert(sub->subs[idx]->size==t-1 && sub->subs[idx+1]->size == t-1);
			sub = btree_merge_siblings(btree, sub, idx);
			idx = t - 1;
			goto LOOP;
		}
	}

	return 0;
}

void btree_delete(struct btree *t, int x)
{
	__btree_delete(t, t->root, &x);
}

bool btree_contains(struct btree *t, int x)
{
	struct btree_node *node = t->root;
	for (;;) {
		int i = 0;
		if (btree_node_search(node->vals, node->size, &x, &i))
			return true;
		if (ISLEAF(node))
			return false;
		node = node->subs[i];
	}
    return false;
}

struct btree_iter
{
    int size;
    int	idx[BTREE_ITER_MAX_DEPTH];
    struct btree_node *sub[BTREE_ITER_MAX_DEPTH];
};

int *btree_min(struct btree *t)
{
	return __btree_min(t->root);
}

struct btree_iter* btree_iter_start(struct btree *t)
{
    (void) t;
    int *v = btree_min(t);
    struct btree_node *node = t->root;
    struct btree_iter *iter = malloc(sizeof(struct btree_iter));
    int n = 1;
	iter->size = -1;
	for (;;) {
		int idx = 0;
		int r = btree_node_search(node->vals, node->size, v, &idx);

		iter->size++;
		assert(iter->size < (int)BTREE_ITER_MAX_DEPTH);
		iter->sub[iter->size] = node;
		iter->idx[iter->size] = idx + !!(r && !n);

		if (ISLEAF(node) || r)
			break;
		node = node->subs[idx];
	}
    return iter;
}

void btree_iter_end(struct btree_iter *i)
{
    free(i);
}

static inline int *_btree_next(struct btree_iter *iter)
{
	struct btree_node *node = iter->sub[iter->size];
	int idx      = iter->idx[iter->size];

	iter->idx[iter->size]++;
	if (!ISLEAF(node) && idx < (int)node->size) {
		struct btree_node *sub = node->subs[idx + 1];
		for (;;) {
			iter->size++;
			assert(iter->size < (int)BTREE_ITER_MAX_DEPTH);
			iter->sub[iter->size] = sub;
			iter->idx[iter->size] = 0;
			if (ISLEAF(sub))
				break;
			sub = sub->subs[0];
		}
	}

	return idx < (int)node->size ? node->vals + idx : NULL;
}

static inline int *_btree_iter(struct btree_iter *iter)
{
	for (; iter->size >= 0; iter->size--) {
		int *r =_btree_next(iter);
		if (r)
			return r;
	}

	return NULL;
}

bool btree_iter_next(struct btree_iter *i, int *x)
{
	int *res = _btree_iter(i);
    if (!res)
        return false;
    *x = *res;
    return true;
}