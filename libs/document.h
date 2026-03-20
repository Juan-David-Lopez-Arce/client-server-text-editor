#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <stdbool.h>
/**
 * This file is the header file for all the document functions. You will be tested on the functions inside markdown.h
 * You are allowed to and encouraged multiple helper functions and data structures, and make your code as modular as possible. 
 * Ensure you DO NOT change the name of document struct.
 */

 typedef enum {
    VISIBLE,  
    DELETED,  
    NOT_VISIBLE_YET,
}Chunk_status;

typedef struct chunk{
    Chunk_status status;
    size_t length;
    struct chunk *next;
    char *content;
    bool is_ordered_list;
} chunk;

typedef struct {
    chunk *head;
    size_t length;
    uint64_t version;
} document;


// Functions from here onwards.
#endif