# Linux TCP Server Architecture in C

This reference covers the core concepts, data structures, and system call lifecycles required to build a POSIX-compliant TCP server in C.

---

## 1. Network Byte Order vs. Host Byte Order

Modern CPU architectures (x86-64, ARM) store multi-byte integers in **Little-Endian** format (least-significant byte first), whereas Internet protocols mandate **Big-Endian** format (most-significant byte first).

To prevent data corruption of port numbers and IP representations across the wire, use the byte-ordering conversion helpers from `<arpa/inet.h>`:

| Function | Signature | Purpose |
| :--- | :--- | :--- |
| `htons()` | `uint16_t htons(uint16_t hostshort);` | **H**ost **TO** **N**etwork **S**hort (16-bit ports) |
| `ntohs()` | `uint16_t ntohs(uint16_t netshort);` | **N**etwork **TO** **H**ost **S**hort (reading received ports) |
| `htonl()` | `uint32_t htonl(uint32_t hostlong);` | **H**ost **TO** **N**etwork **L**ong (32-bit IPv4 addresses) |
| `ntohl()` | `uint32_t ntohl(uint32_t netlong);` | **N**etwork **TO** **H**ost **L**ong |
| `inet_pton()` | `int inet_pton(int af, const char *src, void *dst);` | Presentation-to-Network: converts string `"127.0.0.1"` to binary bytes |
| `inet_ntop()` | `const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);` | Network-to-Presentation: converts binary bytes back to string |

---

## 2. Core POSIX Data Structures

Socket addressing relies on structures defined in `<netinet/in.h>` and `<sys/socket.h>`:

```c
#include <netinet/in.h>

// IPv4 Address Structure
struct sockaddr_in {
    sa_family_t    sin_family; // Address family: AF_INET
    in_port_t      sin_port;   // 16-bit port in Network Byte Order (via htons())
    struct in_addr sin_addr;   // 32-bit IPv4 address struct
    char           sin_zero[8];// Padding to match size of struct sockaddr
};

struct in_addr {
    in_addr_t      s_addr;     // 32-bit IPv4 address in Network Byte Order
};

// Generic Socket Address (used for polymorphic casting in syscalls)
struct sockaddr {
    sa_family_t    sa_family;  // Address family (e.g., AF_INET, AF_INET6, AF_UNIX)
    char           sa_data[14];// Protocol-specific address data
};
```

* **`INADDR_ANY` (`0.0.0.0`)**: A wildcard IP address constant instructing the kernel to bind the socket to all available local network interfaces.

---

## 3. Server Syscall Lifecycle

```
+-----------------------------------------------------------------+
|                            socket()                             |
|       Allocates an unbound kernel socket file descriptor        |
+--------------------------------+--------------------------------+
                                 |
                                 v
+-----------------------------------------------------------------+
|                             bind()                              |
|          Binds local address and port to the socket descriptor  |
+--------------------------------+--------------------------------+
                                 |
                                 v
+-----------------------------------------------------------------+
|                            listen()                             |
|          Marks socket passive and establishes connection queue  |
+--------------------------------+--------------------------------+
                                 |
                                 v
+-----------------------------------------------------------------+
|                            accept()                             |
|   Blocks until client 3-way handshake completes; returns new fd |
+--------------------------------+--------------------------------+
                                 |
                                 v
+-----------------------------------------------------------------+
|                      recv()  <--->  send()                      |
|       Bidirectional streaming data transfer on client_fd        |
+--------------------------------+--------------------------------+
                                 |
                                 v
+-----------------------------------------------------------------+
|                            close()                              |
|           Releases descriptor; initiates TCP FIN teardown       |
+-----------------------------------------------------------------+
```

---

## 4. Detailed System Call Breakdown

### 1. `socket()` — Endpoint Allocation

```c
#include <sys/socket.h>

int socket(int domain, int type, int protocol);
```

Allocates an unconfigured network communication endpoint in the Linux kernel and returns an integer file descriptor indexing the process's file descriptor table.

* **Parameters:**
  * **`domain` (Address Family):** Dictates protocol family and address structure format.
    * `AF_INET`: IPv4 protocols (`struct sockaddr_in`).
    * `AF_INET6`: IPv6 protocols (`struct sockaddr_in6`).
    * `AF_UNIX` / `AF_LOCAL`: POSIX local IPC via filesystem pathnames (`struct sockaddr_un`).
  * **`type` (Communication Semantics):** Defines delivery semantics and boundary properties.
    * `SOCK_STREAM`: Sequenced, reliable, two-way, connection-based byte streams (TCP).
    * `SOCK_DGRAM`: Connectionless, unreliable datagrams with preserved boundaries (UDP).
    * `SOCK_RAW`: Direct access to lower network layers (requires `CAP_NET_RAW` / root).
    * *Linux Bitwise Modifiers:* Can be OR'd with `SOCK_NONBLOCK` or `SOCK_CLOEXEC`.
  * **`protocol`:** Selects the specific transport protocol. Passing `0` selects the canonical default for the given domain and type (e.g., `IPPROTO_TCP` for `AF_INET` + `SOCK_STREAM`).
* **Return Value:**
  * **Success:** Non-negative integer file descriptor (lowest available index).
  * **Failure:** `-1`, setting `errno` (e.g., `EAFNOSUPPORT`, `EMFILE`).

---

### 2. `bind()` — Name Assignment

```c
#include <sys/socket.h>

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```

Associates an unnamed socket descriptor with a local transport address (IP interface and port).

* **Parameters:**
  * **`sockfd`:** File descriptor returned by `socket()`.
  * **`addr`:** Pointer to a concrete address structure (`sockaddr_in`, `sockaddr_in6`, etc.) cast to `(struct sockaddr *)`.
  * **`addrlen`:** Byte size of the target address structure (e.g., `sizeof(struct sockaddr_in)`).
* **Behavior & Usage:**
  * **Servers:** Universally call `bind()` to reserve a fixed, predictable port so clients can find them.
  * **Clients:** Generally skip `bind()`; calling `connect()` causes the kernel to automatically assign an ephemeral port and outbound local interface.
* **Return Value:**
  * **Success:** `0`.
  * **Failure:** `-1`, setting `errno` (e.g., `EADDRINUSE` if the address/port is already claimed, `EACCES` if binding to ports $< 1024$ without root privileges).

---

### 3. `listen()` — Passive Queue Configuration

```c
#include <sys/socket.h>

int listen(int sockfd, int backlog);
```

Marks a stream socket (`SOCK_STREAM` or `SOCK_SEQPACKET`) as passive, informing the kernel to buffer incoming client connection requests.

* **Parameters:**
  * **`sockfd`:** A bound, connection-oriented socket descriptor.
  * **`backlog`:** The maximum capacity of the queue holding pending, fully established connections waiting to be accepted.
* **Kernel Mechanism:**
  * **SYN Queue (Incomplete Handshakes):** Tracks connections negotiating the 3-way handshake (`SYN` $\rightarrow$ `SYN-ACK` $\rightarrow$ `ACK`).
  * **Accept Queue (Completed Handshakes):** Holds connections ready for retrieval via `accept()`. `backlog` governs the maximum size of this queue. If full, incoming SYN/ACK packets are dropped or ignored, prompting client retransmissions.
* **Return Value:**
  * **Success:** `0`.
  * **Failure:** `-1`, setting `errno` (e.g., `EOPNOTSUPP` if called on an unsupported type like `SOCK_DGRAM`).

---

### 4. `accept()` — Connection Extraction

```c
#include <sys/socket.h>

int accept(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen);
```

Extracts the next completed connection from the listening socket's accept queue, allocates a new socket descriptor, and returns it.

* **Parameters:**
  * **`sockfd`:** The passive listening socket created via `socket()` and activated via `listen()`.
  * **`addr`:** Pointer to a caller-allocated `sockaddr` struct (or `NULL`). The kernel fills this with the remote peer's identity (IP and ephemeral port).
  * **`addrlen`:** Value-result argument:
    * **Input:** Must point to a variable initialized to the allocated byte size of `addr`.
    * **Output:** Overwritten by the kernel with the actual size of the peer address populated.
* **Behavior:**
  * **Blocking:** If the queue is empty, execution halts until a client finishes the 3-way handshake (unless the socket is marked non-blocking, in which case it returns `-1` with `EAGAIN` / `EWOULDBLOCK`).
  * **Descriptor Separation:** `sockfd` remains open and listening for other connections. The return value is a brand new descriptor dedicated to the individual client session.
* **Return Value:**
  * **Success:** Non-negative integer file descriptor representing the accepted client session.
  * **Failure:** `-1`, setting `errno` (e.g., `ECONNABORTED`, `EMFILE`).

---

### 5. `recv()` — Stream and Datagram Reception

```c
#include <sys/socket.h>

ssize_t recv(int sockfd, void *buf, size_t len, int flags);
```

Reads incoming data from a connected socket descriptor into user-space memory.

* **Parameters:**
  * **`sockfd`:** Connected socket file descriptor (e.g., `client_fd`).
  * **`buf`:** Target buffer for incoming bytes.
  * **`len`:** Maximum number of bytes to read in this invocation.
  * **`flags`:** Bitwise OR flags modifying reception semantics:
    * `0`: Standard blocking read.
    * `MSG_PEEK`: Reads data from the queue without consuming it (leaves it in the kernel buffer).
    * `MSG_WAITALL`: Requests blocking until the complete length `len` is read (can still return early on peer disconnect or signal).
    * `MSG_DONTWAIT`: Non-blocking operation for this call only.
* **Stream Semantics (TCP):**
  * TCP provides a byte stream, not discrete messages. A sender calling `send()` twice may be read by a single `recv()` call, or one message may require multiple `recv()` calls. Message framing (e.g., length headers or delimiters) must be handled by the application protocol.
* **Return Value:**
  * **`> 0`:** Actual number of bytes read.
  * **`== 0`:** **Orderly Peer Shutdown (EOF).** The remote peer closed its transmission end (sent a TCP `FIN`). The read loop should exit.
  * **`< 0`:** Syscall failure (`-1`), setting `errno` (e.g., `EINTR`, `EAGAIN`).

---

### 6. `send()` — Stream and Datagram Transmission

```c
#include <sys/socket.h>

ssize_t send(int sockfd, const void *buf, size_t len, int flags);
```

Transfers data from application memory into the kernel's outbound socket write buffer.

* **Parameters:**
  * **`sockfd`:** Connected socket file descriptor.
  * **`buf`:** Pointer to data to transmit.
  * **`len`:** Requested number of bytes to transmit.
  * **`flags`:** Bitwise OR flags:
    * `0`: Standard blocking transmission.
    * `MSG_NOSIGNAL`: Suppresses delivery of `SIGPIPE` if the connection is broken; returns `-1` with `errno = EPIPE` instead.
    * `MSG_DONTWAIT`: Non-blocking operation.
* **Kernel Buffering & Partial Writes:**
  * A successful return only confirms data was copied into the local kernel socket buffer—not that the peer received or acknowledged it.
  * Under heavy load or non-blocking modes, `send()` may write fewer than `len` bytes (partial write). Robust code must loop and check the returned byte count until all bytes are sent.
* **Return Value:**
  * **Success:** Number of bytes transferred to the socket buffer.
  * **Failure:** `-1`, setting `errno` (e.g., `EPIPE`, `ECONNRESET`).

---

### 7. `close()` — Descriptor Teardown

```c
#include <unistd.h>

int close(int fd);
```

Decrements the file descriptor reference count in the kernel. When it drops to zero, associated kernel data structures are reclaimed and network teardown begins.

* **Parameters:**
  * **`fd`:** Any open file descriptor (`server_fd`, `client_fd`, standard I/O, file).
* **Network Behavior (TCP):**
  * Initiates TCP active closure by sending a `FIN` packet to the remote peer.
  * Frees the descriptor index back to the process table for subsequent allocation.
* **Return Value:**
  * **Success:** `0`.
  * **Failure:** `-1`, setting `errno` (e.g., `EBADF`).

---

## 5. Common Pitfalls & Best Practices

| Pitfall | Root Cause | Best Practice Solution |
| :--- | :--- | :--- |
| **Peer IP prints as `0.0.0.0:0`** | Passing server struct pointer instead of client struct to `accept()` | Pass `&client_addr` and a pointer to an initialized `socklen_t` storing `sizeof(client_addr)`. |
| **Leaking garbage bytes in `send()`** | Passing static buffer size instead of bytes read | Pass the exact return value of `recv()` (`bytes_received`) as the length argument to `send()`. |
| **Buffer overflow / missing delimiter** | Unbounded string reads without null terminators | Zero buffer memory (`memset()`), read at most `BUFFER_SIZE - 1` bytes, or treat incoming streams strictly as raw binary bytes. |
| **Partial writes** | Assuming `send()` always writes all requested bytes | Loop `send()` with offset pointers until cumulative sent bytes match the target payload size. |
| **Crash via `SIGPIPE`** | Writing to a socket after remote side closed | Pass `MSG_NOSIGNAL` flag to `send()`, or ignore `SIGPIPE` using `signal(SIGPIPE, SIG_IGN)`. |
| **Descriptor leaks** | Forgetting to close sockets on error paths | Ensure every path leading to `exit()` calls `close()` on all open file descriptors. |