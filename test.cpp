
#include <iostream>
#include <cstring>
#include "hashtable.h"
#include "murmurhash.h"

// Example Key-Value Node (inherits from HNode)
struct KVNode {
    HNode node;      // required for hashtable
    std::string key; // use std::string for convenience
    std::string val;
};

// Equality function for keys
bool kv_eq(HNode *a, HNode *b) {
    auto *ka = reinterpret_cast<KVNode*>(a);
    auto *kb = reinterpret_cast<KVNode*>(b);
    return ka->key == kb->key;
}

// Helper to create a KVNode
KVNode* make_kv(const std::string &key, const std::string &val) {
    KVNode *kv = new KVNode();
    kv->key = key;
    kv->val = val;
    kv->node.hashed_key = murmurhash64(key.c_str(), key.size(), 0x1234);
    kv->node.next = nullptr;
    return kv;
}

// Print helper
void print_lookup(HMap &map, const std::string &key) {
    KVNode temp;
    temp.key = key;
    temp.node.hashed_key = murmurhash64(key.c_str(), key.size(), 0x1234);

    HNode *found = hm_lookup(&map, &temp.node, kv_eq);
    if (found) {
        auto *kv = reinterpret_cast<KVNode*>(found);
        std::cout << "Found: " << kv->key << " => " << kv->val << "\n";
    } else {
        std::cout << "Key \"" << key << "\" not found.\n";
    }
}

int main() {
    HMap map{}; // initialize empty

    // Insert key-value pairs
    hm_insert(&map, &make_kv("apple", "red")->node, kv_eq);
    hm_insert(&map, &make_kv("banana", "yellow")->node, kv_eq);
    hm_insert(&map, &make_kv("grape", "purple")->node, kv_eq);

    // Lookup some keys
    print_lookup(map, "apple");
    print_lookup(map, "banana");
    print_lookup(map, "orange"); // doesn't exist

    // Delete a key
    KVNode temp;
    temp.key = "banana";
    temp.node.hashed_key = murmurhash64("banana", 6, 0x1234);

    HNode *deleted = hm_delete(&map, &temp.node, kv_eq);
    if (deleted) {
        auto *kv = reinterpret_cast<KVNode*>(deleted);
        std::cout << "Deleted: " << kv->key << " => " << kv->val << "\n";
        delete kv; // free memory
    }

    // Lookup after deletion
    print_lookup(map, "banana");

    // Cleanup: clear all remaining nodes manually
    // (in a real program you'd track them in a list or custom allocator)
    // For this test, we skip full cleanup since the process ends.

    return 0;
}
