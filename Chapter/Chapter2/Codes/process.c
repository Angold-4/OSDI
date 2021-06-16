
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

int main(int argc, char* argv[]) {
    int x = 2;
    int pid = fork();
    if (pid == -1) {
	return -1;
    }

    if (pid == 0) {
	x++;
    }
    printf("Process id %d\n", getpid());
    printf("x is equal to %d\n", x);
    if (pid != 0) {
	wait(NULL);
    }
    return 0;
}
