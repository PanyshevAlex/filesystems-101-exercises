#include <solution.h>
#include <stdlib.h>
#include <stdio.h>

struct btree
{
	int *value;
	struct btree *left, *right;
};

struct btree* btree_alloc(unsigned int L)
{
	if (L == 0)
		return NULL;
	struct btree *tree = (struct btree *)malloc(sizeof(struct btree));
	tree->value = NULL;
	tree->left = btree_alloc((L-1)/2);
	tree->right = btree_alloc((L-1)/2);
	return tree;
}

void btree_free(struct btree *t)
{
	if (t == NULL)
		return;
	btree_free(t->left);
	btree_free(t->right);
	free(t->value);
	free(t);
}

void btree_insert(struct btree *t, int x)
{
	if (t->value == NULL)
	{
		int *val = (int *)malloc(sizeof(int));
		*val = x;
		t->value = val;
		return;
	}
	if (x > *(t->value))
	{
		if (t->right == NULL)
		{
			struct btree *new_right_node = (struct btree *)malloc(sizeof(struct btree));
			t->right = new_right_node;
			int *val = (int *)malloc(sizeof(int));
			*val = x;
			new_right_node->value = val;
			return;
		}
		else
		{
			btree_insert(t->right, x);
			return;
		}
	}
	if (x < *(t->value))
	{
		if (t->right == NULL)
		{
			struct btree *new_left_node = (struct btree *)malloc(sizeof(struct btree));
			t->right = new_left_node;
			int *val = (int *)malloc(sizeof(int));
			*val = x;
			new_left_node->value = val;
			return;
		}
		else
		{
			btree_insert(t->left, x);
			return;
		}
	}
}

int *btree_min(struct btree *t)
{
	if (t->left == NULL)
		return t->value;
	return btree_min(t->left);
}

void print_tree(struct btree *t)
{
	if (t == NULL)
		return;
	print_tree(t->left);
	printf("%d ", *t->value);
	print_tree(t->right);
}
void btree_delete(struct btree *t, int x)
{
	(void) t;
	(void) x;
	if (t == NULL)
		return;
	if (x < *(t->value))
		btree_delete(t->left, x);
	if (x > *(t->value))
		btree_delete(t->right, x);
	if (x == *(t->value))
	{
		if (t->left != NULL && t->right != NULL)
		{
			free(t->value);
			t->value = btree_min(t->right);
			btree_delete(t->right, *t->value);
		}
		else if (t->left != NULL)
		{
			struct btree *left_tmp = t->left;
			free(t->value);
			t->value = t->left->value;
			t->left = t->left->left;
			t->right = t->left->right;
			free(left_tmp);
		}
		else if (t->right != NULL)
		{
			struct btree *right_tmp = t->right;
			free(t->value);
			t->value = t->right->value;
			t->left = t->right->left;
			t->right = t->right->right;
			free(right_tmp);
		}
		else
		{
			free(t->value);
		}
	}
}

bool btree_contains(struct btree *t, int x)
{
	if (t == NULL)
		return false;
	if (t->value == NULL)
		return false;
	if (*t->value == x)
		return true;
	if (x > *(t->value)) // go right
		return btree_contains(t->right, x);
	else // go left
		return btree_contains(t->left, x);
}

struct btree_iter
{
	struct btree **memory;
	int memory_size;
	int position;
};

struct btree_iter* btree_iter_start(struct btree *t)
{
	(void) t;
	struct btree_iter *iter = (struct btree_iter *)malloc(sizeof(struct btree_iter));
	iter->memory_size = 10;
	iter->memory = (struct btree **)malloc(iter->memory_size*sizeof(struct btree*));
	iter->position = 0;
	iter->memory[0] = t;
	return iter;
}

void btree_iter_end(struct btree_iter *i)
{
	free(i->memory);
	free(i);
}

bool btree_iter_next(struct btree_iter *i, int *x)
{
	if (i->position == -1)
		return false;
	while (i->memory[i->position]->left != NULL)
	{
		if (i->position >= i->memory_size)
		{
			i->memory = (struct btree **)realloc(i->memory, 2 * i->memory_size * sizeof(struct btree*));
			i->memory_size *= 2;
		}
		i->memory[i->position+1] = i->memory[i->position]->left;
		i->position++;
	}
	*x = *(i->memory[i->position]->value);
	if (i->memory[i->position]->right != NULL)
		i->memory[i->position] = i->memory[i->position]->right;
	else
	{
		i->memory[i->position] = NULL;
		i->position--;
	}
	return true;
}
