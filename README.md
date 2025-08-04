#  Redis Clone in C++

A minimal Redis server built from scratch using C++. No frameworks, no interpreters, just raw socket programming, data structures, and systems-level engineering.

## Supported Commands (so far)
```
SET key value
GET key
DEL key
```

Supports **pipelined requests**: fire off multiple commands without waiting for replies in between. Efficient and clean.

---

## Event Loop based: Why No Threads?

Instead of multithreading, this Redis clone uses a custom **event loop** built with `poll()`. Here's why:

-  **Threaded IO doesn't scale well** for high-connection servers: [C10k problem](https://en.wikipedia.org/wiki/C10k_problem)
-  **Memory-heavy**: Each thread = stack space.
-  **Short-lived clients** (like PHP apps) = thread churn = CPU + latency hit.
-  **Multi-process/threading** makes worst-case latency worse.
-  Modern high-perf servers (like Nginx & Redis) use **event loops**.

### Bottom Line:  
**Event loop > Threads** when scaling to thousands of concurrent connections.

### Side note:
A production implementation should replace poll() with epoll() as it stores the FDs in the kernel so that the FD args aren't copied in every itereation. Since this is a small scale implementation, I took the liberty to just use poll().

---

## Why C++?

C++ gives low-level control like C, **plus**:

- `std::string`, `std::vector` = fewer memory bugs cuz we don't wanna cleanup the custom Dynamic buffers/arrays as they would be passed around a lot.
- Zero Python/Node-style abstraction overhead
- Built a **custom dynamic buffer** system inspired by vectors: minimal, efficient, tailored to RESP protocol parsing.

---

## Hashmaps ≠ STL

I didn't use `std::unordered_map`. Here's why:

### STL Isn't Built for Real-Time Systems

- STL is **throughput-optimized**, not **latency-safe**.
- Worst-case: an insert triggers **O(N) resize**, pausing everything cause who needs that :O

### Solution: Progressive Resizing

- On resize, keys are migrated **gradually**, not all at once.
- Uses `calloc()` over `malloc()+memset()` to:
  - Leverage **mmap-backed lazy-zeroing**
  - Avoid O(N) latency when initializing large memory chunks

### Collision Strategy: Chaining (Not Open Addressing)

- I used **linked lists per bucket**
- Benefits:
  - Stable references (no pointer invalidation during resize)
  - O(1) inserts and deletes
  - Easier to build using **intrusive data structures**

---

## Intrusive Data Structures

Forget templates and void pointers.

I embeded structure nodes **inside** our data and use a `container_of()` macro (inspired by the Linux kernel) to access data from within a node.

### Advantages:
- No heap allocations for structure nodes
- Better cache locality
- Enables **multi-indexed data structures**
- Shares nodes across collections like Sorted Sets (score + name)

---

##  In Progress

-  **AVL Trees** (for balanced Sorted Sets)
-  **Timers & Timeouts**
-  **TTL & Expiration** (`EXPIRE`, `PERSIST`)

---

##  Run It
```
g++ -Wall -Wextra -O2 -g server.cpp hashtable.cpp murmurhash.cpp -o server
./server
g++ -Wall -Wextra -O2 -g 03_client.cpp -o client
./client
```

## Resources
[Redis Internals](https://github.com/redis/redis)

[Beej’s Guide to Network Programming Using Internet Sockets by Brian “Beej” Hall](https://beej.us/guide/bgnet/) along with man7 pages for detailed reference.

[Network programming, data structures, and low-level C by James Smith](https://build-your-own.org/redis/#table-of-contents)
