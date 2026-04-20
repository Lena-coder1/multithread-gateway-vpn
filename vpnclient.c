// lena.mukhtar n00639928

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "winsock_compat.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

// Server configuration constants
#define PROXY_PORT 8888
#define VPN_SERVER_PORT 8080
#define BUFFER_SIZE 32768

//Structure to hold proxy configuration
 
typedef struct {
    SSL_CTX *ssl_ctx;
    char vpn_server_ip[16];
    int vpn_server_port;
} proxy_config;

// Structure to pass connection arguments to thread

typedef struct {
    int client_fd;
    proxy_config *config;
} connection_args;


void *handle_local_connection(void *arg);
int connect_to_vpn_server(SSL_CTX *ssl_ctx, char *server_ip, int port, SSL **ssl);

//Initializes the SSL context for the client.

SSL_CTX* init_ssl_client() {
    SSL_CTX *ctx;
    
    // Initialize OpenSSL library and load algorithms
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    // Create new SSL context using TLS client method
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }
    
    

    // Bypass certificate verification for testing
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    return ctx;
}

// Establishes an SSL connection to the VPN server.

int connect_to_vpn_server(SSL_CTX *ssl_ctx, char *server_ip, int port, SSL **ssl) {
    int sockfd;
    struct sockaddr_in server_addr;
    
    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Set up server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    
    // Connect to VPN server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect to VPN server failed");
        close(sockfd);
        return -1;
    }
    
    // Create SSL structure and associate with socket
    *ssl = SSL_new(ssl_ctx);
    SSL_set_fd(*ssl, sockfd);
    
    // Perform SSL handshake
    if (SSL_connect(*ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(*ssl);
        close(sockfd);
        return -1;
    }
    
    printf("Connected to VPN server %s:%d\n", server_ip, port);
    printf("SSL cipher: %s\n", SSL_get_cipher(*ssl));
    
    return sockfd;
}

//Parses an HTTP CONNECT request to extract the target host and port.

int parse_connect_request(char *buffer, int len, char *host, int *port) {
    char *method, *url, *colon;
    
   
    method = strtok(buffer, " ");
    if (!method || strcmp(method, "CONNECT") != 0) {
        return -1;
    }
    
   
    url = strtok(NULL, " ");
    if (!url) return -1;
    

    colon = strchr(url, ':');
    if (colon) {
        *colon = '\0';
        strcpy(host, url);
        *port = atoi(colon + 1);
    } else {
        strcpy(host, url);
        *port = 80;  
    }
    
    return 0;
}

//Thread function to handle each local client connection.

void *handle_local_connection(void *arg) {
    connection_args *conn_args = (connection_args *)arg;
    int client_fd = conn_args->client_fd;
    SSL_CTX *ssl_ctx = conn_args->config->ssl_ctx;
    free(conn_args);
    
    char buffer[BUFFER_SIZE];
    char target_host[256];
    int target_port;
    
    // Read HTTP request from client
    int n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[n] = '\0';
    printf("Received request:\n%s\n", buffer);
    
    // Parse CONNECT request
    if (parse_connect_request(buffer, n, target_host, &target_port) != 0) {
        printf("Not a CONNECT request, closing\n");
        close(client_fd);
        return NULL;
    }
    
    printf("Proxying connection to %s:%d\n", target_host, target_port);
    
    // Send HTTP 200 response to establish tunnel
    const char *response = "HTTP/1.1 200 Connection Established\r\n\r\n";
    write(client_fd, response, strlen(response));
    
    // Connect to VPN server via SSL
    SSL *ssl;
    int vpn_fd = connect_to_vpn_server(ssl_ctx, "127.0.0.1", 8080, &ssl);
    if (vpn_fd < 0) {
        close(client_fd);
        return NULL;
    }
    
    // Send target host:port to VPN server
    char target_info[512];
    snprintf(target_info, sizeof(target_info), "%s:%d\n", target_host, target_port);
    SSL_write(ssl, target_info, strlen(target_info));
    
    // Bidirectional data forwarding loop
    fd_set fds;
    int client_closed = 0, tunnel_closed = 0;
    
    while (!client_closed && !tunnel_closed) {
        FD_ZERO(&fds);
        FD_SET(client_fd, &fds);
        FD_SET(vpn_fd, &fds);
        
        // Wait for data on either connection
        select(FD_SETSIZE, &fds, NULL, NULL, NULL);
        
        // Handle data from client to VPN tunnel
        if (FD_ISSET(client_fd, &fds)) {
            n = read(client_fd, buffer, sizeof(buffer));
            if (n <= 0) {
                client_closed = 1;
            } else {
                if (SSL_write(ssl, buffer, n) <= 0) {
                    tunnel_closed = 1;
                }
            }
        }
        
        // Handle data from VPN tunnel to client
        if (FD_ISSET(vpn_fd, &fds)) {
            n = SSL_read(ssl, buffer, sizeof(buffer));
            if (n <= 0) {
                tunnel_closed = 1;
            } else {
                if (write(client_fd, buffer, n) <= 0) {
                    client_closed = 1;
                }
            }
        }
    }
    
    // Clean up connections
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(vpn_fd);
    close(client_fd);
    
    printf("Connection closed\n");
    return NULL;
}

//Main function - Entry point of the VPN client proxy.

int main(int argc, char *argv[]) {
    int listen_fd;
    struct sockaddr_in addr;
    SSL_CTX *ssl_ctx;
    proxy_config config;
    
    // Initialize Winsock for Windows compatibility
    if (winsock_init() != 0) {
        printf("Winsock initialization failed\n");
        return 1;
    }
    
    // Check command line arguments
    if (argc < 2) {
        printf("Usage: %s <vpn_server_ip>\n", argv[0]);
        winsock_cleanup();
        return 1;
    }
    
    // Initialize SSL context
    ssl_ctx = init_ssl_client();
    if (!ssl_ctx) {
        printf("SSL initialization failed\n");
        winsock_cleanup();
        return 1;
    }
    
    // Set up proxy configuration
    config.ssl_ctx = ssl_ctx;
    strcpy(config.vpn_server_ip, argv[1]);
    config.vpn_server_port = 8080;
    
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
    addr.sin_port = htons(PROXY_PORT);
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
    
    printf("Application proxy listening on 127.0.0.1:%d\n", PROXY_PORT);
    printf("Configure your browser to use this proxy (HTTP proxy)\n");
    
    // Main accept loop
    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        
        // Accept new client connection
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        
        printf("New connection from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // Create detached thread to handle this client
        pthread_t thread;
        connection_args *conn_args = malloc(sizeof(connection_args));
        conn_args->client_fd = client_fd;
        conn_args->config = &config;
        
        pthread_create(&thread, NULL, handle_local_connection, conn_args);
        pthread_detach(thread);
    }
    

    
    return 0;
}