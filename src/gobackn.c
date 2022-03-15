/*******************************************
 * Ethan Behar ebehar
 * Created: 10/23/21
 * 
 * Gobackn.c is a program that implements RUDP.
 * The program's main logic is file transfer and RUDP using the go back n approach.
 * Go back n means send up to a window size of packets. If an ACK for a previous sent packet is recently, go back to that packet and retransmitt all packets since that one.
 
 * ./netster -p 8443 -r 2 -f output
 * ./netster 127.0.0.1 -p 8443 -r 2 -f shorttest
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

// Constants
#define MAX_MESSAGE_SIZE 256
// For test-netster testing as per Julia G (thank you.)
#define TIMEOUT 60 // 7000=7s 5000=5s 1000=1s 500=.5s
// For local testing
// #define TIMEOUT 1000 // 7000=7s 5000=5s 1000=1s 500=.5s

// GoBackN Constants
#define SERVER_DATA_RECEIVE_STATE 0
#define SERVER_FIN_STATE 1
#define MAX_WINDOW_SIZE 30
#define ACK_MESSAGE_TYPE 'A'
#define DATA_MESSAGE_TYPE 'D'
#define FIN_MESSAGE_TYPE 'F'// server/client shutting down

// Declare header structure
typedef struct header_t {
    long sequence_number;
    char message_type;
    long chunk;
    long data_length;
} header_t;

typedef struct packet_t {
    struct header_t header;
    char* payload;
} packet_t;

// General helper functions
struct addrinfo gbn_setup_hints();
char* gbn_long_to_char_array(long number);
struct header_t gbn_create_header(long sequence_number, char message_type, long chunk, long data_length);

// Runs the server/client main loops
void gbn_run_server(int file_sockfd, FILE* fp);
void gbn_run_client(int file_sockfd, FILE* fp);

/*
 * General Helper Method Implementations
 */

struct addrinfo gbn_setup_hints() 
{
    // Create hints
    struct addrinfo hints;

    // Clear its memory
    memset(&hints, 0, sizeof(hints));

    // Set up non-protocol specific fields
    hints.ai_family = AF_UNSPEC; // v4 or v6
    hints.ai_flags = AI_PASSIVE;

    return hints;
}

char* gbn_long_to_char_array(long number)
{
    // Create char* big enough
    char* c_long = malloc(sizeof(number));

    // Conversion
    snprintf(c_long, sizeof(number), "%ld", number);

    return c_long;
}

struct header_t gbn_create_header(long sequence_number, char message_type, long chunk, long data_length) 
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

// Server Setup
void gbn_server(char* iface, long port, FILE* fp)
{    
    // Var for socket
    int file_sockfd = -1;

    // Create hints
    struct addrinfo hints = gbn_setup_hints();

    // Finish hints setup
    hints.ai_socktype = SOCK_DGRAM; // UDP Socket
    hints.ai_protocol = IPPROTO_UDP;  // UDP Protocol Only

    // Get port as char*
    char* c_port = gbn_long_to_char_array(port);

    // Results of getaddrinfo
    struct addrinfo *results, *result_pointer;
    int getaddrinfo_success;

    getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &results);
    if (getaddrinfo_success < 0)
    {
        // fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
        return;
    }

    // Try all address structures until one works... sounds crazy
    for(result_pointer = results; result_pointer != NULL; result_pointer = result_pointer->ai_next)
    {
        // Try to create socket
        file_sockfd = socket(result_pointer->ai_family, result_pointer->ai_socktype, result_pointer->ai_protocol);
        // Creation failed
        if(file_sockfd < 0)
        {
            if(result_pointer->ai_next == NULL)
            {
                // perror("Socket(Last structure!");
            }
            else
            {
                // perror("Socket(trying next structure): ");
            }
        }

        // Try to bind
        int bind_success = bind(file_sockfd, result_pointer->ai_addr, result_pointer->ai_addrlen);
        // Bind failed
        if(bind_success < 0)
        {
            // perror("Bind(trying next structure): ");
            // Release socket, and try next structure
            close(file_sockfd);
            continue;
        }
        // Bind succeed
        else
        {
            // Start main server loop
            gbn_run_server(file_sockfd, fp);

            // If we here we are shutting down
            // Release socket and break out of loop
            close(file_sockfd);
            break;
        }
    }

    // Free stuff and return
    freeaddrinfo(results);
    free(c_port);
}

// Main Server Loop
void gbn_run_server(int file_sockfd, FILE* fp)
{
    // Client info vars
    struct sockaddr client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Server state vars
    struct header_t header;
    long header_size = sizeof(header);
    long next_seq_num = 0;
    long payload_size = 0;
    int send_next_ACK = 0;
    long last_successful_write = 0;
    
    int server_state = SERVER_DATA_RECEIVE_STATE;
    
    while(1) 
    {
        if(server_state == SERVER_DATA_RECEIVE_STATE)
        {
            // printf("\nServer in DATA RECEIVE state.\n");

            // Create the receive buffer
            char *receive_buffer = (void *)malloc(MAX_MESSAGE_SIZE);
            memset(receive_buffer, 0, MAX_MESSAGE_SIZE);

            // Receiving incoming payload
            int receive_status = recvfrom(file_sockfd, receive_buffer, MAX_MESSAGE_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);
            // Receive failure
            if (receive_status < 0)
            {
                // Let client timeout - best approach?
                // perror("Recvfrom data: ");
                // Free our malloc buffers
                free(receive_buffer);
                continue;
            }

            // Grab header info from receive buffer
            memcpy(&header, receive_buffer, header_size);
            // printf("Extracted DATA header: %ld %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);
            
            // Check its a proper message for the server
            if(header.message_type != DATA_MESSAGE_TYPE) 
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
                free(receive_buffer);
                break;
            }

            // Check the sequence number
            if(header.sequence_number != next_seq_num) 
            {
                // Wrong sequence number recevied
                // printf("Message received's sequence %ld did not match expected sequence %ld.\n", header.sequence_number, next_seq_num);
                send_next_ACK = 0;
            }
            else
            {
                // Correct sequence number received
                
                // Buffer for data in packet
                char* data_buffer = malloc(header.data_length);
                // 0 it out
                memset(data_buffer, 0, header.data_length);
                // Copy payload data into buffer
                memcpy(data_buffer, receive_buffer + header_size, header.data_length);

                // Write payload to file
                size_t write_status = fwrite(data_buffer, 1, header.data_length, fp);
                // Write failure
                if(write_status != header.data_length) 
                {
                    // fprintf(stderr, "Fwrite() failed: %zu\n", write_status);
                    // printf("%i%s", ferror(fp), "%s");
                    // Free our malloc buffers
                    free(receive_buffer);
                    free(data_buffer);
                    send_next_ACK = 0;
                    break;
                }
                last_successful_write++;
                // printf("Server received and wrote packet %ld successfully. Successfully wrote %ld packets so far.\n", header.sequence_number, last_successful_write);
                // Send regular ACK
                send_next_ACK = 1;
                // Free mallocs
                free(data_buffer);
            }

            // Now send an ACK
            long ack_sequence_number = -1;
            if(send_next_ACK == 0)
            {
                ack_sequence_number = last_successful_write - 1;
                // if(ack_sequence_number < 0) 
                // {
                //     printf("Forcing ack_seq_num to 0.\n");
                //     ack_sequence_number = 0;
                // }
                // printf("Ack seq num(0): %ld\n", ack_sequence_number);
            }
            else 
            {
                ack_sequence_number = next_seq_num;
                // printf("Ack seq num(1): %ld\n", ack_sequence_number);

            }

            // Create ACK header
            header = gbn_create_header(ack_sequence_number, ACK_MESSAGE_TYPE, 0, 0);
            // printf("Constructed ACK header: %ld %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);

            // Create empty payload
            char *payload_data = (char *)malloc(payload_size);
            memset(payload_data, 0, payload_size);

            // Create the ACK buffer
            char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
            memset(send_buffer, 0, MAX_MESSAGE_SIZE);

            // Put header and empty payload into ACK buffer
            memcpy(send_buffer, &header, header_size);
            memcpy(send_buffer + header_size, &payload_data, sizeof(payload_data));
            
            // Send ACK
            int send_success = sendto(file_sockfd, send_buffer, MAX_MESSAGE_SIZE, 0, (struct sockaddr *) &client_addr, client_addr_len);
            // Send failure
            if (send_success < 0)
            {                        
                // Let client timeout - best approach?
                // perror("SendTo ack: ");
                // Free our malloc buffers
                free(receive_buffer);
                free(payload_data);
                free(send_buffer);
                break;
            }    

            // printf("Server sent ACK for packet %ld successfully.\n", ack_sequence_number);


            if(send_next_ACK == 1) 
            {
                next_seq_num++;
                // printf("Next seq num: %ld\n", next_seq_num);
            }

            // Free our malloc buffers
            free(receive_buffer);
            free(payload_data);
            free(send_buffer);
        }

        if(server_state == SERVER_FIN_STATE)
        {
            // printf("\nServer in FIN ACK state.\n");
            // Create FIN ACK header
            header = gbn_create_header('F', FIN_MESSAGE_TYPE, -1, -1);
            // Create empty payload
            char *payload_data = (char *)malloc(payload_size);
            memset(payload_data, 0, payload_size);
            // Create the FIN ACK buffer
            char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
            memset(send_buffer, 0, MAX_MESSAGE_SIZE);
            // Put header and empty payload into FIN ACK buffer
            memcpy(send_buffer, &header, header_size);
            memcpy(send_buffer + header_size, &payload_data, sizeof(payload_data));

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

// Client Setup and Connection
void gbn_client(char* iface, long port, FILE* fp)
{
    // Create socket
    int file_sockfd = -1;

    // Get hints
    struct addrinfo hints = gbn_setup_hints();

    // Finish hints setup
    hints.ai_socktype = SOCK_DGRAM; // UDP Socket
    hints.ai_protocol = IPPROTO_UDP; // UDP Protocol Only

    // Get port as char*
    char* c_port = gbn_long_to_char_array(port);

    // Results of getaddrinfo vars
    struct addrinfo *results, *result_pointer;
    int getaddrinfo_success;

    // Call getaddrinfo
    getaddrinfo_success = getaddrinfo(iface, c_port, &hints, &results);
    // Failure - log it and leave
    if(getaddrinfo_success < 0) 
    {
        // fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_success));
        return;
    }

    // Try all the address structures until one works
    for(result_pointer = results; result_pointer != NULL; result_pointer = result_pointer->ai_next)
    {
        // Try to create socket
        file_sockfd = socket(result_pointer->ai_family, result_pointer->ai_socktype, result_pointer->ai_protocol);
        if(file_sockfd < 0)
        {
            if(result_pointer->ai_next == NULL)
            {
                // perror("Socket(Last structure!");
            }
            else
            {
                // perror("Socket(trying next structure): ");
            }
            continue;
        }

        int connect_success = connect(file_sockfd, result_pointer->ai_addr, result_pointer->ai_addrlen);
        // Connection failure
        if(connect_success < 0)
        {
            if(result_pointer->ai_next == NULL)
            {
                // perror("Connect(Last structure!");
                close(file_sockfd);
                continue;
            }
            else
            {
                // perror("Connect(trying next structure): ");
                close(file_sockfd);
                continue;
            }      
        }
        // Connection success
        else
        {
            // Start the client main loop
            gbn_run_client(file_sockfd, fp);

            // If we are here, we are shutting down
            // Close the socket
            close(file_sockfd);
            break;
        }
    }

    // Shutting down - release stuff
    freeaddrinfo(results);
    free(c_port);
}

// Main Client Loop
void gbn_run_client(int file_sockfd, FILE* fp)
{
    // Client vars
    long base = 0;
    long next_seq_num = 0;
    long base_fp = 0;
    long current_fp = 0;
    struct header_t header;
    long payload_size = 0;
    long current_window_size = 1;

    // Grab initial file info
    // Go to end of file
    fseek(fp, 0L, SEEK_END);
    
    // Get file size in bytes
    long file_size = ftell(fp);
    
    // Set indicator back to beginning of file.
    rewind(fp);
    
    long header_size = sizeof(header);
    // Size of payload we can send
    long chunk_size = MAX_MESSAGE_SIZE - header_size; 
    // Chop file size into chunks of 256 bytes
    long chunks = (long) (ceil ((double)file_size / (double)chunk_size)); // AKA # OF PACKETS TO SEND
    // Get the remainder of the left over bytes.
    // ie if 27 we want 27 OR 253 we want 2
    long remaining_bytes = file_size % chunk_size;
    // Edge case of clean division means remaining_bytes = chunk_size
    if(remaining_bytes == 0)
    {
        // printf("%s%ld\n", "Remaining bytes was 0. Forced remaining bytes to ", chunk_size);
        remaining_bytes = chunk_size;
    }
    // printf("~~~FILE INFO~~~\n");
    // printf("%s%ld%s", "Header size: ", header_size, "\n");
    // printf("%s%ld%s", "File size in bytes: ", file_size, "\n");
    // printf("%s%ld%s", "Chunk size in bytes: ", chunk_size, "\n");
    // printf("%s%f%s", "Raw chunks: ", ((double)file_size / (double)chunk_size), "\n");
    // printf("%s%ld%s", "Number of chunks: ", chunks, "\n");
    // printf("%s%ld\n", "Remaining bytes: ", remaining_bytes);

    // Main client loop
    while(1) 
    {
        // Data needs to be sent
        if (base < chunks) 
        {
            // printf("\nClient in DATA SEND state\n");

            /**************************
            Step 1*********************
            **************************/
            if(next_seq_num < base + current_window_size && !(next_seq_num >= chunks))
            {
                // send next_seq_num packet
                // Grab right payload size based on current chunk
                // printf("Base: %ld\n", base);
                // printf("NSN: %ld\n", next_seq_num);
                // printf("Chunks: %ld\n", chunks);
                // printf("Current Window Size: %ld\n", current_window_size);

                // for(long resend_seq_num = base; resend_seq_num < next_seq_num; resend_seq_num++)
                // {

                while(next_seq_num < base + current_window_size && !(next_seq_num >= chunks))
                {
                    if(next_seq_num < chunks - 1) 
                    {
                        payload_size = chunk_size;
                    }
                    else
                    {
                        payload_size = remaining_bytes;
                    }
                    
                    // Create header with helper method
                    header = gbn_create_header(next_seq_num, DATA_MESSAGE_TYPE, chunks, payload_size);
                    // printf("Constructed DATA header: %ld %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);

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

                    // Create the send packet buffer
                    char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
                    memset(send_buffer, 0, MAX_MESSAGE_SIZE);

                    // Put header and payload data into packet buffer
                    memcpy(send_buffer, &header, header_size);
                    memcpy(send_buffer + header_size, payload_data, payload_size);

                    int send_status = send(file_sockfd, send_buffer, header_size + payload_size, 0);
                                        // Send it out
                    if(send_status < 0)
                    {
                        // perror("Send data: ");
                        // Free our malloc buffers
                        free(payload_data);
                        free(send_buffer);
                        break;
                    }
                    // printf("Sent packet %ld\n", next_seq_num);
                    // "if next_seq_num == base"
                        // start timer

                    current_fp += payload_size;
                    next_seq_num++;
                }
            }

            /**************************
            Step 2*********************
            **************************/
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
                    /**************************
                    Step 3*********************
                    **************************/
                    // Timeout
                    // printf("~~~Time Out Event At Base Chunk: %ld~~~\n", base);

                    // Reduce window size - congestion control
                    current_window_size = current_window_size / 2;
                    // Just double check it doesn't become zero
                    if(current_window_size <= 1)
                    {
                        current_window_size = 1;
                    }
                    // printf("New CWS is %ld.\n", current_window_size);

                    // Rewind fp to packet location that timed out.
                    // printf("Rewind from %ld to %ld.\n", current_fp, base_fp);
                    long rewind_step = current_fp - base_fp;
                    // printf("Rewind difference = %ld.\n", rewind_step);
                    // Rewind FilePointer to base
                    int rewind_success = fseek(fp, -rewind_step, SEEK_CUR);
                    if(rewind_success < 0) 
                    {
                        // perror("Fseek rewind: ");
                    }
                    current_fp = base_fp;
                    // printf("After rewind current_fp: %ld and base_fp:%ld.\n", current_fp, base_fp);


                    // Now that file is pointing to where the packet timed out
                    for(long resend_seq_num = base; resend_seq_num < next_seq_num; resend_seq_num++)
                    {

                        // Re-send window size
                        // Grab right payload size based on current chunk
                        if(resend_seq_num < chunks - 1) 
                        {
                            payload_size = chunk_size;
                        }
                        else
                        {
                            payload_size = remaining_bytes;
                        }
                        
                        // Create header with helper method
                        header = gbn_create_header(resend_seq_num, DATA_MESSAGE_TYPE, chunks, payload_size);
                        // printf("Constructed Resend-DATA header: %ld %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);

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

                        // Create the send packet buffer
                        char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
                        memset(send_buffer, 0, MAX_MESSAGE_SIZE);

                        // Put header and payload data into packet buffer
                        memcpy(send_buffer, &header, header_size);
                        memcpy(send_buffer + header_size, payload_data, payload_size);

                        int send_status = send(file_sockfd, send_buffer, header_size + payload_size, 0);
                                            // Send it out
                        if(send_status < 0)
                        {
                            // perror("Send data: ");
                            // Free our malloc buffers
                            free(payload_data);
                            free(send_buffer);
                            break;
                        }
                        // printf("Resent packet %ld\n", resend_seq_num);
                        current_fp += payload_size;
                    }
                    break;
                default: ;
                    // Create the receive buffer
                    char *receive_buffer = (void *)malloc(MAX_MESSAGE_SIZE);
                    memset(receive_buffer, 0, MAX_MESSAGE_SIZE);
                    
                    // Attempt to receive...
                    int receive_status = recv(file_sockfd, receive_buffer, MAX_MESSAGE_SIZE, 0);
                    // Receive failed or timed out
                    if(receive_status < 0)
                    {
                        // Error on recv
                        // perror("Recv ack: ");
                        // Free our malloc buffers
                        free(receive_buffer);
                        break;
                    }

                    // Grab header info from receive buffer
                    memcpy(&header, receive_buffer, header_size);
                    // printf("Extracted ACK header: %ld %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);

                    // Check its an ack message
                    if(header.message_type != ACK_MESSAGE_TYPE) 
                    {
                        // Not an ACK message... 
                        // printf("Message received was not of type ACK when ACK was expected. Disregarding message...\n");
                        // Ignore and continue to wait for ack message.
                        break;
                    }

                    // !!!MOST IMPORTANT!!!
                    // Finally update base b/c we know the server received it
                    // If we get a -1 ack then first packet was never received so don't do any of this stuff.
                    if(header.sequence_number != -1)
                    {
                        base = header.sequence_number + 1;
                        // printf("Base was %ld. Base is %ld.\n", base - 1, base);
                        // Make base fp relative to base number
                        long old_base_fp = base_fp;
                        if(base == chunks) 
                        {
                            base_fp = file_size;
                        }
                        else
                        {
                            base_fp = (base * (MAX_MESSAGE_SIZE - header_size));
                        }
                        // printf("Base_fp was %ld. Base_fp is %ld.\n", old_base_fp, base_fp);
                        // Successful ACK so increase window size
                        current_window_size += current_window_size;
                        if(current_window_size > MAX_WINDOW_SIZE) 
                        {
                            current_window_size = MAX_WINDOW_SIZE;
                        }
                        // printf("CWS was %ld. CWS is %ld", current_window_size - current_window_size, current_window_size);

                    }
                    // Free our malloc buffers
                    free(receive_buffer);

                    // Break for switch statement
                    break;
            }
        }
        // Now go into FIN mode
        else
        {
            // printf("\nClient in FIN state\n");


            // Enter 3way handshake phase
            while (1) 
            {
                int timedout = 0;
                // Send Fin
                // Create header with helper method
                header = gbn_create_header('F', FIN_MESSAGE_TYPE, -1, -1);
                // printf("Constructed FIN header: %ld %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);
                // Create payload_data
                char *payload_data = (char *)malloc(payload_size * sizeof(char));
                memset(payload_data, 0, (payload_size * sizeof(char)));
                // Create the send packet buffer
                char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
                memset(send_buffer, 0, MAX_MESSAGE_SIZE);
                // Put header and payload data into packet buffer
                memcpy(send_buffer, &header, header_size);
                memcpy(send_buffer + header_size, payload_data, payload_size);
                // Send it out
                if(send(file_sockfd, send_buffer, header_size + payload_size, 0) < 0)
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
                        // perror("Poll FIN ACK timeout: ");
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
                        header = gbn_create_header('F', FIN_MESSAGE_TYPE, -1, -1);
                        // printf("Constructed FIN ACK header: %ld %c %ld %ld\n", header.sequence_number, header.message_type, header.chunk, header.data_length);

                        // Create payload_data
                        char *payload_data = (char *)malloc(payload_size * sizeof(char));
                        memset(payload_data, 0, (payload_size * sizeof(char)));
                        // Create the send packet buffer
                        char *send_buffer = (char *)malloc(MAX_MESSAGE_SIZE);
                        memset(send_buffer, 0, MAX_MESSAGE_SIZE);
                        // Put header and payload data into packet buffer
                        memcpy(send_buffer, &header, header_size);
                        memcpy(send_buffer + header_size, payload_data, payload_size);
                        // Send it out
                        if(send(file_sockfd, send_buffer, header_size + payload_size, 0) < 0)
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

            // break from main server loop since 3way finshake is over
            break; 
        }
    }
}
