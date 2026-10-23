#include "list.h"
#include <stdlib.h>

int list_member(const list_node *head, int key) {
    while (head && head->key < key) head = head->next;
    return head && head->key == key;
}

int list_insert(list_node **head, int key) {
    while (*head && (*head)->key < key) head = &(*head)->next;
    if (*head && (*head)->key == key) return 0;
    list_node *node = malloc(sizeof(*node));
    if (!node) return -1;
    node->key = key;
    node->next = *head;
    *head = node;
    return 1;
}

int list_delete(list_node **head, int key) {
    while (*head && (*head)->key < key) head = &(*head)->next;
    if (!*head || (*head)->key != key) return 0;
    list_node *node = *head;
    *head = node->next;
    free(node);
    return 1;
}

int list_validate(const list_node *head, size_t *length) {
    const list_node *slow = head, *fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) return 0;
    }
    *length = 0;
    while (head) {
        if (head->next && head->key >= head->next->key) return 0;
        (*length)++;
        head = head->next;
    }
    return 1;
}

void list_free(list_node **head) {
    while (*head) {
        list_node *node = *head;
        *head = node->next;
        free(node);
    }
}
