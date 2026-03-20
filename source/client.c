//TODO: client code that can send instructions to server.
#define _POSIX_C_SOURCE 200112L

//libs
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/types.h> // pid_t type
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/epoll.h>
//headers
#include "../libs/markdown.h"
#include "../libs/threads.h"

//MACROS
#define MAX_FIFO_NAME_LENGTH 20
#define MAX_USERNAME_LENGTH 100
#define ERROR -4
#define NUM_DIGITS_DOC_SIZE 155
#define COMMAND_LEN 256
#define MAX_COMMAND_NUM 100
#define WRITE "write\n"
#define READ "read\n"
#define NUM_RESTRICTED_SYMBOLS 9
#define LEN_ORDER_LIST 3
//all of this is for initially parsing the received document
#define NEWLINE_CHAR '\n'
#define NEWLINE_STR "\n"
#define NULL_BYTE '\0'
#define POINT '.'
#define SPACE ' '
#define ORDERED_LIST "-. "
#define MAX_EVENTS 2
#define DOC "DOC?\n"
#define PERM "PERM?\n"
#define LOG "LOG?\n"
#define DISCONNECT "DISCONNECT\n"
#define VERSION_KEYWORD "VERSION"
#define END_KEYWORD "END\n"

#define INSERT "INSERT"
#define DELETE "DEL"
#define NEWLINE "NEWLINE"
#define HEADING "HEADING"
#define BOLD "BOLD"
#define ITALIC "ITALIC"
#define BLOCKQUOTE "BLOCKQUOTE"
#define O_LIST "ORDERED_LIST"
#define U_LIST "UNORDERED_LIST"
#define CODE "CODE"
#define H_RULE "HORIZONTAL_RULE"
#define LINK "LINK"
#define REJECT_KW "Reject"


pid_t server_pid;
char *username;
//pipes
int fd_c2s;
int fd_s2c;
//permission (read/write)
char permission[COMMAND_LEN];
//document size
uint64_t doc_size;
//initial buffer to put the document
char* doc_buffer;
//LOCAL COPY OF THE DOCUMENT
document *doc = NULL;
//Log of commands sent from the server 
commands *command_log = NULL;
//this is a file object of fd_s2c used to be able to use fgets
FILE* file_s2c;
//boolean to idnicate when we will have to increase the version of our document
//only set to true when the server issues a new version
bool increment_version = true;

void free_resources_and_exit(){
    //close the file object
    fclose(file_s2c);
    //close pipes
    close(fd_s2c);
    close(fd_c2s);
    //free doc
    markdown_free(doc);
    //free log of commands
    if(command_log){
        //iterate through the linked list
        single_command *cursor = command_log->head;
        single_command *temp;
        while(cursor){
            free(cursor->instruction);
            temp = cursor->next;
            free(cursor);
            cursor = temp;
        }
    }
    exit(0);
}

//inserts chunk at the end of the linked list of doc
void insert_txt(const char* txt, int len, bool is_ordered_list){
    //if it has len 0, discard it
    if(len == 0){
        return;
    }
    //we insert in the head on the doc if its length is 0
    if(doc->length == 0){
        //modify the head array
        doc->head->content = (char*) calloc(1, len);
        if(!doc->head->content){
            printf("Calloc failed in insert_txt\n");
        }

        //we fill the content array of the chunk
        for(int i=0; i<len; i++){
            doc->head->content[i] = txt[i];
        }

        //finally, we update the variables of the head
        doc->head->is_ordered_list = is_ordered_list;
        doc->head->status = VISIBLE;
        doc->head->length = len;

        doc->length += len;
        return;
    }

    doc->length += len;

    //if it the doc already has content, we insert at the very end
    chunk* cursor = doc->head;
    while(cursor->next){
        cursor = cursor->next;
    }

    //then we make a chunk with the text
    chunk* new_chunk = (chunk*) calloc(1, sizeof(chunk));
    if(!new_chunk){
        printf("Calloc failed in insert_txt\n");
    }

    //we add it to the linked list
    cursor->next = new_chunk;

    //then we initialize the variables of the chunk
    new_chunk->is_ordered_list = is_ordered_list;
    new_chunk->length = len;
    new_chunk->next = NULL;
    new_chunk->status = VISIBLE;
    new_chunk->content = (char*) calloc(1, len);
    if(!new_chunk->content){
        printf("Calloc failed in insert_txt\n");
    }

    //finally, we fill the content array of the chunk
    for(int i=0; i<len; i++){
        new_chunk->content[i] = txt[i];
    }
    return;
}

void client_init(int argc, char**argv){
    if(argc <= 2){
        printf("Not enough arguments\n");
        return;
    }

    //get username and server_pid
    server_pid = (pid_t)atoi(argv[1]);
    username = argv[2];

    //send SIGTRMIN
    union sigval value;

    if (sigqueue(server_pid, SIGRTMIN, value) == -1) {
        printf("sigqueue error\n");
    }

    //block waiting for SIGRTMIN+1
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGRTMIN+1);

    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1){
        printf("sigprocmask error\n");
    }

    int signo;
    //this line will block until the signal is received
    if(sigwait(&set, &signo) != 0){
        printf("Error with sigwait\n");
    }

    
    if(signo != SIGRTMIN+1){
        printf("Received different signal\n");
    }

    //right after receiving the signal, the client opens the pipes syncronyzed with the server
    char c_to_s_name[MAX_FIFO_NAME_LENGTH];
    char s_to_c_name[MAX_FIFO_NAME_LENGTH];

    sprintf(c_to_s_name, "FIFO_C2S_%d", (int) getpid());
    sprintf(s_to_c_name, "FIFO_S2C_%d", (int) getpid());
    //open at the same time as the server to avoid blocking
    fd_c2s = open(c_to_s_name, O_WRONLY);
    fd_s2c = open(s_to_c_name, O_RDONLY);
    //fd_c2s = open(c_to_s_name, O_WRONLY);

    if(fd_c2s < 0 || fd_s2c < 0){
        printf("Error opening pipes in server\n");
    }

    char temp[MAX_USERNAME_LENGTH];
    snprintf(temp, sizeof(temp), "%s\n", username);
    strncpy(username, temp, MAX_USERNAME_LENGTH);

    //the client sends its username to the server
    if(write(fd_c2s, username, strlen(username)) == -1){
        printf("Write failed in client\n");
    }

    //WE THEN RECEIVE THE RESPONSE FROM THE SERVER TO OUR USERNAME

    //we first need to check if what was sent by the server is a rejection or the perm level
    file_s2c = fdopen(fd_s2c, "r");
    if (file_s2c == NULL) {
        printf("Error opening object file at client\n");
    }

    fgets(permission, COMMAND_LEN, file_s2c);
    if(!(!strcmp(permission, WRITE) || !strcmp(permission, READ))){
        //this means the client got rejected, therefore we close the pipes and exit
        free_resources_and_exit();
    }
    //if we get here, that means that the client is authorized
    //therefore, we store the version number
    char version_str[NUM_DIGITS_DOC_SIZE];
    fgets(version_str, NUM_DIGITS_DOC_SIZE, file_s2c);
    doc->version = (uint64_t)atoi(version_str);
    //then we get the length of the document
    char len_doc[NUM_DIGITS_DOC_SIZE];
    fgets(len_doc, NUM_DIGITS_DOC_SIZE, file_s2c);
    doc_size = (uint64_t)atoi(len_doc);

    //finally, we get the document
    doc_buffer = (char*) calloc(1, doc_size+1);
    if(!doc_buffer){
        printf("Calloc failed in client_init\n");
    }    
    int byted_read = fread(doc_buffer, 1, doc_size, file_s2c);
    if(byted_read != (int)doc_size){
        printf("Bytes read: %d", byted_read);
    }
    
    return;
}

void init_command_log(){
    command_log = (commands*) calloc(1, sizeof(commands));
    if (!command_log){
        printf("Calloc failed in main of client\n");
    }
    command_log->head = (single_command*) calloc(1, sizeof(single_command));
    if (!command_log->head){
        printf("Calloc failed in main of client\n");
    }
    command_log->head->instruction = NULL;
}

// We separate newlines and ordered lists into single chunks to ensure consistency in the document
void doc_init(){
    //we initialize a variable for every line of the doc, so that we can separate newlines and ordered lists from chunks
    char *line_of_doc = calloc(1, doc_size);
    if(!line_of_doc){
        printf("calloc failed in client main function\n");
    }

    char restricted_symbols[NUM_RESTRICTED_SYMBOLS];
    for(int i=1; i<=NUM_RESTRICTED_SYMBOLS; i++){
        //symbols from 1 to 9
        restricted_symbols[i-1] = '0' + i;
    }

    bool insert_next = true;
    int i = 0;
    int size_of_line = 0;
    char ordered_list_buffer[NUM_DIGITS_DOC_SIZE];
    while(true){
        //printf("i: %d, doc_size: %zu\n", i, doc_size);
        //first check if the chunk is a newline
        if(doc_buffer[i] == NEWLINE_CHAR){
            insert_txt(line_of_doc, size_of_line, false);
            memset(line_of_doc, 0, doc_size);
            size_of_line = 0;
            insert_txt(NEWLINE_STR, 1, false);
            i++;
        }
        //only here for the edge case when the first character is a newline
        if(i >= (int)doc_size){
            insert_txt(line_of_doc, size_of_line, false);
            break;
        }
        //then we check if the chunk is an ordered list
        for(int j=0; j<NUM_RESTRICTED_SYMBOLS; j++){
            if(doc_buffer[i] == restricted_symbols[j]){
                if(i+2 < (int)doc_size){
                    //we check whether it is within bounds for there to be an unordered list
                    if(doc_buffer[i+1] == POINT && doc_buffer[i+2] == SPACE){
                        insert_txt(line_of_doc, size_of_line, false);
                        memset(line_of_doc, 0, doc_size);
                        size_of_line = 0;
                        //FIX
                        sprintf(ordered_list_buffer, "%c. ", restricted_symbols[j]);
                        insert_txt(ordered_list_buffer, LEN_ORDER_LIST, true);
                        //UP TO HERE
                        insert_next = false;
                        i+=3;
                        break;
                    }
                }
            }
        }

        if(insert_next){
            //if it was not a newline or an ordered list, we just continue adding to our chunk
            line_of_doc[size_of_line++] = doc_buffer[i++];
        }else{
            insert_next = true;
        }

        if(i >= (int)doc_size){
            insert_txt(line_of_doc, size_of_line, false);
            break;
        }
    }

    //close the document buffers once we parse it into chunks
    free(line_of_doc);
    free(doc_buffer);
}

//this function is given a newline terminated string with a null byte at the end (a proper string)
void single_command_init(const char* instruction){
    //commands in clients only use their content array
    //the linked list of commands is already a global variable
    //if it is the first command, add it to the head
    single_command *node;
    single_command *cursor = command_log->head;
    if(command_log->length == 0){
        node = command_log->head;
    }
    else{
        //else, we get to the end of the linked list
        while(cursor->next){
            cursor = cursor->next;
        }
        //then we create a new command node
        node = (single_command*) calloc(1, sizeof(single_command));
        if (!node){
            printf("Error with calloc in single_command_init\n");
        }
        //we link the node to the linked list
        cursor->next = node;
        node->next = NULL;
    }

    //allocate memory for the char buffer
    node->instruction = (char*) calloc(1, COMMAND_LEN);
    if(!node->instruction){
        printf("Error with calloc in single_command_init\n");
    }
    //fill the content of the buffer
    for(int i=0; i<(int)strlen(instruction); i++){
        node->instruction[i] = instruction[i];
    }
    node->instruction[strlen(instruction)] = '\0';
    //we increment by 1 the number of commands
    command_log->length++;
    return;
}

void handle_log(){
    //iterate through command_log and print them 
    single_command *cursor = command_log->head;
    while(cursor){
        //instructions must be null terminated strings
        if(cursor->instruction){
            printf("%s", cursor->instruction);            
        }
        cursor = cursor->next;
    }
    fflush(stdout);
    return;
}

void handle_doc(){
    if(doc->length == 0){
        printf("\n");
        return;
    }
    markdown_print(doc, stdout);
    return;
}

void handle_perm(){
    printf("%s", permission);
    fflush(stdout);
    return;
}

//command must be a null terminated string (no newline character needed)
//asm: command contains (<command> SUCCESS) or (<command> Reject <reason>)
void command_identifier(char* command){
    if(strlen(command) == 0){
        return;
    }
    //1. Parse command
    char *list_of_tokens[10];
    char *saveptr;
    size_t pos;
    size_t pos_end;
    int level;
    int num_char;
    char *content;
    char *link;
    char *token = strtok_r(command, " ", &saveptr);
    int num_tokens = 0;
    while(token){
        list_of_tokens[num_tokens++] = token;
        token = strtok_r(NULL, " ", &saveptr);
    }

    //we check if the command was rejected
    //if it was, we return, else, we carry out the modifications
    for(int i=0; i<num_tokens; i++){
        if(strcmp(list_of_tokens[i], REJECT_KW) == 0){
            return;
        }
    }

    char* first_token = list_of_tokens[0];
    //we first need to check if the command was rejected

    //this is the only command for which pos is not the first keyword
    if(strcmp(first_token, HEADING) == 0){
        level = atoi(list_of_tokens[1]);
        pos = (size_t) atoi(list_of_tokens[2]);
        markdown_heading(doc, doc->version, level, pos);
        return;
    }

    //for the rest, pos is the first after the keyword
    pos = (size_t) atoi(list_of_tokens[1]);

    //we check the first word of the command and based on that we call the handlers
    if(strcmp(first_token, INSERT) == 0){
        content = list_of_tokens[2];
        markdown_insert(doc, doc->version, pos, content);
    }else if(strcmp(first_token, DELETE) == 0){
        num_char =  atoi(list_of_tokens[2]);
        markdown_delete(doc, doc->version, pos, num_char);
    }else if(strcmp(first_token, NEWLINE) == 0){
        markdown_newline(doc, doc->version, pos);
    }else if(strcmp(first_token, BOLD) == 0){
        pos_end = (size_t) atoi(list_of_tokens[2]);
        markdown_bold(doc, doc->version, pos, pos_end);
    }else if(strcmp(first_token, ITALIC) == 0){
        pos_end = (size_t) atoi(list_of_tokens[2]);
        markdown_italic(doc, doc->version, pos, pos_end);
    }else if(strcmp(first_token, BLOCKQUOTE) == 0){
        markdown_blockquote(doc, doc->version, pos);
    }else if(strcmp(first_token, O_LIST) == 0){
        markdown_ordered_list(doc, doc->version, pos);
    }else if(strcmp(first_token, U_LIST) == 0){
        markdown_unordered_list(doc, doc->version, pos);
    }else if(strcmp(first_token, CODE) == 0){
        pos_end = (size_t) atoi(list_of_tokens[2]);
        markdown_code(doc, doc->version, pos, pos_end);
    }else if(strcmp(first_token, H_RULE) == 0){
        markdown_horizontal_rule(doc, doc->version, pos);
    }else if(strcmp(first_token, LINK) == 0){
        pos_end = (size_t) atoi(list_of_tokens[2]);
        link = list_of_tokens[3];
        markdown_link(doc, doc->version, pos, pos_end, link);
    }else{
        printf("Unknown command from server\n");
    }
    return;
}

//we asume that the commands sent by the user will be processed by the server when they are malformed (IMPLEMENT)
void stdin_handler(){
    //we use fgets to read from stdin
    //we read until EOF, since there might be multiple commands in stdin
    char command[COMMAND_LEN];
    if(fgets(command, COMMAND_LEN, stdin) == NULL){
        printf("Problem at stdin_handler\n");
        return;
    }
    //check whether it is ?DOC, ?LOG, ?PERM or DISCONNECT
    if(strcmp(command, DOC) == 0){
        handle_doc();
    }else if(strcmp(command, LOG) == 0){
        handle_log();
    }else if(strcmp(command, PERM) == 0){
        handle_perm();
    }else{
        //for every other command, send it to the client for processing
        //write commmand without null termination
        write(fd_c2s, command, strlen(command));
    }
}

//we assume that all commands sent from the server are valid
void server_command_handler(){
    //I assumed that no more than 100 commands should enter at the same time
    char read_buffer[COMMAND_LEN*MAX_COMMAND_NUM];
    char *saveptr;

    // We first read all commands into a buffer and then process each individual lines
    int res = read(fd_s2c, read_buffer, COMMAND_LEN*MAX_COMMAND_NUM);
    //this means EOF is reached (the client typed DISCONNECT to stdin)
    if(res == 0){
        printf("Client disconnecting...\n");
        free_resources_and_exit();
    }
    if(res < 0){
        printf("Error with read\n");
    }

    
    int index_read = 0;
    while(index_read < res){
        char command_buffer[COMMAND_LEN];
        //we first process a line of the entire log of commands
        //ASSUMPTION: ALL COMMANDS ARE WELL PARSED (END WITH NEWLINE AND ARE SHORTER THAN 256 BYTES)
        int index_line = 0;
        while(read_buffer[index_read] != NEWLINE_CHAR){
            command_buffer[index_line++] = read_buffer[index_read++];
        }
        //we increment index_read so that the next command could be processed normally
        index_read++;
        //we add a newline at the end
        command_buffer[index_line++] = NEWLINE_CHAR;
        //then we add a null byte to be able to use string functions
        command_buffer[index_line++] = NULL_BYTE;
        //THEN WE JUST PROCESS THE LINE NORMALLY

        //regardless of what the server sends, we first add the command to the linked list
        single_command_init(command_buffer);

        //then we check whether it is an END command
        if(strcmp(command_buffer, END_KEYWORD) == 0){
            continue;
        }
        //we get the first token and check if its a version change
        char *token = strtok_r(command_buffer, " ", &saveptr);
        uint64_t new_version;
        if(strcmp(token, VERSION_KEYWORD) == 0){
            token = strtok_r(NULL, " ", &saveptr);
            new_version = (uint64_t) atoi(token);
            if(new_version != doc->version){
                //this means that we will need to increase version of our document
                increment_version = true;
            }
            //go to the next command, as this one was a version command
            continue;
        }

        //then we tockenize the rest of the command if it is not VERSION or END 
        //this will be <username>
        token = strtok_r(NULL, " ", &saveptr);
        //command starts from here 
        token =  strtok_r(NULL, " ", &saveptr);
        char buff[COMMAND_LEN];
        int buff_counter = 0;
        while(token != NULL){
            for(int i=0; i<(int)strlen(token); i++){
                buff[buff_counter++] = token[i];
            }
            buff[buff_counter++] = SPACE;
            token = strtok_r(NULL, " ", &saveptr);
        }
        //Add a null byte to make it a usable string
        buff[buff_counter++] = NULL_BYTE;
        command_identifier(buff);
    }

    //after iterating through all commands sent by the client,
    //increment version to implement changes (ONLY IF THERE ARE CHANGES)
    if(increment_version){
        markdown_increment_version(doc);
        increment_version = false;
    }
}

int main(int argc, char**argv){
    //we init our local copy of the document
    doc = markdown_init();

    //manages initial handshake
    client_init(argc, argv);

    //SYNC THE LOCAL COPY OF THE DOCUMENT
    doc_init();

    //we then initialize our command log
    init_command_log();

    //create epoll to listen to stdin and fd_s2c
    int ep_fd = epoll_create1(0);
    struct epoll_event ev_info;
    //we set the memory to 0 for safety
    memset(&ev_info, 0, sizeof(struct epoll_event));
    ev_info.events = EPOLLIN;

    
    //first we put the server to client pipe into the epoll fd
    ev_info.data.fd = fd_s2c;
    if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, fd_s2c, &ev_info) == -1) {
        printf("epoll_ctl failed for fd_s2c\n");
        return -1;
    }
    
    //then we put stdin
    ev_info.data.fd = STDIN_FILENO;
    if(epoll_ctl(ep_fd, EPOLL_CTL_ADD, STDIN_FILENO, &ev_info) == -1){
        printf("epoll_ctl failed for stdin\n");
        return -1;
    }

    //here we will store the fdes info when any of the streams are ready to read
    struct epoll_event events[MAX_EVENTS];
    int res;
    //ENTER COMMAND LOOP
    while(true){
        //This line will block until the server or stdin have data
        res = epoll_wait(ep_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < res; i++) {
            if (events[i].events & EPOLLHUP){
                free_resources_and_exit();
            }
            if(events[i].events & EPOLLERR){
                continue;
            }
            if (events[i].data.fd == fd_s2c){
                //LOGIC TO HANDLE COMMANDS FROM THE SERVER PIPE
                server_command_handler();
            }
            else if (events[i].data.fd == STDIN_FILENO){
                stdin_handler();
            }
        }
        memset(events, 0, MAX_EVENTS*sizeof(struct epoll_event));
    }

    return 0;
}