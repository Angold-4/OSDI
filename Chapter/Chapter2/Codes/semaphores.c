#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dispatch/dispatch.h> // use dispatch intead of semaphore on MacOSX (deprecated)

#define THREAD_NUM 16

dispatch_semaphore_t semaphore;


void* routine(void* args) {
    printf("(%d) Waiting in the login queue\n", *(int*)args);
    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    printf("(%d) Logged in\n", *(int*)args);
    sleep(rand() % 5 + 1);
    printf("(%d) Logged out\n", *(int*)args);
    dispatch_semaphore_signal(semaphore);
    free(args);
}

int main(int argc, char *argv[]) {
    pthread_t th[THREAD_NUM];
    semaphore = dispatch_semaphore_create(4);
    int i;
    for (i = 0; i < THREAD_NUM; i++) {
        int* a = malloc(sizeof(int));
        *a = i;
        if (pthread_create(&th[i], NULL, &routine, a) != 0) {
            perror("Failed to create thread");
        }
    }

    for (i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(th[i], NULL) != 0) {
            perror("Failed to join thread");
        }
    }
    dispatch_release(semaphore);
    return 0;
}