#include <stddef.h>
#include <stdint.h>

struct HNode {
  HNode* next = NULL;
  uint64_t hashed_key = 0;
};

struct HTab {
  HNode **tab = NULL;
  size_t size = 0;
  size_t mask = 0;
};

struct HMap {
  HTab curr_table;
  HTab old_table;
  size_t migration_idx = 0;
};

struct HNode* hm_lookup(HMap* map, HNode* node, bool (*eq)(HNode*, HNode*));
struct HNode* hm_insert(HMap* map, HNode* node, bool (*eq)(HNode*, HNode*));
struct HNode* hm_delete(HMap* map, HNode* node, bool (*eq)(HNode*, HNode*));
struct HMap* map();
