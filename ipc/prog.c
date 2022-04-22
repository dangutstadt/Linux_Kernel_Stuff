#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/msg.h>
#include <stddef.h>

typedef struct _file_info
{
    int num_words;
    char path[1024];
} file_info;

typedef struct Dir Dir;

typedef struct Dir
{
    char path[1024];
    int dir_info_index;
    Dir *arr_dirs;
    int file_info_index;
    file_info arr_files[20];
} Dir;

typedef struct msg_q
{
    long mtype;
    Dir *dir;
} msg_q;

// User Shared space between process so the stupid message queue will work...

void listdir(const char *name, int indent, Dir *dir_list_instance)
{
    DIR *dir;
    struct dirent *entry;

    strcpy(dir_list_instance->path, name);
    dir_list_instance->dir_info_index = 0;
    dir_list_instance->file_info_index = 0;
    dir_list_instance->arr_dirs = NULL;

    if (!(dir = opendir(name)))
        return;
    int b = 1;
    // dir_info dir_info_instance;
    file_info file_info_instance;

    int pfds[2];

    while (b == 1 && (entry = readdir(dir)) != NULL)
    {
        char path[1024];
        if (entry->d_type == DT_DIR)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            // Folder

            snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);
            key_t key = ftok("/home/osboxes/Desktop/a.out", 'z');
            int msqid = msgget(key, 0666 | IPC_CREAT);

            if (!fork())
            {
                Dir dir_list_sub_inst;
                strcpy(dir_list_sub_inst.path, name);
                // printf("ppid:[%d] pid:[%d]\t%*s[%s]\n", getppid(),getpid(),indent, "", entry->d_name);

                listdir(path, indent + 2, &dir_list_sub_inst);

                // send to father
                printf("Dir %s subfolders&files:\n", dir_list_sub_inst.path);

                b = 0;
                // pass to parent
                msg_q pmb;
                pmb.mtype = 1;
                strcpy(pmb.path, dir_list_sub_inst.path);
                printf("Im the son: %s\n", pmb.path);
                msgsnd(msqid, &pmb, sizeof(char) * 1024, 0);
                msgctl(msqid, IPC_RMID, NULL);
            }
            else
            {

                msg_q pmb;
                msgrcv(msqid, &pmb, sizeof(char) * 1024, 1, 0);

                wait(NULL);
                printf("Father your son is: %s\n", pmb.path);
                dir_list_instance->arr_dirs = realloc(dir_list_instance->arr_dirs, sizeof(dir_list_instance->arr_dirs) + sizeof(Dir));
                // dir_list_instance->arr_dirs[dir_list_instance->dir_info_index++] = pmb.dir;
            }
        }
        else
        {
            file_info *file_info_instance = malloc(sizeof(file_info));
            snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);

            pipe(pfds);
            if (!fork())
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
                memcpy(&(dir_list_instance->arr_files[dir_list_instance->file_info_index++]), file_info_instance, sizeof(file_info));
            }
        }
    }

    closedir(dir);
}

int main(int argc, char *argv[])
{
    printf("pid:[%d]\n", getpid());
    Dir dir_list_instance;
    Dir *instance_ptr = &dir_list_instance;

    listdir(argv[1], 1, instance_ptr);
    printf("final:\n");
    for (int i = 0; i < instance_ptr->file_info_index; i++)
    {
        file_info fi = (file_info)instance_ptr->arr_files[i];
        printf("\n[filename]=%s\n[wc]=%d\n", fi.path, fi.num_words);
    }
    for (int i = 0; i < instance_ptr->dir_info_index; i++)
    {
        Dir di = (Dir)instance_ptr->arr_dirs[i];
        printf("\n[folder]=%s\n", di.path);
        for (int j = 0; j < di.file_info_index; j++)
        {
            file_info fi = (file_info)di.arr_files[j];
            printf("\n[filename]=%s\n[wc]=%d\n", fi.path, fi.num_words);
        }
    }
    return 0;
}