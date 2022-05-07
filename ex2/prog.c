/**
 * @file prog.c
 * @author Dan Gutstadt 209321470 - Omri Tcherner 318946886
 * @brief 
 * @version 0.1
 * @date 2022-05-07
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <netdb.h>

#define PORT 8887
#define D_PORT 80
#define BUFFER_SIZE 4096
#define FD_COUNT 2 
#define LEAVE_COMMAND -1
#define INVALID_ARGUMENT -2

typedef struct sockaddr saddr;
typedef struct sockaddr_in saddr_in;
typedef struct in_addr in_addr;
typedef struct pollfd pollfd;

typedef struct _upload_obj {
    int file_fd;
    int socket_fd;
} upload_obj;

typedef struct _download_obj {
    int id;
    char* url;
    int size_downloaded;
    int socket_fd;
    int fd;
} download_obj;


int main(int argc, char *argv[]) 
{
    struct pollfd fds[FD_COUNT];
    char buffer[BUFFER_SIZE];
    struct sockaddr_in address;
    socklen_t addrlen;
    int byte_count = 0;
    int ret = 0;
    int uploads_count = 0;
    int downloads_count = 0;
    int recv_fd = 0;
    download_obj* downloads = malloc(0);
    upload_obj* uploads = malloc(0);
    pollfd *upload_fds = malloc(0);
    pollfd *downloads_fds = malloc(0);

    static const int NO_TIMEOUT = -1;
    static const int SERVE_COUNT = 5;

    printf("---- Welcome to our server ----\n");
    printf("[*] Initializing server...\n");

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = socket(AF_INET, SOCK_STREAM, 0);
    fds[1].events = POLLIN;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if(bind(fds[1].fd, (saddr*) &address, sizeof(address)) < 0)
    {
        printf("[ERROR] Couldn't bind to socket\n");
        goto cleanup;
    }

    if(listen(fds[1].fd, SERVE_COUNT) == -1) 
    {
        printf("[ERROR] Couldn't listen to socket\n");
        goto cleanup;
    }

    while(1)
    {
        poll(fds, FD_COUNT, 200);

        // Client
        if (fds[0].revents & POLLIN)
        {
            byte_count = read(STDIN_FILENO, buffer, BUFFER_SIZE - 1);
            buffer[byte_count] = '\0';

            char *token;

            if(!strcmp(buffer, "leave\n"))
            {
                printf("[*] Leaving!\n");
                goto cleanup;
                break;
            }
            else if (!strcmp(buffer, "show\n"))
            {
        
                if (downloads_count == 0) {
                    printf("There are not any files !\n");
                } else {
                    for (int index = 0; index < downloads_count; index++)
                    {
                        printf("%d %s %d\n", downloads[index].id, downloads[index].url, downloads[index].size_downloaded);
                    }
                }
            }
            else
            {
                token = strtok(buffer, " ");
                if (!strcmp(token, "start"))
                {
                    char* url = strtok(NULL, " ");

                    printf("[*] Starting to download from URL: %s\n", url);
                    
                    // Adding another struct of download to the downloads list
                    downloads = realloc(downloads, (sizeof(download_obj) * (downloads_count+1)));
                    downloads_fds = realloc(downloads_fds, sizeof(struct pollfd) * (downloads_count+1));

                    char * filename = strtok(NULL, " ");
                    filename[strlen(filename)-1] = '\0';  
                    
                    // Population the download structure
                    downloads[downloads_count].id = downloads_count+1;
                    
                    downloads[downloads_count].url = malloc(strlen(url) + 1);
                    strcpy(downloads[downloads_count].url, url);

                    downloads[downloads_count].socket_fd = socket(AF_INET, SOCK_STREAM, 0);
                    downloads[downloads_count].fd = open(filename, O_WRONLY | O_CREAT, 0644);
                    downloads[downloads_count].size_downloaded = 0;

                    downloads_fds[downloads_count].fd = downloads[downloads_count].socket_fd;
                    downloads_fds[downloads_count].events = POLLIN;

                    url = malloc(strlen(downloads[downloads_count].url) + 1);
                    strcpy(url, downloads[downloads_count].url);

                    strtok(url, "/");
                    char * hostname = strtok(NULL, "/");
                    
                    // Getting the port out of the url (if there is one)
                    char* ch_port = (char *)strchr(hostname, ':');
                    int port;
                    if (ch_port) {
                        *ch_port = '\0';
                        ch_port++;
                        port = atoi(ch_port);
                    } else {
                        port = D_PORT;
                    }

                    // Creating the socket connection to server via http 1.0 
                    saddr_in connect_addr;
                    connect_addr.sin_family = AF_INET;
                    connect_addr.sin_port = htons(port);
                    connect_addr.sin_addr = *(((struct in_addr **)gethostbyname(hostname)->h_addr_list)[0]);
                    connect(downloads[downloads_count].socket_fd, (struct sockaddr*)&connect_addr, sizeof(connect_addr));
                    char request[BUFFER_SIZE];
                    sprintf(request, "GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n\r\n", filename, hostname);
                    send(downloads[downloads_count].socket_fd, request, strlen(request), 0);
                    
                    // Creating final url string
                    char* final_url = malloc(strlen(filename) + strlen(downloads[downloads_count].url));
                    sprintf(final_url, "%s%s", url, filename);
                    free(downloads[downloads_count].url);
                    downloads[downloads_count].url = final_url;

                    downloads_count++;
                }
                else if (!strcmp(token, "stop"))
                {
                    char* id = strtok(NULL, " ");
                    printf("[*] Stopping download for ID: %s\n", id);
                    int index = atoi(id);
                    close(downloads[index-1].fd);
                    close(downloads[index-1].socket_fd);
                } else {
                    printf("[ERROR] Invalid argument received\n");
                }   
            }
        }
       
        // Server
        else if (fds[1].revents & POLLIN)
        {
            // Adding new upload
            uploads = realloc(uploads, sizeof(upload_obj) * (uploads_count+1));
            upload_fds = realloc(upload_fds, sizeof(struct pollfd) * (uploads_count+1));

            recv_fd = accept(fds[1].fd, (saddr*) &address, (socklen_t*) &addrlen);
            read(recv_fd, buffer, BUFFER_SIZE);
    

            uploads[uploads_count].socket_fd = recv_fd;
            upload_fds[uploads_count].fd = recv_fd;
            upload_fds[uploads_count].events = POLLOUT;

            char *t =strtok(buffer, " ");
            if (strcmp(t, "GET") != 0)
                continue;
            char *token = strtok(NULL, " ");
            uploads[uploads_count].file_fd = open(token, O_RDONLY);

            uploads_count+=1;
        }

        // Handles the uploads (Server)
        poll(upload_fds, uploads_count, 200);
        for (int index = 0; index < uploads_count; index++)
        {
            int terminated = 0;
            memset(buffer, 0, BUFFER_SIZE);

            // Checks for connection termination (stop)
            if(upload_fds[index].revents & POLLHUP) {
                terminated = 1;
            }

            if(terminated == 0 && upload_fds[index].revents & POLLOUT) {
                
                // Sends data
                int bytes_read = read(uploads[index].file_fd, &buffer, BUFFER_SIZE);        
                if (bytes_read) {
                    send(uploads[index].socket_fd, &buffer, bytes_read, 0);
                } else {
                    terminated = 1;
                }
            }
            
            if (terminated) {
                close(uploads[index].file_fd);
                close(uploads[index].socket_fd);
                 
                // Removes terminated record from uploads
                for(int jndex = index; jndex < uploads_count; jndex++) {
                    uploads[jndex] = uploads[jndex+1];
                    upload_fds[jndex] = upload_fds[jndex+1];
                }
                                
                uploads = realloc(uploads, sizeof(upload_obj) * uploads_count);
                upload_fds = realloc(upload_fds, sizeof(struct pollfd) * uploads_count);
                index--;
            }
        }
        
        // Handles the downloads for (Client)
        poll(downloads_fds, downloads_count, 200);
        for (int index = 0; index < downloads_count; index++) {

            int terminated = 0;
            memset(buffer, 0, BUFFER_SIZE);
            
            // In case the download is compleate
            if(fcntl(downloads_fds[index].fd, F_GETFD) != 0) {
                terminated = 1;
            }

    
            if (terminated == 0 && downloads_fds[index].revents & POLLIN) {

                // Getting data from server
                int bytes_read = recv(downloads[index].socket_fd, &buffer, BUFFER_SIZE, 0);
                if (bytes_read == 0) {
                    terminated = 1;
                }  else {
                    write(downloads[index].fd, &buffer, bytes_read);
                    downloads[index].size_downloaded += bytes_read;
                }
            }

            if (terminated) {
                printf("[*] Finished download %s\n", downloads[index].url);
                close(downloads[index].fd);
                close(downloads[index].socket_fd);
                free(downloads[index].url);

                // Removing relevant record
                for(int jndex = index; jndex < downloads_count; jndex++) {
                    downloads[jndex] = downloads[jndex+1];
                    
                    // Importent - we must do that to sincronize the id with the place of the records in all the lists
                    downloads[jndex].id -=1;
                    downloads_fds[jndex] = downloads_fds[jndex+1];
                }
                downloads = realloc(downloads, sizeof(download_obj) * --downloads_count);
                downloads_fds = realloc(downloads_fds, sizeof(pollfd) * downloads_count);
                index--;
            }
        }
        
        // Makes sure the buffer is 
        memset(buffer, 0, BUFFER_SIZE);
    }


cleanup:
    printf("[*] Starting cleanup...\n");

    free(uploads);
    free(upload_fds);
    free(downloads);
    free(downloads_fds); 

    printf("---- Server exiting... ----\n");
    return 0;
}