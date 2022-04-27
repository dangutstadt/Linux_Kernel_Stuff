#include <stdio.h>
#include <poll.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8888
#define BUFFER_SIZE 4096
#define FD_COUNT 2 
#define LEAVE_COMMAND -1

typedef struct sockaddr saddr;

int handle_show()
{
    return 0;
}

int handle_start_stop(char* buffer)
{
    return 0;
}

int parse_command(char* buffer)
{
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
        return handle_start_stop(buffer);
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

    if(bind(fds[1].fd, (saddr*) &address, sizeof(address) < 0))
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
        if(fds[0].revents & POLLIN)
        {
            byte_count = read(STDIN_FILENO, buffer, BUFFER_SIZE - 1);

            // add null terminator at the end
            buffer[byte_count] = '\0';

            ret = parse_command(buffer);
            if(ret == LEAVE_COMMAND)
                break;
            

        }
    }

cleanup:
    printf("[*] Starting cleanup...\n");

    if(fds[1].fd != -1)
    {
        close(fds[1].fd);
    }

    printf("---- Server exiting... ----\n");
    return 0;
}