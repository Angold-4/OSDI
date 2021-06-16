#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

int mails = 0;
pthread_mutex_t mutex;

void* routine() {
    for (int i = 0; i < 1000000; i++) {
	pthread_mutex_lock(&mutex);
	mails++; // any time, only one thread can run this instruction
	pthread_mutex_unlock(&mutex);
	// read mails:	
	// movl	_mails(%rip), %eax

	// increment
	// addl	$1, %eax
	
	// write mails
	// movl	%eax, _mails(%rip)
    }
}

int main(int argc, char* argv[]) {
    pthread_t p1, p2;
    pthread_mutex_init(&mutex, NULL); // deafult
    if (pthread_create(&p1, NULL, &routine, NULL) != 0) {
	return 1;
    }
    if (pthread_create(&p2, NULL, &routine, NULL) != 0) {
	return 2;
    }

    // waiting for threads finish
    pthread_join(p1, NULL);
    pthread_join(p2, NULL);

    pthread_mutex_destroy(&mutex);
    printf("Number of mails: %d\n", mails);  // Number of mails: 2000000

    return 0;
}
