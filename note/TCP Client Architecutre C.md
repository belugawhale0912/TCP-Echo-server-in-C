# Linux TCP Client Architecture in C

This guide covers the core concepts, data structures, and system call lifecycles required to build a POSIX-compliant TCP client in C.

---

## 1. Client vs. Server Architecture

Unlike a server, which passively listens on a fixed, well-known port for incoming traffic, a client acts as the active initiator of communication.

| Feature | Server (`server.c`) | Client (`client.c`) |
| :--- | :--- | :--- |
| **Port Assignment** | Deterministic/fixed via `bind()` (e.g., `8085`) | Dynamic/ephemeral assigned automatically by kernel during `connect()` |
| **Mode** | Passive listening (`listen()`, `accept()`) | Active connection initiator (`connect()`) |
| **Descriptors** | At least two: listening socket (`server_fd`) and session socket (`client_fd`) | Single communication socket descriptor (`client_fd`) |
| **Syscall Chain** | `socket` $\rightarrow$ `bind` $\rightarrow$ `listen` $\rightarrow$ `accept` $\rightarrow$ `recv`/`send` | `socket` $\rightarrow$ `connect` $\rightarrow$ `send`/`recv` |

---

## 2. Client Syscall Lifecycle

```
 +-----------------------------------------------------------------+
 |                            socket()                             |
 |       Allocates an active client socket descriptor              |
 +--------------------------------+--------------------------------+
                                  |
                                  v
 +-----------------------------------------------------------------+
 |                          inet_pton()                            |
 |       Parses text IP ("127.0.0.1") into network binary bytes    |
 +--------------------------------+--------------------------------+
                                  |
                                  v
 +-----------------------------------------------------------------+
 |                           connect()                             |
 |       Assigns ephemeral port & executes TCP 3-way handshake     |
 +--------------------------------+--------------------------------+
                                  |
                                  v
 +-----------------------------------------------------------------+
 |                      send()  <--->  recv()                      |
 |       Streams outbound requests and receives server replies     |
 +--------------------------------+--------------------------------+
                                  |
                                  v
 +-----------------------------------------------------------------+
 |                            close()                              |
 |       Tears down TCP stream (sends FIN) and frees descriptor    |
 +-----------------------------------------------------------------+
```

---

## 3. Detailed Syscall Breakdown

### 1. `inet_pton()` — Presentation to Network Translation

```c
#include <arpa/inet.h>

int inet_pton(int af, const char *src, void *dst);
```

Converts a human-readable IP address string into binary network byte order format.

* **Parameters:**
  * **`af`:** Address family (`AF_INET` for IPv4, `AF_INET6` for IPv6).
  * **`src`:** Null-terminated C-string containing the presentation IP (e.g., `"127.0.0.1"` or `"192.168.1.15"`).
  * **`dst`:** Pointer to memory where binary bytes should be written (e.g., `&server_addr.sin_addr`).
* **Why not `inet_addr()`?**
  * `inet_addr()` returns `(in_addr_t)-1` on error. Because the valid broadcast address `255.255.255.255` is represented in binary as `-1`, `inet_addr()` cannot differentiate between an error and valid broadcast traffic. `inet_pton()` returns status via an integer code while writing the output directly to a pointer buffer.
* **Return Values:**
  * `1`: Parsing succeeded; binary output successfully written to `dst`.
  * `0`: `src` does not contain a valid network address for the specified family.
  * `-1`: `af` is invalid; sets `errno = EAFNOSUPPORT`.

---

### 2. `connect()` — Active Handshake Initiation

```c
#include <sys/socket.h>

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```

Connects the active socket `sockfd` to the destination address specified by `addr`.

* **Parameters:**
  * **`sockfd`:** File descriptor created via `socket()`.
  * **`addr`:** Pointer to destination `sockaddr_in` struct cast to `(struct sockaddr *)`.
  * **`addrlen`:** Size in bytes of the destination struct (`sizeof(server_addr)`).
* **Automatic Ephemeral Port Assignment:**
  * Clients rarely call `bind()`. When `connect()` is invoked on an unbound socket, the Linux kernel automatically:
    1. Selects an optimal source IP interface based on kernel routing tables.
    2. Allocates a free port from the ephemeral port range (configurable via `/proc/sys/net/ipv4/ip_local_port_range`, typically `32768`–`60999`).
    3. Binds the socket to that ephemeral port before transmitting packets.
* **TCP Handshake:**
  * `connect()` sends a `SYN` packet to the server and blocks until it receives `SYN-ACK` and replies with `ACK` (completing the 3-way handshake).
* **Return Values & Common Errors:**
  * `0`: Connection successfully established.
  * `-1`: Connection failed, setting `errno`:
    * `ECONNREFUSED`: No process was listening on the target port, or the remote firewall rejected the packet with an RST.
    * `ETIMEDOUT`: Packets were dropped silently without acknowledgment (host down, routing blackout, or firewall drop).
    * `ENETUNREACH`: No network route exists to reach the destination subnet.

---

### 3. `send()` and `recv()` in Client Context

* **`send()`:** Transmits formatted user input. Ensure only `strlen(input)` bytes are sent rather than full buffer capacities to prevent sending uninitialized bytes.
* **`recv()`:** Blocks until response bytes arrive or the server terminates the session.
  * **Return Value `0`:** Crucial indicator of EOF. In TCP, `recv() == 0` means the server performed an active close or crashed, notifying the client that it should clean up its local socket and terminate the loop.

---

## 4. Common Client Pitfalls & Best Practices

| Pitfall | Cause | Solution |
| :--- | :--- | :--- |
| **`connect: Connection refused`** | Server process not running or bound to a different port | Ensure server is started, actively calling `listen()`, and port matches. |
| **Sending trailing garbage bytes** | Passing `sizeof(send_buffer)` instead of `strlen()` to `send()` | Pass `strlen(send_buffer)` to transmit only the actual payload. |
| **Silent disconnection hangs** | Ignoring `recv() == 0` check | Always verify if `recv() <= 0` and break immediately to avoid infinite reading loops. |
| **Hostname resolution failure** | Passing `"localhost"` or `"example.com"` directly to `inet_pton()` | `inet_pton()` does **not** perform DNS resolution. For hostnames, use `getaddrinfo()`. |
| **Buffer overflow on display** | Printing raw `recv_buffer` with `%s` without null termination | Read at most `BUFFER_SIZE - 1` and ensure memory is zeroed or null-terminated prior to `printf()`. |