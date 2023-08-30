/**
 *  The program takes the path of a file as input, the output consists of all the palindrome
 *  strings contained within the file.
 *  When the program starts, it creates 3 threads R, P and W which have access to a shared data
 *  structure through mutual exclusion using semaphores.
 *  The roles of the 3 threads are as follows:
 *  -   R reads the file line by line and inserts, at each iteration, the read line (the string)
 *      inside the shared data structure;
 *  -   P analyzes, at each iteration, the string inserted by R in the data structure, if the
 *      string is palindrome, P will have to wake up W to print the string;
 *  -   W prints every palindrome string found.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <linux/limits.h>

#define BUFFER_SIZE 4096

typedef enum { R, P, W } thread_i;

typedef struct {
    char buffer[BUFFER_SIZE];
    bool ended_work;

    sem_t sem[3];
} shared;

typedef struct {
    pthread_t tid;
    FILE *file;
    
    shared *shared;
} thread_data;

void init_shared(shared *sh) {
    sh->ended_work = false;

    int err;
    // semaphores init
    if ((err = sem_init(&sh->sem[R], 0, 1)) != 0) {
        fprintf(stderr, "Error in sem_init: %d\n", err);
        return;
    }
    if ((err = sem_init(&sh->sem[P], 0, 0)) != 0) {
        fprintf(stderr, "Error in sem_init: %d\n", err);
        return;
    }
    if ((err = sem_init(&sh->sem[W], 0, 0)) != 0) {
        fprintf(stderr, "Error in sem_init: %d\n", err);
        return;
    }
}

void destroy_shared(shared *sh) {
    for (int i = 0; i < 3; i++)
        sem_destroy(&sh->sem[i]);
    free(sh);
}

void reader_thread(void *arg) {
    thread_data *td = (thread_data *)arg;
    char readerbuff[BUFFER_SIZE];
    int err;

    while (fgets(readerbuff, BUFFER_SIZE, td->file)) {
        if (readerbuff[strlen(readerbuff) - 1] == '\n')
            readerbuff[strlen(readerbuff) - 1] = '\0';

        // wait to be able to insert into the buffer
        if ((err = sem_wait(&td->shared->sem[R])) != 0)
            fprintf(stderr, "Error in sem_wait: %d\n", err);

        strncpy(td->shared->buffer, readerbuff, BUFFER_SIZE);

        // P can check the string
        if ((err = sem_post(&td->shared->sem[P])) != 0)
            fprintf(stderr, "Error in sem_post: %d\n", err);
    }

    td->shared->ended_work = true;

    if ((err = sem_post(&td->shared->sem[P])) != 0)
        fprintf(stderr, "Error in sem_post: %d\n", err);

    if ((err = sem_post(&td->shared->sem[W])) != 0)
        fprintf(stderr, "Error in sem_post: %d\n", err);  
}

bool is_palindrome(char *str) {
    int i = 0;
    while (i < strlen(str) / 2) {
        if (str[i] != str[strlen(str) - i - 1])
            return false;
        i++;
    }
    return true;
}

void palindrome_thread(void *arg) {
    thread_data *td = (thread_data *)arg;
    int err;

    while (!td->shared->ended_work) {
        // waits for insert into the buffer
        if ((err = sem_wait(&td->shared->sem[P])) != 0)
            fprintf(stderr, "Error in sem_wait: %d\n", err);   
        
        if (is_palindrome(td->shared->buffer)) {
            // W has to print the palindrome string
            if ((err = sem_post(&td->shared->sem[W])) != 0)
                fprintf(stderr, "Error in sem_post: %d\n", err);
        }
        else {
            // R can continue to read the buffer
            if ((err = sem_post(&td->shared->sem[R])) != 0)
                fprintf(stderr, "Error in sem_post: %d\n", err);
        }
    }
}

void writer_thread(void *arg) {
    thread_data *td = (thread_data *)arg;
    int err;

    while (!td->shared->ended_work) {
        // waits to be awakened by P
        if ((err = sem_wait(&td->shared->sem[W])) != 0)
            fprintf(stderr, "Error in sem_wait: %d\n", err);

        printf("%s\n", td->shared->buffer);

        // R can continue to read the buffer
        if ((err = sem_post(&td->shared->sem[R])) != 0)
            fprintf(stderr, "Error in sem_post: %d\n", err);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <input-file>\n", argv[0]);
        exit(1);
    }

    FILE *f;
    if ((f = fopen(argv[1], "r")) == NULL) {
        fprintf(stderr, "Error in fopen\n");
        exit(1);
    }

    int err;
    thread_data td[3];
    shared *sh = malloc(sizeof(shared));
    
    init_shared(sh);

    // init threads
    td[R].file = f;
    for (int i = 0; i < 3; i++)
        td[i].shared = sh;
    
    // create threads
    if ((err = pthread_create(&td[R].tid, NULL, (void *)reader_thread, &td[R])) != 0) {
        fprintf(stderr, "Error in pthread_create: %d\n", err);
        exit(1);
    }
    if ((err = pthread_create(&td[P].tid, NULL, (void *)palindrome_thread, &td[P])) != 0) {
        fprintf(stderr, "Error in pthread_create: %d\n", err);
        exit(1);
    }
    if ((err = pthread_create(&td[W].tid, NULL, (void *)writer_thread, &td[W])) != 0) {
        fprintf(stderr, "Error in pthread_create: %d\n", err);
        exit(1);
    }

    // waiting for threads to terminate 
    for (int i = 0; i < 3; i++) {
        if ((err = pthread_join(td[i].tid, NULL)) != 0) {
            fprintf(stderr, "Error in pthread_create: %d\n", err);
            exit(1);
        }     
    }

    destroy_shared(sh);

    fclose(f);

    exit(0);
}