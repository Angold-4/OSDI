#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>
#include <dispatch/dispatch.h> // use dispatch intead of semaphore on MacOSX (deprecated)

// https://stackoverflow.com/questions/1413785/sem-init-on-os-x

#define THREAD_NUM 8


dispatch_semaphore_t semEmpty; // Empty slots
dispatch_semaphore_t semFull;  // Full slots

pthread_mutex_t mutexBuffer;

int buffer[10];
int count = 0;

void* producer(void* args) {
    while (1) {
        // Produce
        int x = rand() % 100;
        sleep(1);

        // Add to the buffer
	dispatch_semaphore_wait(semEmpty, DISPATCH_TIME_FOREVER); // down Empty slots, stop when semEmpty == 0 (full)
        pthread_mutex_lock(&mutexBuffer);
        buffer[count] = x;
        count++;
        pthread_mutex_unlock(&mutexBuffer);
	dispatch_semaphore_signal(semFull); // up
    }
}

void* consumer(void* args) {
    while (1) {
        int y;

        // Remove from the buffer
	dispatch_semaphore_wait(semFull, DISPATCH_TIME_FOREVER); // down Full slots, stop whe semFull == 0 (empty)
        pthread_mutex_lock(&mutexBuffer);
        y = buffer[count - 1];
        count--;
        pthread_mutex_unlock(&mutexBuffer);
	dispatch_semaphore_signal(semEmpty);

        // Consume
        printf("Got %d\n", y);
        sleep(1);
    }
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    pthread_t th[THREAD_NUM];
    pthread_mutex_init(&mutexBuffer, NULL);
    semEmpty = dispatch_semaphore_create(10);
    semFull = dispatch_semaphore_create(0);
    int i;
    for (i = 0; i < THREAD_NUM; i++) {
        if (i > 0) {
            if (pthread_create(&th[i], NULL, &producer, NULL) != 0) {
                perror("Failed to create thread");
            }
        } else {
            if (pthread_create(&th[i], NULL, &consumer, NULL) != 0) {
                perror("Failed to create thread");
            }
        }
    }
    for (i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(th[i], NULL) != 0) {
            perror("Failed to join thread");
        }
    }
    dispatch_release(semEmpty);
    dispatch_release(semFull);
    pthread_mutex_destroy(&mutexBuffer);
    return 0;
}
