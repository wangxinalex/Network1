/*=============================================================================
#     FileName: sircd.c
#         Desc: the main body of IRC server
#       Author: wangxinalex
#        Email: wangxinalex@gmail.com
#     HomePage: singo.10ss.me
#      Version: 0.0.1
#   LastChange: 2013-04-12 13:40:47
#      History:
=============================================================================*/
#include "rtlib.h"
#include "rtgrading.h"
#include "csapp.h"
#include "sircd.h"
#include <stdlib.h>

/* Macros */
#define MAX_MSG_TOKENS 10
#define MAX_MSG_LEN 512
#define SHORT_MESSAGE_LEN 256
#define EMPTY_FD (-1)

/* Global variables */
u_long curr_nodeID;
rt_config_file_t   curr_node_config_file;  /* The config_file  for this node */
rt_config_entry_t *curr_node_config_entry; /* The config_entry for this node */

client* global_client_queue[FD_SETSIZE];
channel* global_channel_list[FD_SETSIZE];
long channel_count;
//record the maximum channel_id
int max_channel_count;
int verbosity;
int global_client_count;

pool client_pool;

/* Function prototypes */
void init_node( int argc, char *argv[] );
size_t get_msg( char *buf, char *msg );
int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1] );

void init_pool(int listenfd, pool* p);
void add_client(int connfd, pool* p);
void init_client(int connfd);
void echo_client(pool* p);
int check_command(char* msg);
int dispatch_command(int cmd_num, char* msg, int connfd, pool* p);

void dump_channel_list();
void dump_pool(pool* p);
void dump_client_queue(pool *p);

int handle_nick(int connfd, char* nick);
int handle_user(int connfd, char* username,char*  hostname, char* servername, char* realname);
int handle_quit(int connfd, char* msg, pool* p);
int handle_join(int connfd, char* channel_name);
int handle_part(int connfd, char* names);
int handle_list(int connfd);
int handle_privmsg(int connfd, char* target, char* msg);
int handle_who(int connfd, char* channel_name);

channel* get_channel_by_name(char* channel_name);
channel* get_channel_by_id(int channel_id);
/* Main */
int main( int argc, char *argv[] )
{
    int listenfd, connfd;
    struct sockaddr_in cliaddr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
   // pool *pool = Malloc(sizeof(pool));
    verbosity = VER_NORMAL;
    init_node( argc, argv );
    printf( "I am node %lu and I listen on port %d for new users\n", curr_nodeID, curr_node_config_entry->irc_port );
    channel_count = 0;
    max_channel_count = 0;
    global_client_count = 0;
/**
 *open_listenfd - open and return a listening socket on port, it is a combination of the following steps:
 * 1.create a socket descriptor
 * 2.bind the socket descriptor with a local protocol
 * 3.Make it a listening socket ready to accept connection requests 
 */
    listenfd=Open_listenfd((int)curr_node_config_entry->irc_port);
    init_pool(listenfd,&client_pool);
    while(1){
        //all descriptors in the read set was ready for read
        client_pool.ready_set = client_pool.read_set; 
        client_pool.nready = Select(client_pool.maxfd+1,&(client_pool.ready_set),NULL,NULL,NULL);

        if(FD_ISSET(listenfd,&(client_pool.ready_set))){
            //return the next ready completed connection from the front of the completed connection queue
            connfd = Accept(listenfd, (SA*)&cliaddr,&addrlen);
            //add a client in the global client queue
            add_client(connfd, &client_pool);
        }
        //echo the command from the clients
        echo_client(&client_pool);
    }

    return 0;
}

/*
 * void init_node( int argc, char *argv[] )
 *
 * Takes care of initializing a node for an IRC server
* from the given command line arguments
 */
void init_node( int argc, char *argv[] )
{
    int i;

    if( argc < 3 )
    {
        printf( "%s <nodeID> <config file> [-v verbosity]\n", argv[0] );
        exit( 0 );
    }

    if( argc == 5){
       if(strcmp(argv[3],"-v")==0){
            if(strcmp(argv[4],"2")==0){
                verbosity = VER_FULL;
            }else if(strcmp(argv[4],"1")==0){
                verbosity = VER_USER;
            }else{
                verbosity = VER_NORMAL;
            }
       } 
    }

    /* Parse nodeID */
    curr_nodeID = atol( argv[1] );

    /* Store  */
    rt_parse_config_file(argv[0], &curr_node_config_file, argv[2] );

    /* Get config file for this node */
    for( i = 0; i < curr_node_config_file.size; ++i )
        if( curr_node_config_file.entries[i].nodeID == curr_nodeID )
             curr_node_config_entry = &curr_node_config_file.entries[i];

    /* Check to see if nodeID is valid */
    if( !curr_node_config_entry )
    {
        printf( "Invalid NodeID\n" );
        exit(1);
    }
}
/**
 * void init_pool(int listenfd, pool* p)
 * Initialize the client pool, set the listenfd as the maximum descriptor
 */
void init_pool(int listenfd, pool* p ){
    int i;
    p->maxindex = EMPTY_FD;
    for(i = 0; i < FD_SETSIZE; i++){
        p->clientfd[i] = EMPTY_FD;
    }
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    
    FD_SET(listenfd, &p->read_set);

}
/**
 * void add_client(int connfd, pool* p)
 * add a client t the client pool
 */
void add_client(int connfd, pool* p){
    int i;
    //the newly added client takes up a descriptor
    p->nready--;
    //search the pool and find the empty position for the coming descriptor
    for(i = 0; i < FD_SETSIZE; i++){
       if(p->clientfd[i] == EMPTY_FD){
            //set the added descriptor to the read set of the pool
            FD_SET(connfd,&p->read_set);
            //add the client descriptor to the empty room in the client list of the pool
            p->clientfd[i] = connfd;
            Rio_readinitb(&p->client_read_buf[i], connfd);
            if(p->maxindex < i){
                p->maxindex = i;
            }
            if(p->maxfd < connfd){
                p->maxfd = connfd;
            }
            break;
       } 
    
    }
    if(i == FD_SETSIZE){
        unix_error("Clients full.");
        return;
    }
   //create a real client in the global client queue 
    init_client(connfd);
    // dump_client_queue(p);    
}
/**
 * void create_client(int connfd)
 * Initialize a client and add it in the global client queue
 */
void init_client(int connfd){
    client *newclient = Malloc(sizeof(client));
    newclient->clientfd = connfd;
    newclient->nick_is_set = 0;
    newclient->user_is_set = 0;
    newclient->channel_id = EMPTY_FD;
    global_client_queue[connfd] = newclient;
    global_client_count ++;
}
/**
 * void dump_pool(pool* p)
 * dump the member variables and the client list of the pool. 
 * mainly for debug use
 */
void dump_pool(pool* p){
    printf("Largest descriptor : %d\t Number of ready clients : %d\tMax index of ready client : %d\n",
           p->maxfd,p->nready,p->maxindex);
    int i;
    printf("Client\tdescriptor\n");
    for(i = 0; i < FD_SETSIZE; i++){
        printf("%6d\t %6d\n",i,p->clientfd[i] );
        if(p->clientfd[i]==EMPTY_FD){
            break;
        }
    }
}
/**
 * void dump_client_queue(pool* p)
 * dump the global_client_queue 
 * mainly for debug use
 */
void dump_client_queue(pool* p){
    int i;
    //printf("Channel Id\t Channel name \tisActive\t client_count\n");
    printf("===========Client List============\n");
    client *this_client;
    for(i = 0; i <= p->maxfd; i++){
        if((this_client = global_client_queue[i]) == NULL){
           continue; 
        }
        printf("%15s %10d\n","Descriptor",i);
        printf("%15s %10s\n","Nickname",this_client->nick_is_set?this_client->nickname:"Not set");
        printf("%15s %10s\n","Username",this_client->username);
        printf("%15s %10s\n","Hostname",this_client->hostname);
        printf("%15s %10s\n","Servername",this_client->servername);
        printf("%15s %10s\n","Realname",this_client->realname);
        printf("%15s %10d\n","Channel id",this_client->channel_id);
        printf("%15s %10d\n","Nick_is_set",this_client->nick_is_set);
        printf("%15s %10d\n\n","User_is_set",this_client->user_is_set);

    }
    printf("==================================\n");
}
/**
 * void dump_channel_list()
 * dump all the active channel in the global_channel_list, mainly for debug use
 */
void dump_channel_list(){
    int i;
    printf("Channel Id\t Channel name \tisActive\t client_count\n");
    for(i =0; i<FD_SETSIZE; i++){
        if(global_channel_list[i]==NULL){
            break;
        }
        printf("%10d\t %12s\t %6d\t %19d\n", global_channel_list[i]->channel_id,
               global_channel_list[i]->channel_name,
               global_channel_list[i]->isActive,
               global_channel_list[i]->client_count);
    }

}

/**
 * void echo_client(pool* p)
 * Echo a text line for each client
 */
void echo_client(pool* p){
    int i,connfd,n;
    rio_t rio;
    char buf[MAX_MSG_LEN];
    if(verbosity >= VER_USER){
        if(verbosity == VER_FULL){
            dump_pool(p);
        }
        dump_client_queue(p);
    }
    for(i = 0; (i <= p->maxindex) && (p->nready > 0); i++){
        connfd = p->clientfd[i];
        rio = p->client_read_buf[i];
		if(connfd <= 0 ){
			continue;
		}
        if(FD_ISSET(connfd, &p->ready_set)){

            if((n = Rio_readlineb(&rio,buf,MAX_MSG_LEN))!=0){
                char msg[MAX_MSG_LEN];
                get_msg(buf,msg);
                int cmd_num=0;
                char echomsg[MAX_MSG_LEN * 2];
                //check the format of the command
                if((cmd_num = check_command(msg)) == CMD_UNKNOWN){
                    sprintf(echomsg,"%s ERR UNKNOWNCOMMAND\n",msg);
                    Rio_writen(connfd,echomsg,strlen(echomsg));
                }else{
                    if(!dispatch_command(cmd_num,msg,connfd,p)){
                        sprintf(echomsg,"%s command failed.\n",msg);
                        Rio_writen(connfd,echomsg,strlen(echomsg));
                    }
                }
            }else{
                Close(connfd);
                FD_CLR(connfd,&p->read_set);
                p->clientfd[i] = EMPTY_FD;
                //Free the memory space for the client
                Free(global_client_queue[connfd]);
            }        
        }  
    }
}

/**
 * int check_command(char* msg)
 * check the command the corresponding parameter list
 */
int check_command(char* msg){
    char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1];
    if(!tokenize(msg,tokens)){
        unix_error("tokenize error.\n");
    }
    char* command = tokens[0];
    if(!strncmp(command, "NICK",sizeof("NICK"))){
        return CMD_NICK;
    }else if(!strncmp(command, "USER",sizeof("USER"))){
        return CMD_USER;
    }else if(!strncmp(command, "QUIT",sizeof("QUIT"))){
        return CMD_QUIT;
    }else if(!strncmp(command, "JOIN",sizeof("JOIN"))){
        return CMD_JOIN;
    }else if(!strncmp(command, "PART",sizeof("PART"))){
        return CMD_PART;
    }else if(!strncmp(command, "LIST",sizeof("LIST"))){
        return CMD_LIST;
    }else if(!strncmp(command, "PRIVMSG",sizeof("PRIVMSG"))){
        return CMD_PRIVMSG;
    }else if(!strncmp(command, "WHO",sizeof("WHO"))){
        return CMD_WHO;
    }else{ 
        return CMD_UNKNOWN;
    }

}

/**
 * int dispatch_command(int cmd_num, char* msg, int connfd, pool* p)
 * dispatch the command according to the result of dispatch_command()
 */
int dispatch_command(int cmd_num, char* msg, int connfd, pool* p){
    char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1];
    //reinitialize the tokens to clear the previous one
    memset(tokens,'\0',sizeof(tokens));
    if(!tokenize(msg,tokens)){
        unix_error("tokenize error.\n");
    }
    //char* command = tokens[0];
    char *echo;
    switch(cmd_num){
        case CMD_NICK:
            if(strlen(tokens[1])){
                //printf("%s %d\n",tokens[1],strlen(tokens[1]));
                return handle_nick(connfd, tokens[1]);
            }else{
                echo = "The command should be like this: NICK <nickname>\n";
                Rio_writen(connfd,echo,strlen(echo));

            }
            break;
        case CMD_USER:
            if(strlen(tokens[1]) && strlen(tokens[2]) && strlen(tokens[3]) && strlen(tokens[4])){
                return handle_user(connfd, tokens[1], tokens[2], tokens[3], tokens[4]);
            }else{
                //Delete the first parameter
                strcpy(tokens[1],"");
                echo = "The command should be like this: USER <username> <hostname> <servername> :<realname>\n";
                Rio_writen(connfd,echo,strlen(echo));
            }
            break;
        case CMD_QUIT:
            if(strlen(tokens[1])){ 
                return handle_quit(connfd, tokens[1], p);
            }else{
                echo = "The command should be like this:QUIT <msg>\n";
                Rio_writen(connfd,echo,strlen(echo));
            }
            break;
        case CMD_JOIN:
            if(strlen(tokens[1])){ 
                return handle_join(connfd, tokens[1]);
            }else{
                echo = "The command should be like this: JOIN <channel_name>\n";
                Rio_writen(connfd,echo,strlen(echo));
            }
            break;
        case CMD_PART:
             if(strlen(tokens[1])){ 
                return handle_part(connfd, tokens[1]);
            }else{
                echo = "The command should be like this: PART <names>\n";
                Rio_writen(connfd,echo,strlen(echo));
            }
            break;
        case CMD_LIST:
            return handle_list(connfd); 
            break;
        case CMD_PRIVMSG:
            if(strlen(tokens[1]) && strlen(tokens[2])){ 
                return handle_privmsg(connfd, tokens[1], tokens[2]);
            }else{
                echo = "The command should be like this: PRIVMSG <target> <msg>\n";
                Rio_writen(connfd,echo,strlen(echo));
            }
            break;
        case CMD_WHO:
            if(strlen(tokens[1])){ 
                return handle_who(connfd, tokens[1]);
            }else{
                echo = "The command should be like this: WHO <channel_name>\n";
                Rio_writen(connfd,echo,strlen(echo));
            }
            break;

        default :
            return 0;
    }
    return 0;

}

/**
 * int handle_nick(int connfd, char* nickname)
 * give a user a nickname or change the previous one. Your server should handle dupicate
 * nickname and report error message
 */
int handle_nick(int connfd, char* nickname){
    if(strlen(nickname) > NAME_LENGTH){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf, "NOTE :the length of nickname is too long.\n");
        Rio_writen(connfd, buf, strlen(buf));
        return 0;
    }
    client* this_client = global_client_queue[connfd];
    int i;
    for(i = 0; i<FD_SETSIZE; i++){
        //the nickname has been occupied
        if((global_client_queue[i] != NULL) && (strcasecmp(global_client_queue[i]->nickname,nickname) == 0)){
            char buf[MAX_MSG_LEN];
            sprintf(buf,"NOTE: nickname %s has been registered.\n",nickname);
            Rio_writen(connfd, buf, strlen(buf));
            return 1;
        }
    }
    if(this_client!=NULL){
        strncpy(this_client->nickname, nickname,NAME_LENGTH);
        this_client->nick_is_set = 1;
        if(this_client->user_is_set == 1){
            char buf[MAX_MSG_LEN];
            char hostname[NAME_LENGTH];
            gethostname(hostname, NAME_LENGTH);
            sprintf(buf, ":%s 375 %s :- <server> Message of the day -.\n", hostname, this_client->nickname);
            sprintf(buf, "%s:%s 372 %s :- Send motd. \n",buf, hostname, this_client->nickname);
            sprintf(buf, "%s:%s 376 %s :End of /MOTD command.\n", buf, hostname, this_client->nickname);
            Rio_writen(connfd, buf, strlen(buf));
            return 1;
        
        }
        return 1;
    }
    return 0;
}

/**
 * int handle_user(int connfd, char* username,char*  hostname,char* realname )
 * Specify the username, hostname, and real name of a user
 */
int handle_user(int connfd, char* username, char*  hostname, char* servername, char* realname ){
    if(strlen(username) > NAME_LENGTH || strlen(hostname) > NAME_LENGTH || strlen(servername) > NAME_LENGTH 
       || strlen(realname) > NAME_LENGTH){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf, "NOTE :the length of name(s) is too long.\n");
        Rio_writen(connfd, buf, strlen(buf));
        return 0;
    }
    client* this_client = global_client_queue[connfd];
    if(this_client->user_is_set == 1){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf,"ERR_ALREADYREGISTERED\n");
        Rio_writen(connfd, buf ,strlen(buf));
    }else if(this_client != NULL){
        this_client->user_is_set = 1;
        strncpy(this_client->username, username, NAME_LENGTH);
        strncpy(this_client->hostname, hostname, NAME_LENGTH);
        strncpy(this_client->servername, servername, NAME_LENGTH);
        strncpy(this_client->realname, realname, NAME_LENGTH);
        // char buf[MAX_MSG_LEN];
        // sprintf(buf, ":%s 375 %s :- <server> Message of the day -.\n", hostname,this_client->nickname);
        // sprintf(buf, "%s:%s 372 %s :- Send motd. \n",buf, hostname, this_client->nickname);
        // sprintf(buf, "%s:%s 376 %s :End of /MOTD command.\n", buf, hostname, this_client->nickname);
        // Rio_writen(connfd, buf, strlen(buf));
        return 1;
    
    }
    return 0;
}

/**
 * channel* get_channel_by_name(char *channel_name)
 * get a chennel by chennel name
 */
channel* get_channel_by_name(char *channel_name){
    int i;
    for(i=0; i < FD_SETSIZE; i++){
        if((global_channel_list[i] != NULL) && (global_channel_list[i]->isActive == 1)
           && (strcasecmp(global_channel_list[i]->channel_name,channel_name) == 0)){
            return global_channel_list[i];           
         }
    }
    return NULL;
}

/**
 * channel* get_channel_by_id(int channel_id)
 * get a chennel by chennel id
 */
channel* get_channel_by_id(int channel_id){
    int i;
    for(i=0; i < FD_SETSIZE; i++){
        if((global_channel_list[i] != NULL) && (global_channel_list[i]->isActive == 1)
           && (global_channel_list[i]->channel_id == channel_id)){
            return global_channel_list[i];           
         }
    }
    return NULL;
}
/**
 * int leave_channel(int connfd, char* channel_name)
 * clients leaves a channel, if the channel was empty after the client leaves, 
 * delete the channel from the global_channel_list
 * when it was used in handle_part, it should echo the appropriate message to
 * all clients (including the originator itself). 
 */
int leave_channel(int connfd, char* channel_name){
    client* this_client;
    channel* this_channel;
    if(((this_client = global_client_queue[connfd]) == NULL)
       ||(this_channel = get_channel_by_name(channel_name)) == NULL){
        return 0;
     }
   // this_client->channel_id = EMPTY_FD;
    int i;
    //send messages to all clients in the channel
    for(i=0; i<FD_SETSIZE; i++){
        if(this_channel->connected_clients[i] == 1){
            char buf[SHORT_MESSAGE_LEN];
            sprintf(buf, ":%s!%s@%s QUIT :the client is not in channel %s.\n",
                    this_client->nickname,
                    this_client->username,
                    this_client->hostname,
                    this_channel->channel_name);
            // printf("%s to %d\n",buf, this_channel->clients_list[i]);
            Rio_writen(this_channel->clients_list[i], buf, strlen(buf));
        }
        if(this_channel->clients_list[i] == connfd){
            this_channel->clients_list[i] = 0;
            this_channel->connected_clients[i] = 0;
        }

    }

    this_channel->client_count--;
    // printf("The channel %s now has %d clients.\n", this_channel->channel_name,this_channel->client_count);
    //if the channel is empty after the last client leaves
    if(this_channel->client_count==0){
       this_channel->isActive = 0;
        for(i=0; i<FD_SETSIZE; i++){
            if((global_channel_list[i]!=NULL) 
               && (global_channel_list[i]->channel_id == this_channel->channel_id)){
                //delete this channel from the global_channel_list
                // printf("Delete channel %s.\n",this_channel->channel_name);
                global_channel_list[i]=NULL;
                break;
            }
        }
        channel_count--;
    }
    return 1;
} 
/**
 * int handle_quit(int connfd, char* msg, pool *p)
 * End the client session. The server should announce the client's departure to all other
 * users sharing the channel with the departing client.
 */
int handle_quit(int connfd, char* msg, pool* p){
    client* this_client = global_client_queue[connfd];
    channel* this_channel = get_channel_by_id(this_client->channel_id);
    if(this_channel != NULL){
        leave_channel(connfd, this_channel->channel_name);
        char buf[SHORT_MESSAGE_LEN];
        //send message to all other users in the channel
        int i;
        for(i=0; i<FD_SETSIZE; i++){
            if((this_channel->connected_clients[i]!=0) &&
               (this_channel->clients_list[i]!=connfd)){
                sprintf(buf, ":%s!%s@%s QUIT :Connetion closed.\n",
                        this_client->nickname,
                        this_client->username,
                        this_client->hostname);
                Rio_writen(this_channel->clients_list[i],buf,strlen(buf));
            }
        }
    }
    FD_CLR(connfd, &p->read_set);
    p->clientfd[connfd] = EMPTY_FD;
    global_client_queue[connfd] = NULL;
    global_client_count --;
    Close(connfd);
    return 1;

}
/**
 * int handle_join(int connfd, char* channel_name)
 * Start listening to a specific channel. Although the standard 
 * IRC protocol allows a client to join multiple channels simultaneously, 
 * your server should restrict a client to be a member of at most one channel. 
 * Joining a new channel should implicitly cause the client to leave the current 
 * channel.
 */
int handle_join(int connfd, char* channel_name){
    if(strlen(channel_name) > NAME_LENGTH){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf, "NOTE :the length of channel_name is too long.\n");
        Rio_writen(connfd, buf, strlen(buf));
        return 0;
    }
    int k;
    for(k = 0; k < FD_SETSIZE; k++){
        //the nickname has been occupied
        if((global_client_queue[k] != NULL) && (strcasecmp(global_client_queue[k]->nickname,channel_name) == 0)){
            char buf[MAX_MSG_LEN];
            sprintf(buf,"NOTE: channel_name %s duplicated with user's nickname.\n",channel_name);
            Rio_writen(connfd, buf, strlen(buf));
            return 1;
        }
    }

    client* this_client;
    channel* this_channel;
    if((this_client = global_client_queue[connfd]) == NULL){
        return 0;
    }
    this_channel = get_channel_by_name(channel_name);

    if((this_channel!=NULL) && (this_channel->channel_id == this_client->channel_id)){
        //if the client was already on this channel, silently ignore the command
        return 1;
    }
    else {
        //the client has already been in another channel, it should leave the original channel first
        if(this_client->channel_id != EMPTY_FD){
            channel *old_channel = get_channel_by_id(this_client->channel_id);
            leave_channel(connfd, old_channel->channel_name);
        } 
        //if there is no such a channel, create a new channel and put the client in it
        if(this_channel  == NULL){
            // printf("Create a new channel.\n");
            channel* new_channel = Malloc(sizeof(channel));
            memset(new_channel,0,sizeof(channel));
            channel_count ++;
            max_channel_count ++;
            new_channel->isActive = 1;
            strncpy(new_channel->channel_name, channel_name, NAME_LENGTH);
            new_channel->client_count = 0;
            if(channel_count >= FD_SETSIZE){
                return 0;
            }

            int i;
            for(i=0; i<FD_SETSIZE; i++){
                if(global_channel_list[i]==NULL){
                    global_channel_list[i] = new_channel;
                    new_channel->channel_id = i;
                    break;
                }
            }
            this_channel = new_channel;
        }

        //the client has not yet been in another channel
        this_client->channel_id = this_channel->channel_id;
        int i;
        for(i=0; i< FD_SETSIZE; i++){
            if(this_channel->connected_clients[i] == 0){
                this_channel->clients_list[i] = this_client->clientfd;
                this_channel->connected_clients[i] = 1;
                this_channel->client_count++;
                break;
            }
        
        }
    }
    if(verbosity == VER_FULL){
        dump_channel_list();
    }
    char hostname[NAME_LENGTH];
    gethostname(hostname, NAME_LENGTH);
    char buf[SHORT_MESSAGE_LEN];
    sprintf(buf, ":%s JOIN %s\n", this_client->nickname, channel_name);
    sprintf(buf, "%s:%s 353 %s = %s : %s\n",buf, hostname, this_client->nickname, 
            channel_name, this_client->nickname);
    sprintf(buf, "%s:%s 366 %s %s :End of /NAMES list\n",buf, hostname, this_client->nickname,
            channel_name);
    //this_channel = get_channel_by_name(channel_name);
    int j;
    for(j=0; j<FD_SETSIZE; j++){
        if(this_channel->connected_clients[j] == 1){
            Rio_writen(this_channel->clients_list[j], buf, strlen(buf));
        }
    }

    return 1;
}
/**
 * int handle_part(int connfd, char* names) 
 * Depart a specific channel. Though a user may only be in one channel at a time,
 * PART should still handle multiple arguments. If no such channel exists or it
 * exists but the user is not currently in that channel, send the appropriate error 
 * message.
 */
int handle_part(int connfd, char* names){
    client* this_client;
    if((this_client = global_client_queue[connfd])==NULL){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf,"ERROR: this client does not exist.\n");
        Rio_writen(connfd, buf, strlen(buf));
        return 0;
    }
    char* channel_names;
    for(channel_names = strtok(names,","); channel_names != NULL; channel_names = strtok(NULL,",")){
         channel* this_channel;
         if((this_channel = get_channel_by_name(channel_names))==NULL){
            char buf[MAX_MSG_LEN];
            sprintf(buf, ":%s!%s@%s QUIT :ERR_NOSUCHCHANNEL\n",this_client->nickname,
                    this_client->username,
                    this_client->hostname);
            Rio_writen(connfd, buf, strlen(buf));
        //the client is not in this_channel 
         }else if(this_client->channel_id == EMPTY_FD || this_client->channel_id != this_channel->channel_id){
            char buf[MAX_MSG_LEN];
            sprintf(buf, ":%s!%s@%s QUIT :the client is not in  the channel %s.\n",
                    this_client->nickname,
                    this_client->username,
                    this_client->hostname,
                    this_channel->channel_name);
            Rio_writen(connfd, buf, strlen(buf));
         }else if(this_channel->channel_id == this_channel->channel_id){
             // printf("Leave channel.\n");
            leave_channel(connfd, this_channel->channel_name);            
         }
    }
    return 1;
}

/**
 * int handle_list(int connfd)
 * List all existing channels on the local server only. Your server should ignore parameters 
 * and list all channels and the number of users on the local server in each channel.
 */
int handle_list(int connfd){
    client *this_client;
    if((this_client = global_client_queue[connfd]) == NULL){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf,"ERROR: this client does not exist.\n");
        Rio_writen(connfd, buf, strlen(buf));
        return 0;
    }
    char buf[MAX_MSG_LEN];
    char hostname[NAME_LENGTH];
    gethostname(hostname, NAME_LENGTH);
    sprintf(buf, ":%s 321 %s Channel :Users Name\n",hostname, 
            this_client->nickname);
    int i;
    // printf("channel_count = %ld\n",channel_count);
    for(i = 0; i<max_channel_count; i++){
        if(global_channel_list[i]!=NULL){
            sprintf(buf, "%s:%s 322 %s %s %d\n", buf, hostname,
                this_client->nickname,
                global_channel_list[i]->channel_name,
                global_channel_list[i]->client_count);
        }
    }
    sprintf(buf, "%s:%s 323 %s :End of /LIST.\n",buf, hostname,
            this_client->nickname);
    // printf("%s\n",buf);
    Rio_writen(connfd, buf, strlen(buf));
    return 1;
}

/**
 * int handle_privmsg(int connfd, char* target, char* msg)
 * Send messages to users. The target can be either a nickname or a channel.
 * If the target is a channel, the message will be broadcast to every user 
 * on the specified channel, except the message originator. 
 * If the target is a nickname, the message will be sent only to that user.
 */
int handle_privmsg(int connfd, char* target, char* msg){
    if(strlen(msg) >= MAX_MSG_LEN){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf, "NOTE :the length of message is too long.\n");
        Rio_writen(connfd, buf, strlen(buf));
        return 0;
    }
    client *this_client;
    channel* this_channel;
    if((this_client = global_client_queue[connfd]) == NULL){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf,"ERROR: this client does not exist.\n");
        Rio_writen(connfd, buf, strlen(buf));
        return 0;
    }
    char *names;
    //split the names list by delimiter ","
    for(names = strtok(target,","); names != NULL; names = strtok(NULL,",")){
        //if the name is a user nickname
        int i;
        for(i = 0; i < FD_SETSIZE; i++){
            if((global_client_queue[i]!=NULL) 
               && (strcmp(global_client_queue[i]->nickname,names) == 0)){
                char buf[MAX_MSG_LEN];
                sprintf(buf, ":%s PRIVMSG %s :%s\n",
                        this_client->nickname,
                        global_client_queue[i]->nickname,
                        msg);
                // printf("%s\n", buf);
                Rio_writen(global_client_queue[i]->clientfd,buf, strlen(buf));
            }
        }
        //if the name is a channel_name
        if((this_channel = get_channel_by_name(names))!=NULL){
            int j;
            char buf[MAX_MSG_LEN];
            for(j = 0; j < FD_SETSIZE; j++){
                if((this_channel->connected_clients[j] == 1) && (this_channel->clients_list[j] != connfd)){
                    sprintf(buf, ":%s PRIVMSG %s :%s\n", this_client->nickname,
                            this_channel->channel_name,msg);
                    // printf("%s\n", buf);
                    Rio_writen(this_channel->clients_list[j], buf, strlen(buf));
                }
            }
        }
        // printf("names = %s\n",names);
    }
    return 1;
}
/**
 * int handle_who(int connfd, char* channel_name)
 * query information about clients or channels. In this project,
 * your server only needs to support querying channels on the local server.
 * It should do an exact match on the channel name and return the users on that channel
 */
int handle_who(int connfd, char* channel_name){
    client* this_client;
    channel* this_channel;
    char hostname[NAME_LENGTH];
    gethostname(hostname, NAME_LENGTH);
    if((this_client = global_client_queue[connfd]) == NULL ){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf,"ERROR: this client does not exist.\n");
        Rio_writen(connfd, buf, strlen(buf));
        return 0;
    }
    if((this_channel = get_channel_by_name(channel_name))==NULL){
        char buf[SHORT_MESSAGE_LEN];
        sprintf(buf, "NOTE: The channel %s does not exist.\n", channel_name);
        Rio_writen(connfd, buf, strlen(buf));
        return 1;
    }
    char buf[MAX_MSG_LEN];
    sprintf(buf, ":%s 352 %s %s please see following: ", hostname, this_client->nickname,
            this_channel->channel_name);
    int i;
    client* each_client;
    for(i = 0; i < FD_SETSIZE; i++){
        if(this_channel->connected_clients[i] == 1){
            //print all the clients in this channel
            each_client = global_client_queue[this_channel->clients_list[i]];
            sprintf(buf, "%s %s", buf, each_client->nickname);
        }
    }
    sprintf(buf, "%s H :0 The MOTD\n", buf);
    sprintf(buf,"%s:%s 315 %s %s :End of /WHO list.\n",buf, hostname,
            this_client->nickname,
            this_channel->channel_name);
    Rio_writen(connfd, buf, strlen(buf));
    return 1;
}

/*
 * size_t get_msg( char *buf, char *msg )
 *
 * char *buf : the buffer containing the text to be parsed
 * char *msg : a user malloc'ed buffer to which get_msg will copy the message
 *
 * Copies all the characters from buf[0] up to and including the first instance
 * of the IRC endline characters "\r\n" into msg.  msg should be at least as
 * large as buf to prevent overflow.
 *
 * Returns the size of the message copied to msg.
 */
size_t get_msg(char *buf, char *msg)
{
    char *end;
    int  len;

    /* Find end of message */
    end = strstr(buf, "\r\n");

    if( end )
    {
        len = end - buf + 2;
    }
    else
    {
        /* Could not find \r\n, try searching only for \n */
        end = strstr(buf, "\n");
	if( end )
	    len = end - buf + 1;
	else
	    return -1;
    }

    /* found a complete message */
    memcpy(msg, buf, len);
    msg[end-buf] = '\0';

    return len;	
}

/*
 * int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1] )
 *
 * A strtok() variant.  If in_buf is a space-separated list of words,
 * then on return tokens[X] will contain the Xth word in in_buf.
 *
 * Note: You might want to look at the first word in tokens to
 * determine what action to take next.
 *
 * Returns the number of tokens parsed.
 */
int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1] )
{
    int i = 0;
    const char *current = in_buf;
    int  done = 0;

    /* Possible Bug: handling of too many args */
    while (!done && (i<MAX_MSG_TOKENS)) {
        char *next = strchr(current, ' ');

	    if (next) {
	        memcpy(tokens[i], current, next-current);
	        tokens[i][next-current] = '\0';
	        current = next + 1;   /* move over the space */
	        ++i;

	        /* trailing token */
	        if (*current == ':') {
	            ++current;
		    strcpy(tokens[i], current);
		    ++i;
		    done = 1;
	        }
	    } else {
	        strcpy(tokens[i], current);
	        ++i;
	        done = 1;
	    }
    }

    return i;
}
