/**
 * Given the paths of n regular files as input, n reverse_file threads and one
 * print_file thread are generated.
 * Each reverse_file thread reverses the content of the file associated with
 * the i-th reverse_file and enters the path of the reversed file in a shared buffer.
 * The print file thread takes the reversed file path from the buffer and prints the
 * content of the file.
 * To open the file and reverse the content and print the content you need to use
 * file mapping.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/limits.h>
#include <pthread.h>
#include <semaphore.h>

#define BUFFER_SIZE 4

typedef struct {
    char buffer[BUFFER_SIZE][PATH_MAX];
    int in;
    int out;
    int paths_num;
    int paths_to_consume;

    sem_t mutex;
    sem_t empty;
    sem_t full;
} shared_data;

typedef struct {
    pthread_t tid;
    int thread_i;
    char *filepath;

    shared_data *shared;
} threads_data;

void init_shared(shared_data *shared, int paths_num) {
    shared->in = shared->out = 0;

    shared->paths_num = paths_num;

    shared->paths_to_consume = 0;

    // semaphores init
    int err;
    if ((err = sem_init(&shared->empty, 0, BUFFER_SIZE)) != 0) {
        fprintf(stderr, "Error in sem_init: %d\n", err);
        return;
    }
    if ((err = sem_init(&shared->full, 0, 0)) != 0) {
        fprintf(stderr, "Error in sem_init: %d\n", err);
        return;       
    }
    if ((err = sem_init(&shared->mutex, 0, 1)) != 0) {
        fprintf(stderr, "Error in sem_init: %d\n", err);
        return;
    }
}

void destroy_shared(shared_data *shared) {
    sem_destroy(&shared->empty);
    sem_destroy(&shared->full);
    sem_destroy(&shared->mutex);
    free(shared);    
}

void reverse_file(void *arg) {
    threads_data *td = (threads_data *)arg;
    int fd;
    struct stat statbuf;
    char *map;
    char tmp;
    int err;

    // map the file to reverse it
    if ((fd = open(td->filepath, O_RDWR)) == -1) {
        fprintf(stderr, "Error in open");
        return;
    }

    if (fstat(fd, &statbuf) == -1) {
        fprintf(stderr, "Error in fstat");
        return;
    }

    if (!S_ISREG(statbuf.st_mode)) {
        fprintf(stderr, "%s is not a file", td->filepath);
        return;
    }

    if ((map = mmap(NULL, statbuf.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        fprintf(stderr, "Error in mmap");
        return;
    }

    // reverse the file
    for (int i = 0; i < (statbuf.st_size / 2); i++) {
        tmp = map[statbuf.st_size - i - 1];
        map[statbuf.st_size - i - 1] = map[i];
        map[i] = tmp;
    }

    fprintf(stdout, "[reverse_file%d]: %s\n", td->thread_i ,td->filepath);

    if (close(fd) == -1) {
        fprintf(stderr, "Error in close");
        return;
    }

    // unmap file
    if (munmap(map, statbuf.st_size) == -1) {
        fprintf(stderr, "Error in munmap");
        return;
    }

    // down(empty)
    if ((err = sem_wait(&td->shared->empty)) != 0)
        fprintf(stderr, "Error in sem_wait: %d\n", err);
    // down(mutex)
    if ((err = sem_wait(&td->shared->mutex)) != 0)
        fprintf(stderr, "Error in sem_wait: %d\n", err);

    // insert reversed file path
    strncpy(td->shared->buffer[td->shared->in], td->filepath, PATH_MAX);

    td->shared->in = (td->shared->in + 1) % BUFFER_SIZE;

    // up(mutex)
    if ((err = sem_post(&td->shared->mutex)) != 0)
        fprintf(stderr, "Error in sem_post: %d\n", err);
    // up(full)
    if ((err = sem_post(&td->shared->full)) != 0)
        fprintf(stderr, "Error in sem_post: %d\n", err);
}

void print_file(void *arg) {
    threads_data *td = (threads_data *)arg;
    int fd;
    struct stat statbuf;
    char *map;
    int err;

    while (td->shared->paths_to_consume < td->shared->paths_num) {
        // down(full)
        if ((err = sem_wait(&td->shared->full)) != 0)
            fprintf(stderr, "Error in sem_wait: %d\n", err);
        // down(mutex)
        if ((err = sem_wait(&td->shared->mutex)) != 0)
            fprintf(stderr, "Error in sem_wait: %d\n", err);

        // consume reversed file path
        td->filepath = td->shared->buffer[td->shared->out];

        td->shared->out = (td->shared->out + 1) % BUFFER_SIZE;
        td->shared->paths_to_consume++;

        // up(mutex)
        if ((err = sem_post(&td->shared->mutex)) != 0)
            fprintf(stderr, "Error in sem_post: %d\n", err);
        // up(empty)
        if ((err = sem_post(&td->shared->empty)) != 0)
            fprintf(stderr, "Error in sem_post: %d\n", err);

        // map the file to show content
        if ((fd = open(td->filepath, O_RDONLY)) == -1)
            fprintf(stderr, "Error in open");

        if ((lstat(td->filepath, &statbuf)) == -1)
            fprintf(stderr, "Error in lstat");

        if ((map = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED)
            fprintf(stderr, "Error in mmap");

        // show content
        fprintf(stdout, "\n[print_file]: %s\n", td->filepath);
        puts(map);
        fprintf(stdout, "\n");

        if (close(fd) == -1)
            fprintf(stderr, "Error in close");

        // unmap file
        if (munmap(map, statbuf.st_size) == -1) {
            fprintf(stderr, "Error in munmap");
            exit(1);         
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input-file-1> <input-file-2> ... <input-file-n>\n", argv[0]);
        exit(1);
    }

    int file_paths_num = argc - 1;
    threads_data td[file_paths_num + 1];
    shared_data *shared = malloc(sizeof(shared_data));
    int err;

    init_shared(shared, file_paths_num);

    // init and create reverse_file threads
    for (int i = 0; i < file_paths_num; i++) {
        td[i].thread_i = i + 1;
        td[i].filepath = argv[i + 1];
        td[i].shared = shared;

        if ((err = pthread_create(&td[i].tid, NULL, (void *)reverse_file, &td[i])) != 0) {
            fprintf(stderr, "Error in pthread_create: %d\n", err);
            exit(1);
        }
    }

    // init and create print_file thread
    td[file_paths_num].shared = shared;
    if ((err = pthread_create(&td[file_paths_num].tid, NULL, (void *)print_file, &td[file_paths_num])) != 0) {
        fprintf(stderr, "Error in pthread_create: %d\n", err);
        exit(1);
    }

    // waiting for threads to terminate
    for (int i = 0; i < file_paths_num + 1; i++) {
        if ((err = pthread_join(td[i].tid, NULL)) != 0) {
            fprintf(stderr, "Error in pthread_join: %d\n", err);
            exit(1);            
        }
    }

    destroy_shared(shared);

    exit(0);
}