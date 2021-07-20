## Operating Systerms Design and Implementation Notes

# 3. Semaphore and IPC Problems
##### By Jiawei Wang

Before we take a further step into **Semaphore**, let's make a conclusion of the [Prev Note](https://github.com/Angold-4/OSDI/blob/master/Chapter/Chapter2/2Communication.md):<br>
1. The main difference between normal **strict alternation** and **condition variables** to avoid race conditions is that:<br>
**Condition variables make the waiting process blocked rather than busy waiting.**<br>Which makes the code more efficient and also can prevent more unknown errors.<br>

2. In the [prev note](https://github.com/Angold-4/OSDI/blob/master/Chapter/Chapter2/2Communication.md). We use **Mutex** to prevent chaos, and using **Condition Variable** to guarantee sequences.

In this note. First we will introduce **Semaphore**. Then we will use this new technique to solve two classical **IPC problems**.<br>
<br>

## 1. Semaphore
This was the situation until E. W. Dtra (1965) suggested **using an integer variable to count the number of wakeups saved for future use**. In his proposal, a new variable type, called a **semaphore**, was introduced.

In my opinion: **Semaphore** is a combination of both **Mutex** and **Condition Variables**:<br>
* A semaphore could have the value 0, indicating that no wakeups were saved, or some positive value if one or more wakeups were **pending**.<br>

* We can do both **up** and **down** operations with semaphore.<br>

    1. The **down operation** on a semaphore checks to see if the value is greater than 0. If so, it decrements the value and just continues. If the value is 0, the process is put to sleep without completing the down for the moment.

    2. The **up operation** increments the value of the semaphore addressed. If one or more processes were sleeping on that semaphore, unable to complete an earlier down operation, one of them is chosen by the system and is allowed to complete its down.

    3. One important thing to notice is that: **Both operations of incrementing the semaphore and waking up one process is indivisible, so as decreasing and sleep a process**

**Example: [Codes/semaphore.c](Codes/semaphore.c)**
```c
#include <dispatch/dispatch.h> // use dispatch intead of semaphore on MacOSX (deprecated)

#define THREAD_NUM 4

dispatch_semaphore_t semaphore;

void* routine(void* args) {
    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);    // down
    sleep(1);
    printf("Hello from thread %d\n", *(int*)args);
    dispatch_semaphore_signal(semaphore);    // up
    free(args);
}

int main(int argc, char *argv[]) {
    pthread_t th[THREAD_NUM];
    semaphore = dispatch_semaphore_create(2); // only allow two threads execute at the same time
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
```
**Output:**<br>
```
❯ ./a.out
Hello from thread 0
Hello from thread 2
(sleep one sec)
Hello from thread 1
Hello from thread 3
```

In this example: <br>we assign two to the `semaphore` variable -- Only two threads are allowed to execute at any moments.

If you want to practice one more example of semaphore, you can check [Codes/semaphores.c](Codes/semaphores.c).<br>Which gives us a **Log In Model** in real life. and the server can only deal with 4 users at the same time.

<br>

## 2. Classical IPC Problems

### 1. The Dining Philosophers Problem
![philosophers](Sources/philosophers.png)
```
Dinning Philosophers
Five philosophers are seated around a circular table.
Each philosopher has a plate of spaghetti. The spaghetti is so slippery that a philosopher needs two forks to eat it.
The life of a philosopher consists of alternate periods of eating and thinking.
When a philosopher gets hungry, she tries to acquire her left and right fork, one at a time, in either order.
If successful in acquiring two forks, she eats for a while, then puts down the forks and continues to think.
The key question is: can you write a program for each philosopher that does what it is supposed to do and never gets sucked 
```
```c
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
```
The solution presented in **[philosopher.c](Codes/philosopher.c)** is deadlock-free and allows the maximum parallelism for an arbitrary number of philosophers.


### 2. The Producer-Consumer Problem 
```
The producer-consumer problem (also known as the bounded buffer problem). 
Many processes share a common, fixed-size buffer. 
Some of them, the producer, puts information into the buffer, and the other one, the consumer, takes it out.
```
```c
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
```
