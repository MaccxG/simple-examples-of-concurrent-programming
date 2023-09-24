/**
 *  The program takes the number of games as input and manages a series of games between
 *  two virtual players P1 and P2 who play chinese morra. The program creates: two threads
 *  P1 and P2 which represent the players, a judge thread and a scoreboard thread.
 *  The threads share a data structure, which contains the data for each game to operate,
 *  and are coordinated via condition vars.
 *  The judge starts the game, the players make their move, the judge check the moves:
 *  if there is a winner, it will call the scoreboard to show the partial score and the
 *  judge will move on to the next game; instead, if the game is drawn, the judge raises the
 *  same game.
 *  At the end of all games, the scoreboard is in charge of show the final score and
 *  the final winner.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

typedef enum { PLAYER1, PLAYER2, JUDGE, SCOREBOARD } threads_name;

char *moves_type[3] = {"rock", "paper", "scissors"};

typedef struct {
    char *moves[2];
    int winner;
    int games_num;
    int ended_games;

    pthread_mutex_t mutex;
    pthread_cond_t cond[4];
    bool do_move[2];
    bool show_score;
} shared;

typedef struct {
    pthread_t tid;
    int thread_i;

    shared *sh;
} threads_data;

void init_shared(shared *sh, int games_num) {
    sh->winner = -1;
    sh->games_num = games_num;
    sh->ended_games = 0;

    // cond init
    int err;
    if ((err = pthread_mutex_init(&sh->mutex, NULL)) != 0) {
        fprintf(stderr, "Error in pthread_mutex_init: %d\n", err);
        return;
    }
    
    for (int i = 0; i < 4; i++) {
        if ((err = pthread_cond_init(&sh->cond[i], NULL)) != 0) {
            fprintf(stderr, "Error in pthread_cond_init: %d\n", err);
            return;
        }
    }

    sh->do_move[0] = sh->do_move[1] = false;
    sh->show_score = false;  
}

void destroy_shared(shared *sh) {
    pthread_mutex_destroy(&sh->mutex);
    for (int i = 0; i < 4; i++)
        pthread_cond_destroy(&sh->cond[i]);
    free(sh);
}

void player(void *arg) {
    threads_data *td = (threads_data *)arg;
    int err;

    while (1) {
        // lock
        if ((err = pthread_mutex_lock(&td->sh->mutex)) != 0)
            fprintf(stderr, "Error in pthread_mutex_lock: %d\n", err);

        // the players await the judge
        while (!td->sh->do_move[td->thread_i - 1]) {
            if ((err = pthread_cond_wait(&td->sh->cond[td->thread_i - 1], &td->sh->mutex)) != 0)
                fprintf(stderr, "Error in pthread_cond_wait: %d\n", err);
        }

        // all games have been played
        if (td->sh->ended_games == td->sh->games_num) {
            if ((err = pthread_mutex_unlock(&td->sh->mutex)) != 0)
                fprintf(stderr, "Error in pthread_mutex_unlock: %d\n", err);
            break;            
        }

        td->sh->moves[td->thread_i - 1] = moves_type[rand() % 3];
        printf("P%d -> %s\n", td->thread_i, td->sh->moves[td->thread_i - 1]);            

        // the players have made their own move
        td->sh->do_move[td->thread_i - 1] = false;
        if ((err = pthread_cond_signal(&td->sh->cond[JUDGE])) != 0)
            fprintf(stderr, "Error in pthread_cond_signal: %d\n", err);

        // unlock
        if ((err = pthread_mutex_unlock(&td->sh->mutex)) != 0)
            fprintf(stderr, "Error in pthread_mutex_unlock: %d\n", err);
    }
}

int checkWinner(char *P1_move, char *P2_move) {
    // draw game
    if (strcmp(P1_move, P2_move) == 0)
        return -1;

    // P1 won
    if (strcmp(P1_move, moves_type[0]) == 0 && strcmp(P2_move, moves_type[2]) == 0 ||
        strcmp(P1_move, moves_type[1]) == 0 && strcmp(P2_move, moves_type[0]) == 0 ||
        strcmp(P1_move, moves_type[2]) == 0 && strcmp(P2_move, moves_type[1]) == 0)
        return 0;

    // P2 won
    return 1;
}

void judge(void *arg) {
    threads_data *td = (threads_data *)arg;
    int err;

    while (td->sh->ended_games < td->sh->games_num) {
        // lock
        if ((err = pthread_mutex_lock(&td->sh->mutex)) != 0)
            fprintf(stderr, "Error in pthread_mutex_lock: %d\n", err);

        printf("\nGame %d)\n", td->sh->ended_games + 1);

        // wake up the players
        td->sh->do_move[PLAYER1] = td->sh->do_move[PLAYER2] = true;
        if ((err = pthread_cond_signal(&td->sh->cond[PLAYER1])) != 0)
            fprintf(stderr, "Error in pthread_cond_signal: %d\n", err);
        if ((err = pthread_cond_signal(&td->sh->cond[PLAYER2])) != 0)
            fprintf(stderr, "Error in pthread_cond_signal: %d\n", err);
        
        // wait for both players moves
        while ((td->sh->do_move[PLAYER1] || td->sh->do_move[PLAYER2])) {
            if ((err = pthread_cond_wait(&td->sh->cond[JUDGE], &td->sh->mutex)) != 0)
                fprintf(stderr, "Error in pthread_cond_wait: %d\n", err);
        }
        
        // check if there is a winner or if it is a draw
        td->sh->winner = checkWinner(td->sh->moves[PLAYER1], td->sh->moves[PLAYER2]);

        if (td->sh->winner >= 0) {  // there is a winner
            // the judge moves on to the next game
            td->sh->ended_games++;

            // wake up the scoreboard to show the score
            td->sh->show_score = true;
            if ((err = pthread_cond_signal(&td->sh->cond[SCOREBOARD])) != 0)
                fprintf(stderr, "Error in pthread_cond_signal: %d\n", err);

            // the judge waits for the score to be shown
            while (td->sh->show_score) {
                if ((err = pthread_cond_wait(&td->sh->cond[JUDGE], &td->sh->mutex)) != 0)
                    fprintf(stderr, "Error in pthread_cond_wait: %d\n", err);
            }
        }   
        else    // it's a draw
            printf("Draw\n");

        // unlock
        if ((err = pthread_mutex_unlock(&td->sh->mutex)) != 0)
            fprintf(stderr, "Error in pthread_mutex_unlock: %d\n", err);
    }
    
    // warns players that the match is over
    td->sh->do_move[PLAYER1] = td->sh->do_move[PLAYER2] = true;
    if ((err = pthread_cond_signal(&td->sh->cond[PLAYER1])) != 0)
        fprintf(stderr, "Error in pthread_cond_signal: %d\n", err);
    if ((err = pthread_cond_signal(&td->sh->cond[PLAYER2])) != 0)
        fprintf(stderr, "Error in pthread_cond_signal: %d\n", err);
}

void scoreboard(void *arg) {
    threads_data *td = (threads_data *)arg;
    int score[2] = {0};
    int err;

    while (td->sh->ended_games < td->sh->games_num) {
        // lock
        if ((err = pthread_mutex_lock(&td->sh->mutex)) != 0)
            fprintf(stderr, "Error in pthread_mutex_lock: %d\n", err);

        // the scoreboard awaits the judge
        while (!td->sh->show_score) {
            if ((err = pthread_cond_wait(&td->sh->cond[SCOREBOARD], &td->sh->mutex)) != 0)
                fprintf(stderr, "Error in pthread_cond_wait: %d\n", err);
        }

        // update the score of the winner of the last game
        score[td->sh->winner]++;
        
        printf("Partial score:\n");
        printf("P1 = %d, P2 = %d\n", score[0], score[1]);

        // the judge regains control
        td->sh->show_score = false;
        if ((err = pthread_cond_signal(&td->sh->cond[JUDGE])) != 0)
            fprintf(stderr, "Error in pthread_cond_signal: %d\n", err);

        // unlock
        if ((err = pthread_mutex_unlock(&td->sh->mutex)) != 0)
            fprintf(stderr, "Error in pthread_mutex_unlock: %d\n", err);
    }

    printf("\nFinal score:\n");
    printf("P1 = %d, P2 = %d\n", score[0], score[1]);
    if (score[0] == score[1])
        printf("Draw game\n\n");
    else {
        printf("Final winner of the match is ");
        if (score[0] > score[1])
            printf("P1\n\n");
        else
            printf("P2\n\n");
    }
}

int main(int argc, char **argv) {
    // check parameters number
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <number of matches>\n", argv[0]);
        exit(1);
    }

    char *str_end1;
    int games_num = (int)strtol(argv[1], &str_end1, 10);

    // check parameter
    if ((*str_end1 != '\0' || games_num <= 0)) {
        fprintf(stderr, "Invalid input\n");
        exit(1);
    }

    threads_data td[4];
    shared *sh = malloc(sizeof(shared));
    int err;
    srand(time(NULL));

    init_shared(sh, games_num);

    // init and create threads
    for (int i = 0; i < 4; i++)
        td[i].sh = sh;

    td[0].thread_i = 1;
    if ((err = pthread_create(&td[0].tid, NULL, (void *)player, &td[0])) != 0) {
        fprintf(stderr, "Error in pthread_create: %d\n", err);
        exit(1);
    }
    
    td[1].thread_i = 2;
    if ((err = pthread_create(&td[1].tid, NULL, (void *)player, &td[1])) != 0) {
        fprintf(stderr, "Error in pthread_create: %d\n", err);
        exit(1);
    }

    if ((err = pthread_create(&td[2].tid, NULL, (void *)judge, &td[2])) != 0) {
        fprintf(stderr, "Error in pthread_create: %d\n", err);
        exit(1);
    }

    if ((err = pthread_create(&td[3].tid, NULL, (void *)scoreboard, &td[3])) != 0) {
        fprintf(stderr, "Error in pthread_create: %d\n", err);
        exit(1);
    }

    // waiting for threads to terminate
    for (int i = 0; i < 4; i++) {
        if ((err = pthread_join(td[i].tid, NULL)) != 0) {
            fprintf(stderr, "Error in pthread_join: %d\n", err);
            exit(1);            
        }
    }

    destroy_shared(sh);

    exit(0);
}