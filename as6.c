/*
 * James Dempsey - jed117
 * EECS Assignment 6
 * Baboon Crossing Using System V Sempaphores/Shared Memory
 * Adapted from the 2015 HW Solution on course webesite
 * Extensive re-usage of code from George Hodulik - gmh73@case.edu
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// This serves as both the key for the semaphores and the shared memory
// (the same key fits two different locks)
#define SEMAPHORE_KEY        0xFA2B

// The position of the various semaphores that we are using in the
// "semaphore array" that semget gets
#define SEMAPHORE_MUTEX      0
#define SEMAPHORE_AtoB	 1
#define SEMAPHORE_BtoA  2
#define NUMBER_OF_SEMAPHORES 3

// To distinguish baboon crossing to A from baboon corssing to B when forking
#define BABOON_TOA 	  1
#define BABOON_TOB 	  2

// The amount of loop iterations to stall between making baboon processes
#define BABOON_CREATE_STALL_TIME	7000
// The amount of loop iterations to stall when crossing the rope
#define CROSS_ROPE_STALL_TIME	100000

// Set to a positive nonzero int if you want a more detailed output, 0 or negative otherwise
#define PRINT_DEBUG_INFO 1

// Function for making/forking a new baboon process
void baboon_fork(int direction_a_or_b);
// What a BaboonToA executes
void toAProcess();
// What an BaboonToB executes
void toBProcess();

// Semaphore functions
void semaphore_wait(int semid, int semnumber);
void semaphore_signal(int semid, int semnumber);
int create_semaphore(int value);

// A staller function that runs an empty for loop iterations times
void stall(int iterations);

int get_semid(key_t semkey);
int get_shmid(key_t shmkey);

// This union is required by semctl(2)
// http://linux.die.net/man/2/semctl
union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                (Linux-specific) */
};

// Shared memory struct to store XingCount, XedCount, toBWaitCount, toAWaitCount, and XingDirection
struct shared_variable_struct {
	int XingCount;
	int XedCount;
	int toBWaitCount;
	int toAWaitCount;
	enum {None, DirToB, DirToA} XingDirection;
};

// A function to aid in debugging -- not fully necessary
void debug_print_shared(struct shared_variable_struct *shared);

// Main
int main(int argc, char *argv[]) {
	// For reference, give the user our PID
	printf("*** Hello World! I am %d.\n", getpid());

	// The input of the executable file is of the for eewwweeewwee (not case-sensitive) where e is an east bound car and w is a west bound car
	if (argc != 2) {
		printf("!!! PID: %d: Please run this program with one argument, the order of the baboons crossing to a or b that you want to cross the rope.\n", getpid());
		exit(EXIT_FAILURE);
	}

	// Tell the user that their argument has been accepted
 	printf("*** PID: %d: I have been called with the following argument: `%s`.\n", getpid(), argv[1]);

	// Let's try to create some semaphores
	union semun semaphore_values;

	// Set up our semaphore values according to Tekin's solutions
	unsigned short semaphore_init_values[NUMBER_OF_SEMAPHORES];
	semaphore_init_values[SEMAPHORE_MUTEX] = 1;
	semaphore_init_values[SEMAPHORE_AtoB] = 0;
	semaphore_init_values[SEMAPHORE_BtoA] = 0;
	semaphore_values.array = semaphore_init_values;

	// Actually make the semaphores now
	int semid = get_semid((key_t)SEMAPHORE_KEY);
	if (semctl(semid, SEMAPHORE_MUTEX, SETALL, semaphore_values) == -1) {
		perror("semctl failed");
		exit(EXIT_FAILURE);
	}

	// Shared Memory
	int shmid = get_shmid((key_t)SEMAPHORE_KEY);
	struct shared_variable_struct * shared_variables = shmat(shmid, 0, 0);

	//Set the initial values of the shared memory
	shared_variables->XingCount = 0;
	shared_variables->XedCount = 0;
	shared_variables->toBWaitCount = 0;
	shared_variables->toAWaitCount = 0;
	shared_variables->XingDirection = None;

	// Go through each character and make a new baboon process based on the input
	int i = 0;
	while(argv[1][i] != 0) {
		switch (argv[1][i]) {
			case 'w':
			case 'W':
				baboon_fork(BABOON_TOA);
				break;

			case 'e':
			case 'E':
				baboon_fork(BABOON_TOB);
				break;

			default:
				printf("!!! PID: %d: Invalid argument: `%c`! The only valid arguments are 'w', 'W', 'e', and 'E'.\n", getpid(), argv[1][i]);
				exit(EXIT_FAILURE);
				break;

		}
		//Stall between each baboon creation (stall length is a bit less than the time it takes to cross the bridge)
		stall(BABOON_CREATE_STALL_TIME);
		i++;

	}

	// We need to wait until all the car process exit
	int j;
	for (j = 0; j < i; j++) {
		wait(NULL);
	}

	// And when they are done,
	printf("Baboons have crossed without killing eachother. Great Success!\n");

	// We need to clean up after ourselves
	if (shmdt(shared_variables) == -1) {
		perror("shmdt failed");
		exit(EXIT_FAILURE);
	}

	if (shmctl(shmid, IPC_RMID, NULL) < 0) {
		perror("shmctrl failed");
		exit(EXIT_FAILURE);
	}

	if (semctl(semid, SEMAPHORE_MUTEX, IPC_RMID, semaphore_values) == -1) {
		perror("semctl failed");
		exit(EXIT_FAILURE);
	}
}

// Fork a new baboon to wanting to cross to a or b process
void baboon_fork(int direction_a_or_b) {
	pid_t child_pid;
	child_pid = fork();
	if (child_pid == -1) {
		perror("Fork Failed");
		exit(EXIT_FAILURE);
	} else if (child_pid == 0) {
		// Child
		if (direction_a_or_b == BABOON_TOB) {
			toBProcess();
		} else if (direction_a_or_b == BABOON_TOA) {
			toAProcess();
		} else {
			printf("!!! Invalid Car Type!\n");
			exit(EXIT_FAILURE);
		}
	} else {
		// Parent
		return;
	}
}

// Print all of the shared variables if we are in debug mode
void debug_print_shared(struct shared_variable_struct *shared){
	if(PRINT_DEBUG_INFO > 0){
		int XingCount;
		int XedCount;
		int toBWaitCount;
		int toAWaitCount;
		enum {None, DirToB, DirToA} XingDirection;

		XingCount = shared->XingCount;
		XedCount = shared->XedCount;
		toBWaitCount = shared->toBWaitCount;
		toAWaitCount = shared->toAWaitCount;
		XingDirection = shared->XingDirection;

		printf("\t Shared Variables status at PID %d: XingCount = %d, XedCount = %d, toBWaitCount = %d, toAWaitCount = %d, Dir = %d\n",
				getpid(), XingCount, XedCount, toBWaitCount, toAWaitCount, XingDirection);
	}
}

void toBProcess(void) {
	printf("*** PID: %d: I am an East Bound Car!\n", getpid());

	// Get the semaphores and shared memory
	int semid = get_semid((key_t)SEMAPHORE_KEY);
	int shmid = get_shmid((key_t)SEMAPHORE_KEY);
	struct shared_variable_struct * shared_variables = shmat(shmid, 0, 0);

	//Implementing algorithm in solutions "verbatim", with some minor changes I have noted with *** in comments
	printf("--- PID: %d: E: Waiting on Mutex.\n", getpid());
	semaphore_wait(semid, SEMAPHORE_MUTEX);
	printf("--- PID: %d: E: Passed Mutex.\n", getpid());

	if ((shared_variables->XingDirection == DirToB ||
			shared_variables->XingDirection == None) &&
			shared_variables->XingCount < 4 &&
			// *** I added that even if 5 East cars have gone, this car may still go if no west bound ones are waiting
			(shared_variables->XedCount + shared_variables->XingCount < 5 || shared_variables->toAWaitCount == 0) &&
			// *** We also need to check if any EastBoundCars are waiting, because if they are, this car should wait behind them, not skip them all
			shared_variables->toBWaitCount == 0) {
		printf("--- PID: %d: E: It's my turn -- Going to Cross.\n", getpid());
		shared_variables->XingDirection = DirToB;
		shared_variables->XingCount++;

		debug_print_shared(shared_variables);
		printf("--- PID: %d: E: Signaling MUTEX.\n", getpid());
		semaphore_signal(semid, SEMAPHORE_MUTEX);
	} else {
		printf("--- PID: %d: E: I have to wait.\n", getpid());
		shared_variables->toBWaitCount++;
		debug_print_shared(shared_variables);
		printf("--- PID: %d: E: Signaling Mutex.\n", getpid());
		semaphore_signal(semid, SEMAPHORE_MUTEX);
		semaphore_wait(semid, SEMAPHORE_BtoA);
		printf("--- PID: %d: E: Was waiting, now I'm signaled.\n", getpid());
		shared_variables->toBWaitCount--;
		shared_variables->XingCount++;
		shared_variables->XingDirection = DirToB;

		debug_print_shared(shared_variables);

		// *** I also have cars here check to see if anyone is waiting behind them, and signal those behind them
		// *** This is necessary because otherwise only one car would be on the bridge at a time.
		if(shared_variables->toBWaitCount > 0 && shared_variables->XingCount < 4 &&
			(shared_variables->XedCount + shared_variables->XingCount < 5 || shared_variables->toAWaitCount == 0)){
			printf("--- PID: %d: E: Signaling an East car behind me.\n", getpid());
			printf("--- PID: %d: E: Going to cross.\n", getpid());
			semaphore_signal(semid, SEMAPHORE_BtoA);
			// *** We do not need to signal mutex because it is "passed on" to the signaled car behind this one
		}else{
			printf("--- PID: %d: E: Going to cross.\n", getpid());
			debug_print_shared(shared_variables);
			printf("--- PID: %d: E: Signaling MUTEX.\n", getpid());
			semaphore_signal(semid, SEMAPHORE_MUTEX);
		}
	}

	printf("@@@ PID: %d: E: I am crossing the bridge!.\n", getpid());
	//It takes some time to cross the bridge
	stall(CROSS_ROPE_STALL_TIME);

	// Everything below is exactly the algorithm in the assignment 2 solution
	printf("@@@ PID: %d: E: Crossed the bridge -- Waiting for MUTEX!.\n", getpid());
	semaphore_wait(semid, SEMAPHORE_MUTEX);
	printf("@@@ PID: %d: E: Passed MUTEX!.\n", getpid());
	shared_variables->XedCount++;
	shared_variables->XingCount--;

	if(shared_variables->toBWaitCount != 0 &&
		(shared_variables->XingCount + shared_variables->XedCount < 5 ||
		   shared_variables->toAWaitCount == 0)){
		debug_print_shared(shared_variables);
		printf("@@@ PID: %d: E: Signaling a waiting east bound car!.\n", getpid());
		semaphore_signal(semid,SEMAPHORE_BtoA);
	}else if(shared_variables->XingCount == 0 && shared_variables->toAWaitCount != 0
				&& (shared_variables->toBWaitCount==0 ||
					shared_variables->XedCount + shared_variables->XingCount >= 5)){
		printf("@@@ PID: %d: E: Changing direction to west bound and signaling a waiting west car.\n", getpid());
		shared_variables->XingDirection = DirToA;
		shared_variables->XedCount = 0;
		debug_print_shared(shared_variables);
		semaphore_signal(semid, SEMAPHORE_AtoB);
	}else if(shared_variables->XingCount == 0 && shared_variables->toBWaitCount == 0
				&& shared_variables->toAWaitCount == 0){
		printf("@@@ PID: %d: E: Setting XingDirection to None -- no one is waiting.\n", getpid());
		shared_variables->XingDirection = None;
		shared_variables->XedCount = 0;

		debug_print_shared(shared_variables);
		printf("@@@ PID: %d: E: Signaling Mutex.\n", getpid());
		semaphore_signal(semid, SEMAPHORE_MUTEX);
	}else{
		debug_print_shared(shared_variables);
		printf("@@@ PID: %d: E: Signaling Mutex.\n", getpid());
		semaphore_signal(semid, SEMAPHORE_MUTEX);
	}

	if (shmdt(shared_variables) == -1) {
		perror("shmdt failed");
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}

void toAProcess(void) {
	printf("*** PID: %d: I am a West Bound Car!\n", getpid());

	// Get the semaphores and shared memory
	int semid = get_semid((key_t)SEMAPHORE_KEY);
	int shmid = get_shmid((key_t)SEMAPHORE_KEY);
	struct shared_variable_struct * shared_variables = shmat(shmid, 0, 0);

	//Implementing algorithm in solutions "verbatim", with some minor changes I have noted with *** in comments

	printf("--- PID: %d: W: Waiting on Mutex.\n", getpid());
	semaphore_wait(semid, SEMAPHORE_MUTEX);
	printf("--- PID: %d: W: Passed Mutex.\n", getpid());
	debug_print_shared(shared_variables);

	if ((shared_variables->XingDirection == DirToA ||
			shared_variables->XingDirection == None) &&
			shared_variables->XingCount < 4 &&
			// *** I added that even if 5 East cars have gone, this car may still go if no east bound ones are waiting
			(shared_variables->XedCount + shared_variables->XingCount < 5 || shared_variables->toBWaitCount == 0) &&
			// *** We also need to check if any WestBoundCars are waiting, because if they are, this car should wait behind them, not skip them all
			shared_variables->toAWaitCount == 0) {
		printf("--- PID: %d: W: It's my turn -- Going to Cross.\n", getpid());
		shared_variables->XingDirection = DirToA;
		shared_variables->XingCount++;
		debug_print_shared(shared_variables);
		printf("--- PID: %d: W: Signaling MUTEX.\n", getpid());
		semaphore_signal(semid, SEMAPHORE_MUTEX);
	} else {
		printf("--- PID: %d: W: I have to wait.\n", getpid());
		shared_variables->toAWaitCount++;
		debug_print_shared(shared_variables);
		printf("--- PID: %d: W: Signaling Mutex.\n", getpid());
		semaphore_signal(semid, SEMAPHORE_MUTEX);
		semaphore_wait(semid, SEMAPHORE_AtoB);
		printf("--- PID: %d: W: Was waiting, now I'm signaled.\n", getpid());
		shared_variables->toAWaitCount--;
		shared_variables->XingCount++;
		shared_variables->XingDirection = DirToA;

		debug_print_shared(shared_variables);

		// *** I also have cars here check to see if anyone is waiting behind them, and signal those behind them
		// *** This is necessary because otherwise only one car would be on the bridge at a time.
		if(shared_variables->toAWaitCount > 0 && shared_variables->XingCount < 4 &&
			(shared_variables->XedCount + shared_variables->XingCount < 5 || shared_variables->toBWaitCount == 0)){
			printf("--- PID: %d: W: Going to cross.\n", getpid());
			printf("--- PID: %d: W: Signalling a West car behind me.\n", getpid());
			semaphore_signal(semid, SEMAPHORE_AtoB);
			// *** We do not need to signal mutex because it is "passed on" to the signaled car behind this one
		}else{
			printf("--- PID: %d: W: Going to cross.\n", getpid());
			debug_print_shared(shared_variables);
			printf("--- PID: %d: W: Signaling MUTEX.\n", getpid());
			semaphore_signal(semid, SEMAPHORE_MUTEX);
		}
	}

	printf("@@@ PID: %d: W: I am crossing the bridge!.\n", getpid());
	//It takes some time to cross the bridge
	stall(CROSS_ROPE_STALL_TIME);

	//Everything here and below is the same as the solution to assignment 2
	printf("@@@ PID: %d: W: Crossed -- Waiting for MUTEX!.\n", getpid());
	semaphore_wait(semid, SEMAPHORE_MUTEX);

	printf("@@@ PID: %d: W: Passed MUTEX!.\n", getpid());
	shared_variables->XedCount++;
	shared_variables->XingCount--;

	if(shared_variables->toAWaitCount != 0 &&
		(shared_variables->XingCount + shared_variables->XedCount < 5 ||
		   shared_variables->toBWaitCount == 0)){
		debug_print_shared(shared_variables);
		printf("@@@ PID: %d: W: Signaling a waiting west bound car!.\n", getpid());
		semaphore_signal(semid,SEMAPHORE_AtoB);
	}else if(shared_variables->XingCount == 0 && shared_variables->toBWaitCount != 0
				&& (shared_variables->toAWaitCount==0 ||
					shared_variables->XedCount + shared_variables->XingCount >= 5)){
		printf("@@@ PID: %d: W: Changing direction to east.\n", getpid());
		shared_variables->XingDirection = DirToB;
		shared_variables->XedCount = 0;
		debug_print_shared(shared_variables);
		printf("@@@ PID: %d: W: Signaling a waiting east car.\n", getpid());
		semaphore_signal(semid, SEMAPHORE_BtoA);
	}else if(shared_variables->XingCount == 0 && shared_variables->toBWaitCount == 0
				&& shared_variables->toAWaitCount == 0){
		printf("@@@ PID: %d: W: Setting XingDirection to None -- no one is waiting.\n", getpid());
		shared_variables->XingDirection = None;
		shared_variables->XedCount = 0;

		debug_print_shared(shared_variables);
		printf("@@@ PID: %d: W: Signaling Mutex.\n", getpid());
		semaphore_signal(semid, SEMAPHORE_MUTEX);
	}else{
		debug_print_shared(shared_variables);
		printf("@@@ PID: %d: W: Signaling Mutex.\n", getpid());
		semaphore_signal(semid, SEMAPHORE_MUTEX);
	}
	if (shmdt(shared_variables) == -1) {
		perror("shmdt failed");
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}

// Just stall with an empty for loop
void stall(int iterations){
	int i;
	for(i = 0; i < iterations; i++){
		;
	}
}

// These two functions are wrapper functions for the System-V
// style semaphores that were talked about in class.
// They implement semaphore wait and signal functions as discussed in class.
// Used in 2013 Assignment 5 solutions.
void semaphore_wait(int semid, int semnumber) {
	// declare a sembuf
	struct sembuf wait_buffer;
	// We will perfom an operation on the semnumber semaphore of semid
	wait_buffer.sem_num = semnumber;
	//We will subtract 1 from the semaphore
	wait_buffer.sem_op = -1;
	wait_buffer.sem_flg = 0;
	// Perform the semaphore operation and check for errors
	if (semop(semid, &wait_buffer, 1) == -1)  {
		perror("semaphore_wait failed");
		exit(EXIT_FAILURE);
	}
}

void semaphore_signal(int semid, int semnumber) {
	// declare a sembuf
	struct sembuf signal_buffer;
	// We will perform an operation on the semnumber semaphore of semid
	signal_buffer.sem_num = semnumber;
	//We will add 1 to the semaphore
	signal_buffer.sem_op = 1;
	signal_buffer.sem_flg = 0;
	// Perform the semaphore operation and check for errors
	if (semop(semid, &signal_buffer, 1) == -1)  {
		perror("semaphore_signal failed");
		exit(EXIT_FAILURE);
	}
}

// Small wrapper functions to convert the keys of the shared memory
// and the semaphores to values.
// Used in 2013 Assignment 5 solutions.
int get_semid(key_t semkey) {
	int value = semget(semkey, NUMBER_OF_SEMAPHORES, 0777 | IPC_CREAT);
	if (value == -1) {
		perror("semget failed");
		exit(EXIT_FAILURE);
	}
	return value;
}

int get_shmid(key_t shmkey) {
	int value = shmget(shmkey, sizeof(struct shared_variable_struct), 0777 | IPC_CREAT);
	if (value == -1) {
		perror("shmkey failed");
		exit(EXIT_FAILURE);
	}
	return value;
}
