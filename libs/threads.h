#ifndef THREADS_H
#define THREADS_H

#include <sys/types.h>
#include <time.h>
#include <stdio.h>

#include "document.h"

//represents one command, contains the instruction and the timestamp to simplify sorting in the queue 
typedef struct single_command{
    time_t timestamp;
    char *instruction;
    char *user;
    struct single_command *next;
    bool write_perm;
}single_command;

//contains a linked list with all the commands of a version of the document
//will be a global variable and will be emptyed every time a new version is released
typedef struct{
    //array of command structs
    single_command *head;
    size_t length;
}commands;

//contains all the arguments passed to a client thread in the server
typedef struct{
    //pid to create fifo name
    pid_t client_pid;
}client_info;

//head of a linked list containing the fd_s2c of every client
struct pipe_list{
    struct pipes_node *head;
};

//client thread pid and server to client pipes linked list
struct pipes_node{
    char* username;
    int fd_s2c;
    int fd_c2s;
    struct pipes_node *next;
};

#endif