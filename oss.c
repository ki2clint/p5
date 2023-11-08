#include "shared.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/msg.h>
#include <errno.h>
#include <string.h>

#define MAX_LOG_LINES 100000
#define MAX_RUNTIME 5

#define MAX_USER_PROCESSES 18
#define USER_PROCESS "./user"
#define MAX_WAIT_TIME 3
#define RESOURCE_RESPONSE 4
#define REQUEST_RESOURCE 1
#define RELEASE_RESOURCE 2
#define TERMINATE_PROCESS 3

int immediate_grants = 0;
int delayed_grants = 0;
int msgid;
int shmid;
int semid;
int active_children = 0;
void signal_handler(int sig);

SharedData *shared_data;

FILE *log_file = NULL;
int log_line_count = 0;
bool verbose_mode = false;  //true = verbose logging, false = verbose off

void setup_logging() {
        log_file = fopen(LOG_FILENAME, "w");
        if (log_file == NULL) {
                perror("Failed to open log file");
                exit(1);
        }
}

void close_logging() {
        if (log_file != NULL) {
                fclose(log_file);
                log_file = NULL;
        }
}
void log_message(const char *format, ...) {
        va_list args;
        va_start(args, format);
        if (log_line_count < MAX_LOG_LINES) {
                if(log_file != NULL) {
                vfprintf(log_file, format, args);
                fflush(log_file);
                log_line_count++;
                } else {
                        fprintf(stderr, "Log file is not op en!\n");
                }

                if (verbose_mode || log_line_count % 20 == 0) {
                        print_allocation_table(shared_data);
                }
        }
        va_end(args);
}

void print_allocation_table(SharedData *shared_data) {
        if (verbose_mode && log_line_count < MAX_LOG_LINES) {
                log_message("Current System Resources Table:\n");
                log_message("R0 R1 R2 R3 ...(up to # of resources)\n", shared_data);

                for (int i = 0; i < MAX_USER_PROCESSES; i++) {
                        if (shared_data->pcbs[i].pid != 0) {
                                fprintf(log_file, "P%d ", i);
                                for (int j = 0; j < MAX_RESOURCES; j++) {
                                        fprintf(log_file, "%d ", shared_data->pcbs[i].allocated[j]);
                                }
                                fprintf(log_file, "\n");
                                fflush(log_file);
                                log_line_count++;
                        }
                }
        }
}

void initializeProcessControlBlocks(SharedData* shared_data) {
        for (int i = 0; i < MAX_USER_PROCESSES; i++) {
                shared_data->pcbs[i].pid = 0;  // Initialize with 0, which means not in use
                for (int j = 0; j < MAX_RESOURCES; j++) {
                        shared_data->pcbs[i].max_claims[j] = rand() % (shared_data->resources[j].total + 1);
                        shared_data->pcbs[i].needs[j] = shared_data->pcbs[i].max_claims[j];
                        shared_data->pcbs[i].allocated[j] = 0;
                }
        }
}


int initSharedMemory(size_t size, int* shmid) {
        *shmid = shmget(SHM_KEY, size, IPC_CREAT | IPC_EXCL | 0666);
        if (*shmid == -1) {
                if(errno == EEXIST) {
                        *shmid = shmget(SHM_KEY, size, 0666);
                        if (*shmid == -1) {
                                perror("shmget");
                                return -1;
                        }

                        if (shmctl(*shmid, IPC_RMID, NULL) == -1) {
                                perror("shmctl");
                                return -1;
                        }
                        *shmid = shmget(SHM_KEY, size, IPC_CREAT | IPC_EXCL | 0666);
                        if (*shmid == -1) {
                                perror("shmget");
                                return -1;
                        }
                } else {
                        perror("shmget");
                        return -1;
                }
        }
        return 0;
}

void* attachSharedMemory(int shmid, int shmflg) {
        void* addr = shmat(shmid, NULL, shmflg);
        if (addr ==(void*) -1) {
                perror("shmat");
                return NULL;
        }
        return addr;
}

void detachSharedMemory(const void* shmaddr) {
        if (shmdt(shmaddr) == -1){
                perror("shmdt");
        }
}

void removeSharedMemory( int shmid) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1) {
                perror("shmctl");
        }
}

int initSemaphore(int* semid) {
        printf("Initializing semaphore...\n"); //debug
        *semid = semget(SEM_KEY, 1, IPC_CREAT | 0666);
        if (*semid == -1) {
                perror("semget");
                log_message("Failed to initialize semaphore, errno: %d\n", errno); //debug
                return -1;
        }
        printf("Semaphore initialization successful. Semaphore ID is %d.\n", *semid); //debug
        if (semctl(*semid, 0, SETVAL, 1) == -1) {
                perror("semctl");
                log_message("Failed to set semaphore value, errno: %d\n", errno);
                return -1;
        }
        return 0;
}

void removeSemaphore(int semid) {
        if(semctl(semid, 0, IPC_RMID) == -1) {
                perror("semctl");
        }
}

void semaphoreWait(int semid) {
        if (semid <= 0) {       //debug
                log_message("invalid semaphore id %d in semaphoreWait\n", semid); //debug
                return; //debug
        }               //debug
        log_message("Waiting on semaphore with ID %d\n", semid);  //debug
        struct sembuf sop = { .sem_num = 0, .sem_op = -1, .sem_flg = 0 };
        while (semop(semid, &sop, 1) == -1) {
                if (errno != EINTR) {
                        perror("semop - semaphoreWait");
                        log_message("Failed to wait on semaphore with ID %d, errno: %d\n", semid, errno);
                        exit(EXIT_FAILURE);
                }
        }
        log_message("Successfully waited on semaphore with ID %d\n", semid); //debug

}

void semaphoreSignal(int semid) {
        if (semid <= 0) { //debug
                log_message("invalid semaphore ID %d in semaphoreSignal\n", semid); //debug
                return; //debug
        }               //debug
        log_message("Signaling semaphore with ID %d\n", semid); //debug
        struct sembuf sop = { .sem_num = 0, .sem_op = 1, .sem_flg = 0 };
        while (semop(semid, &sop, 1) == -1) {
                if (errno != EINTR) {
                        perror("semop - semaphoreSignal");
                        log_message("Failed to signal semaphore with ID %d, errno: %d\n", semid, errno);
                        exit(EXIT_FAILURE);
                }
        }
        log_message("Successfully signaled semaphore with ID %d\n", semid); //debug
}

int initMessageQueue(int* msgid) {
        *msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
        if (*msgid == -1) {
                perror("msgget");
                return -1;
        }
        return 0;
}

void removeMessageQueue(int msgid) {
        if (msgctl(msgid, IPC_RMID, NULL) == -1) {
                perror("msgctl");
    }
}

void initializeResources(SharedData* shared_data, int semid) {
        log_message("Attempting to wait on semaphore with ID %d\n in initializeResources", semid); //debug
        semaphoreWait(semid);
        printf("Successfully waited on semaphore with ID %d\n", semid); //debug
        srand(time(NULL)); //seed the random number generator

        int shareableResourcesCount = MAX_RESOURCES / 5;
        for (int i = 0; i < MAX_RESOURCES; i++) {
                shared_data->resources[i].total = rand() % 10 + 1; // Assign random instances [1,10]
                shared_data->resources[i].available = shared_data->resources[i].total;
                shared_data->resources[i].shareable = (i < shareableResourcesCount);
                for (int j = 0; j < MAX_USER_PROCESSES; j++) {
                        shared_data->resources[i].allocated[j] = 0;
                }
        }
        log_message("About to signal semaphore with ID %d\n in initializeResources", semid); //debug
        semaphoreSignal(semid);
        log_message("Successfully signaled semaphore with ID %d\n in initializeResources", semid); //debug
}

void incrementLogicalClock(LogicalClock* clock, unsigned int sec, unsigned int ns) {
        clock->nanoseconds += ns;
        if (clock->nanoseconds >= 1000000000) {
                clock->seconds += 1;
                clock->nanoseconds -= 1000000000;
        }
        clock->seconds += sec;
}

//function to fork user processes
void forkUserProcesses(SharedData* shared_data, int* active_children, int shmid) {
        while (*active_children < MAX_USER_PROCESSES) {
                semaphoreWait(semid);

                pid_t pid = fork();
                if (pid == -1) {
                        perror("fork");
                        exit(EXIT_FAILURE);
                }

                if (pid == 0) {
                        char shmid_str[10];
                        char semid_str[10];
                        sprintf(shmid_str, "%d", shmid);
                        sprintf(semid_str, "%d", semid);
                        execl(USER_PROCESS, USER_PROCESS, shmid_str, semid_str, (char *)NULL);
                                perror("execl");
                                exit(1);
                } else { //parent process
                        //increment the number of active children
                        (*active_children)++;
                        incrementLogicalClock(&shared_data->clock, 0, rand() % 500000 + 1000);
                        semaphoreSignal(semid);
                }
        }
}

//check if the system is in a safe state
int check_safety(SharedData* shared_data) {
        int work[MAX_RESOURCES];
        int finish[MAX_USER_PROCESSES] = {0};
        int found, i, j;

        //initialize work with available resources
        for (i = 0; i < MAX_RESOURCES; i++) {
                work[i] = shared_data->resources[i].available;
        }

        do {
                found = 0;
                for (i = 0; i < MAX_USER_PROCESSES; i++) {
                        if (!finish[i]) {
                                for (j = 0; j < MAX_RESOURCES; j++) {
                                        if (shared_data->pcbs[i].needs[j] > work[j])
                                                break;
                                }
                                if (j == MAX_RESOURCES) { // If all needs are met
                                        for (j = 0; j < MAX_RESOURCES; j++)
                                                work[j] += shared_data->pcbs[i].allocated[j];
                                        finish[i] = 1;
                                        found = 1;
                                }
                        }
                }
        } while (found);

        for (i = 0; i < MAX_USER_PROCESSES; i++) {
                if (!finish[i]){
                        if(verbose_mode) {
                                char log_buffer[100];
                                snprintf(log_buffer, sizeof(log_buffer), "Process P%d is involved in a eadlock.\n", i);
                                log_message(log_buffer);
                        }
                        return 0;
                }
        }

        if (verbose_mode) {
                log_message("The system is in a safe state.\n");
        }
        return 1;
}

void release_resources(SharedData* shared_data, int process_index) {
        char log_buffer[256];
        snprintf(log_buffer, sizeof(log_buffer),
                "Master has detected Process P%d releasing resources at time %u:%u\n",
                process_index, shared_data->clock.seconds, shared_data->clock.nanoseconds);
        log_message(log_buffer);

        for (int i = 0; i < MAX_RESOURCES; i++) {
                shared_data->resources[i].available += shared_data->pcbs[process_index].allocated[i];
                shared_data->pcbs[process_index].allocated[i] = 0;
                shared_data->pcbs[process_index].needs[i] = shared_data->pcbs[process_index].max_claims[i];
        }
        snprintf(log_buffer, sizeof(log_buffer),
                "Process P%d has released its resources at time %u:%u\n",
                process_index, shared_data->clock.seconds, shared_data->clock.nanoseconds);
        log_message(log_buffer);
}

int send_acknowledgment(int msgid, long mtype, int process_id, int success) {
        ResourceMessage msg;
        msg.mtype = mtype;
        msg.process_id = process_id;
        msg.success = success; // 1 = granted, 0 = not granted

        printf("Sending message of type %ld to process %d\n", msg.mtype, msg.process_id);
        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("msgsnd");
                return -1;
        }
        printf("Message sent successfully\n");
        return 0;
}

void log_statistics() {
        if (log_line_count < MAX_LOG_LINES) {
                char stats_buffer[100];
                snprintf(stats_buffer, sizeof(stats_buffer),
                        "Immediate Grants: %d, Delayed Grants: %d\n",
                        immediate_grants, delayed_grants);
                log_message(stats_buffer);
        }
}

int request_resources(SharedData* shared_data, int process_index, int request[]) {
        char log_buffer[256];

        snprintf(log_buffer, sizeof(log_buffer),
                "Master has detected Process P%d requesting resources at time %u:%u\n",
                process_index, shared_data->clock.seconds, shared_data->clock.nanoseconds);
        log_message(log_buffer);

        snprintf(log_buffer, sizeof(log_buffer),
                "Master running deadlock detection at time %u:%u\n",
                shared_data->clock.seconds, shared_data->clock.nanoseconds);
        log_message(log_buffer);

        for (int i = 0; i < MAX_RESOURCES; i++) {
                if (request[i] > shared_data->pcbs[process_index].needs[i]) {
                        fprintf(stderr, "Process %d has requested more resources than it needs.\n", process_index);
                        return -1;
                }
        }

        for (int i = 0; i < MAX_RESOURCES; i++) {
                if (request[i] > shared_data->resources[i].available) {
                        snprintf(log_buffer, sizeof(log_buffer),
                                "Request cannot be granted immediately. Process P%d must wait for resources at time %u:%u\n",
                                process_index, shared_data->clock.seconds, shared_data->clock.nanoseconds);
                        log_message(log_buffer);

                        delayed_grants++;
                        log_statistics();
                        return 0;
                }

                shared_data->resources[i].available -= request[i];
                shared_data->pcbs[process_index].allocated[i] += request[i];
                shared_data->pcbs[process_index].needs[i] -= request[i];
        }

        if (check_safety(shared_data)) {
                snprintf(log_buffer, sizeof(log_buffer),
                        "Safe state after granting request. Master granting process P%d resources at time %u:%u\n",
                        process_index, shared_data->clock.seconds, shared_data->clock.nanoseconds);
                log_message(log_buffer);
                send_acknowledgment(msgid, RESOURCE_RESPONSE, process_index, 1);

                immediate_grants++;
                log_statistics();
                return 1;
        } else {

                for (int i = 0; i < MAX_RESOURCES; i++) {
                        shared_data->resources[i].available += request[i];
                        shared_data->pcbs[process_index].allocated[i] -= request[i];
                        shared_data->pcbs[process_index].needs[i] += request[i];
                }

                snprintf(log_buffer, sizeof(log_buffer),
                        "Unsafe state after granting request; request not granted. Process P%d added to wait queue at time %u:%u\n",
                        process_index, shared_data->clock.seconds, shared_data->clock.nanoseconds);
                log_message(log_buffer);
                send_acknowledgment(msgid, RESOURCE_RESPONSE, process_index, 0);

                delayed_grants++;
                log_statistics();

                return 0;
        }
}

void signal_handler(int sig) {
        log_message("Signal received, cleaning up...\n");

        for (int i = 0; i < MAX_USER_PROCESSES; i++) {
                if (shared_data->pcbs[i].pid != 0) {
                        kill(shared_data->pcbs[i].pid, SIGTERM);
                }
        }

        pid_t pid;
        int status;
        while((pid = waitpid(-1, &status, 0)) > 0) {
                log_message("Child process %d terminated with status %d\n", pid, WEXITSTATUS(status));
        }

        if (pid == -1 && errno != ECHILD) {
                log_message("Error waiting for child processes: %s\n", strerror(errno));
        }

        detachSharedMemory(shared_data);
        removeSharedMemory(shmid);
        removeSemaphore(semid);
        removeMessageQueue(msgid);

        close_logging();

        exit(0);
}

void setup_signal_handlers() {
        struct sigaction sa;
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGALRM, &sa, NULL);

        alarm(MAX_RUNTIME);
}

int main(int argc, char* argv[]) {

        SharedData* shared_data;
        int active_children = 0;

        setup_logging();
        log_message("Operating System Simulator Starting...\n");

        for (int i = 1; i < argc; i++) {
                if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
                        verbose_mode = true;
                }
        }
        if (initSharedMemory(sizeof(SharedData), &shmid) ==  -1) {
                exit(1);
        }

        shared_data = (SharedData*)attachSharedMemory(shmid, 0);
        if (shared_data == NULL) {
                removeSharedMemory(shmid);
                exit(1);
        }

        if(initSemaphore(&semid) == -1){
                detachSharedMemory(shared_data);
                removeSharedMemory(shmid);
                exit(1);
        }

        if (initMessageQueue(&msgid) == -1) {
                detachSharedMemory(shared_data);
                removeSharedMemory(shmid);
                removeSemaphore(semid);
                exit(1);
        }
        printf("Process %d: Waiting on semaphore with ID %d\n", getpid(), semid); //debug
        semaphoreWait(semid);
        printf("Process %d: Passed semaphore with ID %d\n", getpid(), semid);
        initializeResources(shared_data, semid);
        shared_data->clock.seconds = 0;
        shared_data->clock.nanoseconds = 0;
        log_message("About to signal semaphore with ID %d\n", semid); //debug
        semaphoreSignal(semid);
        log_message("Successfully signaled semaphore with ID %d\n", semid); //debug
        setup_signal_handlers();

        forkUserProcesses(shared_data, &active_children, shmid);

        while (active_children > 0) {
                incrementLogicalClock(&shared_data->clock, 0, rand() % 500000 + 1000);
                usleep(100000);

                ResourceMessage msg;
                if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), 0, IPC_NOWAIT) == -1) {
                        if (errno == ENOMSG) {
                                continue;
                        } else {
                                perror("msgrcv");
                                break;
                        }
                }
                printf("Attempting to wait on semaphore with ID %d\n", semid);
                semaphoreWait(semid);
                printf("Successfully waited on semaphore with ID %d\n", semid);
                switch (msg.mtype) {
                        case REQUEST_RESOURCE:
                                if (request_resources(shared_data, msg.process_id, msg.request)) {

                        }
                        break;
                case RELEASE_RESOURCE:

                        release_resources(shared_data, msg.process_id);
                        break;
                case TERMINATE_PROCESS:
                        release_resources(shared_data, msg.process_id);
                        int status;
                        waitpid(shared_data->pcbs[msg.process_id].pid, &status, 0);
                        active_children--;
                        log_message("Process %d terminated with status %d\n", msg.process_id, status); //debug
                        break;
                default:
                        fprintf(stderr, "Received an unkown message type: %ld\n", msg.mtype);
                        break;
                }
                log_message("About to signal semaphore with ID %d\n", semid); //debug
                semaphoreSignal(semid);
                log_message("Successfully signaled semaphore with ID %d\n", semid); //debug
        }

        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, 0)) > 0) {
            log_message("Child process %d terminated with status %d\n", pid, status);
        }

        char final_message[256];
        snprintf(final_message, sizeof(final_message), "Operating System Simulator Ending at time %u:%u...\n", shared_data->clock.seconds, shared_data->clock.nanoseconds);
        log_message(final_message);

        close_logging();

        detachSharedMemory(shared_data);
        removeSharedMemory(shmid);
        removeSemaphore(semid);
        removeMessageQueue(msgid);

        return 0;
}
