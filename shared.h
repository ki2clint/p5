#ifndef SHARED_H
#define SHARED_H

#include <stddef.h>
#include <sys/times.h>

#define MAX_RESOURCES 20
#define SHM_KEY 0x1234
#define SEM_KEY 0x5678
#define MSG_KEY 0x9999
#define LOG_FILENAME "oss_log.txt"
#define MAX_LOG_LINES 100000
#define REQUEST_RESOURCE 1
#define RELEASE_RESOURCE 2
#define TERMINATE_PROCESS 3

typedef struct {
        LogicalClock clock;
        ResourceDescriptor resources[MAX_RESOURCES];
} SharedData;

typedef struct {
        long mtype;
        int process_id;
        int resource_request[MAX_RESOURCES];
        int success;
} ResourceMessage;

typedef struct {
        unsigned int seconds;
        unsigned int nanoseconds;
} LogicalClock;

typedef struct {
        int total;
        int available;
        int allocated[MAX_RESOURCES];
        int max_claim[MAX_RESOURCES];
} ResourceDescriptor;

int initSharedMemory(size_t size, int* shmid);
void* attachSharedMemory(int shmid, int shmflg);
void detachSharedMemory(const void* shmaddr);
void removeSharedMemory(int shmid);

int initSemaphore(int* semid);
void removeSemaphore(int semid);
void semaphoreWait(int semid);
void semaphoreSignal(int semid);

void setup_logging();
void log_message(const char *format, ...);
void close_logging();
void print_allocation_table();

#endif
