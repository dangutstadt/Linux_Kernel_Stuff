#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <stdlib.h>

typedef struct _file_info
{
    int num_words;
    char path[PATH_MAX];
} file_info;


typedef struct msg_q
{
    long mtype;
    file_info file_info;
} msg_q;


void listdir(const char *name, int msqid)
{
    DIR *dir = NULL;
    struct dirent *entry = NULL;

    
    // if the dir can't be opened, return
    if (!(dir = opendir(name)))
        return;

    
    int pfds[2];
    while ((entry = readdir(dir)) != NULL)
    {
        char path[PATH_MAX];
        if (entry->d_type == DT_DIR)
        {
            // handle directories
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);

            pid_t  pid;
            if ((pid = fork()) == 0)
            {
                // in Child

                // Run on child folder
                listdir(path, msqid);
                exit(0);
            } else {
                waitpid(pid, NULL, 0);
            }
        }
        else
        {
            file_info file_info_instance;
            snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);

            pipe(pfds);
            if (fork() == 0)
            {
                close(1);
                dup(pfds[1]);
                close(pfds[0]);
                execlp("wc", "wc", "-w", path, NULL);
            }
            else
            {
                close(0);
                dup(pfds[0]);

                char* t;
                t = file_info_instance.path;

                scanf("%d %s", &file_info_instance.num_words, t);
                
                msg_q pmb;
                pmb.mtype = 1;
                pmb.file_info = file_info_instance;
                
                msgsnd(msqid, &pmb, sizeof(file_info), 0);

                struct msqid_ds ds;
                do {
                    msgctl(msqid, IPC_STAT, &ds);
                } while(ds.msg_qnum != 0);
            }

        }
    }

    closedir(dir);
}

int main(int argc, char *argv[])
{

    // message queue starts here

    // generate IPC key
    key_t key = ftok("/tmp", 'r');
    int msqid = msgget(key, 0666 | IPC_CREAT);
    
    if (fork() == 0) {
        // start running on directories
        listdir(argv[1], msqid);

        // close msg queue
        msgctl(msqid, IPC_RMID, NULL);
        exit(0);
    } 

    msg_q pmb;

    file_info* files = NULL;
    int files_counter = 0;
    float  avg = 0;
    float  variance = 0;

    // getting all dir infos from message queue
    while(msgrcv(msqid, &pmb, sizeof(file_info), 1, 0) != -1) {

        files = realloc(files, sizeof(file_info) * (files_counter + 1));
        files[files_counter++] = pmb.file_info;

        avg += pmb.file_info.num_words;
    }

    // calculate avg and variance

    printf("amnoit %d", files_counter);
    avg /= files_counter;

    for (int i = 0; i < files_counter; i++) {
        variance += (files[i].num_words - avg) * (files[i].num_words - avg);
    }
    variance /= files_counter;

    // filename data and summary
    for (int i = 0; i < files_counter; i++)
    {
        printf("[*] Name: %s, wc: %d, average deviation: %f\n", files[i].path, files[i].num_words, files[i].num_words - avg);
    }
    
    printf("\n\nSummary\n**************\nAverage: %f\nVariance: %f\n**************\n", avg, variance);

    free(files);

    return 0;
}