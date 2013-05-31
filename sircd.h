/*=============================================================================
#     FileName: sircd.h
#         Desc: the header file of the sircd
#       Author: wangxinalex
#        Email: wangxinalex@gmail.com
#     HomePage: singo.10ss.me
#      Version: 0.0.1
#   LastChange: 2013-04-11 20:03:03
#      History:
=============================================================================*/
#ifndef _SIRCD_H
#define _SIRCD_H

#define VER_NORMAL 0
#define VER_USER 1
#define VER_FULL 2

#define CMD_NICK 1
#define CMD_USER 2
#define CMD_QUIT 3
#define CMD_JOIN 4
#define CMD_PART 5
#define CMD_LIST 6
#define CMD_PRIVMSG 7
#define CMD_WHO 8
#define CMD_ADDUSER 9
#define CMD_UNKNOWN 0

#define NAME_LENGTH 32
typedef struct s_client{
    int clientfd;                   //the file descriptor of the client
    char nickname[NAME_LENGTH];     
    char username[NAME_LENGTH];
    char hostname[NAME_LENGTH];
    char servername[NAME_LENGTH];
    char realname[NAME_LENGTH];
    int channel_id;                 //the id of the channel where the client was in 
    int nick_is_set;                //whether the nickname is registered
    int user_is_set;                //whether the user is registered
}client;

typedef struct s_pool{
    int maxfd;              //largest descriptor in sets
    fd_set read_set;        //all active read descriptors
    fd_set write_set;       //all active write descriptors
    fd_set ready_set;       //descriptors ready for reading
    int nready;             //return of select()
    int maxindex;          //max index in client array
    int clientfd[FD_SETSIZE];//client descriptor list 
    rio_t client_read_buf[FD_SETSIZE];//the read buffer from which get the messages
}pool;

typedef struct s_channel{
    char channel_name[NAME_LENGTH];  //the name of the channel
    int channel_id;         //the id of the channel
    int clients_list[FD_SETSIZE];//the client descriptor of the client in this pool(not necessarily connected now)
    int connected_clients[FD_SETSIZE];//whether the client with this descriptor is connected in this channel
    int isActive;           //whether the channel is still active
    int client_count;       //the number of connected clients in this channel 
}channel;

#endif 
