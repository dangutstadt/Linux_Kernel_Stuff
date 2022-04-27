#include <stdio.h>
#include <poll.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

#define PORT 8888
#define BUFFER_SIZE 4096
#define FD_COUNT 2 
#define LEAVE_COMMAND -1
#define INVALID_ARGUMENT -2

typedef struct sockaddr saddr;

typedef struct _download_obj {
    int id;
    char* url;
    int size_downloaded;
    int fd;
} download_obj;

int handle_show()
{
    return 0;
}

int handle_start(char* url)
{
    printf("[*] Starting to download from URL: %s\n", url);
    return 0;
}

int handle_stop(char* id)
{
    printf("[*] Stopping download for ID: %s\n", id);
    return 0;
}

int parse_command(char* buffer)
{
    char *token;

    if(!strcmp(buffer, "leave\n"))
    {
        return LEAVE_COMMAND;
    }
    else if (!strcmp(buffer, "show\n"))
    {
        return handle_show();
    }
    else
    {
        token = strtok(buffer, " ");
        if (!strcmp(token, "start"))
        {
            return handle_start(strtok(NULL, " "));
        }
        else if (!strcmp(token, "stop"))
        {
            return handle_stop(strtok(NULL, " "));
        }
        
        return INVALID_ARGUMENT;
    }
}

int main(int argc, char *argv[]) 
{
    struct pollfd fds[FD_COUNT];
    char buffer[BUFFER_SIZE];
    struct sockaddr_in address;
    socklen_t addrlen;
    int byte_count = 0;
    int ret = 0;
    int download_count = 0;
    int recv_fd = 0;
    download_obj* objs = malloc(0);



    static const int NO_TIMEOUT = -1;
    static const int SERVE_COUNT = 5;

    printf("---- Welcome to our server ----\n");
    printf("[*] Initializing server...\n");

    // stdin
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    // server socket 
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

    // main server loop
    while(true)
    {
        poll(fds, FD_COUNT, NO_TIMEOUT);

        // check if someone's ready to read
        if (fds[0].revents & POLLIN)
        {
            byte_count = read(STDIN_FILENO, buffer, BUFFER_SIZE - 1);

            // add null terminator at the end
            buffer[byte_count] = '\0';

            ret = parse_command(buffer);
            if(ret == LEAVE_COMMAND)
            {
                printf("[*] Leaving!\n");
                break;
            }
            else if(ret == INVALID_ARGUMENT)
            {  
                printf("[ERROR] Invalid argument received\n");
            }
        }
        else if (fds[1].revents & POLLIN)
        {
            recv_fd = accept(fds[1].fd, (saddr*) &address, (socklen_t*) &addrlen);
            read(recv_fd, buffer, BUFFER_SIZE);

            // TODO: Handle file from socket

            close(recv_fd);
        }

        // zero out buffer for next poll
        memset(buffer, 0, BUFFER_SIZE);
    }

cleanup:
    printf("[*] Starting cleanup...\n");

    if(fds[1].fd != -1)
    {
        close(fds[1].fd);
    }

    if(objs != NULL)
    {
        free(objs);
    }
    
    if (recv_fd > STDERR_FILENO)
    {
        close(recv_fd);
    }


    printf("---- Server exiting... ----\n");
    return 0;
}