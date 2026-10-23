#ifndef LIST_H
#define LIST_H
#include <stddef.h>

typedef struct list_node {
    int key;
    struct list_node *next;
} list_node;

/* Adapted from data/seminars/pth_ll_rwl/pth_ll_rwl.c.
 * Synchronization belongs to the caller and covers the entire operation. */
int list_member(const list_node *head, int key);
/* Insert: 1 inserted, 0 already present, -1 allocation failed. */
int list_insert(list_node **head, int key);
int list_delete(list_node **head, int key);
int list_validate(const list_node *head, size_t *length);
void list_free(list_node **head);
#endif
