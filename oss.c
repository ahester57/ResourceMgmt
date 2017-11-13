/*
$Id: oss.c,v 1.3 2017/11/13 04:00:18 o1-hester Exp o1-hester $
$Date: 2017/11/13 04:00:18 $
$Revision: 1.3 $
$Log: oss.c,v $
Revision 1.3  2017/11/13 04:00:18  o1-hester
soon to be

Revision 1.2  2017/10/26 03:30:40  o1-hester
glad its over

Revision 1.1  2017/10/23 07:27:24  o1-hester
Initial revision

Revision 1.5  2017/10/11 20:32:12  o1-hester
turnin

Revision 1.4  2017/10/10 20:19:07  o1-hester
threading

Revision 1.3  2017/10/09 22:00:42  o1-hester
threading

Revision 1.2  2017/10/04 07:55:18  o1-hester
clock set up

Revision 1.1  2017/10/04 07:45:30  o1-hester
Initial revision

$Author: o1-hester $
*/
/* 	Austin Hester
 * 	10/25/17
 * 	CS 4760 - Operating Systems
 * 	Sanjiv Bhatia
 * 	University of Missouri - St. Louis
 * 	Operating System Simulator
 * 	Assignment 4 - Process Scheduling
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include "osstypes.h"
#include "ipchelper.h"
#include "sighandler.h"
#include "deadlock.h"
#define FILEPERMS (O_WRONLY | O_TRUNC | O_CREAT)

// id and operations for semaphores
int semid;
struct sembuf mutex[2];
struct sembuf msgwait[1];
struct sembuf dispatchwait[1];

// clock
void updateclock(oss_clock_t* clock);
void* systemclock(void* args);
// spawning children
long spawnchild();
void* logchildcreate(void* args);
// message listener
void* msgthread(void* args);
// initiators
int initsemaphores(int semid);
int initsighandlers();
oss_clock_t* initsharedclock(const int shmid);
resource_table* initsharedtable(const int shmid);
//print usage/error
void printusage();
void printopterr(const char optopt);

// oss main. spawns a number of children that do some stuff
int
main (int argc, char** argv)
{
	/*************** Get options  *****************/
	// use getopt_long for long-named options
	// --quiet/silent
	// --help/version
	// --timeout/FILENAME
	// fatal error on malloc return NULL
	int maxslaves = 5;
	int timeout = 69;
	char* fname = "default.log";
	opterr = 0;
	char c;
	while ((c = getopt(argc, argv, "hs:l:t:")) != -1)
	{
		switch(c)
		{
		case 'h':
			// -h => help
			printusage();
			return 0;
		case 's':
			// -s <max # children>
			maxslaves = atoi(optarg);	
			// increase this when you implement child limiter 
			if (maxslaves < 1 || maxslaves > 100) {
				char* m0 = "x must be from 1-100.";
				fprintf(stderr, "\t%s\n", m0);
				printusage();
				return 1;
			}
			break;
		case 'l':
			// -l <filename>
			if (isalpha(optarg[0])) {
				fname = optarg;
				break;
			}
			char* m0 = "Filename must start with a letter.";
			fprintf(stderr, "\t%s\n", m0);
			printusage();
			return 1;	
		case 't':
			// -t <timeout>
			timeout	= atoi(optarg);
			if (timeout < 1 || timeout > 1000) {
				char* m0 = "z must be from 1-1000.";
				fprintf(stderr,"\t%s\n", m0);
				printusage();
				return 1;
			}
			break;
		case '?':
			// unknown or misused option
			printopterr(optopt);
			printusage();
		return 1;
		default:
			return 1;
		}
	} // done getting options

	// seed random 
	struct timespec tm;
	clock_gettime(CLOCK_MONOTONIC, &tm);
	srand((unsigned)(tm.tv_sec ^ tm.tv_nsec ^ (tm.tv_nsec >> 31)));

	/************** Open log file ****************/
	int logf = open(fname, FILEPERMS, PERM);
	if (logf == -1) {
		perror("OSS: Could not open log file:");
		return 1;
	}

	// get keys from file
	key_t mkey, skey, shmkey, diskey;
	mkey = ftok(KEYPATH, MSG_ID);
	skey = ftok(KEYPATH, SEM_ID);
	shmkey = ftok(KEYPATH, SHM_ID);
	diskey = ftok(KEYPATH, SHM_ID*2);
	if (mkey == -1 || skey == -1 || shmkey == -1 || diskey == -1) {
		perror("OSS: Failed to retreive keys.");
		return 1;
	}

	/************** Set up signal handler ********/
	// handles SIGINT & SIGALRM
	if (initsighandlers() == -1) {
		perror("OSS: Failed to set up signal handlers.");
		return 1;
	}

	/************** Set up semaphore *************/
	/*	 contains 3 sems:
	 *	0 = child shared memory lock
	 *	1 = log file (oss) i/o lock 
	 *	2 = msg wait/signal 		     
	 *	3 = dispatch			*/
	semid = getsemid(skey, 4);
	if (semid == -1) {
		perror("OSS: Failed to create semaphore.");
		return 1;
	}	
	if (initsemaphores(semid) == -1) {
		perror("OSS: Failed to init semaphores.");
		return 1;
	}
	// mutex for log file
	setsembuf(mutex, 1, -1, 0);
	setsembuf(mutex+1, 1, 1, 0);
	// msgwait for message queues
	setsembuf(msgwait, 2, -1, 0);
	// for dispatching processes
	setsembuf(dispatchwait, 3, -1, 0);

	/************** Set up message queue *********/
	/* Messages are sent by child when they expire.
	 * Parent (oss) waits for child to signal on sem 2.
	 * After spawning original children, parent creates
	 * a thread to listen for new message. */
	int msgid = getmsgid(mkey);
	if (msgid == -1) {
		perror("OSS: Failed to create message queue.");
		return 1;
	}

	/************** Set up shared memory *********/
	// A pointer to type oss_clock_t is stored in shared memory. 
	// oss_clock_t contains: { int sec, int nsec }.
	int shmid = getclockshmid(shmkey);
	if (shmid == -1) {
		perror("OSS: Failed to create shared memory segment.");
		return 1;
	}	
	oss_clock_t* clock;
	clock = initsharedclock(shmid); 
	if (clock == (void*)-1) {
		perror("OSS: Failed to attach shared memory.");	
		return 1;
	}
	int tabid = gettableshmid(diskey);
	if (tabid == -1) {
		perror("OSS: Failed to create shared memory segment.");
		return 1;
	}	
	resource_table* table = initsharedtable(tabid);
	if (table == (void*)-1) {
		perror("OSS: Failed to attach shared memory.");	
		return 1;
	}

	/********* Process control block vector  *****/

	// begin timeout alarm
	alarm(timeout);

	/****************** Spawn Children ***********/
	// Up to 18 children are spawned at once here
	// Any more are created later in parent code block.
	long cpid = (long)getpid();
	int childcount = 0;
	int term = (maxslaves < 18) ? maxslaves : 18;
	while (childcount < term)
	{
		//addtoblock(makenewprocessblock());
		// spawn new child
		cpid = spawnchild(logf);
		childcount++;
	}
	
	// If master fails at spawning children
	if (cpid == -1) {
		perror("OSS: Failed to create child.");
		if (removeshmem(msgid, semid, shmid, clock) == -1) {
			perror("OSS: Failed to remove shared mem segment");
			return 1;
		}
	}


	//for (i = 0; i < MAXPROCESSES; i++) {
	//	fprintf(stderr, "%d\t", block[i].proc_id);
	//	fprintf(stderr, "Priority: %d\n", block[i].priority);
	//}
	//
	/***************** Parent *****************/
	/* The parent code block begins by starting the 
	 * internal clock thread followed by the
	 * message listening thread. These threads allow
	 * parent to wait for children without interrupting
	 * program flow.
	 * It then enters a loop which waits for a child to
	 * terminate, and spawns another one if necessary.
	 */
	if (cpid > 0) {
		// say when parent enters this section
		fprintf(stderr, "OSS: In parent code block.\n");
		dprintf(logf, "OSS: In parent code block.\n");
		/******* start system clock thread *****/
		// this way clock does not pause when waiting
		pthread_t ctid;
		int e;
		e = pthread_create(&ctid, NULL, systemclock, (void*)clock);
		if (e) {
			char* m0 = "Failed to create clock thread.";
			fprintf(stderr, "OSS: %s\n", m0);
			return 1;
		}
		/**** start message listener thread ****/
		// t_params contiain messageq id and logfile descriptor
		int t_params[2];
		t_params[0] = msgid;
		t_params[1] = logf;
		pthread_t tid;
		e = pthread_create(&tid, NULL, msgthread, (void*)t_params);
		if (e) {
			char* m0 = "Failed to create msg listener.";
			fprintf(stderr, "OSS: %s\n", m0);
			return 1;
		}

		childcount = 0;
		// The PARENT LOOP
		int nextuser = 0;
		while (childcount < 18)
		{
				
			if (childcount < 18) {
				// wait until next scheduled time
				while (clock->sec < nextuser){}
				int ran = (int)(rand()) % 1;
				nextuser += ran;
				if (ran == 0) {
					usleep(50000);
				}
				if (childcount == 18) {
					break;
				}
				childcount++;
			}

		}
		// Waits for all children to be done
		fprintf(stderr, "OSS: Waiting for children now.\n");
		while (wait(NULL))
		{
			if (errno == ECHILD)
				break;
		}
		/* wait until clock is finished to close everything
		 * with pthread_join( "clock thread id" );
		 * lets message thread deal with its queue */
		fprintf(stderr, "OSS: Stopping clock now.\n");
		int sp = clock->sec;
		pthread_cancel(ctid);
		
		// close message listening thread
		pthread_cancel(tid);

		// output stuff
		fprintf(stderr, "OSS: All children accounted for\n");
		dprintf(logf, "OSS: All children accounted for\n");
		fprintf(stderr, "OSS: Internal clock reached %ds.\n", sp);
		dprintf(logf, "OSS: Internal clock reached %ds.\n", sp);
		fprintf(stderr, "OSS: Message thread closed.\n");
		dprintf(logf, "OSS: Message thread closed.\n");
		fprintf(stderr, "OSS: Filename for log: %s\n", fname);
		dprintf(logf, "OSS: Filename for log: %s\n", fname);
		// close log file
		close(logf);
		// clean up IPC
		if (removeshmem(msgid, semid, shmid, clock) == -1) {
			// failed to remove shared mem segment
			perror("OSS: Failed to remove shared memory.");
			return 1;
		}

		fprintf(stderr, "OSS: Done. %ld\n",(long)getpgid(getpid()));
	}	
	return 0;	
}


// updates internal system clock
void
updateclock(oss_clock_t* clock)
{
	if (clock == NULL)
		return;
	clock->nsec += 25;
	if (clock->nsec >= BILLION) {
		clock->sec += 1;
		clock->nsec = 0;
	}
	return;
}

// thread for udpating system clock
void*
systemclock(void* args)
{
	oss_clock_t* clock = (oss_clock_t*)(args);
	if (clock == NULL)
		return NULL;
	while (clock->sec < 10000)
	{
		if (clock == NULL)
			return NULL;
		pthread_testcancel();
		updateclock(clock);			
		if (clock == NULL)
			return NULL;
	}
	return NULL;
}

// spawns a new child, return childpid, -1 on error
// takes logfile descripter
long
spawnchild(int logfile)
{
	int e;
	long cpid = fork();
	/***************** Child ******************/
	if (cpid <= 0) {
		// execute child
		execl("./user", "user", (char*)NULL);
		perror("OSS: Exec failure.");
		return -1; // if error
	}
	// parent makes thread to write to file
	// Logging done in thread due to mutual exclusion
	pthread_t ltid;
	long t_params[2];
	t_params[0] = cpid;
	t_params[1] = (long)logfile;
	// create thread, send t_params
	e = pthread_create(&ltid, NULL, logchildcreate, (void*)t_params);
	if (e) 
		perror("OSS: Failed to create child log thread.");
	e = pthread_join(ltid, NULL); // w/o this shit gets crazy
	if (e)
		perror("OSS: Failed to wait for child log thread.");
	return cpid;
}

// thread for logging child creation
// logfile protected by semaphores
// takes void* as arg
void*
logchildcreate(void* args)
{
	long childpid = *((long*)(args));
	long logf = *((long*)(args)+1);
	pthread_testcancel(); // testcancel sets possible cancellation pts

	// wait until your turn
	if (semop(semid, mutex, 1) == -1) {
		perror("C_LOG: Failed to lock logfile.");
		pthread_exit(NULL);
		return NULL;
	}

	// CRITICAL SECTION
	fprintf(stderr, "C_LOG: ... child %ld spawned.\n", childpid);
	dprintf(logf, "C_LOG: ... child %ld spawned.\n", childpid);
	//fprintf(stderr, "T_SPAWN: %ld\t%ld\n", childpid, logf);

	// unlock file
	if (semop(semid, mutex+1, 1) == -1) { 		
		perror("C_LOG: Failed to unlock logfile.");
		pthread_exit(NULL);	
		return NULL;
	}

	pthread_testcancel();
	return NULL;
}

// thread to wait for and recieve messages from child
// logfile protected by semaphores
// takes a void* as arg
void*
msgthread(void* args)
{
	int msgid = *((int*)(args));
	int logf = *((int*)(args)+1);
	pthread_testcancel();
	while (69)
	{
		pthread_testcancel();
		// wait until signaled by child 
		
		if (semop(semid, msgwait, 1) == -1) {
			perror("MSGTHREAD: Failed to wait for message.");
			return NULL;
		}
		
		// sleep for 1000 microseconds, gives spawnLOG more time
		usleep(1000);

		// wait until your turn
		
		if (semop(semid, mutex, 1) == -1) {
			perror("MSGTHREAD: Failed to lock logfile.");
			//pthread_exit(NULL);
			return NULL;
		}
		

		// CRITICAL SECTION
		mymsg_t msg;
		// retrieve message
		ssize_t sz = getmessage(msgid, &msg);
		if (sz != (ssize_t)-1) {
			char* m0 = "MSGTHREAD: ";
			char* m1 = msg.mtext;
			dprintf(logf, "%s%s\n",m0,m1);
			fprintf(stderr, "%s%s\n",m0,m1);
		} 


		// unlock file
		if (semop(semid, mutex+1, 1) == -1) { 		
			perror("MSGTHREAD: Failed to unlock logfile.");
			//pthread_exit(NULL);	
			return NULL;
		}
		
		pthread_testcancel();
	}
	return NULL;	
}

/***** NOTE about semaphores *****/
/* I do NOT log every time sem is waited on
 * and acquired. That would be ridiculous. Thanks. */ 

// initialize semaphores, return -1 on error
int
initsemaphores(int semid)
{
	// set up child shm read lock
	if (initelement(semid, 0, 1) == -1) {
		if (semctl(semid, 0, IPC_RMID) == -1)
			return -1;
		return -1;
	}
	// set up log file i/o lock
	if (initelement(semid, 1, 1) == -1) {
		if (semctl(semid, 0, IPC_RMID) == -1)
			return -1;
		return -1;
	}
	// set up message queue lock
	if (initelement(semid, 2, 1) == -1) {
		if (semctl(semid, 0, IPC_RMID) == -1)
			return -1;
		return -1;
	}
	// set up dispatch lock
	if (initelement(semid, 3, 0) == -1) {
		if (semctl(semid, 0, IPC_RMID) == -1)
			return -1;
		return -1;
	}
	return 0;
}

// initialize signal handlers, return -1 on error
int
initsighandlers()
{
	struct sigaction newact = {{0}};
	struct sigaction timer = {{0}};
	timer.sa_handler = handletimer;
	timer.sa_flags = 0;
	newact.sa_handler = catchctrlc;
	newact.sa_flags = 0;
	// set timer handler
	if ((sigemptyset(&timer.sa_mask) == -1) ||
	    (sigaction(SIGALRM, &timer, NULL) == -1)) {
		return -1;
	}	
	// set intr handler
	if ((sigemptyset(&newact.sa_mask) == -1) ||
	    (sigaction(SIGINT, &newact, NULL) == -1)) {
		return -1;
	}	
	return 0;
}

// initialize shared memory, return -1 on error
oss_clock_t*
initsharedclock(const int shmid)
{
	oss_clock_t* clock;
	clock = attachshmclock(shmid);
	if (clock == (void*)-1) {
		// failed to init shm
		return (void*)-1;
	}
	clock->sec = 0;
	clock->nsec = 0;
	return clock;
}

// initialize shared memory, return -1 on error
resource_table*
initsharedtable(const int shmid)
{
	resource_table* table;
	table = attachshmtable(shmid);
	if (table == (void*)-1) {
		// failed to init shm
		return (void*)-1;
	}
	int i,j;
	for (i = 0; i < NUMRESOURCES; i++) {
		resource_dt newrsc;
		for (j = 0; j < MAXPROCESSES; j++) {
			newrsc.requests[j] = -1;
			newrsc.allocation[j] = -1;
			newrsc.release[j] = -1;
		}
		if ( (int)rand() % 10 < 2 )
			newrsc.issharable = 1;
		else
			newrsc.issharable = 0;
		newrsc.instances = (int)rand() % 10 + 1;
		newrsc.available = newrsc.instances;
		table->table[i] = &newrsc;
	}
	return table;
}

// when the user dun goofs
void
printusage()
{
	fprintf(stderr, "\n\tUsage: ");
	fprintf(stderr, "./oss [-s x] [-l filename] ");
	fprintf(stderr, "[-t z]\n\n\tx: max # slave pxs [1-100]\n");
	fprintf(stderr, "\tfilename: of log\n");
	fprintf(stderr, "\tz: timeout (sec) [1-1000]\n\n");
	return;
}

// when the user dun goofs
void
printopterr(const char optopt)
{
	if (optopt == 's' || optopt == 'l' || optopt == 't')
		fprintf(stderr, "\tOption -%c requires an argument.\n", optopt);
	else if (isprint(optopt))
		fprintf(stderr, "\tInvalid option %c\n", optopt);
	return;
}

