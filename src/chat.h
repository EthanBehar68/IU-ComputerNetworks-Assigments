/*******************************************
 * Ethan Behar ebehar
 * Created: 9/20/21
 * 
 * 
 * Chat.h is the header for chat.c
 * See chat.c for more details
 * *******************************************/

#include <netinet/in.h>

#ifndef P1_H
#define P1_H

// Starts chat server either UDP or TCP
void chat_server(char *, long, int);
// Starts chat client either UDP or TCP
void chat_client(char *, long, int);

#endif
