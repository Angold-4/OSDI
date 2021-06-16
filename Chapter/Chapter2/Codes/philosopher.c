#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <dispatch/dispatch.h> // use dispatch intead of semaphore on MacOSX (deprecated)


/* Dinning Philosophers
 *
 * Five philosophers are seated around a circular table.
 * Each philosopher has a plate of spaghetti. The spaghetti is so slippery that a philosopher needs two forks to eat it.
 * The life of a philosopher consists of alternate periods of eating and thinking.
 * When a philosopher gets hungry, she tries to acquire her left and right fork, one at a time, in either order.
 * If successful in acquiring two forks, she eats for a while, then puts down the forks and continues to think.
 *
 * The key question is: can you write a program for each philosopher that does what it is supposed to do and never gets sucked 
 */ 

#define N         5   /* number of philosophers */
#define LEFT      (i + N - 1) % N 
#define RIGHT     (i + 1) % N

#define THINKING  0   /* philosopher is thinking */
#define HUNGRY    1   /* philosopher is trying to get forks */
#define EATING    2   /* philosopher is eating */

int state[N];
int phil[N] = { 0, 1, 2, 3, 4 };

dispatch_semaphore_t mutex;  /* mutual exclusion for critical regions */
dispatch_semaphore_t s[N];   /* semaphore per philosopher */

void think(int i) {
    int thinktime = rand() % 10 + 1;
    printf("%d philosopher is thinking...\n", i);
    sleep(thinktime);
}

void eat(int i) {
    int eattime = rand() % 5 + 1;
    printf("%d philosopher is eating...\n", i);
    sleep(eattime);
}

void test(int i) {
    if(state[i] == HUNGRY && state[LEFT] != EATING && state[RIGHT] != EATING) {
	state[i] = EATING;
	dispatch_semaphore_signal(s[i]);
    }
}

void take_forks(int i) {
    dispatch_semaphore_wait(mutex, DISPATCH_TIME_FOREVER); 
    state[i] = HUNGRY;
    test(i);
    dispatch_semaphore_signal(mutex);
    dispatch_semaphore_signal(s[i]);
}

void put_forks(int i) {
    dispatch_semaphore_wait(mutex, DISPATCH_TIME_FOREVER); 
    state[i] = THINKING;
    test(LEFT);
    test(RIGHT);
    dispatch_semaphore_signal(mutex);
}

void* philosopher(void* arg) {
    while (1) {
	int index = *(int*) arg;
	think(index);
	take_forks(index);
	eat(index);
	put_forks(index);
    }

}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    pthread_t th[N];  /* number of threads */
    mutex = dispatch_semaphore_create(1);  /* binary semaphore */
    for (int i = 0; i < N; i++) {
	s[i] = dispatch_semaphore_create(0);
    }

    int i;
    // create
    for (i = 0; i < N; i++) {
	if(pthread_create(&th[i], NULL, philosopher, &phil[i]) != 0) {
	    perror("Failed to create a philosopher");
	}
    }

    // join
    for (i = 0; i < N; i++) {
	if (pthread_join(th[i], NULL) != 0) {
	    perror("Failed to join thread");
	}
    }

    // release
    dispatch_release(mutex);
    for (int i = 0; i < N; i++) {
	dispatch_release(s[i]);
    }
}
