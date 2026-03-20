// TODO: server code that manages the document and handles client instructions
#define _POSIX_C_SOURCE 200112L
//imports
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/epoll.h>
//headers
#include "../libs/markdown.h"
#include "../libs/threads.h"
//macros
#define COMMAND_LEN 256
#define MAX_BUFFER_ROLES 50
#define MAX_FIFO_NAME_LENGTH 20
#define ROLES_FILENAME "roles.txt"
#define ERROR -10
#define MAX_LINE_BUFFER 100
#define NUM_DIGITS_DOC_SIZE 155
#define DISCONNECT "DISCONNECT\n"
#define DOC "DOC?\n"
#define LOG "LOG?\n"
#define QUIT "QUIT\n"
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
#define SUCCESS 0
#define INVALID_CURSOR_POS -1
#define DELETED_POSITION -2
#define OUTDATED_VERSION -3
#define UNAUTHORISED -4
#define WRITE "write\n"
#define COMMAND_LEN 256
#define MAX_COMMAND_NUM 100

//STRUCTS
struct ret_merge{
    size_t len;
    char *buffer;
};

//GLOBAL VARIABLES
//specified as an arg when initializing the server
int time_interval;
//contains the entire document in its most updated version
document *doc;
//mutex to guard the doc
pthread_mutex_t doc_mutex = PTHREAD_MUTEX_INITIALIZER;

//contains an array of commands for the current version
commands *version_commands;
//mutex to guard version commands
pthread_mutex_t version_commands_mutex = PTHREAD_MUTEX_INITIALIZER;

//conatins a log of all commands applied on the document
//this linked list does not need any mutexes (it is only accessed by one thread)
commands *all_commands;
//mutex ti guard all_commands
pthread_mutex_t all_commands_mutex = PTHREAD_MUTEX_INITIALIZER;

//linked list containing all the usernames and pipes of clients
struct pipe_list *pipes;
//mutex to guard pipes
pthread_mutex_t pipes_mutex = PTHREAD_MUTEX_INITIALIZER;

//****** HELPER FUNCTIONS ******//
//creates and adds command to version commands in sorted order
void add_to_version_commands(time_t timestamp, char* instruction, char* user, char* permission){
    //first we create the command

    single_command *command = (single_command*) calloc(1, sizeof(single_command));
    if(!command){
        printf("Calloc failed in add_to_version_commands\n");
    }
    command->timestamp = timestamp;
    //we copy the values of the strings
    command->instruction = (char*) calloc(1, COMMAND_LEN);
    if(!command->instruction){
        printf("Calloc failed in add_to_version_commands\n");
    }
    command->user = (char*) calloc(1, COMMAND_LEN);
    if(!command->user){
        printf("Calloc failed in add_to_version_commands\n");
    }
    //set the command permission
    if(strcmp(permission, WRITE) == 0){
        //this means the client has write permission
        command->write_perm = true;
    }else{
        command->write_perm = false;
    }

    strcpy(command->instruction, instruction);
    strcpy(command->user, user);
    //set the next node to null
    command->next = NULL;

    //then we add the command to the linked list in sorted order

    //case 1: the version commands linked list is empty
    if(!version_commands->head){
        version_commands->head = command;
        return;
    }
    //case 2: the new command was sent earlier than the head
    if(command->timestamp < version_commands->head->timestamp){
        command->next = version_commands->head;
        version_commands->head = command;
        return;
    }
    //case 3: we iterate through the linked list to find the place the command belongs to
    single_command *cursor = version_commands->head;
    while(cursor->next){
        if(cursor->timestamp < command->timestamp){
            if(command->timestamp < cursor->next->timestamp){
                //insert the node in the correct position
                command->next = cursor->next;
                cursor->next = command;
                return;
            }
        }
        cursor = cursor->next;
    }
    //case 4: the command goes at the end of the linked list
    cursor->next = command;
    return;
}

//iterates through pipes until finding username. Eliminate that chunk
void remove_pipe(char* username){
    struct pipes_node *cursor = pipes->head;
    //first check the head, then iterate through the linked list
    if(strcmp(cursor->username, username) == 0){
        pipes->head = cursor->next;
        free(cursor->username);
        free(cursor);
        return;
    }

    while(cursor->next){
        if(strcmp(cursor->next->username, username) == 0){
            struct pipes_node *temp = cursor->next->next;
            free(cursor->next->username);
            free(cursor->next);
            cursor->next = temp;
            return;
        }
        cursor = cursor->next;
    }
    printf("ERROR: USERNAME NOT FOUND\n");
}

void init_command_log(){
    all_commands = (commands*) calloc(1, sizeof(commands));
    if(!all_commands){
        printf("Calloc failed in init_command_log\n");
    }
    version_commands = (commands*) calloc(1, sizeof(commands));
    if(!version_commands){
        printf("Calloc failed in init_command_log\n");
    }
}

void add_client_pipe(int fd_s2c, int fd_c2s, char* username){
    //we initialize a new node
    struct pipes_node *new_node = (struct pipes_node*) calloc(1, sizeof(struct pipes_node));
    if(!new_node){
        printf("Calloc failed in add_client_pipe\n");
    }

    new_node->fd_s2c = fd_s2c;
    new_node->fd_c2s = fd_c2s;
    //we allocate memory for the username
    char *buffer = (char*) calloc(1, MAX_BUFFER_ROLES);
    if(!buffer){
        printf("Calloc failed in add_client_pipe\n");
    }
    strcpy(buffer, username);
    new_node->username = buffer;
    new_node->next = NULL;

    //NOW WE LINK THE NODE WITH THE LIST(pipes)

    //if the head is null, we create a new head
    if(!pipes->head){
        pipes->head = new_node;
        return;
    }
    //else we get to the last position of the linked list and insert the node there
    struct pipes_node* cursor = pipes->head;
    while(cursor->next){
        cursor = cursor->next;
    }
    //we add it to the linked list
    cursor->next = new_node;
    return;
}

void init_pipes(){
    //init memory for linked list
    pipes = (struct pipe_list*) calloc(1, sizeof(struct pipe_list));
    if(!pipes){
        printf("Calloc failed in init_pipes\n");
    }
    //set the head to null
    pipes->head = NULL;
}

void disconnect(int fd_s2c, int fd_c2s, const char* pipe1, const char* pipe2, FILE* F_c2s, char* permission){
    close(fd_c2s);
    close(fd_s2c);
    fclose(F_c2s);
    unlink(pipe1);
    unlink(pipe2);
    free(permission);
    return;
}

struct ret_merge* merge_perm_version_doc(char *permission){
    char version[MAX_LINE_BUFFER];
    sprintf(version, "%ld\n", doc->version);
    size_t len_permission = strlen(permission);
    size_t len_version = strlen(version);
    //we need to get the size of the document
    chunk* cursor = doc->head;
    size_t len_doc = doc->length;

    char doc_size_str[NUM_DIGITS_DOC_SIZE];
    //we put the size of the document into a buffer
    sprintf(doc_size_str, "%zu\n", len_doc);

    size_t len_doc_size_buffer = strlen(doc_size_str);

    struct ret_merge* ret = (struct ret_merge*) malloc(sizeof(struct ret_merge));
    if(!ret){
        printf("Malloc failed in merge_perm_version_doc\n");
    }

    //next, we create a buffer big enough to store the data
    //add a +1 for debugging
    ret->buffer = (char*) calloc(1, len_permission + len_version + len_doc + len_doc_size_buffer+1);
    if(!ret->buffer){
        printf("Error with calloc in merge_perm_version_doc\n");
    }

    char* buffer = ret->buffer;

    //finally, we put the data into the buffer
    int counter = 0;
    for(int i=0; i<(int)len_permission; i++){
        if(permission[i] != '\0'){
            buffer[counter++] = permission[i];
        }
    }
    for(int i=0; i<(int)len_version; i++){
        if(version[i] != '\0'){
            buffer[counter++] = version[i];
        }
    }

    //then we add the size of the document
    for(int i=0; i<(int)len_doc_size_buffer; i++){
        if(doc_size_str[i] != '\0'){
            buffer[counter++] = doc_size_str[i];
        }
    }

    //then we iterate through the document
    cursor = doc->head;
    while(cursor){
        for(int i=0; i<(int)cursor->length; i++){
            buffer[counter++] = cursor->content[i];
        }
        cursor = cursor->next;
    }
    
    ret->len = counter;
    return ret;
}

char* write_permission(FILE *roles_file, char* client_username){
    //We allocate a buffer in the heap so that we can return a string with the permission
    char* return_val = (char*) calloc(1, MAX_LINE_BUFFER);
    char line_buffer[MAX_BUFFER_ROLES];
    int count_chars = 0;
    int j;
    while(fgets(line_buffer, MAX_BUFFER_ROLES, roles_file) != NULL){
        for(int i=0; i< (int) strlen(client_username); i++){
            if(line_buffer[i] == client_username[i]){
                count_chars++;
            }else{
                //if at the start, the characters are not the same, the username is not there
                //therefore, we continue checking the next lines
                break;
            }
            //if the username is found in roles.txt, we see the permission
            if(count_chars == (int) strlen(client_username) - 1){
                i++;
                //we iterate through the line buffer to see the permission
                j = i;
                while(line_buffer[j] == ' '){
                    j++;
                }
                //then we write the permission to the buffer, it also writes the newline, thats why there is a +1
                sprintf(return_val, "%s", &line_buffer[j]);
                return return_val;
            }
        }
    }
    free(return_val);
    //We get here if the username is not in the whole document
    return NULL;
}

void server_init(int argc, char**argv){
    //checks number of arguments
    if(argc < 2){
        printf("Not enough arguments\n");
        exit(0);
    }

    time_interval = atoi(argv[1]);
    //if atoi returns 0, that means that the time interval is not an int or if the time intered is 0
    //either way, it is not accepted
    if(time_interval == 0){
        printf("atoi function failed, invalid input.\n");
        exit(0);
    }

    pid_t server_pid = getpid();
    //print pid to stout
    printf("Server PID: %d\n", (int)server_pid);
    return;
}

//returns array with two ints which are the pipes
char** fifo_init(pid_t client_pid){
    //we initalize buffers to store the names of the pipes
    //we take into account that the max number of digits in pid is 7
    char c_to_s_name[MAX_FIFO_NAME_LENGTH];
    char s_to_c_name[MAX_FIFO_NAME_LENGTH];

    // check if pipes exist
    sprintf(c_to_s_name, "FIFO_C2S_%d", (int) client_pid);
    sprintf(s_to_c_name, "FIFO_S2C_%d", (int) client_pid);

    if (access(c_to_s_name, F_OK) == 0) {
        printf("File exists.\n");
        unlink(c_to_s_name);
    }
    if (access(s_to_c_name, F_OK) == 0) {
        printf("File exists.\n");
        unlink(s_to_c_name);
    }

    // Create pipes
    if(mkfifo(c_to_s_name, 0666) == -1){
        printf("Error with mkfifo\n");
    }
    if(mkfifo(s_to_c_name, 0666) == -1){
        printf("Error with mkfifo\n");
    }

    char **ret_array = (char**) calloc(1, 2*sizeof(char*));
    if(!ret_array){
        printf("Malloc failed in fifo_init\n");
    }
    ret_array[0] = (char*) calloc(1, MAX_FIFO_NAME_LENGTH);
    if(!ret_array[0]){
        printf("Malloc failed in fifo_init\n");
    }
    ret_array[1] = (char*) calloc(1, MAX_FIFO_NAME_LENGTH);
    if(!ret_array[1]){
        printf("Malloc failed in fifo_init\n");
    }
    for(int i=0; i<=(int)strlen(c_to_s_name); i++){
        ret_array[0][i] = c_to_s_name[i];
    }
    for(int i=0; i<=(int)strlen(s_to_c_name); i++){
        ret_array[1][i] = s_to_c_name[i];
    }

    return ret_array;
}

//****** CLIENT MANAGER THREAD ******//

void* client_thread(void *data){
    // get data
    client_info *info = (client_info*) data;
    pid_t client_pid = info->client_pid;
    free(info);

    // create named pipes
    char** names = fifo_init(client_pid);
    char *c_to_s_name = names[0];
    char *s_to_c_name = names[1];

    // Send signal
    union sigval value;
    if (sigqueue(client_pid, SIGRTMIN+1, value) == -1) {
        printf("sigqueue error\n");
    }

    //open the fifos in nonblocking blocking mode
    int fd_c2s;
    int fd_s2c;
    fd_c2s = open(c_to_s_name, O_RDONLY);
    fd_s2c = open(s_to_c_name, O_WRONLY);

    if(fd_c2s < 0 || fd_s2c < 0){
        printf("Error opening pipes in server\n");
    }

    //Block until the client sends username through pipe
    char client_username[MAX_BUFFER_ROLES];
    //create epoll instance to monitor the pipe
    int ep_fd = epoll_create1(0);
    struct epoll_event ev_info = {.events = EPOLLIN, .data.fd = fd_c2s};
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, fd_c2s, &ev_info);
    //here we will store the fdes info when it is ready (only the fd_c2s)
    struct epoll_event events[1];
    //This line will block until the client has written to its pipe

    // WE OPEN AN OBJECT FOR fd_c2s
    FILE* F_c2s = fdopen(fd_c2s, "r");
    if(!F_c2s){
        printf("Error opening file object with fd_c2s\n");
    }

    int res = epoll_wait(ep_fd, events, 1, -1);
    if(res == -1){
        printf("Error with epoll_wait\n");
    }
    if (events[0].events & EPOLLHUP || events[0].events & EPOLLERR) {
        printf("Something is wrong with fd\n");
    }
    else {
        fgets(client_username, MAX_BUFFER_ROLES, F_c2s);
    }

    //1. CHECK IF THE USERNAME IS IN THE TXT FILE
    //for this, we first open the file
    FILE *roles_file = fopen(ROLES_FILENAME, "r");
    if (roles_file == NULL) {
        printf("Error opening roles file\n");
    }
    //inside the function, we handle the logic to check if the user is in roles.txt
    //here, in this condition check, we manage the logic for when a user is not found
    //we initialize a version buffer because we may have to print it in the document we send the client
    char* permision = write_permission(roles_file, client_username);
    char* buffer;
    struct ret_merge* return_values;
    size_t len;
    char rejection[] = "Reject UNAUTHORISED\n";
    if(!permision){
        //this path is taken if the username is not found
        printf("Not found\n");
        //first we send the rejection command
        write(fd_s2c, rejection, strlen(rejection));
        //sleep for a second and free resources
        sleep(1);
        disconnect(fd_s2c, fd_c2s, c_to_s_name, s_to_c_name, F_c2s, permision);
        pthread_exit(NULL);
    }else{
        printf("Client found\n");
        //send permission, version, length, and full document

        //MUTEX LOCK
        pthread_mutex_lock(&doc_mutex);
        return_values = merge_perm_version_doc(permision);
        
        //Finaly, we send the information to the client
        buffer = return_values->buffer;
        len = return_values->len;

        write(fd_s2c, buffer, len);
        //MUTEX UNLOCK
        pthread_mutex_unlock(&doc_mutex);
        free(return_values->buffer);
        free(return_values);
        //we wait until the whole document is sent before any version changes happen
    }

    /*
    we then add the client to the pipes list
    this enables the client to receive new versions
    *///MUTEX LOCKED
    pthread_mutex_lock(&pipes_mutex);
    add_client_pipe(fd_s2c, fd_c2s, client_username);
    //MUTEX UNLOCKED
    pthread_mutex_unlock(&pipes_mutex);

    //Main control loop to poll the client pipe
    while(true){
        char buffer[MAX_LINE_BUFFER];
        fgets(buffer, MAX_LINE_BUFFER, F_c2s);
        if(strcmp(buffer, DISCONNECT) == 0){
            //HANDLE LOGIC FOR DISCONNECTION
            //if it enters here, the loop breaks
            disconnect(fd_s2c, fd_c2s, c_to_s_name, s_to_c_name, F_c2s, permision);
            //MUTEX LOCK
            pthread_mutex_lock(&pipes_mutex);
            remove_pipe(client_username);
            pthread_mutex_unlock(&pipes_mutex);
            //MUTEX UNLOCK
            pthread_exit(NULL);
        }
        //normal logic (NO DISCONNECTION)
        time_t timestamp = time(NULL);
        //adds the command to the version linked list in sorted order
        //MUTEX LOCK
        pthread_mutex_lock(&version_commands_mutex);
        add_to_version_commands(timestamp, buffer, client_username, permision);
        //MUTEX UNLOCK
        pthread_mutex_unlock(&version_commands_mutex);
    }
    return NULL;
}

//****** THREADS CREATED FOR THE RUNNING SERVER (EVEN WITHOUT CLIENTS) ******//
//and helper functions specific to those threads

void handle_doc(){
    if(doc->length == 0){
        printf("\n");
        return;
    }
    markdown_print(doc, stdout);
    return;
}
 
void handle_log(){
    //iterate through command_log and print them 
    single_command *cursor = all_commands->head;
    while(cursor){
        //instructions must be null terminated strings
        printf("%s", cursor->instruction);
        cursor = cursor->next;
    }
    fflush(stdout);
    return;
}

void handle_quit(){
    //first, to check if there are any clients left, we check the pipes linked list
    if(pipes->head != NULL){
        //this means there are still clients connected 
        struct pipes_node *cursor = pipes->head;
        int counter = 0;
        while(cursor){
            counter++;
            cursor = cursor->next;
        }
        printf("QUIT rejected, %d clients still connected.\n", counter);
        return;
    }
    //If no clients are connected:
    // write the document to doc.md
    FILE* doc_md = fopen("doc.md", "w");
    if(!doc_md){
        printf("Errror opening doc.md in handle_quit()\n");
    }

    markdown_print(doc, doc_md);
    //free resources
    fclose(doc_md);
    markdown_free(doc);

    single_command *cursor = all_commands->head;
    single_command *temp;
    while(cursor){
        temp = cursor;
        cursor = cursor->next;
        free(temp->instruction);
        free(temp->user);
        free(temp);
    }
    free(pipes);

    cursor = version_commands->head;
    while(cursor){
        temp = cursor;
        cursor = cursor->next;
        free(temp->instruction);
        free(temp->user);
        free(temp);
    }
    free(version_commands);
    //end the entire program
    fflush(stdout);
    fflush(stderr);
    _exit(0);
}

//manages polling stdin. Is in charge of identifying DOC?, LOG?, QUIT
void* stdin_listener_thread(void* arg){
    while(true){
        char buffer[MAX_LINE_BUFFER];
        fgets(buffer, MAX_LINE_BUFFER, stdin);
        if(strcmp(buffer, DOC) == 0){
            //manage DOC? logic
            //MUTEX LOCK
            pthread_mutex_lock(&doc_mutex);
            handle_doc();
            pthread_mutex_unlock(&doc_mutex);
        }else if(strcmp(buffer, LOG) == 0){
            //manage LOG? logic
            //MUTEX LOCK
            pthread_mutex_lock(&all_commands_mutex);
            handle_log();
            pthread_mutex_unlock(&all_commands_mutex);
            //MUTEX UNLOCK
        }else if(strcmp(buffer, QUIT) == 0){
            //manage QUIT logic
            handle_quit();
        }else{
            printf("Command was not recognised\n");
        }
    }
    (void)arg;
    return NULL;
}

//identifies and runs commands in markdown.c function
int doc_modifier(char *command, bool permission){
    //if the client does not have write permission, just return error number
    if(!permission){
        return UNAUTHORISED;
    }
    //we make a buffer so that we do not damadge the original command string
    char command_buff[COMMAND_LEN];

    strcpy(command_buff, command);

    char *saveptr;
    char *token = strtok_r(command_buff, " ", &saveptr);
    //this is the only command for which pos is not the first keyword
    if(strcmp(token, HEADING) == 0){
        int level = atoi(strtok_r(NULL, " ", &saveptr));
        size_t pos = (size_t) atoi(strtok_r(NULL, " ", &saveptr));
        
        return (markdown_heading(doc, doc->version, level, pos));
    }
    //for the rest, pos is the first keyword
    size_t pos = (size_t) atoi(strtok_r(NULL, " ", &saveptr));
    size_t pos_end;
    int result;
    //we check the first word of the command and based on that we call the handlers
    if(strcmp(token, INSERT) == 0){
        saveptr[strlen(saveptr)-1] = '\0';
        result = markdown_insert(doc, doc->version, pos, saveptr);
    }else if(strcmp(token, DELETE) == 0){
        int num_char =  atoi(strtok_r(NULL, " ", &saveptr));
        result = markdown_delete(doc, doc->version, pos, num_char);
    }else if(strcmp(token, NEWLINE) == 0){
        result = markdown_newline(doc, doc->version, pos);
    }else if(strcmp(token, BOLD) == 0){
        pos_end = (size_t) atoi(strtok_r(NULL, " ", &saveptr));
        result = markdown_bold(doc, doc->version, pos, pos_end);
    }else if(strcmp(token, ITALIC) == 0){
        pos_end = (size_t) atoi(strtok_r(NULL, " ", &saveptr));
        result = markdown_italic(doc, doc->version, pos, pos_end);
    }else if(strcmp(token, BLOCKQUOTE) == 0){
        result = markdown_blockquote(doc, doc->version, pos);
    }else if(strcmp(token, O_LIST) == 0){
        result = markdown_ordered_list(doc, doc->version, pos);
    }else if(strcmp(token, U_LIST) == 0){
        result = markdown_unordered_list(doc, doc->version, pos);
    }else if(strcmp(token, CODE) == 0){
        pos_end = (size_t) atoi(strtok_r(NULL, " ", &saveptr));
        result = markdown_code(doc, doc->version, pos, pos_end);
    }else if(strcmp(token, H_RULE) == 0){
        result = markdown_horizontal_rule(doc, doc->version, pos);
    }else if(strcmp(token, LINK) == 0){
        pos_end = (size_t) atoi(strtok_r(NULL, " ", &saveptr));
        saveptr[strlen(saveptr) - 1] = '\0';
        result = markdown_link(doc, doc->version, pos, pos_end, saveptr);
    }else{
        printf("Unknown command from server\n");
        return ERROR;
    }
    return result;
}

//makes a command in the form EDIT <user> <command> SUCCESS
//OR EDIT <user> <command> Reject <reason>
char* command_parser(int ret_val, char* user, char* command){
    //create buffer
    char* edited_command = (char*) calloc(1, COMMAND_LEN);
    if(!edited_command){
        printf("Calloc failed in command_parser\n");
    }

    //insert EDIT
    edited_command[0] = 'E';
    edited_command[1] = 'D';
    edited_command[2] = 'I';
    edited_command[3] = 'T';
    edited_command[4] = ' ';

    int pos_edited_command = 5;
    //then we insert the username
    //we do len - 1 to not include the newline character
    for(int i=0; i<(int)(strlen(user) - 1); i++){
        edited_command[pos_edited_command++] = user[i];
    }
    edited_command[pos_edited_command++] = ' ';
    //we insert the command
    //we do len - 1 to not include the newline character
    for(int i=0; i<(int)(strlen(command) - 1); i++){
        edited_command[pos_edited_command++] = command[i];
    }
    
    //then we add success or reject
    if(ret_val == SUCCESS){
        char* success = " SUCCESS\n";
        for(int i=0; i<(int)strlen(success); i++){
            edited_command[pos_edited_command++] = success[i];
        }
        return edited_command;
    }

    //If we get here, it means the command was rejected
    char* reject = " Reject ";
    for(int i=0; i<(int)strlen(reject); i++){
        edited_command[pos_edited_command++] = reject[i];
    }

    if(ret_val == INVALID_CURSOR_POS){
        char *invalid_pos = "INVALID_CURSOR_POS\n";
        for(int i=0; i<(int)strlen(invalid_pos); i++){
            edited_command[pos_edited_command++] = invalid_pos[i];
        }
    } else if(ret_val == DELETED_POSITION){
        char *del_pos = "DELETED_POSITION\n";
        for(int i=0; i<(int)strlen(del_pos); i++){
            edited_command[pos_edited_command++] = del_pos[i];
        }
    } else if(ret_val == OUTDATED_VERSION){
        char *out_ver = "OUTDATED_VERSION\n";
        for(int i=0; i<(int)strlen(out_ver); i++){
            edited_command[pos_edited_command++] = out_ver[i];
        }
    } else if(ret_val == UNAUTHORISED){
        char *unautho = "UNAUTHORISED\n";
        for(int i=0; i<(int)strlen(unautho); i++){
            edited_command[pos_edited_command++] = unautho[i];
        }
    }
    //we add a null byte so that we can process each command in flatten_version_commands
    edited_command[pos_edited_command] = '\0';

    return edited_command;
}

//modifies the instruction buffer of every command in version_commands
//for this, command_parser and doc_modifier are called
void command_processor(){
    bool successful_edit = false;
    if(!version_commands->head){
        return;
    }
    single_command* cursor = version_commands->head;
    //we modify the instruction buffer of every command in version_commands
    while(cursor){
        //we carry out the edit in the local doc and we get the return value
        //return value depends on whether the command is successfull
        int ret_val = doc_modifier(cursor->instruction, cursor->write_perm);
        if(ret_val == SUCCESS){
            //this means that there was at least one successful edit command
            //which also means that the version must be incremented
            successful_edit = true;
        }
        //then we change the instruction in the node 
        char* new_command = command_parser(ret_val, cursor->user, cursor->instruction);
        free(cursor->instruction);
        cursor->instruction = new_command;
        cursor = cursor->next;
    }
    //the version of the document is modified if there is at least one successful edit
    if(successful_edit){
        markdown_increment_version(doc);
    }
    return;
}

//returns null terminated string with all commands ready to broadcast
char* flatten_version_commands(){
    //iterate through all the linked list and add chars to buffer
    char *buffer = calloc(1, MAX_COMMAND_NUM*COMMAND_LEN);
    if(!buffer){
        printf("calloc failed in flatten_version_commands\n");
    }
    single_command *cursor = version_commands->head;
    int buffer_count = 0;
    while(cursor){
        for(int i=0; i<(int)strlen(cursor->instruction); i++){
            buffer[buffer_count++] = cursor->instruction[i];
        }
        cursor = cursor->next;
    }
    //we add a null byte that will not be sent to the client
    buffer[buffer_count] = '\0';
    return buffer;
}
cls
//Manages time, parses commands from the log and sends commands to the users
void* time_manager_thread(void *arg){
    struct timespec ts;
    ts.tv_sec = time_interval / 1000;
    ts.tv_nsec = (time_interval % 1000) * 1000000;
    while(true){
        nanosleep(&ts, NULL);
        //This will change the instruction buffer of all commands in the version linked list
        //It will also modify the local copy of the document
        //the version of the document is modified if necessary in that function
        
        //MUTEX LOCKED
        pthread_mutex_lock(&doc_mutex);
        pthread_mutex_lock(&version_commands_mutex);

        command_processor();

        //then we add VERSION n at the start and END at the end
        //for VERSION, we create a new node an add it at the head
        //for END, we just call add_to_version_commands, which adds the command at the end of the Linked list

        single_command *version = (single_command*) calloc(1, sizeof(single_command));
        if(!version){
            printf("Calloc failed in time_manager_thread\n");
        }
        version->instruction = (char*) calloc(1, COMMAND_LEN);
        if(!version->instruction){
            printf("Calloc failed in time_manager_thread\n");
        }

        sprintf(version->instruction, "VERSION %zu\n", doc->version);
        //add to linked list
        version->next = version_commands->head;
        version_commands->head = version;

        // Add END
        single_command *end = (single_command*) calloc(1, sizeof(single_command));
        if(!end){
            printf("Calloc failed in time_manager_thread\n");
        }
        end->instruction = (char*) calloc(1, COMMAND_LEN);
        if(!end->instruction){
            printf("Calloc failed in time_manager_thread\n");
        }

        strncpy(end->instruction, "END\n", COMMAND_LEN);

        //add to linked list
        single_command *c = version_commands->head;
        //go to the end and insert
        while(c->next){
            c = c->next;
        }
        c->next = end;
        
        //then we get the commands flattened into a string
        char* commands_to_clients = flatten_version_commands();
        //send data to all clients by iterating through all s2c pipes
        
        //MUTEX LOCK
        pthread_mutex_lock(&pipes_mutex);
        struct pipes_node *cursor = pipes->head;
        while(cursor){
            write(cursor->fd_s2c, commands_to_clients, strlen(commands_to_clients));
            cursor = cursor->next;
        }
        free(commands_to_clients);

        //MUTEX UNLOCKED
        pthread_mutex_unlock(&doc_mutex);
        pthread_mutex_unlock(&pipes_mutex);

        //MUTEX LOCKED
        pthread_mutex_lock(&all_commands_mutex);
        //finally, link the nodes from version_commands into all_commands log
        single_command *cursor_c = all_commands->head;
        if(!cursor_c){
            all_commands->head = version_commands->head;
        }else{
            //we get to the end of the linked list
            while(cursor_c->next){
                cursor_c = cursor_c->next;
            }
            //then we link the new nodes
            cursor_c->next = version_commands->head;
        }
        //and we set the head of version_commands to NULL
        //this means that there are no commands queued for the newer version
        version_commands->head = NULL;

        //MUTEX UNLOCKED
        pthread_mutex_unlock(&version_commands_mutex);
        pthread_mutex_unlock(&all_commands_mutex);

    }
    (void)arg;
    return NULL;
}

//****** MAIN THREAD  ******//

int main(int argc, char**argv){
    server_init(argc, argv);
    //we initialize our linked list of pipes and usernames
    init_pipes();
    //we initialize global(and version) command logs
    init_command_log();
    //we initialize our document
    doc = markdown_init();
    //we initialize the thread that polls stdin
    pthread_t client_thread_ids;
    //NOTE: WE DON'T USE PTHREAD JOIN BECAUSE WE KNOW THAT MAIN
    //WILL END LATER THAN ALL CLIENT THREADS
    pthread_create(&client_thread_ids, NULL, stdin_listener_thread, NULL);
    pthread_create(&client_thread_ids, NULL, time_manager_thread, NULL);

    //Manage new clients joining the server by waiting for their signals
    sigset_t sig_set;

    //1. Initialize and add SIGRTMIN
    sigemptyset(&sig_set);
    sigaddset(&sig_set, SIGRTMIN);

    //2. Block SIGRTMIN so it can be caught by sigmask
    if (sigprocmask(SIG_BLOCK, &sig_set, NULL) == -1) {
        printf("sigprocmask error\n");
        exit(0);
    }

    //We block the SIGRTMIN for all threads
    pthread_sigmask(SIG_BLOCK, &sig_set, NULL);

    siginfo_t info;
    memset(&info, 0, sizeof(info));

    int signo;
    while (1) {
        //here we wait until a signal arrives to the process
        signo = sigwaitinfo(&sig_set, &info);
        if (signo == SIGRTMIN) {
            //we create a sturct with all of the data that the thread will use
            client_info* client_t = malloc(sizeof(client_info));
            if (!client_t) {
                printf("malloc failed\n");
                exit(0);
            }
            client_t->client_pid = info.si_pid;
            pthread_create(&client_thread_ids, NULL, client_thread, (void *) client_t);
        }else{
            printf("%d\n", signo);
            printf("sigwaitinfo failed\n");
            exit(0);
        }
    }
}