#include "hashtable.h"
#include <stdlib.h>
#include <assert.h>

static const double max_lf = 0.6;

void create_table(HTab* table, size_t size) {
  table->tab = (HNode**)calloc(size, sizeof(HNode*));
  table->mask = size - 1;
  table->size = 0;
}//use multiple of 2

struct HMap* map() {
  HMap* map = new HMap();
  return map;
}

struct HNode* h_lookup(HTab* table, HNode* key, bool(*eq)(HNode*, HNode*)) {
  if(!table->tab) return NULL;

  size_t idx = key->hashed_key & table->mask;
  struct HNode* head = table->tab[idx];

  while(head) {
    if(key->hashed_key != head->hashed_key) {
      head=head->next;
      continue;
    }
    if(eq(key, head)) return head;
    head = head->next;
  }
  return NULL;
}

struct HNode* h_delete(HTab* table, HNode* key, bool(*eq)(HNode*, HNode*)) {
  if(!table->tab) return 0;

  size_t idx = key->hashed_key & table->mask;
  struct HNode** curr = &table->tab[idx];

  while(*curr) {
    HNode* node = *curr;
    if(node->hashed_key == key->hashed_key && eq(key, node)) {
      *curr = node->next;
      table->size--;
      return node;
    }
    curr = &node->next;
  }

  return NULL;
}

void h_insert(HTab* table, HNode* node) {
  size_t pos = node->hashed_key & table->mask;
  struct HNode *next = table->tab[pos];
  node->next = next;
  table->tab[pos] = node;
  table->size++;
}

size_t work_per_cycle = 128;

void resize(HMap* map) {
  if(!map->old_table.tab) {
    return;
  }

  size_t work_done = 0;
  HNode **from = map->old_table.tab + map->migration_idx;

  while(work_done < work_per_cycle && map->old_table.size > 0) {
    if(!*from) {
      map->migration_idx++;
      from = map->old_table.tab + map->migration_idx;
      continue;
    }
    HNode* node = *from;
    *from = node->next;
    map->old_table.size--;
    h_insert(&map->curr_table, node);
    work_done++;
  }
  
  if (map->old_table.tab && map->old_table.size == 0) {
    free(map->old_table.tab);
    map->old_table = HTab{};    // reset to empty
    map->migration_idx = 0;
  }
}

void trigger_resize(HMap* map, size_t size) {
  assert(map->old_table.tab == NULL);
  map->old_table = map->curr_table;
  create_table(&map->curr_table, size);
  map->migration_idx = 0;
}

struct HNode* hm_lookup(HMap* map, HNode* node, bool (*eq)(HNode*, HNode*)) {
  resize(map);

  if(HNode* fetched_node = h_lookup(&map->curr_table, node, eq)) return fetched_node;
  
  return h_lookup(&map->old_table, node, eq);
}

struct HNode* hm_delete(HMap* map, HNode* node, bool (*eq)(HNode*, HNode*)) {
  if(HNode* del_node = h_delete(&map->curr_table, node, eq)) return del_node;
  return h_delete(&map->old_table, node, eq);
}

struct HNode* hm_insert(HMap* map, HNode* node, bool (*eq)(HNode*, HNode*)) {
  if(!map->curr_table.tab) {
    create_table(&map->curr_table, 16);
  }

  HNode* replaced = h_delete(&map->curr_table, node, eq);

  if (!replaced) {
    replaced = h_delete(&map->old_table, node, eq);
  }

  h_insert(&map->curr_table, node);

  if(!map->old_table.tab) {
    size_t size = map->curr_table.size;
    size_t threshold = max_lf*(map->curr_table.mask+1);
    if(size >= threshold) trigger_resize(map, 2*size);
  }
  resize(map);
  return replaced;

 }
