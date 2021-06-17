#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

int x = 2;

// threads shared memory
void* routine() {
    x++;
    printf("Test from threads %d\n", getpid());
    sleep(3);
    printf("The value of x is %d\n", x);
}

void* routine2() {
    printf("Test from threads %d\n", getpid());
    sleep(3);
    printf("The value of x is %d\n", x);
}

int main(int argc, char* argv[]) {
    pthread_t t1, t2;
    // pthread_create 2nd parameter is the property of this thread
    // 4th parameter is the arguments of function(3rd parameter)
    if (pthread_create(&t1, NULL, &routine, NULL) != 0) return 1;
    if (pthread_create(&t2, NULL, &routine2, NULL) != 0) return 2;

    // pthread_join is used for waiting the return of the thread (otherwise the thread will not be executed)
    // the 2nd parameter is the addr of storing the return value
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return 0;
}
