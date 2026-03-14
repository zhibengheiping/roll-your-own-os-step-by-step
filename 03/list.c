#include "list.h"

void
list_init(struct list_node *node) {
  node->prev = node;
  node->next = node;
}

void
list_insert_before(struct list_node *node, struct list_node *anchor) {
  list_insert_after(anchor, node->prev);
}

void
list_insert_after(struct list_node *node, struct list_node *anchor) {
  anchor->next->prev = node->prev;
  node->prev->next = anchor->next;
  node->prev = anchor;
  anchor->next = node;
}

void
list_remove(struct list_node  *node) {
  node->prev->next = node->next;
  node->next->prev = node->prev;
  list_init(node);
}
