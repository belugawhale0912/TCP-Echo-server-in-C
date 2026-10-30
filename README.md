# TCP Echo Server in C

A simple TCP client/server echo application written in C using the BSD socket API. The server accepts a connection from a client, reads incoming data, and sends the same data back to the client (echo behavior).

## Project overview

This project demonstrates the core mechanics of a TCP connection in C:

- creating a socket
- binding and listening for incoming connections
- accepting client connections
- sending and receiving data over a TCP stream
- handling the connection lifecycle cleanly

It includes both a server and a client implementation under the `sourcecode/` directory.

## Project structure

```text
TCP-Echo-server-in-C/
├── README.md
├── note/
│   ├── TCP Client Architecutre C.md
│   └── TCP Server Architecture C.md
└── sourcecode/
    ├── client.c
    └── server.c
```

## Files

- `sourcecode/server.c` - TCP echo server
- `sourcecode/client.c` - TCP echo client
- `note/TCP Server Architecture C.md` - notes on server-side socket architecture
- `note/TCP Client Architecutre C.md` - notes on client-side socket architecture

## How it works

### Server

The server:

1. creates a socket with `AF_INET` and `SOCK_STREAM`
2. binds to `INADDR_ANY` on port `8085`
3. listens for incoming connections
4. accepts one client connection
5. reads data from the client in a loop
6. echoes the received data back to the client
7. closes the connection when the client disconnects

### Client

The client:

1. creates a TCP socket
2. connects to `127.0.0.1:8085`
3. reads user input from stdin
4. sends each line to the server
5. prints the echoed response from the server
6. exits when `Ctrl+D` or `Ctrl+C` is pressed

## Requirements

- Linux or Unix-like environment
- GCC compiler
- POSIX sockets support

## Build and run

Compile both programs:

```bash
gcc -o server sourcecode/server.c
gcc -o client sourcecode/client.c
```

Run the server in one terminal:

```bash
./server
```

Then run the client in another terminal:

```bash
./client
```

Example interaction:

```text
Enter message (Ctrl+D or Ctrl+C to exit):
hello
Server echo: hello
```

## Default port

The application uses port:

```text
8085
```

## Notes

This is a beginner-friendly networking project I build to learn basic TCP socket communication in C. It is useful for learning how sockets, TCP streams, and echo-based communication behave in practice.

