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
    int files_in_folder;
    int dirs_in_folder;
    dir_info *pDirs;
    file_info* pFiles;
};

typedef struct _dir_totals
{
    float avg;
    int file_count;
} dir_totals;

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

    int file_index = 0;
    int dir_index = 0;
    
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
                printf("Sending %s to listdir\n", path);
                listdir(path, indent + 2, &dir_list_sub_inst);

                b = 0;

                // pass to parent
                msg_q pmb;
                pmb.mtype = 1;
                pmb.dir = dir_list_sub_inst;
                
                msgsnd(msqid, &pmb, sizeof(dir_info), 0);
                msgctl(msqid, IPC_RMID, NULL);
                exit(0);
            }
            else
            {
                // in Parent
                msg_q pmb;
                msgrcv(msqid, &pmb, sizeof(dir_info), 1, 0);
                
                wait(NULL);

                if(pmb.mtype == 1)
                {
                    dir_list_instance->pDirs[dir_index] = pmb.dir;
                    dir_index++;
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
                exit(0);
            }
            else
            {
                close(0);
                dup(pfds[0]);

                char* t;
                t = file_info_instance->path;

                scanf("%d %s", &file_info_instance->num_words, t);

                wait(NULL);
                dir_list_instance->pFiles[file_index] = *file_info_instance;
                printf("%s: has %d words\n", dir_list_instance->pFiles[file_index].path, dir_list_instance->pFiles[file_index].num_words);
                file_index++;
            }
        }
    }

    closedir(dir);
}

float sum_words_in_folder(dir_info dir)
{
    float sum = 0;
    for (int i = 0; i < dir.files_in_folder; i++)
    {
        //printf("i: %d, total: %d\n", i, dir->files_in_folder);
        file_info fi = dir.pFiles[i];
        if (fi.num_words < 0)
        {
            printf("%s %s\n", dir.path, fi.path);
        }
        //sum += fi.num_words;
    }

    return sum;
}

// *****************************************************
// TODO: DAN THIS DOES NOT CALCULATE THE CORRECT AVG!!!!
// Need to implement correctly, plus add a variance calc
// *****************************************************
void calc_avg(dir_info dir, dir_totals* out)
{
    printf("%s\n", dir.path);
    for (int i = 0; i < dir.dirs_in_folder; i++)
    {
        calc_avg(dir.pDirs[i], out);
    }

    out->file_count += dir.files_in_folder;
    out->avg = (out->file_count * out->avg + sum_words_in_folder(dir)) / (out->file_count + 1);
}

int main(int argc, char *argv[])
{
    dir_info dir_list_instance;

    listdir(argv[1], 1, &dir_list_instance);

    float avg, variance;

    dir_totals totals;
    totals.file_count = 0;
    totals.avg = 0.0;
    calc_avg(dir_list_instance, &totals);

    free(dir_list_instance.pFiles);
    free(dir_list_instance.pDirs);
    return 0;
}