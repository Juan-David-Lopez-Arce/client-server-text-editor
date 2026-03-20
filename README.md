## Concurrent Client–Server Text Editor

C | Unix Pipes | Concurrency | Systems Programming

A high-performance concurrent text editor in C that allows multiple clients to edit a shared document in real time. Built from scratch using threads, epoll, and low-level IPC.

## Highlights

- Built a multi-client real-time editor in C using epoll and threads
- Designed a thread-safe shared document model
- Implemented IPC using Unix pipes
- Handled concurrent edits without race conditions or deadlocks

## Overview

This project implements a multi-client collaborative text editor where:
- A central server maintains the shared document
- Multiple clients can connect and issue edit commands concurrently
- Changes are safely synchronized across clients
- The updated document is broadcast periodically to all connected clients

The document is internally represented as a linked list of character arrays, enabling efficient insertions, deletions, and formatting operations.

## Features

- Supports multiple concurrent clients editing the same document
- Thread-safe operations using mutex-based synchronization
- Real-time document updates broadcast to all clients
- Markdown-style editing (headers, lists, formatting, etc.)

## Why This Project Matters

This project explores how real-time collaborative systems (like Google Docs or Notion) can be built at a low level without relying on high-level frameworks.

It demonstrates how to:
- Handle concurrent edits safely in C
- Design scalable event-driven servers using epoll
- Build efficient shared data structures under contention

## Architecture

The system follows a client–server model with concurrency at the server level.

## Components
- Clients → send edit commands
- Server → processes and synchronizes edits
- Shared Document → stored as a linked structure

Each client communicates with the server via Unix pipes for command delivery and updates.

## Consistency Model

- The server acts as the single source of truth
- All edits are serialized through a central update queue
- Mutex locks ensure atomic modifications to the document
- Clients receive periodic snapshots to stay in sync

## Project Structure
```bash
source/
├── client.c        # Client-side logic and command interface
├── server.c        # Multi-threaded server and coordination logic
├── markdown.c      # Document editing operations (linked list)
libs/               # Header files for each source code file
├── client.h        
├── server.h        
├── markdown.h
```
## File Descriptions

client.c

- Represents a client process
- Sends editing commands to the server
- Receives updated document snapshots
- Supports collaborative editing across multiple clients

server.c

- Spawns multiple threads:
    - One per client connection
    - Worker threads for document updates
    - A broadcasting thread for periodic updates
- Ensures synchronization and consistency across concurrent edits

markdown.c

- Implements all document manipulation logic
- Uses a linked list of character arrays for memory efficiency


## Inter-Process Communication

Communication between clients and server is implemented using Unix pipes, which enables:

- Lightweight IPC between processes
- Efficient message passing for commands and updates

## Platform Compatibility

This project is designed for Linux environments only. It leverages Linux-specific system calls such as `epoll` for high-performance I/O, and therefore will not compile or run natively on macOS or Windows.

If you are using macOS or Windows, you can run the project via:
- WSL (Windows Subsystem for Linux)
- Docker
- A Linux virtual machine

## How to Run

1. Compile
```bash
make
```
3. Start the Server
```bash
./server
```
5. Launch Clients (in separate terminals)
```bash
./client
```

You can start multiple clients to simulate concurrent editing.

Example Workflow

1. Start the server
2. Launch multiple clients
3. Clients send edit commands (insert/delete/format)
4. Server processes edits safely
5. Updated document is broadcast to all clients periodically

## Key Learnings

- Designing thread-safe systems in C
- Managing concurrency and synchronization
- Implementing inter-process communication with Unix pipes
- Building efficient data structures for dynamic text editing
- Coordinating multi-threaded server architectures

## Challenges

- Designing a thread-safe editing system without introducing deadlocks
- Synchronizing concurrent edits across multiple clients
- Efficiently managing IPC without blocking performance
- Structuring a dynamic document using linked lists for fast updates
