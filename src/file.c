/*******************************************
 * Ethan Behar ebehar
 * Created: 10/18/21
 * 
 * 
 * File.c is a program that handles a simple file transfer server
 * and client. It builds a UDP or TCP connection based
 * on input parameters The client sends the server a file
 * and based on input parameters.
 * 
 * References: 
 * Char* to number
 * https://stackoverflow.com/questions/628761/convert-a-character-digit-to-the-corresponding-integer-in-c#:~:text=If%20it's%20just%20a%20single,%22%25d%22%2C%20%26value)%3B
 * https://stackoverflow.com/questions/3420629/what-is-the-difference-between-sscanf-or-atoi-to-convert-a-string-to-an-integer
 * ftell/fseek man pages
 * https://www.geeksforgeeks.org/c-program-find-size-file/#:~:text=The%20idea%20is%20to%20use,is%20actually%20size%20in%20bytes.
 * https://www.techonthenet.com/c_language/standard_library_functions/math_h/ceil.php
 *
 * ./netster -p 7779 -f output
 * ./netster 127.0.0.1 -p 7779 -f ShortTest.txt
 * 
 * ./netster -p 7779 -u -f output
 * ./netster 127.0.0.1 -p 7779 -u -f ShortTest.txt
 * cd users/mumma/documents/github/net-fall21/src
*******************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include "file.h"
#include <math.h>

// general helper functions
struct addrinfo chatc_setup_hints();
char* chatc_convert_long_to_char_array(long port);
void chatc_start_listen(int sockfd);
void chatc_print_client_connection_info(struct sockaddr client_addr, socklen_t client_addr_len, int connections_count);
long chatc_get_file_chunks(long file_sze, long chunk_size);
long chatc_get_remaining_bytes(long file_size, long chunk_size);

// helper functions for file transfer
int tcp_read_message_write_file(int sockfd, FILE* fp, long read_bytes);
int udp_read_message_write_file(int sockfd, FILE* fp, long read_bytes, struct sockaddr* client_addr, socklen_t* client_addr_len);
int read_file_send_message(int sockfd, FILE* fp, long send_bytes);

// Starts and runs the TCP server
void start_and_run_tcp_server(char* iface, long port, FILE* fp);
void run_tcp_file_server(int file_sockfd, FILE* fp);

// Starts and runs the UDP server
void start_and_run_udp_server(char* iface, long port, FILE* fp);
void run_udp_file_server(int file_sockfd, FILE* fp);

// Starts and runs the TCP client
void start_and_run_tcp_client(char* iface, long port, FILE* fp);
void run_tcp_file_client(int file_sockfd, FILE* fp) ;

// Starts and runs the UDP client
void start_and_run_udp_client(char* iface, long port, FILE* fp);
void run_udp_file_client(int file_sockfd, FILE* fp);

// Constants
#define BACKLOG 5
#define MAX_MESSAGE_SIZE 256

/*
 * Helper Methods
 */
struct addrinfo chatc_setup_hints() 
{
    // Create hints
    struct addrinfo hints;

    // Clear hints memory
    memset(&hints, 0, sizeof(hints));

    // Set up non-protocol specific hints
    hints.ai_family = AF_UNSPEC;     // v4 or v6
    hints.ai_flags = AI_PASSIVE;

    return hints;
}

char* chatc_convert_port_long_to_char_array(long port) {
    // convert to short 1st
    unsigned short ui_port = htons(port);
    
    // create char* big enough
    char *c_port = malloc(sizeof(port));
    
    // convert short to char*
    snprintf(c_port, sizeof(port), "%hu", ui_port);

    return c_port;
}

void chatc_start_listen(int sockfd) 
{
    if(listen(sockfd, BACKLOG) < 0)
    {
        perror("Listen: ");
        return;
    }
}

void chatc_print_client_connection_info(struct sockaddr client_addr, socklen_t client_addr_len, int connections_count)
{
    // Vars for printable host/port
    char *host = (char *)malloc(NI_MAXHOST * sizeof(char));
    char *service = (char *)malloc(NI_MAXSERV * sizeof(char));

    // Get printable host/port
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
    // TCP logic
    if(connections_count >= 0) 
    {
        printf("%s%d%s%s%s%s%s",
                "connection ",
                connections_count,
                " from ('",
                host,
                "', ",
                service,
                ")\n");
    }
    // UDP logic
    else
    {
        printf("%s%s%s%s%s",
                "got message from ('",
                host,
                "', ",
                service,
                ")\n");
    }
    free(host);
    free(service);
}

long chatc_get_file_chunks(long file_size, long chunk_size) 
{
    printf("%s%lu%s", "File size in bytes: ", file_size, "\n");
    printf("%s%lu%s", "Chunk size in bytes: ", chunk_size, "\n");
    printf("%s%f%s", "Raw chunks: ", ((double)file_size / (double)chunk_size), "\n");
    long chunks = (long)(ceil((double)file_size / (double)chunk_size));
    printf("%s%lu%s", "Number of chunks: ", chunks, "\n");
    return chunks;
}

long chatc_get_remaining_bytes(long file_size, long chunk_size) 
{
    double integral; 
    double remaining_bytes = modf(((double)file_size / (double)chunk_size), &integral);
    long remaining_bytes_long = remaining_bytes * (double)chunk_size;
    printf("%s%lu%s", "Remaining bytes: ", remaining_bytes_long, "\n");
    return remaining_bytes_long;
}

int tcp_read_message_write_file(int sockfd, FILE* fp, long read_bytes)
{
    // Buffer for message
    char *read_message = malloc(read_bytes);
    // 0 out buffers for freshness sake
    memset(read_message, 0, read_bytes);     

    // These messages should be messages containing file data.
    if (recv(sockfd, read_message, read_bytes, 0) < 0)
    {
        // Failure
        perror("Recv: ");
        return -1;
    }
    else 
    {
        // Success
        // Start writing data to a file
        fwrite(read_message, 1, read_bytes, fp);
    }
    // Release the buffer's memory
    free(read_message);
    return 0;
}

int udp_read_message_write_file(int sockfd, FILE* fp, long read_bytes, struct sockaddr* client_addr, socklen_t* client_addr_len)
{
    // Buffer for message
    char *read_message = malloc(read_bytes);
    // 0 out buffers for freshness sake
    memset(read_message, 0, read_bytes);     

    // These messages should be messages containing file data.
    if(recvfrom(sockfd, read_message, read_bytes, 0, client_addr, client_addr_len) < 0)
    {
        // Failure
        perror("RecvFrom: ");
        return -1;
    }
    else 
    {
        // Success
        // Start writing data to a file
        fwrite(read_message, 1, read_bytes, fp);
    }
    // Release the buffer's memory
    free(read_message);
    return 0;
}

int read_file_send_message(int sockfd, FILE* fp, long send_bytes)
{
    // Create buffer of 256 bytes
    char *send_message = (char *)malloc(send_bytes * sizeof(char));
    // 0 out buffers for freshness sake
    memset(send_message, 0, (send_bytes * sizeof(char)));

    size_t readStatus = fread(send_message, 1, send_bytes, fp);
    if (readStatus != send_bytes) 
    {
        fprintf(stderr, "Fread() failed: %zu\n", readStatus);
        printf("%i%s", ferror(fp), "%s");
        return -1;
    }
    else 
    {
        if (send(sockfd, send_message, send_bytes, 0) < 0)
        {
            // Sending data failed
            perror("Send data: ");
            return -1;
        }
    }
    // Release the buffer's memory
    free(send_message);
    return 0;
}

/*
 * Server start point
 */
void file_server(char* iface, long port, int use_udp, FILE* fp) 
{
    if(use_udp == 0) 
    {
        start_and_run_tcp_server(iface, port, fp);
    } 
    else 
    {
        start_and_run_udp_server(iface, port, fp);
    }
}

/*
 * TCP Server start point
 */
void start_and_run_tcp_server(char* iface, long port, FILE* fp) 
{
    // Create socket
    int file_sockfd = -1;
    
    // Get hints
    struct addrinfo hints = chatc_setup_hints();

    // Finish hints setup
    hints.ai_socktype = SOCK_STREAM; // TCP Sock
    hints.ai_protocol = IPPROTO_TCP; // TCP Protocol Only

    // Get port as char*
    char* c_port = chatc_convert_port_long_to_char_array(port);

    // Results of getaddrinfo
    struct addrinfo *result, *rp;
    int getaddrinfo_success;

    // Call getaddrinfo
    getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &result);
    // Failure - log it and leave
    if (getaddrinfo_success != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
        return;
    }

    // Try all the address structures until one works... sounds crazy
    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        // Create socket
        file_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        // Creating socket failed, log, and try next structure
        if (file_sockfd == -1)
        {
            perror("Socket(trying next structure): ");
            continue;
        }

        if(bind(file_sockfd, rp->ai_addr, rp->ai_addrlen) == 0) 
        {
            // Successful bind

            // Start listening on the socket
            chatc_start_listen(file_sockfd);

            // Start server loop
            run_tcp_file_server(file_sockfd, fp);

            // If we here, we are shutting down!
            break;
        }

        // Bind failed, release socket, and try next structure
        close(file_sockfd);
        file_sockfd = -1;
    }

    // Free stuff and return
    if(file_sockfd != -1)
    {
        close(file_sockfd);
    }
    // freeaddrinfo(&hints);
    freeaddrinfo(result);
    free(c_port);
}

void run_tcp_file_server(int file_sockfd, FILE* fp) 
{
    // Connections counter
    int connections_count = 0;

    // Client info
    struct sockaddr client_addr;
    socklen_t client_addr_len = sizeof(client_addr); 

    // Main server loop
    while(1)
    {
        // Accept incoming connection
        int client_sockfd = -1;
        if((client_sockfd = accept(file_sockfd, &client_addr, &client_addr_len)) < 0)
        {
            // Accept Failure
            perror("Accept: ");
            break;
        }
        else
        {
            // Successful accept
            
            // Print connection info
            chatc_print_client_connection_info(client_addr, client_addr_len, connections_count);

            // First message is file size.
            // So grab that so we can calculate when to end.
            long file_size = -1;
            if (recv(client_sockfd, &file_size, sizeof(long), 0) < 0)
            {
                perror("Recv: ");
                break;
            }
            else
            {
                // Chop file size into chunks of 256 bytes
                long chunks = chatc_get_file_chunks(file_size, MAX_MESSAGE_SIZE);
                // Get the remainder of the left over bytes.
                // ie if 27 we want 27 OR 258 we want 2
                long remaining_bytes = chatc_get_remaining_bytes(file_size, MAX_MESSAGE_SIZE);

                // Now that we have the chunk size,
                // let's start receiving the file.
                while(chunks > 0) 
                {
                    if(chunks > 1) 
                    {
                        int error = tcp_read_message_write_file(client_sockfd, fp, MAX_MESSAGE_SIZE);
                        if (error == -1) 
                        {
                            break;
                        }
                    }
                    else
                    {
                        if(remaining_bytes == 0)
                        {
                            remaining_bytes = MAX_MESSAGE_SIZE;
                        }
                        int error = tcp_read_message_write_file(client_sockfd, fp, remaining_bytes);
                        if (error == -1) 
                        {
                            break;
                        }
                    }
                    // Decrement counter
                    chunks--;
                }
            }
            // Reading/Writing completed or failed
            // Regardless break loop
            break;
        }
    }
}

/*
 * UDP Server start point
 */
void start_and_run_udp_server(char* iface, long port, FILE* fp) 
{
    // Create socket
    int file_sockfd = -1;
    
    // Get hints
    struct addrinfo hints = chatc_setup_hints();

    // Finish hints setup
    hints.ai_socktype = SOCK_DGRAM; // UDP Sock
    hints.ai_protocol = IPPROTO_UDP; // UDP Protocol Only

    // Get port as char*
    char* c_port = chatc_convert_port_long_to_char_array(port);

    // Results of getaddrinfo
    struct addrinfo *result, *rp;
    int getaddrinfo_success;

    // Call getaddrinfo
    getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &result);
    // Failure - log it and leave
    if (getaddrinfo_success != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
        return;
    }

    // Try all the address structures until one works... sounds crazy
    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        // Create socket
        file_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        // Creating socket failed, log, and try next structure
        if (file_sockfd == -1)
        {
            perror("Socket(trying next structure): ");
            continue;
        }

        if(bind(file_sockfd, rp->ai_addr, rp->ai_addrlen) == 0) 
        {
            // Successful bind

            // Start server loop
            run_udp_file_server(file_sockfd, fp);

            // If we here, we are shutting down!
            break;
        }

        // Bind failed, release socket, and try next structure
        close(file_sockfd);
        file_sockfd = -1;
    }

    // Free stuff and return
    if(file_sockfd != -1)
    {
        close(file_sockfd);
    }
    // freeaddrinfo(&hints);
    freeaddrinfo(result);
    free(c_port);
}

void run_udp_file_server(int file_sockfd, FILE* fp) 
{
    // Client info
    struct sockaddr client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Main server loop
    while(1)
    {
        // First message is file size.
        // So grab that so we can calculate when to end.
        // Buffer for message
        long file_size = -1;
        if(recvfrom(file_sockfd, &file_size, sizeof(long), 0, (struct sockaddr *)&client_addr, &client_addr_len) < 0)
        {
            perror("RecvFrom: ");
            break;
        }
        else
        {
            // Chop file size into chunks of 256 bytes
            long chunks = chatc_get_file_chunks(file_size, MAX_MESSAGE_SIZE);
            // Get the remainder of the left over bytes.
            // ie if 27 we want 27 OR 258 we want 2
            long remaining_bytes = chatc_get_remaining_bytes(file_size, MAX_MESSAGE_SIZE);

            // Now that we have the chunk size,
            // let's start receiving the file.
            while(chunks > 0) 
            {
                //printf("%s%lu%s", "Processing chunk: ", chunks, "\n");
                if(chunks > 1) 
                {
                    int error = udp_read_message_write_file(file_sockfd, fp, MAX_MESSAGE_SIZE, (struct sockaddr *)&client_addr, &client_addr_len);
                    if(error == -1) 
                    {
                        break;
                    }
                }
                else 
                {
                    if(remaining_bytes == 0)
                    {
                        remaining_bytes = MAX_MESSAGE_SIZE;
                    }
                    int error = udp_read_message_write_file(file_sockfd, fp, remaining_bytes, (struct sockaddr *)&client_addr, &client_addr_len);
                    if(error == -1) 
                    {
                        break;
                    }
                }
                // Decrement chunks
                chunks--;
            }
        }
        // Reading/Writing completed or failed
        // Regardless break loop
        break;
    }
}

/*
 * Client start point
 */
void file_client(char* iface, long port, int use_udp, FILE* fp)
{
    if(use_udp == 0) 
    {
        start_and_run_tcp_client(iface, port, fp);
    } 
    else 
    {
        start_and_run_udp_client(iface, port, fp);
    }
}

/*
 * TCP Client start point
 */
void start_and_run_tcp_client(char* iface, long port, FILE* fp) 
{
    // Create socket
    int file_sockfd = -1;
    
    // Get hints
    struct addrinfo hints = chatc_setup_hints();

    // Finish hints setup
    hints.ai_socktype = SOCK_STREAM; // TCP Sock
    hints.ai_protocol = IPPROTO_TCP; // TCP Protocol Only

    // Get port as char*
    char* c_port = chatc_convert_port_long_to_char_array(port);

    // Results of getaddrinfo
    struct addrinfo *result, *rp;
    int getaddrinfo_success;

    // Call getaddrinfo
    getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &result);
    // Failure - log it and leave
    if (getaddrinfo_success != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
        return;
    }

    // Try all the lists of address structures until one works
    for(rp = result; rp != NULL; rp = rp->ai_next) 
    {
        // Create socket
        if((file_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0)
        {
            // Failed, try next structure
            continue;
        }

        // Attempt connection to server
        if(connect(file_sockfd, rp->ai_addr, rp->ai_addrlen) < 0) 
        {
            // Failed
            perror("Connect: ");
        }
        // Connection success
        else 
        {
            // Start the file transfer logic
            run_tcp_file_client(file_sockfd, fp);

            // If we are here, we are shutting down
            break;
        }

        // Socket created but connection failed - free sock
        close(file_sockfd);
        file_sockfd = -1;
    }

    // Shutting down - release stuff
    if(file_sockfd != -1)
    {
        close(file_sockfd);
    }
    // freeaddrinfo(&hints);
    freeaddrinfo(result);
    free(c_port);
}

void run_tcp_file_client(int file_sockfd, FILE* fp) 
{
    // Main client loop
    while(1) 
    {
        // Go to end of file
        fseek(fp, 0L, SEEK_END);
        
        // Get file size in bytes
        long file_size = ftell(fp);
        
        // Set indicator back to beginning of file.
        rewind(fp);
        
        // Chop file size into chunks of 256 bytes
        long chunks = chatc_get_file_chunks(file_size, MAX_MESSAGE_SIZE);

        // Get the remainder of the left over bytes.
        // ie if 27 we want 27 OR 258 we want 2
        long remaining_bytes = chatc_get_remaining_bytes(file_size, MAX_MESSAGE_SIZE);

        // Send chunk size
        if (send(file_sockfd, &file_size, sizeof(file_size), 0) < 0)
        {
            perror("Send file_size: ");
            break;
        }
        else 
        {
            while(chunks > 0) 
            {
                // Start sending file
                if(chunks > 1) 
                {
                    int error = read_file_send_message(file_sockfd, fp, MAX_MESSAGE_SIZE);
                    if (error == -1) 
                    {
                        break;
                    }
                }
                else 
                {
                    if(remaining_bytes == 0)
                    {
                        remaining_bytes = MAX_MESSAGE_SIZE;
                    }
                    int error = read_file_send_message(file_sockfd, fp, remaining_bytes);
                    if (error == -1) 
                    {
                        break;
                    }
                }
                // Decrement chunks
                chunks--;
            }
            // Sending completed or failed
            // Regardless leave loop
            break;
        }
    }
}

/*
 * UDP Client start point
 */
void start_and_run_udp_client(char* iface, long port, FILE* fp) 
{
    // Create socket
    int file_sockfd = -1;
    
    // Get hints
    struct addrinfo hints = chatc_setup_hints();

    // Finish hints setup
    hints.ai_socktype = SOCK_DGRAM; // TCP Sock
    hints.ai_protocol = IPPROTO_UDP; // TCP Protocol Only

    // Get port as char*
    char* c_port = chatc_convert_port_long_to_char_array(port);

    // Results of getaddrinfo
    struct addrinfo *result, *rp;
    int getaddrinfo_success;

    // Call getaddrinfo
    getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &result);
    // Failure - log it and leave
    if (getaddrinfo_success != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
        return;
    }
    
    // Try all the lists of address structures until one works
    for(rp = result; rp != NULL; rp = rp->ai_next) 
    {
        // Create socket
        if((file_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0)
        {
            // Failed, try next structure
            continue;
        }

        // Attempt connection to server
        if(connect(file_sockfd, rp->ai_addr, rp->ai_addrlen) < 0) 
        {
            // Failed
            perror("Connect: ");
        }
        // Connection success
        else 
        {
            // Start the file transfer logic
            run_udp_file_client(file_sockfd, fp);

            // If we are here, we are shutting down
            break;
        }

        // Socket created but connection failed - free sock
        close(file_sockfd);
        file_sockfd = -1;
    }

    // Shutting down - release stuff
    if(file_sockfd != -1)
    {
        close(file_sockfd);
    }
    // freeaddrinfo(&hints);
    freeaddrinfo(result);
    free(c_port);
}

void run_udp_file_client(int file_sockfd, FILE* fp) 
{
    // Main client loop
    while(1) 
    {
        // Go to end of file
        fseek(fp, 0L, SEEK_END);
        
        // Get file size in bytes
        long file_size = ftell(fp);
        
        // Set indicator back to beginning of file.
        rewind(fp);
        
        // Chop file size into chunks of 256 bytes
        long chunks = chatc_get_file_chunks(file_size, MAX_MESSAGE_SIZE);

        // Get the remainder of the left over bytes.
        // ie if 27 we want 27 OR 258 we want 2
        long remaining_bytes = chatc_get_remaining_bytes(file_size, MAX_MESSAGE_SIZE);

        // Send chunk size
        if (send(file_sockfd, &file_size, sizeof(file_size), 0) < 0)
        {
            perror("Send file_size: ");
            break;
        }
        else 
        {
            while(chunks > 0) 
            {
                // Start sending file
                if(chunks > 1) 
                {
                    int error = read_file_send_message(file_sockfd, fp, MAX_MESSAGE_SIZE);
                    if (error == -1) 
                    {
                        break;
                    }
                }
                else 
                {
                    if(remaining_bytes == 0)
                    {
                        remaining_bytes = MAX_MESSAGE_SIZE;
                    }
                    int error = read_file_send_message(file_sockfd, fp, remaining_bytes);
                    if (error == -1) 
                    {
                        break;
                    }
                }
                // Decrement chunks
                chunks--;
            }
            // Sending completed or failed
            // Regardless leave loop
            break;
        }
    }
}
