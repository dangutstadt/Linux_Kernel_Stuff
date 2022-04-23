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

typedef struct dir_info dir_info;

struct dir_info
{
    char path[PATH_MAX];
    int dir_info_index;
    int file_info_index;
    int files_in_folder;
    int dirs_in_folder;
    dir_info *pDirs;
    file_info* pFiles;
};

typedef struct msg_q
{
    long mtype;
    dir_info dir;
} msg_q;


void count_dir_and_files(dir_info *pDirInfo, const char * name)
{
    DIR *dir = NULL;
    struct dirent *entry = NULL;

    // if the dir can't be opened, return
    if (!(dir = opendir(name)))
        return;

    //Determine the number of files and directories
    while((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (entry->d_type == DT_DIR)
        {
            pDirInfo->dirs_in_folder++;
        }
        else
        {
            pDirInfo->files_in_folder++;
        }
    }
    rewinddir(dir);
}

void listdir(const char *name, int indent, dir_info *dir_list_instance)
{
    DIR *dir = NULL;
    struct dirent *entry = NULL;

    // init dir_list_instance
    strcpy(dir_list_instance->path, name);
    dir_list_instance->dir_info_index = 0;
    dir_list_instance->file_info_index = 0;
    dir_list_instance->files_in_folder = 0;
    dir_list_instance->dirs_in_folder = 0;
    dir_list_instance->pDirs = NULL;
    dir_list_instance->pFiles = NULL;

    // if the dir can't be opened, return
    if (!(dir = opendir(name)))
        return;

    count_dir_and_files(dir_list_instance, name);

    // allocate enough space for all files and directories
    dir_list_instance->pFiles = malloc (dir_list_instance->files_in_folder * sizeof(file_info));
    dir_list_instance->pDirs = malloc (dir_list_instance->dirs_in_folder * sizeof(dir_info));
    
    int b = 1;
    
    int pfds[2];
    while (b == 1 && (entry = readdir(dir)) != NULL)
    {
        char path[PATH_MAX];
        if (entry->d_type == DT_DIR)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);

            // generate IPC key
            key_t key = ftok("/tmp", 'z');
            int msqid = msgget(key, 0666 | IPC_CREAT);

            if (fork() == 0)
            {
                // in Child
                dir_info dir_list_sub_inst;
                strcpy(dir_list_sub_inst.path, name);

                // Run on child folder
                listdir(path, indent + 2, &dir_list_sub_inst);

                b = 0;
                

                // pass to parent
                msg_q pmb;
                pmb.mtype = 1;
                pmb.dir = dir_list_sub_inst;
                
                msgsnd(msqid, &pmb, sizeof(dir_info), 0);
                msgctl(msqid, IPC_RMID, NULL);
            }
            else
            {
                // in Parent
                msg_q pmb;

                wait(NULL);

                if(pmb.mtype == 1)
                {
                    dir_list_instance->pDirs[dir_list_instance->dir_info_index] = pmb.dir;
                    dir_list_instance->dir_info_index++;
                }
            }
        }
        else
        {
            file_info* file_info_instance = malloc(sizeof(file_info));
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
                t = file_info_instance->path;

                scanf("%d %s", &file_info_instance->num_words, t);

                wait(NULL);
                memcpy(&(dir_list_instance->pFiles[dir_list_instance->file_info_index]), file_info_instance, sizeof(file_info));
                dir_list_instance->file_info_index++;
            }

            free(file_info_instance);
        }
    }

    closedir(dir);
}


int main(int argc, char *argv[])
{
    printf("pid:[%d]\n", getpid());
    dir_info dir_list_instance;

    listdir(argv[1], 1, &dir_list_instance);
    // for (int i = 0; i < (&dir_list_instance)->file_info_index; i++)
    // {
    //     file_info fi = (file_info)(&dir_list_instance)->pFiles[i];
    //     //printf("\n[filename]=%s\n[wc]=%d\n", fi.path, fi.num_words);
    // }

    // for (int i = 0; i < (&dir_list_instance)->dir_info_index; i++)
    // {
    //     Dir di = (Dir)(&dir_list_instance)->pDirs[i];
    //     printf("\n[folder]=%s, index=%d\n", di.path, di.dir_info_index);
    //     // for (int j = 0; j < di.file_info_index; j++)
    //     // {
    //     //     file_info fi = (file_info)di.pFiles[j];
    //     //     printf("\n[filename]=%s\n[wc]=%d\n", fi.path, fi.num_words);
    //     // }
    //     free(di.pFiles);
    // }
    free(dir_list_instance.pDirs);
    return 0;
}