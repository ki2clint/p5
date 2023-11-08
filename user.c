#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <sys/sem.h>

#define TERMINATION_CHECK_SEC 1
#define SLEEP_TIME_LOWER_BOUND 1000 // 1 millisecond
#define SLEEP_TIME_UPPER_BOUND 500000 // 500 milliseconds
#define TERMINATION_PROBABILITY 5 // 1 in 5 chance to terminate
#define REQUEST_PROBABILITY 2 // 1 in 2 chance to request resources
#define RELEASE_PROBABILITY 3 // 1 in 3 chance to release resources

void generateResourceRelease(ProcessControlBlock *pcb, int *release);
void generateResourceRequest(ProcessControlBlock *pcb, int *request);
int waitForAcknowledgment(int msgid, int process_id);


int shmid;
//int semid;

SharedData *shared_data;
ProcessControlBlock *pcb;
LogicalClock *system_clock;

void attachToSharedMemory() {
        shared_data = (SharedData *)shmat(shmid, NULL, 0);
        if (shared_data == (void *)-1) {
                perror("shmat");
                exit(1);
        }
        system_clock = &shared_data->clock;
}

void generateMaximumClaims(ProcessControlBlock *pcb) {
        for (int i = 0; i < MAX_RESOURCES; i++) {
                pcb->max_claims[i] = rand() % (shared_data->resources[i].total + 1);
        }
}

bool shouldTerminate(ProcessControlBlock *pcb, LogicalClock *system_clock) {
        if (system_clock->seconds - pcb->start_time.seconds >= 1) {
                if (rand() % 5 == 0) {
                        return true;
                }
        }
        return false;
}

bool shouldRequestResources(ProcessControlBlock *pcb) {
        return rand() % 2 == 0;
}

bool shouldReleaseResources(ProcessControlBlock *pcb) {
        return rand() % 3 == 0;
}

void sendTerminationMessage(int msgid, int process_index) {
        ResourceMessage msg;
        msg.mtype = TERMINATE_PROCESS;
        msg.process_id = process_index;

        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("msgsnd");
                exit(1);
        }
}


void semaphoreWait(int semid) {
        struct sembuf sop;
        sop.sem_num = 0;
        sop.sem_op = -1;
        sop.sem_flg = 0;
        if (semop(semid, &sop, 1) == -1) {
                perror("semop - semaphoreWait");
                exit(EXIT_FAILURE);
        }
}

void semaphoreSignal(int semid) {
        struct sembuf sop;
        sop.sem_num = 0;
        sop.sem_op = 1;
        sop.sem_flg = 0;
        if (semop(semid, &sop, 1) == -1) {
                perror("semop - semaphoreSignal");
                exit(EXIT_FAILURE);
        }
}


void sendResourceRequest(int msgid, int process_id, int *resource_request, int semid) {
        ResourceMessage msg;
        msg.mtype = REQUEST_RESOURCE;
        msg.process_id = process_id;
        for (int i = 0; i < MAX_RESOURCES; i++) {
                msg.request[i] = resource_request[i];
        }

        semaphoreWait(semid);

        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("msgsnd");
                exit(1);
        }

        semaphoreSignal(semid);
}

void sendResourceRelease(int msgid, int process_id, int *resource_release) {
        ResourceMessage msg;
        msg.mtype = RELEASE_RESOURCE;
        msg.process_id = process_id;
        for (int i = 0; i < MAX_RESOURCES; i++) {
                msg.request[i] = resource_release[i];
        }

        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("msgsnd");
                exit(1);
        }
}

void updateLogicalClock(ProcessControlBlock *pcb, int semid) {
        semaphoreWait(semid);
        pcb->local_clock.nanoseconds += 100000;
        if (pcb->local_clock.nanoseconds >= 1000000000) {
                pcb->local_clock.nanoseconds -= 1000000000;
                pcb->local_clock.seconds += 1;
        semaphoreSignal(semid);
        }
}

void generateResourceRequest(ProcessControlBlock *pcb, int *request) {
        for (int i = 0; i < MAX_RESOURCES; i++) {
                int max_possible_request = pcb->max_claims[i] - pcb->allocated[i];
                request[i] = rand() % (max_possible_request + 1);
        }
}


void generateResourceRelease(ProcessControlBlock *pcb, int *release) {
        for (int i = 0; i < MAX_RESOURCES; i++) {
                release[i] = rand() % (pcb->allocated[i] + 1);
        }
}

void performUserProcessActions(ProcessControlBlock *pcb, int msgid, int semid) {
        int resource_request[MAX_RESOURCES] = {0};
        int resource_release[MAX_RESOURCES] = {0};

        semaphoreWait(semid);

        if (shouldRequestResources(pcb)) {
                generateResourceRequest(pcb, resource_request);
                sendResourceRequest(msgid, pcb->pid_index, resource_request, semid);
                waitForAcknowledgment(msgid, pcb->pid_index);
        }

        if (shouldReleaseResources(pcb)) {
                generateResourceRelease(pcb, resource_release);
                sendResourceRelease(msgid, pcb->pid_index, resource_release);
                waitForAcknowledgment(msgid, pcb->pid_index);
        }

        updateLogicalClock(pcb, semid);
        semaphoreSignal(semid);
}

int waitForAcknowledgment(int msgid, int process_id) {
        ResourceMessage msg;
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), process_id + 1, 0) == -1) {
                perror("msgrcv");
                return -1;
        }
        return 0;
}

int main(int argc, char *argv[]) {
        srand(time(NULL) ^ (getpid()<<16));
        if (argc != 5) {
                fprintf(stderr, "Usage: %s msgid process_index msgid\n", argv[0]);
                return 1;
        }

        //int shmid = atoi(argv[1]);
        int process_index = atoi(argv[2]);
        int msgid = atoi(argv[3]);
        int semid = atoi(argv[4]);

        attachToSharedMemory();

        pcb = &shared_data->pcbs[process_index];

        generateMaximumClaims(pcb);

        while (!shouldTerminate(pcb, &shared_data->clock)) {
                performUserProcessActions(pcb, msgid, semid);
                usleep(rand() % 500000 + 1000); // Sleep for 1 to 500 milliseconds
        }

        sendTerminationMessage(msgid, process_index);

        if (shmdt(shared_data) == -1) {
                perror("shmdt");
                return 1;
        }

        return 0;
}
