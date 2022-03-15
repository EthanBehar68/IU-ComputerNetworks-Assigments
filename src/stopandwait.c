/*******************************************
 * Ethan Behar ebehar
 * Created: 10/23/21
 * 
 * Stopandwait.c is a program that implements RUDP 3.0.
 * The program's main logic is file transfer and RUDP 3.0.
 * 
 * References: 
 * Timeout related:
 * https://stackoverflow.com/questions/13547721/udp-socket-set-timeout
 * http://www.mathcs.emory.edu/~cheung/Courses/455/Syllabus/9-netw-prog/timeout.html
 * 
 * ./netster -p 8443 -r 1 -f output
 * ./netster 127.0.0.1 -p 8443 -r 1 -f shorttest
 * strace -e trace=%network
 * dd bs=1024 count=$((2**3)) < /dev/urandom > random8
 * open.sice.indana.edu
*******************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include "stopandwait.h"

// Declare header structure
typedef struct header_t {
    char sequence_number;
    char message_type;
    long chunk;
    long data_length;
} header_t;

typedef struct packet_t {
    struct header_t header;
    char* payload;
} packet_t;

// general helper functions
struct addrinfo snw_setup_hints();
char* snw_long_to_char_array(long number);
long snw_get_file_chunks(long file_sze, long chunk_size);
long snw_get_remaining_bytes(long file_size, long chunk_size);
struct header_t snw_create_header(char sequence_number, char message_type, long chunk, long data_length);

// helper functions for stop and wait
char update_sequence_number(char sequence_number);
char update_ack_number(char sequence_number);

// Runs the UDP server
void snw_run_server(int file_sockfd, FILE* fp);

// Runs the UDP client
void snw_run_client(int file_sockfd, FILE* fp);

// Constants
#define MAX_MESSAGE_SIZE 256

// Stop-N-Wait Constants
#define SERVER_RECEIVE_STATE 0
#define SERVER_ACK_STATE 1
#define SERVER_FIN_STATE 2
#define CLIENT_SEND_STATE 0
#define CLIENT_WAIT_STATE 1
#define SEQUENCE_ZERO '0'
#define SEQUENCE_ONE '1'
#define ACK_ZERO '0'
#define ACK_ONE '1'
#define DATA_MESSAGE_TYPE 'D' // client sending, server receiving
#define ACK_MESSAGE_TYPE 'A' // server sending, client receiving
#define FIN_MESSAGE_TYPE 'F'// server/client shutting down

// For test-netster testing as per Julia G (thank you.)
#define TIMEOUT 60 // 7000=7s 5000=5s 1000=1s 500=.5s
// For local testing
// #define TIMEOUT 1000 // 7000=7s 5000=5s 1000=1s 500=.5s

/*
 * Helper Methods
 */
struct addrinfo snw_setup_hints() 
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

char* snw_long_to_char_array(long number) 
{
    // create char* big enough
    char *c_long = malloc(sizeof(number));
    
    // convert long to char*
    snprintf(c_long, sizeof(number), "%ld", number);

    return c_long;
}

long snw_get_file_chunks(long file_size, long header_size) 
{
    // printf("%s%lu%s", "Header size: ", header_size, "\n");
    long chunk_size = (MAX_MESSAGE_SIZE - header_size);
    // printf("%s%lu%s", "File size in bytes: ", file_size, "\n");
    // printf("%s%lu%s", "Chunk size in bytes: ", chunk_size, "\n");
    // printf("%s%f%s", "Raw chunks: ", ((double)file_size / (double)chunk_size), "\n");
    long chunks = (long)(ceil((double)file_size / (double)chunk_size));
    // printf("%s%lu%s", "Number of chunks: ", chunks, "\n");
    return chunks;
}

long snw_get_remaining_bytes(long file_size, long header_size) 
{
    long chunk_size = (MAX_MESSAGE_SIZE - header_size);
    long remaining_bytes = file_size % chunk_size;

    if(remaining_bytes == 0)
    {
        // printf("%s%lu\n", "Remaining bytes was 0. Forced remaining bytes to ", chunk_size);
        remaining_bytes = chunk_size;
    }
    else 
    {
        // printf("%s%lu\n", "Remaining bytes: ", remaining_bytes);
    }
    return remaining_bytes;
}

struct header_t snw_create_header(char sequence_number, char message_type, long chunk, long data_length) 
{
    // Create header var
    struct header_t header;

    // Clear header memory
    memset(&header, 0, sizeof(header));

    // Add info
    header.sequence_number = sequence_number;
    header.message_type = message_type;
    header.chunk = chunk;
    header.data_length = data_length;

    return header;
}

char update_sequence_number(char number)
{
    if (number == SEQUENCE_ZERO)
    {
        return SEQUENCE_ONE;
    }
    else
    {
        return SEQUENCE_ZERO;
    }
}
                        
char update_ack_number(char number)
{
    if(number == ACK_ZERO)
    {
        return ACK_ONE;
    }
    else
    {
        return ACK_ZERO;
    }
}

// Server start point
void stopandwait_server(char* iface, long port, FILE* fp)
{
    // Create socket
    int file_sockfd = -1;
    
    // Get hints
    struct addrinfo hints = snw_setup_hints();

    // Finish hints setup
    hints.ai_socktype = SOCK_DGRAM; // UDP Sock
    hints.ai_protocol = IPPROTO_UDP; // UDP Protocol Only

    // Get port as char*
    char* c_port = snw_long_to_char_array(port);

    // Results of getaddrinfo
    struct addrinfo *result, *rp;
    int getaddrinfo_success;

    // Call getaddrinfo
    getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &result);
    // Failure - log it and leave
    if (getaddrinfo_success != 0)
    {
        // fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
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
            // perror("Socket(trying next structure): ");
            continue;
        }

        if(bind(file_sockfd, rp->ai_addr, rp->ai_addrlen) == 0) 
        {
            // Successful bind

            // Start server loop
            snw_run_server(file_sockfd, fp);

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

void snw_run_server(int file_sockfd, FILE* fp) 
{
    // Client info
    struct sockaddr client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Server State
    char server_state = SERVER_RECEIVE_STATE;
    char current_ack_number = ACK_ZERO;
    char previous_ack_number = ACK_ZERO;
    char expected_seqence_number = SEQUENCE_ZERO;
    struct header_t header;
    int payload_size = 0;
    long current_chunk = -1;

    // Main server loop
    while(1)
    {
        if(server_state == SERVER_RECEIVE_STATE)
        {
            // printf("\nServer in receive state\n");
            // Create the receive buffer
            char *receive_buffer = (void *)malloc(MAX_MESSAGE_SIZE);
            memset(receive_buffer, 0, MAX_MESSAGE_SIZE);

            // Receive Data 
            if (recvfrom(file_sockfd, receive_buffer, MAX_MESSAGE_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len) < 0)
            {
                // Receive error
                // perror("RecvFrom data: ");
                // Resend last ACK
                // Don't change other vars or we won't resend same message
                server_state = SERVER_ACK_STATE;
                // Free our malloc buffers
                free(receive_buffer);
                break;
            }

            // Grab header info from receive buffer
            // printf("%s%s%s%s", "Raw char* from client: ", receive_buffer, receive_buffer + sizeof(header), "\n");
            memcpy(&header, receive_buffer, sizeof(header));
            // printf("Extracted header: %c %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);

            // Check its an data message

            if (header.message_type != DATA_MESSAGE_TYPE) 
            {
                if(header.message_type == FIN_MESSAGE_TYPE) 
                {
                    // Go into fin mode!
                    server_state = SERVER_FIN_STATE;
                    continue;
                }
                // Not an DATA or FIN message... 
                // printf("Message received's message type is not DATA when DATA is expected. Disregarding message...\n");
                // printf("Chunk received was %ld\n", current_chunk);
                break;
            }

            // Check the expected sequence number is what was received.
            if(header.sequence_number != expected_seqence_number)
            {
                // Wrong sequence number received
                // printf("Message received's sequence %c did not match expected sequence %c.\n", header.sequence_number, expected_seqence_number);
                // printf("Chunk received was %ld\n", current_chunk);

                // Resend last ACK
                // Don't change other vars or we won't resend same message
                server_state = SERVER_ACK_STATE;
                current_ack_number = previous_ack_number;
                // Free our malloc buffers
                free(receive_buffer);
                break;
            }

            // Record what chunk we are on
            current_chunk = header.chunk;

            // Buffer for data in message
            char* data_buffer = malloc(header.data_length);
            // 0 out buffers for fresness sake
            memset(data_buffer, 0, header.data_length);     
            // copy payload into data_buffer
            memcpy(data_buffer, receive_buffer + sizeof(header), header.data_length);
            // Put payload into file
            size_t write_status = fwrite(data_buffer, 1, header.data_length, fp);
            // Error on read data just break out of loop
            if(write_status != header.data_length)
            {
                // fprintf(stderr, "Fwrite() failed: %zu\n", write_status);
                // printf("%i%s", ferror(fp), "%s");
                // Free our malloc buffers
                free(receive_buffer);
                free(data_buffer);
                break;
            }
            // printf("%s%zu%s", "Wrote ", write_status, " bytes.\n");

            // Successful write
            // Go to ACK STATE now
            server_state = SERVER_ACK_STATE;
            previous_ack_number = current_ack_number;
            current_ack_number = update_ack_number(current_ack_number);
            expected_seqence_number = update_sequence_number(expected_seqence_number);
            // Release the buffer's memory
            free(receive_buffer);
            free(data_buffer);
        }

        if(server_state == SERVER_ACK_STATE) 
        {
            // printf("\nServer in ACK state.\n");
            // Create ACK header
            header = snw_create_header(current_ack_number, ACK_MESSAGE_TYPE, 0, 0);

            // Create empty payload
            char *payload_data = (char *)malloc(payload_size);
            memset(payload_data, 0, payload_size);

            // Create the ACK buffer
            char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
            memset(send_buffer, 0, MAX_MESSAGE_SIZE);

            // Put header and empty payload into ACK buffer
            memcpy(send_buffer, &header, sizeof(header));
            memcpy(send_buffer + sizeof(header), &payload_data, sizeof(payload_data));

            // Send ACK 
            if (sendto(file_sockfd, send_buffer, MAX_MESSAGE_SIZE, 0, (struct sockaddr *) &client_addr, client_addr_len) < 0)
            {
                // Send error
                // ("SendTo ack: ");
                // Resend last ACK
                // Don't change other vars or we won't resend same ACK
                // Free our malloc buffers
                free(payload_data);
                free(send_buffer);
                break;
            }

            // ACK sent successfully
            // Go to receive state
            server_state = SERVER_RECEIVE_STATE;
            // Free our malloc buffers
            free(payload_data);
            free(send_buffer);

            // printf("Exit? chunk == %ld\n", current_chunk);
            if(current_chunk == 1) 
            {
                // We received the last chunk so expect a FIN message next
                // break;
            }
        }
    
        if(server_state == SERVER_FIN_STATE)
        {
            // printf("\nServer in FIN ACK state.\n");
            // Create FIN ACK header
            header = snw_create_header('F', FIN_MESSAGE_TYPE, -1, -1);
            // Create empty payload
            char *payload_data = (char *)malloc(payload_size);
            memset(payload_data, 0, payload_size);
            // Create the FIN ACK buffer
            char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
            memset(send_buffer, 0, MAX_MESSAGE_SIZE);
            // Put header and empty payload into FIN ACK buffer
            memcpy(send_buffer, &header, sizeof(header));
            memcpy(send_buffer + sizeof(header), &payload_data, sizeof(payload_data));

            // Send FIN ACK 
            // printf("Sending FIN ACK.\n");
            if (sendto(file_sockfd, send_buffer, MAX_MESSAGE_SIZE, 0, (struct sockaddr *) &client_addr, client_addr_len) < 0)
            {
                // Send error
                // perror("SendTo ack: ");
                // Resend last ACK
                // Don't change other vars or we won't resend same ACK
                // Free our malloc buffers
                free(payload_data);
                free(send_buffer);
                break;
            }

            break;
        }
    }
}

// Client start point
void stopandwait_client(char* iface, long port, FILE* fp)
{
    // Create socket
    int file_sockfd = -1;
    
    // Get hints
    struct addrinfo hints = snw_setup_hints();

    // Finish hints setup
    hints.ai_socktype = SOCK_DGRAM; // UDP Sock
    hints.ai_protocol = IPPROTO_UDP; // UDP Protocol Only

    // Get port as char*
    char* c_port = snw_long_to_char_array(port);

    // Results of getaddrinfo
    struct addrinfo *result, *rp;
    int getaddrinfo_success;

    // Call getaddrinfo
    getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &result);
    // Failure - log it and leave
    if (getaddrinfo_success != 0)
    {
        // fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
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

        // Socket creation success
        // Attempt connection to server
        if(connect(file_sockfd, rp->ai_addr, rp->ai_addrlen) < 0) 
        {
            // Failed
            // perror("Connect: ");
        }
        //Connection success
        else 
        {
            // Start the file transfer logic
            snw_run_client(file_sockfd, fp);

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

void snw_run_client(int file_sockfd, FILE* fp) 
{
    // Initial set up
    char client_state = CLIENT_SEND_STATE;
    // char previous_sequence_number = SEQUENCE_ZERO;
    char current_seqence_number = SEQUENCE_ZERO;
    char expected_ack_number = ACK_ZERO;
    struct header_t header;
    long payload_size = 0;

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
        long chunks = snw_get_file_chunks(file_size, sizeof(header));

        // Get the remainder of the left over bytes.
        // ie if 27 we want 27 OR 253 we want 2
        long remaining_bytes = snw_get_remaining_bytes(file_size, sizeof(header));

        // While more info to send...
        while(chunks > 0) 
        {
            // If we are in sending state
            if(client_state == CLIENT_SEND_STATE) 
            {
                // printf("\nClient in send state.\n");
                // Grab right payload size based on current chunk
                if(chunks > 1) 
                {
                    payload_size = MAX_MESSAGE_SIZE - sizeof(header);
                }
                else
                {
                    payload_size = remaining_bytes;
                }

                // Create header with helper method
                header = snw_create_header(current_seqence_number, DATA_MESSAGE_TYPE, chunks, payload_size);
                // printf("%s%lu\n", "Size of header: ", sizeof(header));
                // printf("Constructed DATA header: %c %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);

                // Create payload_data
                char *payload_data = (char *)malloc(payload_size * sizeof(char));
                memset(payload_data, 0, (payload_size * sizeof(char)));

                // Put data into payload
                size_t read_status = fread(payload_data, 1, payload_size, fp);
                // printf("FP location: %ld\n", ftell(fp));
                // Error on read data just break out of loop
                if (read_status != payload_size) 
                {
                    // fprintf(stderr, "Fread() failed: %zu\n", read_status);
                    // printf("%i%s\n", ferror(fp), "%s");
                    // Free our malloc buffers
                    free(payload_data);
                    break;
                }
                // printf("%s%ld%s", "Chunk ", chunks, " sent.\n");
                // printf("%s%s%s", "Payload data for server:\n", payload_data, "\n");

                // Create the send packet buffer
                char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
                memset(send_buffer, 0, MAX_MESSAGE_SIZE);

                // Put header and payload data into packet buffer
                memcpy(send_buffer, &header, sizeof(header));
                memcpy(send_buffer + sizeof(header), payload_data, payload_size);
                // This only seems to print the header info, why not the payload data?
                // printf("%s%s%s\n", "Send_buffer contents: ", send_buffer, send_buffer + sizeof(header));

                // Send it out
                if(send(file_sockfd, send_buffer, sizeof(header) + payload_size, 0) < 0)
                {
                    // perror("Send data: ");
                    // Free our malloc buffers
                    free(payload_data);
                    free(send_buffer);
                    break;
                }
                
                // printf("Data payload successfully sent.\n");
                client_state = CLIENT_WAIT_STATE;
                expected_ack_number = update_ack_number(current_seqence_number);
            }

            if(client_state == CLIENT_WAIT_STATE) 
            {
                // printf("\nClient in wait state.\n");

                // Poll setup
                // Accomplishes timeout
                struct pollfd timeout_fd[1];
                memset(&timeout_fd, 0, sizeof(timeout_fd));
                timeout_fd[0].fd = file_sockfd; // Request to poll socket
                timeout_fd[0].events = 0; // Reset all bits - might be redundant due to memset
                timeout_fd[0].events |= POLLIN; // Set POLLIN request bit

                // Poll for timeout
                int poll_status = poll(timeout_fd, 1, TIMEOUT);
                switch (poll_status) {
                    case -1:
                        // Error
                        // perror("Poll timeout: ");
                        break;
                    case 0:
                        // Timeout 
                        // Resend same message
                        // Don't change other vars or we won't resend same message
                        // printf("~~~Time Out Event At Chunk: %lu~~~\n", chunks);
                        client_state = CLIENT_SEND_STATE;
                        // Rewind FilePointer
                        int rewind_success = fseek(fp, -payload_size, SEEK_CUR);
                        if(rewind_success < 0) 
                        {
                            // perror("Fseek: ");
                        }
                        // Get to next loop iteration
                        break;
                    default: ;
                        // Create the receive buffer
                        char *receive_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
                        memset(receive_buffer, 0, MAX_MESSAGE_SIZE);

                        // Receive Acks
                        if (recv(file_sockfd, receive_buffer, sizeof(MAX_MESSAGE_SIZE), 0) < 0)
                        {
                            // Error on recv
                            // perror("Recv ack: ");
                            // Free our malloc buffers
                            free(receive_buffer);
                            break;
                        }

                        // printf("%s%d%s", "Poll status: ", poll_status, ".\n");

                        // Grab header info from receive buffer
                        // printf("%s%s%s%s", "Raw char* from client: ", receive_buffer, receive_buffer + sizeof(header), "\n");
                        memcpy(&header, receive_buffer, sizeof(header));

                        // Check its an ack message
                        if(header.message_type != ACK_MESSAGE_TYPE) 
                        {
                            // Not an ACK message... 
                            // printf("Message received was not of type ACK when ACK was expected. Disregarding message...\n");
                            // Ignore and continue to wait for ack message.
                            break;
                        }

                        // Check the expected ack number is what was received.
                        if(header.sequence_number != expected_ack_number)
                        {
                            // Wrong ack number received
                            // printf("Message received's ACK %c did not match expected ACK %c.\n", header.sequence_number, expected_ack_number);
                            // Resend same message
                            // Don't change other vars or we won't resend same message
                            client_state = CLIENT_SEND_STATE;
                            // Free our malloc buffers
                            free(receive_buffer);
                            // Get to next loop iteration
                            break;
                        }

                        // Everything checks out..
                        // printf("ACK received: %c and matches expected ACK: %c.\n", header.sequence_number, expected_ack_number);
                        // Move into SEND state
                        // Update sequence number    
                        // previous_sequence_number = current_seqence_number;
                        current_seqence_number = update_sequence_number(current_seqence_number);
                        // Update client state
                        client_state = CLIENT_SEND_STATE;
                        // !!!MOST IMPORTANT!!!
                        // Finally update chunks b/c now we can move onto the next chunk
                        chunks--;
                        // Free our malloc buffers
                        free(receive_buffer);

                        // Break for switch statement
                        break;
                }
            }
        }

        // Enter 3way handshake phase
        while (1) 
        {
            int timedout = 0;
            // Send Fin
            // Create header with helper method
            header = snw_create_header('F', FIN_MESSAGE_TYPE, -1, -1);
            // printf("Constructed FIN header: %c %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);
            // Create payload_data
            char *payload_data = (char *)malloc(payload_size * sizeof(char));
            memset(payload_data, 0, (payload_size * sizeof(char)));
            // Create the send packet buffer
            char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
            memset(send_buffer, 0, MAX_MESSAGE_SIZE);
            // Put header and payload data into packet buffer
            memcpy(send_buffer, &header, sizeof(header));
            memcpy(send_buffer + sizeof(header), payload_data, payload_size);
            // Send it out
            if(send(file_sockfd, send_buffer, sizeof(header) + payload_size, 0) < 0)
            {
                // perror("Send FIN: ");
                // Free our malloc buffers
                free(payload_data);
                free(send_buffer);
                timedout = 1; // Restart FIN process loop
                break;
            }

            // Wait for confirmation of FIN ACK
            // Poll setup
            // Accomplishes timeout
            struct pollfd timeout_fd[1];
            memset(&timeout_fd, 0, sizeof(timeout_fd));
            timeout_fd[0].fd = file_sockfd; // Request to poll socket
            timeout_fd[0].events = 0; // Reset all bits - might be redundant due to memset
            timeout_fd[0].events |= POLLIN; // Set POLLIN request bit

            // Poll for timeout
            int poll_status = poll(timeout_fd, 1, TIMEOUT);
            switch (poll_status) {
                case -1:
                    // Error
                    perror("Poll FIN ACK timeout: ");
                    break;
                case 0:
                    // Timeout 
                    // Resend FIN ACK message
                    // printf("~~~Time Out Event At FIN ACK~~~\n");
                    timedout = 1; // restart FIN process loop
                    // Get to next loop iteration
                    break;
                default: ;
                    // Send FIN Ack
                    // Create header with helper method
                    header = snw_create_header('F', FIN_MESSAGE_TYPE, -1, -1);
                    // printf("Constructed FIN ACK header: %c %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);

                    // Create payload_data
                    char *payload_data = (char *)malloc(payload_size * sizeof(char));
                    memset(payload_data, 0, (payload_size * sizeof(char)));
                    // Create the send packet buffer
                    char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
                    memset(send_buffer, 0, MAX_MESSAGE_SIZE);
                    // Put header and payload data into packet buffer
                    memcpy(send_buffer, &header, sizeof(header));
                    memcpy(send_buffer + sizeof(header), payload_data, payload_size);
                    // Send it out
                    if(send(file_sockfd, send_buffer, sizeof(header) + payload_size, 0) < 0)
                    {
                        // perror("Send FIN ACK: ");
                        // Free our malloc buffers
                        free(payload_data);
                        free(send_buffer);
                        break;
                    }
                    // If we timeout here that's okay...
                    // Wait for confirmation of ACK FIN ACK (Not really...)
                    // Poll setup
                    // Accomplishes timeout
                    struct pollfd timeout_fd[1];
                    memset(&timeout_fd, 0, sizeof(timeout_fd));
                    timeout_fd[0].fd = file_sockfd; // Request to poll socket
                    timeout_fd[0].events = 0; // Reset all bits - might be redundant due to memset
                    timeout_fd[0].events |= POLLIN; // Set POLLIN request bit

                    // Poll for timeout
                    int poll_status = poll(timeout_fd, 1, TIMEOUT);
                    switch (poll_status) {
                        case -1:
                            // Error
                            // perror("Poll ACK FIN ACK timeout: ");
                            // printf("Shutting down client host.\n");
                            break;
                        case 0:
                            // Timeout 
                            // printf("~~~Time Out Event At ACK FIN ACK~~~\n");
                            // printf("Shutting down client host.\n");
                            // Get to next loop iteration
                            break;
                        default: ;
                            // Timeout 
                            // printf("Shutting down client host.\n");
                            break;
                    }

            }
            // FIN wasn't received, resend it.
            if (timedout == 1)
            {
                continue;
            }

            // 3 way shake successful enough to dip out
            break;
        }

        // If we got here shut it down...
        break;
    }
}

