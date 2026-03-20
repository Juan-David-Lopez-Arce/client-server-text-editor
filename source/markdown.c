#include "../libs/markdown.h"
#include <string.h>
#include <stdbool.h>

#define SUCCESS 0 
#define ERROR -4
#define INVALID_POSITION -1
#define DELETED_POSITION -2
#define OUTDATED_VERSION -3
#define TRUE 1
#define FALSE 0
#define MAX_ORDERED_LIST 9

struct func_return{
    chunk *ch;
    size_t pos_inside_chunk;
};

//HELPERS

//asm: we assume that there will always be a newline after start_chunk (maybe not)
//this assumption comes from the place in which we call the function (when updating ordered lists)
//this method will get to the next chunk after newline that is not deleted
chunk* get_after_newline(chunk* start_chunk){
    chunk* cursor = start_chunk;
    while(1){
        if(!cursor){
            return cursor;
        }
        if(cursor->content[0] == '\n'){
            cursor = cursor->next;
            break;
        }
        cursor = cursor->next;
    }

    if(!cursor){
        //if its the end of the linked list, we just return
        return cursor;
    }
    
    //get to one that has not being deleted
    while(1){
        if(cursor->status == DELETED){
            if(!cursor->next){
                //if we reach the end, we just return the cursor
                return cursor;
            }
            cursor = cursor->next;
        }else{
            return cursor;
        }
    }

    return cursor;
}

struct func_return* get_to_pos(document *doc, size_t pos){

    /*
    if(pos > doc->length){
        printf("ERROR: POS IS OUT OF BOUNDS IN get_to_pos()\n");
        return NULL;
    }
    */
    size_t acum = 0;
    chunk* cursor = doc->head;
    struct func_return *ret = (struct func_return*) calloc(1, sizeof(struct func_return));
    if(!ret){
        printf("Calloc failed for func_return struct allocation\n");
        return NULL;
    }

    //if pos is 0, we get the head
    if(pos == 0){
        ret->ch = doc->head;
        ret->pos_inside_chunk = 0;
        return ret;
    }
    chunk* prev_chunk = cursor;
    while(1){
        if(!cursor){
            //TESTING NEW STUFF
            printf("CURSOR IS NULL");
            ret->ch = prev_chunk;
            ret->pos_inside_chunk = prev_chunk->length;
            return ret;
        }
        //Jump over chunks that have been just inserted and count chunks that have been deleted
        if(cursor->status == VISIBLE || cursor->status == DELETED){
            ret->pos_inside_chunk = pos - acum;
            acum += cursor->length;
            if(acum > pos){
                ret->ch = cursor;
                return ret;
            }else if (acum == pos){
                ret->ch = cursor;
                return ret;
            }
        }
        prev_chunk = cursor;
        cursor = (chunk*) cursor->next;
    }
}

void split_into_2(chunk* chunk_1, size_t pos_within_chunk){
    //first allocate memory for the first chunk
    chunk *chunk_2 = (chunk*) calloc(1, sizeof(chunk));
    if(!chunk_2){
        printf("Calloc failed for splitting\n");
        return;
    }

    //copy the same status from chunk 1
    chunk_2->status = chunk_1->status;
    chunk_2->length = chunk_1->length - pos_within_chunk;
    chunk_2->next = chunk_1->next;
    chunk_2->is_ordered_list = chunk_1->is_ordered_list;

    //now we split the content into the two chunks using realloc
    chunk_2->content = (char*) calloc(1, chunk_2->length);
    if(!chunk_2->content){
        printf("Calloc failed for allocating content at split\n");
        return;
    }

    //copy the data
    for(size_t i=0; i<chunk_2->length; i++){
        chunk_2->content[i] = chunk_1->content[pos_within_chunk+i];
    }

    //then we realloc the original chunk 
    chunk_1->content = (char*) realloc(chunk_1->content, pos_within_chunk);
    if(!chunk_1->content){
        printf("realloc failed allocating content at split\n");
        return;
    }

    //update the length of the first chunk
    chunk_1->length = pos_within_chunk;

    //then we link chunk 1 to chunk 2
    chunk_1->next = (struct chunk*) chunk_2;
}

//asm: pos will not be 0 and pos + len is within bounds for chunk
void split_into_3(chunk* chunk_1, size_t pos, size_t len){
    
    //1. We initialize two new chunks
    chunk* chunk_2 = (chunk*) calloc(1, sizeof(chunk));
    if(!chunk_2){
        printf("Calloc failed for 3 split\n");
        return;
    }
    chunk* chunk_3 = (chunk*) calloc(1, sizeof(chunk));
    if(!chunk_2){
        printf("Calloc failed for 3 split\n");
        return;
    }

    //setup length
    chunk_2->length = len;
    chunk_3->length = chunk_1->length - (pos + len);
    chunk_1->length = pos;

    //setup status
    chunk_2->status = chunk_1->status;
    chunk_3->status = chunk_1->status;

    //setup content
    //1. allocate space
    chunk_2->content = (char*) calloc(1, chunk_2->length);
    if(!chunk_2->content){
        printf("Calloc failed for chunk_2 (split_into_3)\n");
        return;
    }

    chunk_3->content = (char*) calloc(1, chunk_3->length);
    if(!chunk_3->content){
        printf("Calloc failed for chunk_3 (split_into_3)\n");
        return;
    }

    //copy the values
    for(size_t i=0; i<chunk_2->length; i++){
        chunk_2->content[i] = chunk_1->content[i+pos];
    }

    for(size_t i=0; i<chunk_3->length; i++){
        chunk_3->content[i] = chunk_1->content[i+pos+len];
    }

    //reallocate the buffer for the first chunk
    chunk_1->content = (char*) realloc(chunk_1->content, chunk_1->length);
    if(!chunk_1->content){
        printf("realloc failed for chunk_1 (split_into_3)\n");
        return;
    }

    //setup is_ordered_list
    chunk_2->is_ordered_list = chunk_1->is_ordered_list;
    chunk_2->is_ordered_list = chunk_1->is_ordered_list;

    //link
    chunk *next = chunk_1->next;
    chunk_1->next = chunk_2;
    chunk_2->next = chunk_3;
    chunk_3->next = next;
}

//returns the new position within the entire doc
size_t get_new_pos(document *doc, chunk *start_chunk, size_t pos_within_chunk, size_t pos_in_doc, int start){
    chunk* cursor;
    //this variable contains the final position where the cursor needs to be
    size_t end_pos = pos_in_doc;

    //if it is the start pos, we go right through the linked list
    if(start){
        cursor = start_chunk;
        //add the rest of the chunk to end_pos
        end_pos += (start_chunk->length - pos_within_chunk);
        //stops when the next chunk is not a deleted chunk
        while(cursor->next->status == DELETED){
            end_pos += cursor->length;
            cursor = (chunk*) cursor->next;
        }
        return end_pos;
    }

    end_pos -= pos_within_chunk;
    chunk* selected_chunk = start_chunk;
    //else, if it is the end we go left through the linked list
    while(selected_chunk->status == DELETED){
        cursor = doc->head;
        //we get the chunk right before selected chunk
        while(cursor->next != selected_chunk){
            cursor = (chunk*) cursor->next;
        }
        if(cursor->status != DELETED){
            return end_pos;
        }else{
            end_pos -= cursor->length;
            selected_chunk = cursor;
        }
    }

    printf("something went wrong in get_new_start_pos\n");
    return end_pos;

}

// === Init and Free ===

document *markdown_init(void) {
    //Allocate space for the document object
    document* doc = (document*) calloc(1, sizeof(document));
    if(!doc){
        printf("Calloc failed for document\n");
        return NULL;
    }
    //the length of the document is automatically set to 0
    //Allocate space for the first chunk of the document
    doc->head = (chunk*) calloc(1, sizeof(chunk));
    if(!doc->head){
        printf("Calloc failed for head\n");
        return NULL;
    }
    //set the status of the first chunk
    doc->head->status = NOT_VISIBLE_YET;
    doc->head->content = NULL;
    doc->head->next = NULL;
    doc->head->is_ordered_list = false;
    //the length and the next ptr are both set to 0 when memory was allocated
    doc->version = 0;
    //return a pointer to the created document
    return doc;
}

void markdown_free(document *doc) {
    //if the given pointer is null, display error and return
    if(!doc){
        return;
    }

    //we initialize a cursor to iterate through every chunk of the document
    chunk *cursor = doc->head;
    chunk *temp = cursor;
    while(cursor != NULL){
        //we free every chunk in the document
        cursor = (chunk *)cursor->next;
        //free the array of chars
        free(temp->content);
        free(temp);
        temp = cursor;
    }
    //finally we free the document itself
    free(doc);
    return;
}

// === Edit Commands ===
int markdown_insert(document *doc, uint64_t version, size_t pos, const char *content) {
    //ERROR HANDLING:
    //printf("pos: %zu, content: %s\n", pos, content);
    if(pos > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //Case 0: FIRST EVER INSERT -> In this case, we add the content to the head of the doc
    //We do not have to create a new node, we just use the available one
    size_t content_length = strlen(content);
    if(doc->head->length == 0){
        //we add the content to the head of the linked list
        doc->head->content = calloc(1, content_length);
        if(!doc->head->content){
            printf("Calloc failed for content in insert\n");
            return ERROR;
        }

        //we copy the values of content
        for(size_t i=0; i<content_length; i++){
            doc->head->content[i] = content[i];
        }
        //then we update length and status
        doc->head->length = content_length;
        doc->head->status = NOT_VISIBLE_YET;
        return SUCCESS;
    }

    //for the other cases:
    //First we create a chunk with the content that we wil insert
    chunk *insert_chunk = (chunk*) calloc(1, sizeof(chunk));
    if(!insert_chunk){
        printf("Calloc failed for insert chunk\n");
        return ERROR;
    }
    //It will not be visible until the next version is released
    insert_chunk->status = NOT_VISIBLE_YET;

    //we create a heap allocated buffer because we dont know where *content was allocated
    char *content_2 = (char*) calloc(1, content_length);
    if(!content_2){
        printf("Calloc failed for content in insert\n");
        return ERROR;
    }
    //we then copy the values of content
    for(size_t i=0; i<content_length; i++){
        content_2[i] = content[i];
    }
    insert_chunk->content = content_2;

    //the length of the chunk will be the length of the string + null byte
    insert_chunk->length = content_length;

    //boolean set to false by default
    insert_chunk->is_ordered_list = false;

    //Case 1: The insertion position is 0
    if(pos == 0){
        chunk *temp_head = doc->head;
        //The new chunk is set to be the new head
        insert_chunk->next = (struct chunk*) temp_head;
        doc->head = insert_chunk;

        return SUCCESS;
    }
    
    //we reach the chunk at which pos is located
    struct func_return* struct_ret = get_to_pos(doc, pos);
    chunk* chunk_at_pos = struct_ret->ch;
    size_t pos_inside_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);

    //Case 2: The insertion position is in the middle of two chunks or at the end of the linked list
    if(pos_inside_chunk == chunk_at_pos->length){
        //In this case we just add the new chunk to the linked list
        struct chunk *temp = chunk_at_pos->next;
        chunk_at_pos->next = (struct chunk*) insert_chunk;
        insert_chunk->next = temp;

        return SUCCESS;
    }

    //Cse 3: We have to split a chunk to insert
    //1.Splitting
    split_into_2(chunk_at_pos, pos_inside_chunk);
    //2.Inserting
    struct chunk *temp = chunk_at_pos->next;
    chunk_at_pos->next = (struct chunk*) insert_chunk;
    insert_chunk->next = temp;

    return SUCCESS;
}

int markdown_delete(document *doc, uint64_t version, size_t pos, size_t len) {
    //printf("pos: %zu, len: %zu\n", pos, len);
    //ERROR HANDLING:
    if(pos > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }
    //case 1: doc + len overflows (truncate as per the spec)
    if(pos + len > doc->length){
        len = doc->length - pos;
    }

    // we first have to get to the chunk at which pos is located
    struct func_return* struct_ret = get_to_pos(doc, pos);
    chunk* cursor = struct_ret->ch;
    size_t pos_in_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);
    //our cursor will start at the chunk at which pos is located
    //pos_in_chunk is the initual index at which pos is located within the chunks content

    //counts the number of characters left before completing the target of chars deleted
    size_t chars_left = len;
    while(chars_left > 0){
        //if the chunk is not visible yet, do not count it nor delete anything from it
        if(cursor->status == NOT_VISIBLE_YET){
            cursor = cursor->next;
            continue;
        }

        if(cursor->length - pos_in_chunk > chars_left){
            //if the part to be deleted is in the middle of one chunk, 
            //we split that chunk and delete the middle part
            if(pos_in_chunk > 0){
                split_into_3(cursor, pos_in_chunk, len);
                cursor->next->status = DELETED;
            }else{
                //If pos_in_chunk is at the start, we split into 2 and delete the left chunk
                split_into_2(cursor, chars_left);
                cursor->status = DELETED;
            }
            break;
        }else if(cursor->length - pos_in_chunk < chars_left){
            if(pos_in_chunk > 0){
                split_into_2(cursor, pos_in_chunk);
                cursor->next->status = DELETED;
                //update chars_left
                chars_left -= cursor->next->length;
                cursor = cursor->next->next;
            }else{
                cursor->status = DELETED;
                //update chars_left
                chars_left -= cursor->length;
                cursor = cursor->next;
            }
        }else{
            //if cursor->length - pos_in_chunk < chars_left
            if(pos_in_chunk == 0){
                //this means that exactly all the chunk must be deleted
                cursor->status = DELETED;
            }else{
                split_into_2(cursor, pos_in_chunk);
                cursor->next->status = DELETED;
            }
            break;
        }
        pos_in_chunk = 0;
    }
    //be carefull with the head.
    //If we are to delete the whole text, we just delete the content of the head
    //we do not delete the head itself
    return SUCCESS;
}

// === Formatting Commands ===
int markdown_newline(document *doc, uint64_t version, int pos) {
    //ERROR HANDLING
    if(pos > (int) doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //we just use the insert function previously defined
    if(markdown_insert(doc, version, pos, "\n") == SUCCESS){
        return SUCCESS;
    }else{
        printf("Error in insert function\n");
        return ERROR;
    }
}

int markdown_heading(document *doc, uint64_t version, int level, size_t pos) {
    //ERROR HANDLING
    if(pos > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //we first insert the element, then we check for newline
    if(level == 1){
        markdown_insert(doc, version, pos, "# ");
    }else if(level == 2){
        markdown_insert(doc, version, pos, "## ");
    }else{
        markdown_insert(doc, version, pos, "### ");
    }

    //if it is at the start, no newline is needed
    if(pos == 0){
        return SUCCESS;
    }

    //we reach the chunk at which pos is located
    struct func_return* struct_ret = get_to_pos(doc, pos);
    chunk* chunk_at_pos = struct_ret->ch;
    free(struct_ret);
    //if this chunk is not a newline, we insert one
    if(chunk_at_pos->length == 1){
        if(chunk_at_pos->content[0] != '\n'){
            markdown_newline(doc, version, pos);
        }
    }else{
        //if the length is not 1, it is obviously not a newline block
        markdown_newline(doc, version, pos);
    }

    return SUCCESS;
}

int markdown_bold(document *doc, uint64_t version, size_t start, size_t end) {
    //ERROR HANDLING
    if(start >= end){
        return INVALID_POSITION;
    }
    if(start > doc->length){
        return INVALID_POSITION;
    }
    if(end > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //to check for a DELETED_POSITION error, we first have to get to start and end
    struct func_return* struct_ret = get_to_pos(doc, start);
    chunk* chunk_at_start = struct_ret->ch;
    size_t pos_start_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);
    struct_ret = get_to_pos(doc, end);
    chunk* chunk_at_end = struct_ret->ch;
    size_t pos_end_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);

    if(chunk_at_start->status == DELETED){
        if(chunk_at_end->status == DELETED){
            //if both cursors are inside a deleted chunk, return error
            return DELETED_POSITION;
        }else{
            //if only the start is deleted, then modify the cursor
            //we modify it to be the very end of the deleted chunk (note that the deleted chunk might be a linked list)
            start = get_new_pos(doc, chunk_at_start, pos_start_chunk, start, TRUE);
            //printf("DEBUGGING - start: %d\n", (int) start);
        }
    }else if (chunk_at_end->status == DELETED){
        end = get_new_pos(doc, chunk_at_end, pos_end_chunk, end, FALSE);
        //printf("DEBUGGING - end: %d\n", (int) end);
    }

    if(markdown_insert(doc, version, start, "**") == SUCCESS){
        if(markdown_insert(doc, version, end, "**") == SUCCESS){
            return SUCCESS;
        }else{
            printf("Error in insert function\n");
            return ERROR;
        }
    }else{
        printf("Error in insert function\n");
        return ERROR;
    }
}

int markdown_italic(document *doc, uint64_t version, size_t start, size_t end) {
    //ERROR HANDLING
    if(start >= end){
        return INVALID_POSITION;
    }
    if(start > doc->length){
        return INVALID_POSITION;
    }
    if(end > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //to check for a DELETED_POSITION error, we first have to get to start and end
    struct func_return* struct_ret = get_to_pos(doc, start);
    chunk* chunk_at_start = struct_ret->ch;
    size_t pos_start_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);
    struct_ret = get_to_pos(doc, end);
    chunk* chunk_at_end = struct_ret->ch;
    size_t pos_end_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);

    if(chunk_at_start->status == DELETED){
        if(chunk_at_end->status == DELETED){
            //if both cursors are inside a deleted chunk, return error
            return DELETED_POSITION;
        }else{
            //if only the start is deleted, then modify the cursor
            //we modify it to be the very end of the deleted chunk (note that the deleted chunk might be a linked list)
            start = get_new_pos(doc, chunk_at_start, pos_start_chunk, start, TRUE);
            //printf("DEBUGGING - start: %d\n", (int) start);
        }
    }else if (chunk_at_end->status == DELETED){
        end = get_new_pos(doc, chunk_at_end, pos_end_chunk, end, FALSE);
        //printf("DEBUGGING - end: %d\n", (int) end);
    }

    if(markdown_insert(doc, version, start, "*") == SUCCESS){
        if(markdown_insert(doc, version, end, "*") == SUCCESS){
            return SUCCESS;
        }else{
            printf("Error in insert function\n");
            return ERROR;
        }
    }else{
        printf("Error in insert function\n");
        return ERROR;
    }
}

int markdown_blockquote(document *doc, uint64_t version, size_t pos) {
    //ERROR HANDLING
    if(pos > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //we first insert the element, then we check for newline
    markdown_insert(doc, version, pos, "> ");

    //then we check for newlines
    //if it is at the start, no newline is needed
    if(pos == 0){
        return SUCCESS;
    }
    //we reach the chunk at which pos is located
    struct func_return* struct_ret = get_to_pos(doc, pos);
    chunk* chunk_at_pos = struct_ret->ch;
    free(struct_ret);
    //if this chunk is not a newline, we insert one
    if(chunk_at_pos->length == 1){
        if(chunk_at_pos->content[0] != '\n'){
            markdown_newline(doc, version, pos);
        }
    }else{
        //if the length is not 1, it is obviously not a newline block
        markdown_newline(doc, version, pos);
    }

    return SUCCESS;
}

int markdown_ordered_list(document *doc, uint64_t version, size_t pos) {
    //ERROR HANDLING
    if(pos > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //we first insert the element, then we check for newline
    //we insert this so that enough space is allocated for a digit, a point and a space
    markdown_insert(doc, version, pos, "-. ");

    //FOR ORDERED LISTS, WE HAVE TO FLIP THE is_ordered_list BOOLEAN OF THE INSERTED CHUNK TO BE TRUE

    //edge case: If we want to insert an ordered list as the first element in the document,
    //we have to consider that this will not be a new chunk, but it'll be inserted within the content array of the head
    //this condition check also takes care of the thewline requirement for block-level elements 
    if(pos == 0){
        doc->head->is_ordered_list = true;
        return SUCCESS;
    }

    //we reach the chunk at which pos is located
    struct func_return* struct_ret = get_to_pos(doc, pos);
    chunk* chunk_at_pos = struct_ret->ch;
    free(struct_ret);

    //flip the boolean to true
    chunk_at_pos->next->is_ordered_list = true;

    //THEN WE CHECK FOR NEWLINES

    //if this chunk is not a newline, we insert one
    if(chunk_at_pos->length == 1){
        if(chunk_at_pos->content[0] != '\n'){
            markdown_newline(doc, version, pos);
        }
    }else{
        //if the length is not 1, it is obviously not a newline block
        markdown_newline(doc, version, pos);
    }

    return SUCCESS;
}

int markdown_unordered_list(document *doc, uint64_t version, size_t pos) {
    //ERROR HANDLING
    if(pos > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //we first insert the element, then we check for newline
    markdown_insert(doc, version, pos, "- ");

    //then we check for newlines
    //if it is at the start, no newline is needed
    if(pos == 0){
        return SUCCESS;
    }
    //we reach the chunk at which pos is located
    struct func_return* struct_ret = get_to_pos(doc, pos);
    chunk* chunk_at_pos = struct_ret->ch;
    free(struct_ret);
    //if this chunk is not a newline, we insert one
    if(chunk_at_pos->length == 1){
        if(chunk_at_pos->content[0] != '\n'){
            markdown_newline(doc, version, pos);
        }
    }else{
        //if the length is not 1, it is obviously not a newline block
        markdown_newline(doc, version, pos);
    }

    return SUCCESS;
}

int markdown_code(document *doc, uint64_t version, size_t start, size_t end) {
    //ERROR HANDLING
    if(start >= end){
        return INVALID_POSITION;
    }
    if(start > doc->length){
        return INVALID_POSITION;
    }
    if(end > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //to check for a DELETED_POSITION error, we first have to get to start and end
    struct func_return* struct_ret = get_to_pos(doc, start);
    chunk* chunk_at_start = struct_ret->ch;
    size_t pos_start_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);
    struct_ret = get_to_pos(doc, end);
    chunk* chunk_at_end = struct_ret->ch;
    size_t pos_end_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);

    if(chunk_at_start->status == DELETED){
        if(chunk_at_end->status == DELETED){
            //if both cursors are inside a deleted chunk, return error
            return DELETED_POSITION;
        }else{
            //if only the start is deleted, then modify the cursor
            //we modify it to be the very end of the deleted chunk (note that the deleted chunk might be a linked list)
            start = get_new_pos(doc, chunk_at_start, pos_start_chunk, start, TRUE);
            //printf("DEBUGGING - start: %d\n", (int) start);
        }
    }else if (chunk_at_end->status == DELETED){
        end = get_new_pos(doc, chunk_at_end, pos_end_chunk, end, FALSE);
        //printf("DEBUGGING - end: %d\n", (int) end);
    }

    //Now we insert the elements that make up the code
    if(markdown_insert(doc, version, start, "`") == SUCCESS){
        if(markdown_insert(doc, version, end, "`") == SUCCESS){
            return SUCCESS;
        }else{
            printf("Error in insert function\n");
            return ERROR;
        }
    }else{
        printf("Error in insert function\n");
        return ERROR;
    }

    return SUCCESS;
}

int markdown_horizontal_rule(document *doc, uint64_t version, size_t pos) {
    //ERROR HANDLING
    if(pos > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //we first insert the element, then we check for newline before
    markdown_insert(doc, version, pos, "---");

    //we reach the chunk at which pos is located
    struct func_return* struct_ret = get_to_pos(doc, pos);
    chunk* chunk_at_pos = struct_ret->ch;
    free(struct_ret);

    //ALL OF THE FOLLOWING IS DONE TO ENSURE THERES A NEWLINE AFTER THE NEWLY INSERTED CHUNK

    //we manually create a newline chunk
    chunk* newline = calloc(1, sizeof(chunk));
    if(!newline){
        printf("Calloc failed in horizontal rule\n");
        return ERROR;
    }
    newline->content = calloc(1, 1);
    if(!newline->content){
        printf("Calloc failed in horizontal rule\n");
        return ERROR;
    }
    newline->length = 1;
    newline->content[0] = '\n';
    newline->status = NOT_VISIBLE_YET;
    newline->is_ordered_list = false;

    //first check if the chunk after the inserted one is a newline
    chunk* after_horizontal_rule;
    bool head = false;

    if(!chunk_at_pos->next){
        //This means that the horizontal rule was the first element inserted in the doc
        //therefore, a new chunk was not created, this was just added to the content of the linked list
        chunk_at_pos->next = newline;
        newline->next = NULL;
        after_horizontal_rule = NULL;
        head = true;
    }else{
        after_horizontal_rule = chunk_at_pos->next->next;
    }
    
    //first we check if we have reached the end of the linked list
    if(!after_horizontal_rule && !head){
        chunk_at_pos->next->next = newline;
        newline->next = after_horizontal_rule;
    }else if(!head){
        if(after_horizontal_rule->length == 1){
            if(after_horizontal_rule->content[0] != '\n'){
                //we then add the newline to the linked list
                chunk_at_pos->next->next = newline;
                newline->next = after_horizontal_rule;
            }else{
                //free the memory of the node if it is not being used
                free(newline->content);
                free(newline);
            }
        }else{
            //we then add the newline to the linked list
            chunk_at_pos->next->next = newline;
            newline->next = after_horizontal_rule;
        }
    }

    //then we check for newlines at the start
    //if it is at the start, no newline is needed
    if(pos == 0){
        return SUCCESS;
    }

    //if the previous chunk is not a newline, we insert one
    if(chunk_at_pos->length == 1){
        if(chunk_at_pos->content[0] != '\n'){
            markdown_newline(doc, version, pos);
        }
    }else{
        //if the length is not 1, it is obviously not a newline block
        markdown_newline(doc, version, pos);
    }

    return SUCCESS;
    //this is a block element, therefore, we need a /n before
    //insert horizontal rule and newline after if it does not have one
}

int markdown_link(document *doc, uint64_t version, size_t start, size_t end, const char *url) {
    //ERROR HANDLING
    if(start >= end){
        return INVALID_POSITION;
    }
    if(start > doc->length){
        return INVALID_POSITION;
    }
    if(end > doc->length){
        return INVALID_POSITION;
    }
    if(version != doc->version){
        return OUTDATED_VERSION;
    }

    //to check for a DELETED_POSITION error, we first have to get to start and end
    struct func_return* struct_ret = get_to_pos(doc, start);
    chunk* chunk_at_start = struct_ret->ch;
    size_t pos_start_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);
    struct_ret = get_to_pos(doc, end);
    chunk* chunk_at_end = struct_ret->ch;
    size_t pos_end_chunk = struct_ret->pos_inside_chunk;
    free(struct_ret);

    if(chunk_at_start->status == DELETED){
        if(chunk_at_end->status == DELETED){
            //if both cursors are inside a deleted chunk, return error
            return DELETED_POSITION;
        }else{
            //if only the start is deleted, then modify the cursor
            //we modify it to be the very end of the deleted chunk (note that the deleted chunk might be a linked list)
            start = get_new_pos(doc, chunk_at_start, pos_start_chunk, start, TRUE);
            //printf("DEBUGGING - start: %d\n", (int) start);
        }
    }else if (chunk_at_end->status == DELETED){
        end = get_new_pos(doc, chunk_at_end, pos_end_chunk, end, FALSE);
        //printf("DEBUGGING - end: %d\n", (int) end);
    }

    //we allocate a buffer for the formatted url
    //the  numer 4 is for "]()\0"
    int len_buffer = strlen(url) + 4;
    char *buffer = calloc(1, len_buffer);
    if(!buffer){
        printf("Calloc failed inside markdown_link()\n");
        return ERROR;
    }

    //then we fill the buffer:
    buffer[0] = ']';
    buffer[1] = '(';
    for(int i=0; i<(int)strlen(url); i++){
        buffer[i+2] = url[i];
    }
    buffer[len_buffer - 2] = ')';
    buffer[len_buffer - 1] =  '\0';

    //Now we insert the elements that make up the code
    if(markdown_insert(doc, version, start, "[") == SUCCESS){
        if(markdown_insert(doc, version, end, buffer) == SUCCESS){
            free(buffer);
            return SUCCESS;
        }else{
            free(buffer);
            printf("Error in insert function\n");
            return ERROR;
        }
    }else{
        printf("Error in insert function\n");
        return ERROR;
    }
    return SUCCESS;
}

// === Utilities ===
char *markdown_flatten(const document *doc) {
    //iterate through the whole text and append chars to the buffer
    chunk* cursor = doc->head;
    size_t len = doc->length + 1;

    //we first have to create a buffer to store the restulting string
    char *buffer = (char*) calloc(1, len);
    if(!buffer){
        printf("Calloc failed for buffer (markdown_flatten)\n");
        return NULL;
    }

    //the we copy the content of the linked list into the buffer
    int total_counter = 0;
    while(cursor){
        //Deleted chunks are also shown because they belong to the outdated version
        //"ALL CHANGES SHOULD BE SHOWN AFTER A NEW VERSION IS RELEASED"
        if(cursor->status == VISIBLE || cursor->status == DELETED){
            for(size_t i=0; i<cursor->length; i++){
                buffer[total_counter++] = cursor->content[i];
            }
        }
        cursor = (chunk*) cursor->next;
    }
    buffer[len-1] = '\0';
    return buffer;
}

void markdown_print(const document *doc, FILE *stream) {
    if(doc->length == 0){
        return;
    }
    char* buffer = markdown_flatten(doc);
    fwrite(buffer, 1, doc->length, stream);
    printf("\n");
    fflush(stream);
    free(buffer);
}

// === Versioning ===
void markdown_increment_version(document *doc) {
    //increment version attribute
    doc->version ++;

    //we create a counter for the ordered list formatting
    int ordered_list_counter = 1;

    //Iterate through the linked list
    //1. remove all deleted chunks
    size_t len = 0;
    chunk* cursor = doc->head;
    chunk* last_cursor = doc->head;
    chunk* next_ordered_list;
    while(cursor){
        //1. check whether the chunks have been deleted
        if(cursor->status == DELETED){
            //case 1: The head has been deleted
            if(cursor == doc->head){
                //If the head is the only node, then just delete its content
                if(cursor->next == NULL){
                    free(doc->head->content);
                    doc->head->content = NULL;
                    doc->head->length = 0;
                    //we change the status for this node to be reused
                    doc->head->status = NOT_VISIBLE_YET;
                    break;
                }else{
                    doc->head = cursor->next;
                    free(cursor->content);
                    free(cursor);
                    cursor = doc->head;
                    last_cursor = doc->head;
                }
            }else{
                //case 2: the chunk deleted is not the head
                last_cursor->next = cursor->next;
                free(cursor->content);
                free(cursor);
                cursor = last_cursor->next;
            }
            //get rid of the deleted chunk
            continue;
        }
        //update the number of ordered lists
        if(cursor->is_ordered_list){
            //put the number in string format
            cursor->content[0] = '0' + ordered_list_counter;
            if(ordered_list_counter == MAX_ORDERED_LIST){
                //reset the counter when it reaches 9
                ordered_list_counter = 0;
            }
            //now we need to check if the next element is an ordered list, in order to reset the value if necessary
            next_ordered_list = get_after_newline(cursor);
            if(!next_ordered_list){
                //the end of the linked list is after the next newline
                ordered_list_counter = 0;
            }else{
                //if next_ordered_list is not null, reset counter
                if(!next_ordered_list->is_ordered_list){
                    ordered_list_counter = 0;
                }
            }
            //increment the counter every time we reach an ordered list chunk
            ordered_list_counter++;
        }

        //2. check whether the chunks are not visible and change their status
        if(cursor->status == NOT_VISIBLE_YET){
            cursor->status = VISIBLE;
        }
        //add the length of the ones that are visible and not visible yet
        len += cursor->length;
        last_cursor = cursor;
        cursor = (chunk*) cursor->next;
    }

    doc->length = len;
}