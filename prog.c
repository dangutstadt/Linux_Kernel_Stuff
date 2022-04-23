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

typedef struct Dir Dir;

typedef struct Dir
{
    char path[PATH_MAX];
    int dir_info_index;
    Dir *arr_dirs;
    int file_info_index;
    int files_in_folder;
    file_info* pFiles;
} Dir;

typedef struct msg_q
{
    long mtype;
    char path[1024];
    Dir *dir;
} msg_q;

void listdir(const char *name, int indent, Dir *dir_list_instance)
{
    DIR *dir = NULL;
    struct dirent *entry = NULL;

    // init dir_list_instance
    strcpy(dir_list_instance->path, name);
    dir_list_instance->dir_info_index = 0;
    dir_list_instance->file_info_index = 0;
    dir_list_instance->arr_dirs = NULL;

    // if the dir can't be opened, return
    if (!(dir = opendir(name)))
        return;

    //Determine the number of files
    while((entry = readdir(dir)) != NULL) {
        if ( strcmp(entry->d_name, ".") || strcmp(entry->d_name, "..") )
        {
            dir_list_instance->files_in_folder++;
        }
    }
    rewinddir(dir);

    dir_list_instance->pFiles = (file_info*) malloc (dir_list_instance->files_in_folder * sizeof(file_info));
    
    int b = 1;
    file_info file_info_instance;
    
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
                Dir dir_list_sub_inst;
                strcpy(dir_list_sub_inst.path, name);

                // Run on child folder
                printf("%s\n", path);
                listdir(path, indent + 2, &dir_list_sub_inst);
                printf("Dir %s subfolders&files:\n", dir_list_sub_inst.path);

                b = 0;
                // pass to parent
                msg_q pmb;
                pmb.mtype = 1;
                strcpy(pmb.path, dir_list_sub_inst.path);
                printf("Im the son: %s\n", pmb.path);
                msgsnd(msqid, &pmb, sizeof(char) * PATH_MAX, 0);
                msgctl(msqid, IPC_RMID, NULL);
            }
            else
            {
                // in Parent
                msg_q pmb;
                msgrcv(msqid, &pmb, sizeof(char) * PATH_MAX, 1, 0);

                wait(NULL);
                dir_list_instance->arr_dirs = realloc(dir_list_instance->arr_dirs, sizeof(dir_list_instance->arr_dirs) + sizeof(Dir));
            }
        }
        else
        {
            file_info *file_info_instance = malloc(sizeof(file_info));
            snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);

            pipe(pfds);
            if (fork() == 0)
            {
                printf("ppid:[%d] pid:[%d]\t%s\n", getppid(), getpid(), path);
                close(1);
                dup(pfds[1]);
                close(pfds[0]);
                execlp("wc", "wc", "-w", path, NULL);
            }
            else
            {
                close(0);
                dup(pfds[0]);

                char *t;
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
    Dir dir_list_instance;

    listdir(argv[1], 1, &dir_list_instance);
    printf("final:\n");
    for (int i = 0; i < (&dir_list_instance)->file_info_index; i++)
    {
        file_info fi = (file_info)(&dir_list_instance)->pFiles[i];
        printf("\n[filename]=%s\n[wc]=%d\n", fi.path, fi.num_words);
    }
    for (int i = 0; i < (&dir_list_instance)->dir_info_index; i++)
    {
        Dir di = (Dir)(&dir_list_instance)->arr_dirs[i];
        printf("\n[folder]=%s\n", di.path);
        for (int j = 0; j < di.file_info_index; j++)
        {
            file_info fi = (file_info)di.pFiles[j];
            printf("\n[filename]=%s\n[wc]=%d\n", fi.path, fi.num_words);
        }
        free(di.pFiles);
    }
    free(dir_list_instance.arr_dirs);
    return 0;
}