#pragma once

struct list_node {
  struct list_node  *prev;
  struct list_node  *next;
};

void
list_init(struct list_node *node);
void
list_insert_before(struct list_node *node, struct list_node *anchor);
void
list_insert_after(struct list_node *node, struct list_node *anchor);
void
list_remove(struct list_node  *node);
