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
 
#define N 5
#define THINKING 2
#define HUNGRY 1
#define EATING 0
#define LEFT (phnum + 4) % N
#define RIGHT (phnum + 1) % N
 
int state[N];
int phil[N] = { 0, 1, 2, 3, 4 };
 
dispatch_semaphore_t mutex;
dispatch_semaphore_t S[N];
 
void test(int phnum) {
    if (state[phnum] == HUNGRY
        && state[LEFT] != EATING
        && state[RIGHT] != EATING) {
        // state that eating
        state[phnum] = EATING;
 
        printf("Philosopher %d takes fork %d and %d\n",
                      phnum + 1, LEFT + 1, phnum + 1);
 
        printf("Philosopher %d is Eating\n", phnum + 1);
 
	// up the signal 
	// if do not up it, means cannot eat, then the following function d_s_wait will waiting for it
	dispatch_semaphore_signal(S[phnum]);
    }
}
 
// take up chopsticks
void take_fork(int phnum) {
 
    dispatch_semaphore_wait(mutex, DISPATCH_TIME_FOREVER);
 
    // state that hungry
    state[phnum] = HUNGRY;
 
    printf("Philosopher %d is Hungry\n", phnum + 1);
 
   // eat if neighbours are not eating
    test(phnum);
 
    dispatch_semaphore_signal(mutex);
 
    // if unable to eat wait to be signalled
    dispatch_semaphore_wait(S[phnum], DISPATCH_TIME_FOREVER); /* down and go to next */ 
 
}
 
// put down chopsticks
void put_fork(int phnum) {
 
    dispatch_semaphore_wait(mutex, DISPATCH_TIME_FOREVER);
 
    // state that thinking
    state[phnum] = THINKING;
 
    printf("Philosopher %d putting fork %d and %d down\n",
           phnum + 1, LEFT + 1, phnum + 1);
    printf("Philosopher %d is thinking\n", phnum + 1);
 
    // after put the forks, check whether neighbours can eat
    test(LEFT);
    test(RIGHT);
 
    dispatch_semaphore_signal(mutex);
}
 
void* philospher(void* num) {
 
    while (1) {
 
        int* i = num;
 
        sleep(3); // thinking
 
        take_fork(*i);
 
        sleep(2); // eating
 
        put_fork(*i);
    }
}
 
int main() {
 
    int i;
    pthread_t thread_id[N];
 
    // initialize the semaphores
    mutex = dispatch_semaphore_create(1);
 
    for (i = 0; i < N; i++)
	S[i] = dispatch_semaphore_create(0);

 
    for (i = 0; i < N; i++) {
        // create philosopher processes
        pthread_create(&thread_id[i], NULL,
                       philospher, &phil[i]);
 
        printf("Philosopher %d is thinking\n", i + 1);
    }
 
    for (i = 0; i < N; i++)
        pthread_join(thread_id[i], NULL);
    
    for (i = 0; i < N; i++) 
	dispatch_release(S[i]);

} 
