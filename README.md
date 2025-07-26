# Redis Clone in C – Codecrafters Challenge

Welcome! This repo contains my solutions for the [Codecrafters Redis](https://codecrafters.io/challenges/redis) challenge.
The goal: build a minimal Redis server from scratch in C, with real networking, RESP protocol parsing, and support for strings, lists, streams, blocking commands, and transactions.

---

## What This Project Is About

This project is a deep dive into how Redis works under the hood.
By building everything from the socket layer up, I learned about:

- **Efficient event loops:** Using `epoll` to handle multiple clients concurrently.
- **RESP protocol:** Parsing arrays, bulk strings, and handling all edge cases for protocol compliance.
- **In-memory data structures:** Hash maps for keys, double-linked lists for lists, and custom stream structures.
- **Blocking commands:** Implementing BLPOP and XREAD BLOCK with proper client waiting, notification, and timeouts.
- **Streams:** Supporting XADD (with auto and partial ID generation), XRANGE, and XREAD (including `$` for "only new entries").
- **Transactions:** Full support for MULTI/EXEC, with per-client command queues and correct isolation.
- **Per-client state:** Each client is tracked with its own struct, supporting transaction state, queued commands, and proper cleanup on disconnect.
- **Memory management:** Careful allocation, cleanup, and defensive programming to avoid leaks and crashes.

All features are tested using the Codecrafters test suite and verified with `redis-cli`.

---

## Features Implemented

Here’s what’s working so far:

- **Networking:** Handles multiple clients over TCP (port 6379) using `epoll` for scalability.
- **RESP Parsing:** Full support for arrays, bulk strings, and correct error/null handling.
- **Key-Value Store:** Hash map with support for:
  - **Strings:** `SET`, `GET`, `TYPE`, `INCR` (with integer validation).
  - **Lists:** `RPUSH`, `LPUSH`, `LRANGE`, `LLEN`, `LPOP`, `BLPOP` (with FIFO blocking and timeouts).
  - **Streams:** `XADD` (with correct ID handling), `XRANGE`, `XREAD` (multi-stream, blocking, `$` support).
- **Blocking Commands:**
  - **BLPOP:** Blocks clients, serves them in FIFO order, supports timeouts.
  - **XREAD BLOCK:** Blocks on one or more streams, supports `$` for "wait for new entries", and handles timeouts.
- **Transactions:**
  - **MULTI/EXEC:** Per-client command queues, correct queuing and execution, isolation between clients, and proper cleanup on EXEC/DISCARD or disconnect.
  - **Queued command structure:** Each client maintains its own queue of commands and arguments, ensuring correct transactional behavior.
- **Timeouts:** Dynamic timeout calculation for efficient event loop wakeups.
- **Concurrency:** Robust handling of multiple clients, proper cleanup on disconnect.
- **Defensive Programming:** Protocol compliance, error handling, and edge case coverage.
- **Memory Management:** All data structures are carefully allocated and freed.

---

## How to Run and Test

1. **Build the server:**
   ```sh
   gcc -o redis_server src/main.c src/hashmap.c src/double_linked_list.c src/stream.c src/blocking_wait.c
   ```
2. **Start the server:**
   ```sh
   ./redis_server
   ```
3. **Connect with redis-cli:**
   ```sh
   redis-cli -p 6379
   ```
4. **Try out commands:**
   ```
   # Strings
   SET fruit apple
   GET fruit
   INCR counter
   TYPE fruit
   DEL fruit

   # Lists
   RPUSH mylist a b c
   LRANGE mylist 0 -1
   BLPOP mylist 5

   # Streams
   XADD mystream * temperature 22
   XRANGE mystream - +
   XREAD BLOCK 1000 STREAMS mystream $

   # Blocking
   BLPOP mylist 2
   XREAD BLOCK 5000 STREAMS mystream $

   # Transactions
   MULTI
   SET a 1
   INCR a
   EXEC
   DISCARD
   ```

---

## Notes & Credits

- The Codecrafters challenge and test suite are provided by [Codecrafters](https://codecrafters.io/).
- My contributions here are the C implementation and this documentation.
- If you’re learning systems programming, I highly recommend trying the challenge yourself before looking at solutions!

---

## License Info

My C code and documentation in this repo are released under the [MIT License](LICENSE).

Any test scripts or challenge materials from Codecrafters are © Codecrafters and included here for educational purposes only.

---

Feel free to fork this repo or use it for reference, but remember—building it yourself is the best way to learn!
