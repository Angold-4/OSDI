#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

int mails = 0;
int turn = 0;

void* routine1() {
    for (int i = 0; i < 1000000; i++) {
	while(turn != 0); // busy waiting
	mails++;
	turn = 1;
    }
}

void* routine2() {
    for (int i = 0; i < 1000000; i++) {
	while(turn != 1); // busy waiting
	mails++;
	turn = 0;
    }
}

int main(int argc, char* argv[]) {
    pthread_t p1, p2;
    if (pthread_create(&p1, NULL, &routine1, NULL) != 0) {
	return 1;
    }
    if (pthread_create(&p2, NULL, &routine2, NULL) != 0) {
	return 2;
    }

    // waiting for threads finish
    pthread_join(p1, NULL);
    pthread_join(p2, NULL);

    // race condition
    printf("Number of mails: %d\n", mails);  // Number of mails: 2000000

    return 0;
}

