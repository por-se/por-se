#ifndef KPR_LIST_TYPES
#define KPR_LIST_TYPES

typedef struct {
  void* tail;
  void* head;
  unsigned long size;
} kpr_list;

#define KPR_LIST_INITIALIZER { NULL, NULL, 0 }

typedef struct kpr_list_node {
  struct kpr_list_node* prev;
  struct kpr_list_node* next;
  void* data;
} kpr_list_node;

typedef struct {
  kpr_list_node* cur;
  kpr_list_node* next;
} kpr_list_iterator;

#endif
