/*******************************************
 * Ethan Behar ebehar
 * Created: 9/20/21
 * 
 * 
 * Chat.c is a file that handles a simple chat server
 * and chat client. Builds a UDP or TCP connection based
 * on input parameters. Server echos back messages unless:
 * hello - returns World
 * goodybe - returns farewell - server closes connection, client terminates
 * exit - returns ok - server terminates, client terminates
 * References:
 * Professor Swany's Slides
 * Various man pages!!!
 * https://docs.oracle.com/cd/E19620-01/805-4041/6j3r8iu2l/index.html
 * https://docs.oracle.com/cd/E19620-01/805-4041/6j3r8iu2o/index.html#sockets-47146
 * https://www.gta.ufrj.br/ensino/eel878/sockets/sockaddr_inman.html
 * https://www.gta.ufrj.br/ensino/eel878/sockets/sockaddr_inman.html    
 * https://docs.oracle.com/cd/E19620-01/805-4041/6j3r8iu2l/index.html
 * https://www.geeksforgeeks.org/udp-server-client-implementation-c/
 * https://stackoverflow.com/questions/11233246/how-to-convert-ipv4-mapped-ipv6-address-to-ipv4-string-format 
 * https://www.ibm.com/docs/en/i/7.3?topic=sscaaiic-example-accepting-connections-from-both-ipv6-ipv4-clients
 * https://www.ibm.com/docs/en/i/7.3?topic=clients-example-ipv4-ipv6-client
 * https://linux.die.net/man/3/getaddrinfo
 * https://stackoverflow.com/questions/20196121/passing-struct-to-pthread-as-an-argument
 * https://stackoverflow.com/questions/14729007/i-want-to-make-accept-system-call-as-non-blocking-how-can-i-make-accept-system
 * https://stackoverflow.com/questions/388434/linux-fcntl-unsetting-flag
 * 
 * ./netster -p 7779
 * ./netster 127.0.0.1 -p 7779
 * netcat -t -p 8888 localhost 7779
 * cd documents/github/net-fall21/src
*******************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "chat.h"

// Calls listen for TCP server
void start_listen_old(int);
// TCP server main loop
void tcp_server_running(int);
// TCP read/write with client loop - thread base
void *tcp_server_thread(void * arg);
// UDP server main loop
void udp_server_running(int);
// Handling of UDP messages
int udp_handle_received_message(struct sockaddr_storage, socklen_t, char *, int);
// TCP client main loop
void tcp_client_running(int chat_sockfd);
// UDP client main loop
void udp_client_running(int chat_sockfd, struct sockaddr *server_address, socklen_t address_length);

#define BACKLOG 25

/* Create a server instance of chat application with the specified parameters
* iface = ip address
* port = prt number
* use_udp = use UDP or TCP
 */

// Structure for sending data to threads
// Shutdown allows us to read if thread received an exit message
typedef struct tcp_thread_data
{
    char *host;
    char *service;
    int client_sock;
    int shutdown;
    pthread_t *thread;
} tcp_thread_data;

void chat_server(char *iface, long port, int use_udp)
{
    // Create socket and hints
    int chat_sockfd = -1;
    struct addrinfo hints;
    // Clear hints memory
    memset(&hints, 0, sizeof(hints));

    // Convert port: long to char*
    // unsigned short ui_port = htons(port);
    char *c_port = malloc(sizeof(port));
    snprintf(c_port, sizeof(port), "%ld", port);
    // printf("%s%s", c_port, "\n");
    // fflush(stdout);

    // Results of getaddrinfo
    struct addrinfo *result, *rp;
    int getaddrinfo_success;

    // TCP chat server logic
    if (use_udp == 0)
    {
        // Set up hints
        hints.ai_family = AF_UNSPEC;     // v4 or v6
        hints.ai_socktype = SOCK_STREAM; // TCP Sock
        hints.ai_flags = AI_PASSIVE;
        hints.ai_protocol = IPPROTO_TCP; // TCP Protocol Only

        // Call getaddrinfo
        getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &result);
        // Failure - log it and leave
        if (getaddrinfo_success != 0)
        {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
            exit(1);
        }

        // Try all the address structures until one works... sounds crazy
        for (rp = result; rp != NULL; rp = rp->ai_next)
        {
            // Create socket
            chat_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            // Creating socket failed, log, and try next structure
            if (chat_sockfd == -1)
            {
                // perror("socket failed, trying next structure");
                continue;
            }

            // Now attempt to bind socket
            if (bind(chat_sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            {
                // Success
                // Make this a non-blocking socket
                int flags = fcntl(chat_sockfd, F_GETFL, 0);
                if (flags == -1)
                {
                    // perror("fcntl F_GETFL: ");
                }
                flags = fcntl(chat_sockfd, F_SETFL, flags | O_NONBLOCK);
                if (flags == -1)
                {
                    // perror("fcntl F_SETFL: ");
                }

                // Now listen on that socket
                start_listen_old(chat_sockfd);

                // Start the server loop
                tcp_server_running(chat_sockfd);

                // If we here, we are shutting down!
                break;
            }

            // Bind failed, release socket, and try next structure
            close(chat_sockfd);
            chat_sockfd = -1;
        }
    }
    // UDP chat server logic
    else
    {
        // Set up hints
        hints.ai_family = AF_UNSPEC;    // v4 or v6
        hints.ai_socktype = SOCK_DGRAM; // UDP Sock
        hints.ai_flags = AI_PASSIVE;
        hints.ai_protocol = IPPROTO_UDP; // UDP Protocol Only

        // Call getaddrinfo
        getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &result);
        // Failure - log it and leave
        if (getaddrinfo_success != 0)
        {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
            exit(1);
        }

        // Try all the address structures until one works... sounds crazy
        for (rp = result; rp != NULL; rp = rp->ai_next)
        {
            // Create socket
            chat_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            // Creating sock failed, try next structure
            if (chat_sockfd == -1)
            {
                continue;
            }

            // Now attempt to bind
            if (bind(chat_sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            {
                //Success
                // Start the server loop
                udp_server_running(chat_sockfd);

                // If we are here, we are shutting down!
                break;
            }

            // Bind failed, release socket and try next structure
            close(chat_sockfd);
            chat_sockfd = -1;
        }
    }

    // Free stuff and exit
    if(chat_sockfd != -1)
    {
        close(chat_sockfd);
    }
    freeaddrinfo(result);
    free(c_port);
    exit(0);
}

/*
* Start listening on TCP socket
*/
void start_listen_old(int chat_sockfd)
{
    if (listen(chat_sockfd, BACKLOG) < 0)
    {
        // perror("listen");
        exit(1);
    }
}

/*
* While loop of TCP server
* If we notice a thread's shutdown is set to 1
* Running server is set to 0 and starts shutting down server
*/
void tcp_server_running(int chat_sockfd)
{
    // Flag to kill server
    int running_server = 1;

    // Client info
    struct sockaddr client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Track connections
    int connections_count = 0;

    // Reserve memory for all possible connections (I think...)
    struct tcp_thread_data connection_thread_data[BACKLOG];
    for (int index = 0; index < BACKLOG; index++)
    {
        connection_thread_data[index].host = (char *)malloc(NI_MAXHOST * sizeof(char));
        connection_thread_data[index].service = (char *)malloc(NI_MAXSERV * sizeof(char));
        // connection_thread_data[index].thread = malloc(sizeof(pthread_t));
        connection_thread_data[index].shutdown = 0;
        connection_thread_data[index].client_sock = 0;
        memset(&connection_thread_data[index], 0, sizeof(connection_thread_data[index]));
    }

    while (1)
    {
        // Client chat socket
        int client_connection_sockfd = 0;
        // Accept connection
        if ((client_connection_sockfd = 
            accept(chat_sockfd, &client_addr, &client_addr_len)) < 0)
        {
            // This spams now b/c its unblocking so skip
            // Unless an error we care about
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                // perror("accept");
                continue;
            }
        }
        else
        {
            // Socket from accept will inherit nonblocking flag
            // Undo that!
            int flags = fcntl(client_connection_sockfd, F_GETFL, 0);
            if (flags == -1)
            {
                // perror("fcntl F_GETFL: ");
            }
            flags = fcntl(client_connection_sockfd, F_SETFL, flags & ~O_NONBLOCK);
            if (flags == -1)
            {
                // perror("fcntl F_SETFL: ");
            }

            // Get printable host/port
            char *host = (char *)malloc(NI_MAXHOST * sizeof(char));
            char *service = (char *)malloc(NI_MAXSERV * sizeof(char));

            int getnameinfo_success = 
                getnameinfo(&client_addr, client_addr_len,
                    host, NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV
                );
            if (getnameinfo_success != 0)
            {
                fprintf(stderr, 
                    "Failed to get name from client addr: %s\n", 
                    gai_strerror(getnameinfo_success)
                );
            }

            // Print connection info
            printf("%s%d%s%s%s%s%s",
                   "connection ",
                   connections_count,
                   " from ('",
                   host,
                   "', ",
                   service,
                   ")\n");

            // Set client data before firing off thread
            connection_thread_data[connections_count].host = host;
            connection_thread_data[connections_count].service = service;
            connection_thread_data[connections_count].client_sock = 
                client_connection_sockfd;

            // Some prints I'm leaving
            // for (int index = 0; index < connections_count; index++)
            // {
            //     printf("%s\n", connection_thread_data[index].host);
            //     printf("%s\n", connection_thread_data[index].service);
            //     printf("%d\n", connection_thread_data[index].client_sock);
            //     printf("%d\n", connection_thread_data[index].shutdown);
            //     printf("\n");
            // }

            // Fire off thread
            pthread_t thread_id;
            // connection_thread_data[connections_count].thread = &thread_id;
            pthread_create(&thread_id, 
                NULL, 
                tcp_server_thread, 
                (void *)&connection_thread_data[connections_count]
            );

            connections_count++;
        }

        // Check threads for shutdown flag
        for (int index = 0; index < BACKLOG; index++)
        {
            if (connection_thread_data[index].shutdown == 1)
            {
                running_server = 0;
            }
        }

        // Someone told us to shutdown completely.
        if (running_server == 0)
        {
            // Exit main loop
            break;
        }
    }

    // We were told to shutdown so let's clean up some stuff
    for (int index = 0; index < BACKLOG; index++)
    {
        // Socket is still open so close it.
        if (connection_thread_data[index].client_sock != 0)
        {
            close(connection_thread_data[index].client_sock);
            // pthread_exit(connection_thread_data[index].thread);
        }
        // Null out the data... this necessary?
        memset(&connection_thread_data[index], 
            0, 
            sizeof(connection_thread_data[index])
        );
        // Free our host/service buffers b/c we malloc'd them
        free(connection_thread_data[index].host);
        free(connection_thread_data[index].service);
    }

    return;
}

/*
* Loop for a client's connection encapsulated in a thread
* State = 1
*   We got a hello, send back World
*   Or
*   Normal just echo message
* State = 0
*   We got a goodybe, send back farewell, and close client socket/exit thread
* State = -1
*   We got an exit, send back ok, and shut down server
*/
void *tcp_server_thread(void *arg)
{
    // Cast data to our thread_data struct
    tcp_thread_data *client_data = (tcp_thread_data *)arg;

    // Some prints I'm leaving
    // printf("%s%d%s", "tcp_server_thread created. 
        // Using socket = ", client_data->client_sock, "\n");
    // printf("%s\n", client_data->host);
    // printf("%s\n", client_data->service);
    // printf("%d\n", client_data->client_sock);
    // printf("%d\n", client_data->shutdown);
    // printf("\n");

    // Buffer for message
    char *read_message = malloc(256 * sizeof(char));

    while (1)
    {
        // Clear buffer
        memset(read_message, 0, (256 * sizeof(char)));

        // Read Messages
        int read_value;
        if ((read_value = 
            recv(client_data->client_sock, 
                read_message, 
                sizeof(255 * sizeof(char)), 
                0)
            < 0))
        {
            // Most likely b/c server shutting down 
            // From other thread's exit call
            if (errno == EBADF)
            {
                // Error exit
                // perror("recv: prob. server shutdown - killing thread");
                // Release the buffer's memory
                free(read_message);
                pthread_exit(NULL);
            }
            // Normal error, print it, and move on
            else
            {
                // perror("recv");
                continue;
            }
        }
        // Read succeeded
        else
        {
            // Print we got a message
            printf("%s%s%s%s%s",
                   "got message from ('",
                   client_data->host,
                   "', ",
                   client_data->service,
                   ")\n");

            // Do stuff with the message
            // See comments about function
            int state = 1;
            char world[6] = "world\n";
            char farewell[9] = "farewell\n";
            char ok[3] = "ok\n";
            if (strcmp(read_message, "hello\n") == 0 || strcmp(read_message, "hello") == 0)
            {
                int sent_bytes = send(client_data->client_sock, world, sizeof(world), 0);
                if (sent_bytes < 0)
                {
                    // perror("send");
                }
                state = 1;
            }
            else if (strcmp(read_message, "goodbye\n") == 0 || strcmp(read_message, "goodbye") == 0)
            {
                int sent_bytes = send(client_data->client_sock, farewell, sizeof(farewell), 0);
                if (sent_bytes < 0)
                {
                    // perror("send");
                }
                state = 0;
            }
            else if (strcmp(read_message, "exit\n") == 0 || strcmp(read_message, "exit") == 0)
            {
                int sent_bytes = send(client_data->client_sock, ok, sizeof(ok), 0);
                if (sent_bytes < 0)
                {
                    // perror("send");
                }
                state = -1;
            }
            else
            {
                int sent_bytes = send(client_data->client_sock, read_message, sizeof(read_message), 0);
                if (sent_bytes < 0)
                {
                    // perror("send");
                }
                state = 1;
            }

            // Shutdown server
            if (state < 0)
            {
                // Closer current connection
                close(client_data->client_sock);
                // Set flag to shutdown server
                client_data->shutdown = 1;
                // Break and allow thread to close
                break;
            }
            // Close connection with client_sockfd
            else if (state == 0)
            {
                // Close current connection
                shutdown(client_data->client_sock, SHUT_WR);
                // Break and allow thread to close
                break;
            }
        }
    }

    // Normal exit
    // Release the buffer's memory
    free(read_message);
    // Exit thread
    pthread_exit(NULL);
}

/*
* While loop of UDP server
* State is same as comments above tcp_server_thread function
*/
void udp_server_running(int chat_sockfd)
{
    // Client info
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Buffer for readable host/port
    char host[NI_MAXHOST], service[NI_MAXSERV];

    // Buffer for message
    char *read_message = malloc(256 * sizeof(char));

    while (1)
    {
        // 0 out buffers for freshness sake
        memset(read_message, 0, (256 * sizeof(char)));

        // Read Messages
        int read_value = recvfrom(chat_sockfd, read_message, sizeof(255 * sizeof(char)), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (read_value < 0)
        {
            // perror("recvfrom");
            break;
        }
        else
        {
            // Get readable host/port
            int getnameinfo_success = getnameinfo((struct sockaddr *)&client_addr, client_addr_len, host, NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
            if (getnameinfo_success != 0)
            {
                fprintf(stderr, "Failed to get name from client addr: %s\n", gai_strerror(getnameinfo_success));
            }

            // Read succeeded
            // Print we got a message
            printf("%s%s%s%s%s",
                   "got message from ('",
                   host,
                   "', ",
                   service,
                   ")\n");

            // Do stuff with the message
            int exit = udp_handle_received_message(client_addr, client_addr_len, read_message, chat_sockfd);

            // Shutdown server
            if (exit < 0)
            {
                // UDP doesn't close a current connection
                // Because it is connection-less.
                // So break from main loop
                // We close chat_sockfd down the line
                break;
            }
            // Close connection with new_sockfd
            else if (exit == 0)
            {
                // UDP doesn't close a current connection
                // Because it is connection-less.
                // So just continue on
                memset(&client_addr, 0, sizeof(client_addr));
                continue;
            }
        }
    }

    // Someone told us to shutdown completely.
    // Release the buffer's memory
    free(read_message);

    // Close connection socket
    close(chat_sockfd);
    return;
}

int udp_handle_received_message(struct sockaddr_storage client_addr, socklen_t client_addr_len, char *message_buffer, int new_sockfd)
{
    char world[6] = "world\n";
    char farewell[9] = "farewell\n";
    char ok[3] = "ok\n";
    if (strcmp(message_buffer, "hello\n") == 0 || strcmp(message_buffer, "hello") == 0)
    {
        int sent_bytes = sendto(new_sockfd, world, sizeof(world), 0, (struct sockaddr *)&client_addr, client_addr_len);
        if (sent_bytes < 0)
        {
            // perror("sendto");
        }
        return 1;
    }
    else if (strcmp(message_buffer, "goodbye\n") == 0 || strcmp(message_buffer, "goodbye") == 0)
    {
        int sent_bytes = sendto(new_sockfd, farewell, sizeof(farewell), 0, (struct sockaddr *)&client_addr, client_addr_len);
        if (sent_bytes < 0)
        {
            // perror("sendto");
        }
        return 0;
    }
    else if (strcmp(message_buffer, "exit\n") == 0 || strcmp(message_buffer, "exit") == 0)
    {
        int sent_bytes = sendto(new_sockfd, ok, sizeof(ok), 0, (struct sockaddr *)&client_addr, client_addr_len);
        if (sent_bytes < 0)
        {
            // perror("sendto");
        }
        return -1;
    }
    else
    {
        int sent_bytes = sendto(new_sockfd, message_buffer, sizeof(message_buffer), 0, (struct sockaddr *)&client_addr, client_addr_len);
        if (sent_bytes < 0)
        {
            // perror("sendto");
        }
        return 1;
    }
}

void chat_client(char *host, long port, int use_udp)
{
    // Chat Socket
    int chat_sockfd = -1;

    // Build addrinfo family-agnostic
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    memset(&hints, 0, sizeof(hints));
    int getaddrinfo_success;

    // unsigned short ui_port = htons(port);
    char *c_port = malloc(sizeof(port));
    snprintf(c_port, sizeof(port), "%ld", port);

    //TCP chat client
    if (use_udp == 0)
    {
        hints.ai_family = AF_UNSPEC;     // v4 or v6
        hints.ai_socktype = SOCK_STREAM; // TCP socket
        hints.ai_flags = 0;
        hints.ai_protocol = IPPROTO_TCP; // TCP Protocol Only

        getaddrinfo_success = getaddrinfo(host, c_port, &hints, &result);
        // printf("getaddrinfo_success = %d\n", getaddrinfo_success);
        if (getaddrinfo_success != 0)
        {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
            exit(1);
        }

        // Try all the lists of address structures until one works... sounds crazy
        for (rp = result; rp != NULL; rp = rp->ai_next)
        {
            // Creating sock failed, try next structure
            if ((chat_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1)
            {
                continue;
            }

            // Connection failed
            if (connect(chat_sockfd, rp->ai_addr, rp->ai_addrlen) == -1)
            {
                // Don't exit b/c maybe another structure
                // perror("connect");
            }
            // Connection succeeded
            else
            {
                // Start to read/write to server
                tcp_client_running(chat_sockfd);
                // If we return we were told to shutdown via goodbye/exit messages
                // So break from here!
                break;
            }

            // Connect failed, release socket and try next structure
            close(chat_sockfd);
        }

        // Shutting down - release stuff
        freeaddrinfo(result); // Just following the man page example...
    }
    //UDP Chat Client
    else
    {
        hints.ai_family = AF_UNSPEC;    // v4 or v6
        hints.ai_socktype = SOCK_DGRAM; // UDP socket
        hints.ai_flags = 0;
        hints.ai_protocol = IPPROTO_UDP; // UDP Protocol Only

        getaddrinfo_success = getaddrinfo(host, c_port, &hints, &result);
        // printf("getaddrinfo_success = %d\n", getaddrinfo_success);
        if (getaddrinfo_success != 0)
        {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
            exit(1);
        }

        // Try all the lists of address structures until one works... sounds crazy
        for (rp = result; rp != NULL; rp = rp->ai_next)
        {
            // Creating sock failed, try next structure
            if ((chat_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1)
            {
                continue;
            }
            else 
            {

                // Connection failed
                // if (connect(chat_sockfd, rp->ai_addr, rp->ai_addrlen) == -1)
                // {
                    // Don't exit b/c maybe another structure
                    // perror("connect - attempting next structure -");
                // }
                // Connection succeeded
                // else
                // {
                    // Start to read/write to server
                    udp_client_running(chat_sockfd, rp->ai_addr, rp->ai_addrlen);
                    // If we return we were told to shutdown via goodbye/exit messages
                    // So break from here!
                    break;
                // }
            }

            // Connect failed, release socket and try next structure
            close(chat_sockfd);
        }

        // Shutting down - release stuff
        freeaddrinfo(result); // Just following the man page example...
    }

    // Release the socket
    if(chat_sockfd != -1) 
    {
        close(chat_sockfd);
    }
    exit(0);
}

/*
* Expecting State = 0 when no special messages are sent.
* Specical messages are: hello, goodbye, exit
* Hello sends us to E.S. = 1
*   Just wait for the world message and go back to E.S. = 1
* Goodbye sends us to E.S. = 2
*   Wait for farewell message and terminate my socket and my client program
* Exit sends us to E.S. = 3
*   Wait for ok message and terminate my socket and my client program
*/
void tcp_client_running(int chat_sockfd)
{
    // Send messages
    char *send_message = (char *)malloc(256 * sizeof(char));
    char *read_message = (char *)malloc(256 * sizeof(char));

    int expecting_state = 0;

    while (1)
    {
        memset(send_message, 0, (256 * sizeof(char)));
        memset(read_message, 0, (256 * sizeof(char)));

        int status = scanf("%s", send_message);
        if(status == EOF || errno == EOF) 
        {
            // perror("scanf");    
        }

        if (strcmp(send_message, "hello\n") == 0 || strcmp(send_message, "hello") == 0)
        {
            expecting_state = 1;
        }
        if (strcmp(send_message, "goodbye\n") == 0 || strcmp(send_message, "goodbye") == 0 ||
            strcmp(send_message, "exit\n") == 0 || strcmp(send_message, "exit") == 0)
        {
            expecting_state = 2;
        }

        if (send(chat_sockfd, send_message, strlen(send_message), 0) < 0)
        {
            // perror("send");
            exit(1);
        }

        // Read Messages
        if ((recv(chat_sockfd, read_message, sizeof(255 * sizeof(char)), 0) < 0))
        {
            // perror("recv");
            exit(1);
        }
        // Read succeeded
        else
        {
            if (expecting_state == 1)
            {
                // Server will send world\n so no new line needed
                printf("%s", read_message);
                expecting_state = 0;
            }
            else if (expecting_state == 2 || expecting_state == 3)
            {
                // Server is shutting us down regardless of 2 or 3
                // So print last received message and break out of loop.
                printf("%s", read_message);
                break;
            }
            else
            {
                printf("%s\n", read_message);
            }
        }
    }

    // Shutting down - release stuff and return
    // release the buffer's memory
    free(send_message);
    free(read_message);
    return;
}

/*
* Expecting State = 0 when no special messages are sent.
* Specical messages are: hello, goodbye, exit
* Hello sends us to E.S. = 1
*   Just wait for the world message and go back to E.S. = 1
* Goodbye sends us to E.S. = 2
*   Wait for farewell message and terminate my socket and my client program
* Exit sends us to E.S. = 3
*   Wait for ok message and terminate my socket and my client program
*/
void udp_client_running(int chat_sockfd, struct sockaddr *server_address, socklen_t address_length) 
{
    // Send messages
    char *send_message = (char *)malloc(256 * sizeof(char));
    char *read_message = (char *)malloc(256 * sizeof(char));

    int expecting_state = 0;

    while (1)
    {
        memset(send_message, 0, (256 * sizeof(char)));
        memset(read_message, 0, (256 * sizeof(char)));

        int status = scanf("%s", send_message);
        if(status == EOF || errno == EOF) 
        {
            // perror("scanf");    
        }

        if (strcmp(send_message, "hello\n") == 0 || strcmp(send_message, "hello") == 0)
        {
            expecting_state = 1;
        }
        if (strcmp(send_message, "goodbye\n") == 0 || strcmp(send_message, "goodbye") == 0 ||
            strcmp(send_message, "exit\n") == 0 || strcmp(send_message, "exit") == 0)
        {
            expecting_state = 2;
        }

        if (sendto(chat_sockfd, send_message, strlen(send_message), 0, server_address, address_length) < 0)
        {
            // perror("send");
            break;
            // exit(1);
        }

        // Read Messages
        if ((recvfrom(chat_sockfd, read_message, sizeof(255 * sizeof(char)), 0, server_address, &address_length) < 0))
        {
            // perror("recv");
            break;
            // exit(1);
        }
        // Read succeeded
        else
        {
            if (expecting_state == 1)
            {
                // Server will send world\n so no new line needed
                printf("%s", read_message);
                expecting_state = 0;
            }
            else if (expecting_state == 2 || expecting_state == 3)
            {
                // Server is shutting us down regardless of 2 or 3
                // So print last received message and break out of loop.
                printf("%s", read_message);
                break;
            }
            else
            {
                printf("%s\n", read_message);
            }
        }
    }

    // Shutting down - release stuff and return
    // release the buffer's memory
    free(send_message);
    free(read_message);
    return;
}
