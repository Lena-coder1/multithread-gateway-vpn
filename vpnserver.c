// lena.mukhtar n00639928

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "winsock_compat.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

// Server configuration constants
#define SERVER_PORT 8080
#define BUFFER_SIZE 32768

//Initializes the SSL context for the server.
 
SSL_CTX* init_ssl_server() {
    SSL_CTX *ctx;
    
    // Initialize OpenSSL library and load algorithms
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    // Create new SSL context using TLS server method
    ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }
    
    // Load server certificate
    if (SSL_CTX_use_certificate_file(ctx, "certificates/server-cert.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }
    
    // Load private key
    if (SSL_CTX_use_PrivateKey_file(ctx, "certificates/server-key-nopass.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }
    
    // Verify that private key matches certificate
    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "Private key does not match certificate\n");
        return NULL;
    }
    
    return ctx;
}


int connect_to_target(char *host, int port) {
    int sockfd;
    struct sockaddr_in target_addr;
    struct hostent *he;
    
    // Resolve hostname to IP address
    he = gethostbyname(host);
    if (!he) {
        printf("Cannot resolve host: %s\n", host);
        return -1;
    }
    
    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Set up target address structure
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(port);
    memcpy(&target_addr.sin_addr, he->h_addr, he->h_length);
    
    // Connect to target server
    if (connect(sockfd, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0) {
        perror("Connect to target failed");
        close(sockfd);
        return -1;
    }
    
    printf("Connected to target %s:%d\n", host, port);
    return sockfd;
}


void *handle_vpn_client(void *arg) {
    SSL *ssl = (SSL *)arg;
    char buffer[BUFFER_SIZE];
    
    // Read the first message from client to get target host:port
    int n = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        SSL_free(ssl);
        return NULL;
    }
    buffer[n] = '\0';
    
   
    char *colon = strchr(buffer, ':');
    if (!colon) {
        printf("Invalid target format\n");
        SSL_free(ssl);
        return NULL;
    }
    
    *colon = '\0';
    char *host = buffer;
    int port = atoi(colon + 1);
    
    printf("Forwarding to: %s:%d\n", host, port);
    
    // Connect to the target server
    int target_fd = connect_to_target(host, port);
    if (target_fd < 0) {
        SSL_free(ssl);
        return NULL;
    }
    
   
    fd_set fds;
    int ssl_closed = 0, target_closed = 0;
    
    while (!ssl_closed && !target_closed) {
        FD_ZERO(&fds);
        FD_SET(SSL_get_fd(ssl), &fds);
        FD_SET(target_fd, &fds);
        
      
        select(FD_SETSIZE, &fds, NULL, NULL, NULL);
        

        if (FD_ISSET(SSL_get_fd(ssl), &fds)) {
            n = SSL_read(ssl, buffer, sizeof(buffer));
            if (n <= 0) {
                ssl_closed = 1;
            } else {
                if (write(target_fd, buffer, n) <= 0) {
                    target_closed = 1;
                }
            }
        }
        
        // Handle data from target to SSL client
        if (FD_ISSET(target_fd, &fds)) {
            n = read(target_fd, buffer, sizeof(buffer));
            if (n <= 0) {
                target_closed = 1;
            } else {
                if (SSL_write(ssl, buffer, n) <= 0) {
                    ssl_closed = 1;
                }
            }
        }
    }
    
    // Clean up connections
    close(target_fd);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    
    printf("Tunnel closed\n");
    return NULL;
}

// main function to set up the VPN server and accept incoming connections from clients
int main() {
    int listen_fd, client_fd;
    struct sockaddr_in addr;
    SSL_CTX *ssl_ctx;
    
    // Initialize Winsock for Windows compatibility
    if (winsock_init() != 0) {
        printf("Winsock initialization failed\n");
        return 1;
    }
    
    // Initialize SSL context
    ssl_ctx = init_ssl_server();
    if (!ssl_ctx) {
        printf("Failed to initialize SSL server\n");
        winsock_cleanup();
        return 1;
    }
    
    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Socket creation failed");
        winsock_cleanup();
        return 1;
    }
    
    // Allow socket address reuse
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    
    // Set up server address structure
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    // Bind socket to address
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        winsock_cleanup();
        return 1;
    }
    
    // Start listening for connections
    if (listen(listen_fd, 10) < 0) {
        perror("Listen failed");
        winsock_cleanup();
        return 1;
    }
    
    printf("VPN proxy server listening on port %d\n", SERVER_PORT);
    
    // Main accept loop
    while (1) {
        // Accept new client connection
        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        
        printf("New VPN client connected\n");
        
        // Create new SSL structure for this connection
        SSL *ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, client_fd);
        
        // Perform SSL handshake
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(client_fd);
            continue;
        }
        
        printf("SSL handshake successful\n");
        
        // Create detached thread to handle this client
        pthread_t thread;
        pthread_create(&thread, NULL, handle_vpn_client, ssl);
        pthread_detach(thread);
    }
    

    
    return 0;
}