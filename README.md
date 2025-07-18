# Redis Clone in C – Codecrafters Challenge

Welcome! This repo contains my solutions for the [Codecrafters Redis](https://codecrafters.io/challenges/redis) challenge. The goal is to build a minimal Redis server from scratch in C, implementing core features like TCP networking, RESP protocol parsing, and basic commands (`PING`, `ECHO`, `SET`, `GET`)—all with support for key expiry.

## What This Project Is About

This project is a hands-on exploration of how Redis works under the hood. By building everything from the socket layer up, I learned about:
- Handling multiple clients using `select()`
- Parsing the RESP (REdis Serialization Protocol) format
- Managing an in-memory key-value store
- Implementing TTL (time-to-live) for automatic key expiry

All features were tested using the official Codecrafters test suite and verified with `redis-cli`.

## Features Implemented

Here’s what’s working so far:

- **Networking:** Accepts multiple clients over TCP (port 6379)
- **RESP Parsing:** Handles arrays and bulk strings for command/argument extraction
- **Commands:**
  - `PING` – Replies with `PONG`
  - `ECHO` – Replies with the provided argument as a bulk string
  - `SET` – Stores a key-value pair, with optional PX (TTL in ms)
  - `GET` – Retrieves a value, returns null bulk string if expired or missing
- **Expiry:** Keys set with PX expire automatically after the specified time
- **Memory Management:** Uses a linked list for the key-value store, with careful allocation and cleanup

> For each command, you can connect using `redis-cli` or any RESP-compatible client and interact as you would with a real Redis server.

## How to Run and Test

1. **Build the server:**
   ```sh
   gcc -o redis_server src/main.c
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
   PING
   ECHO hello
   SET fruit apple
   GET fruit
   SET temp value px 100
   GET temp
   # Wait >100ms, then:
   GET temp
   ```

## Notes & Credits

- The Codecrafters challenge and test suite are provided by [Codecrafters](https://codecrafters.io/).
- My contributions here are the C implementation and this documentation.
- If you’re learning systems programming, I highly recommend trying the challenge yourself before looking at solutions!

## License Info

My C code and documentation in this repo are released under the [MIT License](LICENSE).

Any test scripts or challenge materials from Codecrafters are © Codecrafters and included here for educational purposes only.

---

Feel free to fork this repo or use it for reference, but remember—building it yourself is the best way to learn!


[![progress-banner](https://backend.codecrafters.io/progress/redis/54a03e86-4d1b-4867-9b13-512148e7edc8)](https://app.codecrafters.io/users/codecrafters-bot?r=2qF)

## Notes & Credits

- This project was inspired and guided by the [Codecrafters Redis challenge](https://codecrafters.io/challenges/redis).
- Huge thanks to Codecrafters for designing the challenge and providing the test suite—your platform makes learning systems programming fun and accessible!
- My contributions here are the C implementation and this documentation.
- If you’re learning systems programming, I highly recommend trying the challenge yourself before looking at solutions!

This is a starting point for C solutions to the
["Build Your Own Redis" Challenge](https://codecrafters.io/challenges/redis).
