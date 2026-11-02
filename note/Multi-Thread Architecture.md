# POSIX Threads and Network Concurrency Notes

This document summarizes the core technical concepts, memory models, and system behaviors involved in building concurrent network servers using POSIX threads (`pthreads`) in C.

---

## 1. Process vs. Thread: Resource Ownership

When an application starts, the OS initializes a process with an initial thread of execution (the **main thread**) running `main()`.

| Resource | Shared Across All Threads in Process | Private Per Thread |
| :--- | :---: | :---: |
| **Virtual Address Space** | Yes | No |
| **Heap Memory (`malloc`)** | Yes | No |
| **Global & Static Variables** | Yes | No |
| **File Descriptor (FD) Table** | Yes | No |
| **Call Stack (Local Variables)** | No | Yes (typically $2\text{ MB} - 8\text{ MB}$) |
| **CPU Registers & Program Counter (PC)** | No | Yes |
| **Thread ID (`pthread_t`)** | No | Yes |

### File Descriptor Sharing
* The integer table mapping numbers (`0, 1, 2, 3, 4...`) to kernel socket/file objects is **shared across all threads**.
* Multiple threads can theoretically read, write, or close the same integer FD.
* In a concurrent network server, isolation is maintained by convention: the main thread assigns a **distinct client FD** to each worker thread.

---

## 2. POSIX Thread API Mechanics

### 2.1 The `pthread_create` Contract
```c
int pthread_create(pthread_t *thread, 
                   const pthread_attr_t *attr, 
                   void *(*start_routine) (void *), 
                   void *arg);
```

1. **`thread` (`pthread_t *`)**: Output parameter where the kernel writes the thread identifier.
2. **`attr` (`const pthread_attr_t *`)**: Custom attributes (e.g., stack size, detached state). Passing `NULL` applies system defaults.
3. **`start_routine` (`void *(*)(void *)`)**: Function pointer indicating the entry instruction pointer ($PC$) for the thread. It must strictly accept a single `void *` argument and return `void *`.
4. **`arg` (`void *`)**: The single generic pointer delivered to the thread routine.

### 2.2 Why Not Pass Custom Function Signatures?
The C runtime initializes the new execution context using a hardcoded calling convention:
```c
void *result = start_routine(arg);
```
To support arbitrary data without language-level generics, POSIX relies on `void *` as a universal pointer. Typed parameters cannot be passed directly into the function header without triggering compiler errors or ABI calling-convention mismatches.

---

## 3. Passing Data Safely (Race Conditions on Stack)

### The Anti-Pattern: Passing Stack Addresses
```c
// DANGEROUS: Do not pass addresses of stack variables from a loop
int client_fd = accept(server_fd, ...);
pthread_create(&tid, NULL, handle_client, &client_fd);
```

* **The Race Condition:** The variable `client_fd` lives in `main()`'s stack frame. Before the newly spawned thread wakes up and reads `*(&client_fd)`, the main thread might accept another connection and overwrite `client_fd`.
* **Result:** Two distinct threads inadvertently read the same FD integer and communicate with the same client socket.

### The Idiomatic Solution: Heap Packaging
```c
typedef struct {
    int client_fd;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
} client_context_t;

// 1. Allocate dedicated heap chunk per connection
client_context_t *ctx = malloc(sizeof(client_context_t));
ctx->client_fd = client_fd;
// ... populate metadata ...

// 2. Pass heap pointer
pthread_create(&tid, NULL, handle_client, (void *)ctx);

// 3. Worker unboxes and frees container
void *handle_client(void *arg) {
    client_context_t *ctx = (client_context_t *)arg;
    int client_fd = ctx->client_fd;
    free(ctx);
    // ...
}
```

---

## 4. Lifecycle Management: Joinable vs. Detached

Every thread finishes its active execution when its root function returns or when it calls `pthread_exit()`. What happens *after* termination depends on its state:

```text
               ┌───────────────────────┐
               │    Thread Finishes    │
               └───────────┬───────────┘
                           │
             Is it Joinable or Detached?
             ┌─────────────┴─────────────┐
             ▼                           ▼
       [ Joinable ]                 [ Detached ]
  Enters "Zombie" State.         OS immediately frees
  Metadata and stack persist     stack and kernel state.
  until pthread_join() runs.     No leftovers.
```

### 4.1 Joinable Threads (`pthread_join`)
* **Behavior:** The calling thread (usually the main thread) is put to sleep until the target thread finishes execution and yields its `void *` return value.
* **Analogy:** Similar to `await promise` in async paradigms, but blocks the underlying OS thread rather than yielding an event loop.
* **Why not in server accept loops?** If the main thread calls `pthread_join(worker_tid)`, it blocks on that specific client and cannot accept new connections until that client disconnects.

### 4.2 Detached Threads (`pthread_detach`)
* **Behavior:** Informs the kernel: *"No other thread will wait on or read this thread's return value."*
* **Automatic Cleanup:** All kernel resources, thread bookkeeping, and stack allocations are reclaimed immediately upon termination.
* **Self-Detaching Idiom:**
  ```c
  void *handle_client(void *arg) {
      pthread_detach(pthread_self()); // Fire-and-forget
      // ... run I/O loop ...
      close(client_fd);
      return NULL; // Instantly reaped by OS
  }
  ```

---

## 5. Architectural Comparison

### Single-Threaded Sequential Server
```text
accept() ──> [recv / send loop] ──> close() ──> accept() next client
```
* **Throughput:** $1$ client at a time.
* **Limitation:** Any slow client (or high-latency connection) stalls all subsequent clients in the kernel listen backlog.

### Multi-Threaded Server (One-Thread-Per-Client)
```text
           ┌───> [Worker Thread 1] ──> recv / send (FD 4) ──> close(4)
           │
accept() ──┼───> [Worker Thread 2] ──> recv / send (FD 5) ──> close(5)
(Main Loop)│
           └───> [Worker Thread 3] ──> recv / send (FD 6) ──> close(6)
```
* **Role of Main Thread:** Acts as a dispatcher. It only calls `accept()`, allocates a connection payload, creates a thread, and loops back immediately.
* **Role of Worker Thread:** Takes ownership of its individual `client_fd`, detaches itself, executes blocking `recv()` / `send()` cycles, and releases the FD upon client disconnect.

---

## 6. Kernel File Descriptor Allocation Rules
1. **Lowest-Numbered Unused Index Rule:** POSIX guarantees that the kernel always assigns the lowest available integer file descriptor to a new socket or file.
2. **Scheduling Latency & Apparent Monotonicity:** If a new client arrives while a terminating thread is still executing cleanup (`printf`, thread teardown) before its `close(fd)` system call executes, the kernel will select the next available number (e.g., $FD = 6$ instead of reusing $FD = 4$).