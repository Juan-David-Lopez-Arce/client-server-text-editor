## Concurrent Client–Server Text Editor

C | Unix Pipes | Concurrency | Systems Programming

A concurrent, multi-client text editor built in C that allows multiple users to edit a shared document in real time.

Designed to handle concurrent edits safely using threads, synchronization primitives, and efficient inter-process communication via Unix pipes and epoll.

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
- Efficient IPC using Unix pipes
- Markdown-style editing (headers, lists, formatting, etc.)

## Why This Project Matters

This project demonstrates core systems programming concepts used in real-world applications such as collaborative tools, distributed systems, and high-performance servers.

It focuses on:
- Safe concurrency in low-level languages (C)
- Real-time multi-client coordination
- Efficient inter-process communication
- Scalable server design using epoll

## Architecture

The system follows a client–server model with concurrency at the server level.

Components:
Clients  →  Server  →  Shared Document (Linked List)
             ↓
     Broadcast Updates

Each client communicates with the server via Unix pipes

The server manages:

- Client connections (threads)
- Document updates (worker threads)
- Periodic broadcasting (timer thread)

## Project Structure

source/
├── client.c        # Client-side logic and command interface
├── server.c        # Multi-threaded server and coordination logic
├── markdown.c      # Document editing operations (linked list)
libs/               # Header files for each source code file
├── client.h        
├── server.h        
├── markdown.h

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

## Concurrency Design

- Each client is handled by a dedicated thread
- Shared document access is protected using synchronization mechanisms (e.g., mutexes)

Prevents:
- Race conditions
- Inconsistent document states
- Ensures safe concurrent editing

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
make
2. Start the Server
./server
3. Launch Clients (in separate terminals)
./client

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
