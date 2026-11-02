# TCP Echo Server in C

A concurrent TCP client/server echo application written in C using the BSD socket API and POSIX threads (`pthreads`). The server handles incoming connections from multiple clients concurrently, echoing network data back to each client independently without blocking.

## Project Overview

This repository demonstrates systems and network programming in C, progressing from a sequential single-threaded socket baseline to a concurrent, multi-threaded server architecture:

* **Socket Programming:** Creating, binding, and listening on IPv4 TCP sockets.
* **Connection Lifecycle:** Managing the TCP 3-way handshake via `accept()`.
* **POSIX Threads:** Implementing multi-threaded concurrency using `pthread_create()` and `pthread_detach()`.
* **Thread Safety & Memory Isolation:** Avoiding stack-frame overwrite races by heap-allocating client context containers.
* **Resource Management:** Operating system socket reuse, kernel file descriptor table allocation, and automatic zombie thread reclamation.

## Project Structure

```text
TCP-ECHO-SERVER-IN-C/
├── .gitignore
├── README.md
├── note/
│   ├── Multi-Thread Architecture.md
│   ├── TCP Client Architecutre C.md
│   └── TCP Server Architecture C.md
└── sourcecode/
    ├── client.c
    ├── multithread_server.c
    └── server.c
```

## File Manifest

| File | Description |
| :--- | :--- |
| `sourcecode/multithread_server.c` | Concurrent TCP echo server managing parallel client sessions via `pthreads`. |
| `sourcecode/server.c` | Baseline single-threaded TCP echo server (sequential connection handling). |
| `sourcecode/client.c` | Interactive TCP echo client. |
| `note/Multi-Thread Architecture.md` | In-depth reference on POSIX threads, heap vs. stack race conditions, and thread detachment. |
| `note/TCP Server Architecture C.md` | Architecture notes on socket setup, file descriptor lifecycle, and listen backlogs. |
| `note/TCP Client Architecutre C.md` | Architecture notes on client-side socket lifecycle and stream I/O. |

## Architecture & Mechanics

### Multi-Threaded Server Architecture

```text
                     ┌────────────────────────┐
                     │  Main Thread (accept)  │
                     └───────────┬────────────┘
                                 │
     ┌───────────────────────────┼───────────────────────────┐
     │ client_fd = 4             │ client_fd = 5             │ client_fd = 6
     ▼                           ▼                           ▼
┌─────────────────┐         ┌─────────────────┐         ┌─────────────────┐
│ Worker Thread 1 │         │ Worker Thread 2 │         │ Worker Thread 3 │
│  recv() / send()│         │  recv() / send()│         │  recv() / send()│
└─────────────────┘         └─────────────────┘         └─────────────────┘
```

1. **Listener Initialization:** The main thread binds an IPv4 stream socket (`AF_INET`, `SOCK_STREAM`) with `SO_REUSEADDR` to port `8085` and sets the socket to passive listening mode.
2. **Accept Loop:** The main thread calls `accept()`, blocking until a new client completes the TCP handshake.
3. **Heap Context Packaging:** Metadata (`client_fd`, IP address, port) is encapsulated inside a heap-allocated `client_context_t` (`malloc()`) to guarantee that successive loop iterations do not overwrite the thread payload.
4. **Thread Dispatch:** The main thread spawns a worker thread via `pthread_create()` with `(void *)ctx`, then immediately returns to `accept()`.
5. **Worker Detachment:** The worker thread detaches itself via `pthread_detach(pthread_self())`, ensuring its stack and thread descriptor are automatically reclaimed by the OS upon exit.
6. **I/O & Cleanup:** The worker reads and echoes data until the client terminates the connection (`recv() == 0`), at which point the worker calls `close(client_fd)` and exits cleanly.

### Client Workflow

1. Creates an IPv4 TCP stream socket.
2. Connects to `127.0.0.1:8085`.
3. Reads user input line-by-line from standard input (`stdin`).
4. Sends the input buffer over the TCP socket.
5. Receives and displays the echoed response from the server.
6. Terminates on `Ctrl+D` (EOF) or `Ctrl+C`.

## Prerequisites

* **Operating System:** Linux, macOS, or any POSIX-compliant environment
* **Compiler:** GCC or Clang
* **Libraries:** POSIX Threads library (`libpthread`)

## Build & Run

### 1. Compilation

Compile the multi-threaded server with thread support enabled:

```bash
gcc -Wall -Wextra -o multithread_server sourcecode/multithread_server.c -pthread
```

Compile the client and baseline single-threaded server:

```bash
gcc -Wall -Wextra -o client sourcecode/client.c
gcc -Wall -Wextra -o server sourcecode/server.c
```

### 2. Start the Server

```bash
./multithread_server
```

### 3. Connect Multiple Clients

Open multiple terminal sessions to verify concurrent execution:

```bash
# Terminal 1
./client
```

```bash
# Terminal 2
./client
```

#### Example Interaction

```text
# Terminal 1 (Client A)
Enter message (Ctrl+D or Ctrl+C to exit):
hello from client A
Server echo: hello from client A

# Terminal 2 (Client B)
Enter message (Ctrl+D or Ctrl+C to exit):
hello from client B
Server echo: hello from client B
```

## Key Technical Concepts Demonstrated

* **Concurrency Model:** One-thread-per-connection pattern using POSIX threads.
* **Race Condition Prevention:** Explicit heap allocation per thread avoids stack frame variable corruption across iterations.
* **Memory & Resource Management:** Fire-and-forget thread management via `pthread_detach()` eliminates zombie thread leaks without blocking the main loop on `pthread_join()`.
* **Socket Options:** `SO_REUSEADDR` prevents `EADDRINUSE` errors during immediate server restarts while previous sockets remain in `TIME_WAIT`.