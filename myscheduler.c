#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>
//  you may need other standard header files


//  CITS2002 Project 1 2023
//  Student1:   23340022            Cameron O'Neill
//  Student2:   23424609            Akhil Gorasia


//  myscheduler (v2.01)
//  Compile with:  cc -std=c11 -Wall -Werror -o myscheduler myscheduler.c


//  THESE CONSTANTS DEFINE THE MAXIMUM SIZE OF sysconfig AND command DETAILS
//  THAT YOUR PROGRAM NEEDS TO SUPPORT.  YOU'LL REQUIRE THESE //  CONSTANTS
//  WHEN DEFINING THE MAXIMUM SIZES OF ANY REQUIRED DATA STRUCTURES.

#define MAX_DEVICES                     4
#define MAX_DEVICE_NAME                 20
#define MAX_COMMANDS                    10
#define MAX_COMMAND_NAME                20
#define MAX_SYSCALLS_PER_PROCESS        40
#define MAX_SYSCALL_NAME                6   // Leaving room for null-byte
#define MAX_RUNNING_PROCESSES           50
#define MAX_PROCESS_NAME                15  // CHANGE THIS FOR LONGER PROCESS NAMES

//  NOTE THAT DEVICE DATA-TRANSFER-RATES ARE MEASURED IN BYTES/SECOND,
//  THAT ALL TIMES ARE MEASURED IN MICROSECONDS (usecs),
//  AND THAT THE TOTAL-PROCESS-COMPLETION-TIME WILL NOT EXCEED 2000 SECONDS
//  (SO YOU CAN SAFELY USE 'STANDARD' 32-BIT ints TO STORE TIMES).

#define DEFAULT_TIME_QUANTUM            100

#define TIME_CONTEXT_SWITCH             5
#define TIME_CORE_STATE_TRANSITIONS     10
#define TIME_ACQUIRE_BUS                20


//  ADDITIONAL GLOBAL VARIABLES
#define BUFFER_SIZE                     1024
int     DEVICE_COUNT = 0;
int     TIME_QUANTUM = DEFAULT_TIME_QUANTUM;
int     COMMAND_COUNT = -1;                     // Count is increased for every new command found (so it will be increased to start at indexing 0)
char    DEVICE_USING_BUS[MAX_DEVICE_NAME] = ""; // Nothing is using the bus currently




/*

QUESTIONS & TODO LIST:
-> Are r/w speeds needed to be stored in 32 or 64-bit values?
-> Ensure total number of non-terminated processes (including blocked) <= MAX_RUNNING_PROCESSES & error handling in such cases it does happen
-> error cases for dequeue (pid = -1)
-> pid counting starts at 0 not 1!!!!

*/

int cpuTime;
int globalClock;
int pid;


// Process Structure

typedef struct {    // Not all values will have a value, some may be NULL
    int duration;
    char sysCallName[MAX_SYSCALL_NAME];
    char deviceName[MAX_DEVICE_NAME];
    char processName[MAX_PROCESS_NAME];
    int capacity;
    int sleepDuration;
} SystemCall;

typedef enum {
    NEW,
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED,
    WAITING_UNBLOCK,
    WAITING // Waiting for child processes to finish
} ProcessState;

typedef enum {
    IDLE,
    WORKING,
    SPAWN,
    READY_TO_RUNNING,
    BLOCKED_TO_READY,
    RUNNING_TO_READY,
    RUNNING_TO_BLOCKED,
    RUNNING_TO_SLEEPING
} CPUStates;

typedef struct {
    int pid;
    ProcessState state;
    char processName[MAX_COMMAND_NAME];
    int currActionIndex;
    int elapsedCPUTime;
    char waitingOnDevice[MAX_DEVICE_NAME];
    int blockDuration;
    SystemCall processSyscalls[MAX_SYSCALLS_PER_PROCESS];
    int busProgress;    // 0 for bus not needed, -1 for bus is needed, 1 for process is using bus, 2 for process has finished with bus
    int readSpeed;
    int waitTime;
    int parent; // Pointer to the parent (space efficient) - MUST USE VOID *
    int numOfSpawnedProcesses;
    int spawned;
} Process;

struct {
    char name[MAX_DEVICE_NAME];
    int readSpeed;    // 64-bit values
    int writeSpeed;
} devices[MAX_DEVICES];

struct {
    char name[MAX_COMMAND_NAME];
    int numActions;
    SystemCall actions[MAX_SYSCALLS_PER_PROCESS];   // Array of all system call actions (in order)
} commands[MAX_COMMANDS];

struct {
    int count_BLOCKED;
    int count_IO;
    int count_WAITING;
    int count_SLEEPING;
    Process currBlocked[MAX_RUNNING_PROCESSES];
    int unblockIO[MAX_RUNNING_PROCESSES];
    int unblockWAITING[MAX_RUNNING_PROCESSES];
    int unblockSLEEPING[MAX_RUNNING_PROCESSES];
} BlockedQueue;

int count_READY = 0;
Process readyQueue[MAX_RUNNING_PROCESSES];
CPUStates CPUState = IDLE;
int time_transition = 0;
int nprocesses = 0;
Process currProcess;
Process nextOnREADY;
void readyQueue_enqueue(Process p);

int find_commandIndex(const char cName[]) {
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(cName, commands[i].name) == 0) {
            return i;
        }
    }

    // No command exists with that name
    printf("ERROR - Command '%s' not found.\n", cName);
    exit(EXIT_FAILURE);
}

int find_deviceIndex(const char dName[]) {
    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (strcmp(dName, devices[i].name) == 0) {
            return i;
        }
    }

    // No device exists with that name
    printf("ERROR - Device '%s' not found.\n", dName);
    exit(EXIT_FAILURE);
}

void initliasiseProcess(int cmdIndex, int parentPid, int spawned) {
    if (nprocesses > MAX_RUNNING_PROCESSES) {
        printf("ERROR - Cannot have more than %i running processes concurrently.\n", MAX_RUNNING_PROCESSES);
        exit(EXIT_FAILURE);
    }

    Process p;
    p.state = NEW;
    p.blockDuration = 0;
    p.currActionIndex = 0;
    p.elapsedCPUTime = 0;
    p.numOfSpawnedProcesses = 0;
    p.pid = pid;
    p.parent = parentPid;
    p.busProgress = 0;
    p.spawned = spawned;
    strcpy(p.processName, commands[cmdIndex].name);
    strcpy(p.waitingOnDevice, "");

    // Copy all the system calls from system configuration for a given command
    for (int i = 0; i < MAX_SYSCALLS_PER_PROCESS; i++) { p.processSyscalls[i] = commands[cmdIndex].actions[i]; }
    pid++;  // Incriment the next available pid
    nprocesses++;
    readyQueue_enqueue(p);
}

void readyQueue_enqueue(Process p) {
    // If max running processes has been reached
    if (count_READY >= MAX_RUNNING_PROCESSES) { return; }

    readyQueue[count_READY] = p;
    count_READY++;
    p.state = READY;
}

Process readyQueue_dequeue(void) {
    if (count_READY > 0) {
        Process front = readyQueue[0];

        for (int i = 0; i < count_READY; i++) {
            readyQueue[i] = readyQueue[i + 1];
        }
        count_READY--;
        front.state = RUNNING;
        return front;
        
    // THIS ERROR NEEDS TO BE PROPERLY HANDLED (EXIT OR SOMETHING) <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    } else {
        Process error;
        error.pid = -1;
        return error;
    }
}

void BlockedQueue_enqueue(Process p) {
    if (BlockedQueue.count_BLOCKED < MAX_RUNNING_PROCESSES) {
        if (p.state != WAITING) { p.state = BLOCKED; }          // Set state to BLOCKED if the process isnt waiting (a different blocked state)
        BlockedQueue.currBlocked[BlockedQueue.count_BLOCKED] = p;
        BlockedQueue.count_BLOCKED++;
    }
}


void BlockedQueue_dequeueIndex(int index_to_unblock) {
    // Remove the processes from the queue and move everything up
    nextOnREADY = BlockedQueue.currBlocked[index_to_unblock];
    for (int i = index_to_unblock; i < BlockedQueue.count_BLOCKED - 1; i++) {
        BlockedQueue.currBlocked[i] = BlockedQueue.currBlocked[i + 1];
    }
    BlockedQueue.count_BLOCKED--;
    CPUState = BLOCKED_TO_READY;
    time_transition = 10;
}

// A function prototype, for the case that the bus is updated, an idle tick can be processed on the same tick
void tick_idle();

// Updates what process/device now has access to the bus (if its free)
bool updateBus(void) {
    // For the case that the bus is still being used -> Cant update
    if (strcmp(DEVICE_USING_BUS, "") != 0) { return false; }

    int max_rSpeed  = -1;   // Read speed
    int max_wTime   = -1;   // Wait time (if the read speed is the same, it then compares wait times)
    int index       = -1;   // Index of the next possible process to use the bus
    for (int i = 0; i < BlockedQueue.count_BLOCKED; i++) {
        if (BlockedQueue.currBlocked[i].busProgress == -1) { // Make sure its waiting for the bus
            if (BlockedQueue.currBlocked[i].readSpeed > max_rSpeed || (BlockedQueue.currBlocked[i].readSpeed == max_rSpeed && BlockedQueue.currBlocked[i].waitTime > max_wTime)) {
                // Update the new maximums
                max_rSpeed  = BlockedQueue.currBlocked[i].readSpeed;
                max_wTime   = BlockedQueue.currBlocked[i].waitTime;
                index       = i;
            }
        }
    }

    // Bus is updated
    if (index != -1) {
        strcpy(DEVICE_USING_BUS, BlockedQueue.currBlocked[index].waitingOnDevice);
        printf("@%08d    device.%s acquired DATABUS, reading -- bytes, will take %iusecs (20+%i)\n", globalClock, DEVICE_USING_BUS, BlockedQueue.currBlocked[index].blockDuration+20, BlockedQueue.currBlocked[index].blockDuration);
        BlockedQueue.currBlocked[index].busProgress = 1;    // Next process is now using the bus (IO incriments only while IDLE)
        BlockedQueue.currBlocked[index].blockDuration += 20; // IO doesnt incriment during transitions (must counter-act)
        // On the same tick the bus is updated, another idle tick is processed
        tick_idle();
        return true;
    }

    // Bus is NOT updated
    return false;
}


//fucked system
int idle_cycle_point = 1;
void tick_idle(void) {
    // Unblock Sleeping process
    //printf("%i - cycle pt0: %i\n",globalClock, idle_cycle_point);
    if (idle_cycle_point == 1 && BlockedQueue.count_SLEEPING > 0) {
        int index_to_unblock = BlockedQueue.unblockSLEEPING[0];
        for (int i = 0; i < BlockedQueue.count_SLEEPING - 1; i++) {
            BlockedQueue.unblockSLEEPING[i] = BlockedQueue.unblockSLEEPING[i+1];
        }

        BlockedQueue.count_SLEEPING--;
        BlockedQueue_dequeueIndex(index_to_unblock);
        idle_cycle_point++;
        printf("@%08d    pid%i.SLEEPING->READY, transition takes 10usecs (%i..%i)\n", globalClock, nextOnREADY.pid, globalClock+1, globalClock+10);
        return;

    } else if (idle_cycle_point <= 1) { idle_cycle_point++; }
    //printf("cycle pt1: %i\n",idle_cycle_point);

    if (idle_cycle_point == 2 && BlockedQueue.count_WAITING > 0) {
        int index_to_unblock = BlockedQueue.unblockWAITING[0];
        for (int i = 0; i < BlockedQueue.count_WAITING - 1; i++) {
            BlockedQueue.unblockWAITING[i] = BlockedQueue.unblockWAITING[i+1];
        }

        BlockedQueue.count_WAITING--;
        BlockedQueue_dequeueIndex(index_to_unblock);
        idle_cycle_point++;
        printf("@%08d    pid%i.WAITING->READY, transition takes 10usecs (%i..%i)\n", globalClock, nextOnREADY.pid, globalClock+1, globalClock+10);
        return;

    } else if (idle_cycle_point <= 2) { idle_cycle_point++; }
    //printf("cycle pt2: %i\n",idle_cycle_point);

    if (idle_cycle_point == 3 && BlockedQueue.count_IO > 0) {
        int index_to_unblock = BlockedQueue.unblockIO[0];
        for (int i = 0; i < BlockedQueue.count_IO - 1; i++) {
            BlockedQueue.unblockIO[i] = BlockedQueue.unblockIO[i+1];
        }

        BlockedQueue.count_IO--;
        BlockedQueue_dequeueIndex(index_to_unblock);
        idle_cycle_point++;
        printf("@%08d    pid%i.BLOCKED->READY, transition takes 10usecs (%i..%i)\n", globalClock, nextOnREADY.pid, globalClock+1, globalClock+10);
        return;

    } else if (idle_cycle_point <= 3) { idle_cycle_point++; }
    //printf("cycle pt3: %i\n",idle_cycle_point);

    // Commence any pending I/O -> The bus must be in need first
    if (idle_cycle_point == 4 && strcmp(DEVICE_USING_BUS, "") == 0) {
        idle_cycle_point++;
        // If the bus is updated, consider the current idle tick complete and return (if not conintue)
        if (updateBus()) { return; }

    } else if (idle_cycle_point <= 4) { idle_cycle_point++; }
    //printf("cycle pt4: %i\n",idle_cycle_point);

    // Commence/resume the next READY process
    if ( idle_cycle_point == 5 && count_READY > 0) {
        CPUState = READY_TO_RUNNING;
        time_transition = 5;

        idle_cycle_point = 1;   // Back to the start of the cycle
        printf("@%08d    pid%i.READY->RUNNING, transition takes 5usecs (%i..%i)\n", globalClock, readyQueue[0].pid, globalClock+1, globalClock+5);
        return;

    }
    //printf("cycle pt5: %i (count ready: %i)\n",idle_cycle_point, count_READY);

    //Remain idle if there is nothing else to do in the cycle
    idle_cycle_point = 1;   // Reset the cycle for the next idle tick
    printf("@%08d    Idle\n", globalClock);
}

int TQelapsed = 0;
void tick_work(void) {
    //printf("Process Name: %s\n", currProcess.processName);
    //printf("Process Duration Reamining: %i\n", currProcess.processSyscalls[currProcess.currActionIndex].duration);
    
    SystemCall currAction = currProcess.processSyscalls[currProcess.currActionIndex];
    TQelapsed++;

    if (TQelapsed == TIME_QUANTUM) {
        // On the same work tick, time quantum has expired, so it must be added back to the READY queue
        currProcess.elapsedCPUTime++;
        cpuTime++; // <<<----------------------------------- NOT SURE ON THIS --------------------------------
        printf("@%08d    c\t\t\t\t%s(onCPU=%i)\n", globalClock, currProcess.processName, currProcess.elapsedCPUTime);
        printf("@%08d    time quantum expired, pid%i.RUNNING->READY, transition takes 10usecs (%i..%i)\n", globalClock, currProcess.pid, globalClock+1, globalClock+10);
        readyQueue_enqueue(currProcess);
        time_transition = 10;
        CPUState = RUNNING_TO_READY;
        TQelapsed = 0;
        // Time limit reached, cannot do any more work
        return;
    }

    if (currAction.duration - currProcess.elapsedCPUTime <= 0) {
        TQelapsed = 0; 
        //PROCESS THE SYSTEM CALL
        if (strcmp(currAction.sysCallName, "sleep") == 0) {
                // Enqueues the process on the next tick, as the procedure to do RUNNING->SLEEPING happens on the next tick
                //currProcess.blockDuration += currAction.sleepDuration + 1;          // MAYBE NOT (COME BACK TO THIS) -> +1 to account for one whole tick being used to start the transition (to keep consitent with answers)
                //currProcess.currActionIndex++;
                //BlockedQueue_enqueue(currProcess);                                  // So time can move while its still transitioning
                CPUState = RUNNING_TO_SLEEPING;
                currProcess.blockDuration = currProcess.processSyscalls[currProcess.currActionIndex].sleepDuration;
                currProcess.currActionIndex++;
                BlockedQueue_enqueue(currProcess); 
                time_transition = 10;
                printf("@%08d    sleep %i , pid%i.RUNNING->SLEEPING, transition takes 10usecs (%i..%i)\n", globalClock, currProcess.blockDuration, currProcess.pid, globalClock+1, globalClock+10);
                //time_transition = -1;   // Intermediate state (to keep consistent with answer timings)
                // The rest of the variables are set on the next tick (to keep consitent with answers)

        }   else if (strcmp(currAction.sysCallName, "exit") == 0) {
            if (currProcess.spawned == 1) {
                int found = 0;
                Process *queue = BlockedQueue.currBlocked;
                for (int i = 0; i < BlockedQueue.count_BLOCKED; i++) {
                    if (queue[i].pid == currProcess.parent) {
                        found = 1;
                        queue[i].numOfSpawnedProcesses--;
                        // Set the parent on a wait call to be unblocked on the next idle tick
                        BlockedQueue.unblockWAITING[BlockedQueue.count_WAITING] = i;
                        BlockedQueue.count_WAITING++;
                        queue[i].state = WAITING_UNBLOCK;
                        printf("WAITING PROCESS PID.%i ADDED TO UNBLOCK\n", queue[i].pid);
                        break;
                    }
                }
                if (found == 0) {
                    for (int i = 0; i < count_READY; i++) {
                        if (readyQueue[i].pid == currProcess.parent) {
                            readyQueue[i].numOfSpawnedProcesses--;
                            break;
                        }
                    }
                }
            }
            printf("@%08d    Exit, pid%i.RUNNING->EXIT, transition takes 0usecs\n", globalClock, currProcess.pid);
            CPUState = IDLE;
            nprocesses--;
            tick_idle();
        
        }   else if (strcmp(currAction.sysCallName, "spawn") == 0) {
            int cmdIndex = find_commandIndex(currAction.processName);
            initliasiseProcess(cmdIndex, currProcess.pid, 1);                          // Requires no time from NEW -> READY
            currProcess.numOfSpawnedProcesses++;                                    // Keep track of the number of spawned processes
            currProcess.currActionIndex++;
            time_transition = 10;
            readyQueue_enqueue(currProcess);
            CPUState = RUNNING_TO_READY;    // Put the current process (the parent) back onto READY queue (already done in the previous tick)
            printf("@%08d    spawn '%s', pid%i.NEW->READY, transition takes 0usecs\n",globalClock, currProcess.processSyscalls[currProcess.currActionIndex-1].processName, pid-1);
            printf("@%08d    pid%i.RUNNING->READY, transition takes 10usecs (%i..%i)\n", globalClock, currProcess.pid, globalClock+1, globalClock+10);

        }   else if (strcmp(currAction.sysCallName, "wait") == 0) {
            if (currProcess.numOfSpawnedProcesses == 0) {
                currProcess.currActionIndex++;
                readyQueue_enqueue(currProcess);
                time_transition = 10;
                CPUState = RUNNING_TO_READY;
                printf("@%08d    wait (but no child processes), pid%i.RUNNING->READY\n", globalClock, currProcess.pid);
                printf("@%08d    transition takes 10usecs (%i..%i)\n", globalClock, globalClock+1, globalClock+10);

            } else {
                currProcess.state = WAITING;    // Signify the process is waiting
                currProcess.currActionIndex++;
                BlockedQueue_enqueue(currProcess);
                time_transition = 10;
                CPUState = RUNNING_TO_BLOCKED;
                printf("@%08d    wait, pid%i.RUNNING->WAITING\n", globalClock, currProcess.pid);
                printf("@%08d    transition takes 10usecs (%i..%i)\n", globalClock, globalClock+1, globalClock+10);
            }
        
        }   else if (strcmp(currAction.sysCallName, "read") == 0) {
            int devIndex        = find_deviceIndex(currAction.deviceName);          // Finds array index of the device (crashes if it doesnt exist)
            double rSpeed       = (double) devices[devIndex].readSpeed;             // Dont need double as its not used in duration calculation
            double capac        = (double) currAction.capacity;

            currProcess.blockDuration   = (int) ceil(capac / rSpeed * 1000000);           // Ciel to round up (if the duration falls in between in certain cases)
            currProcess.readSpeed       = (int) rSpeed;
            currProcess.busProgress     = -1;                                       // Set the process's busProgress to signify its waiting on the bus for IO
            strcpy(currProcess.waitingOnDevice, currAction.deviceName);
            currProcess.currActionIndex++;
            BlockedQueue_enqueue(currProcess);
            //updateBus();
            time_transition = 10;
            CPUState        = RUNNING_TO_BLOCKED;
            printf("@%08d    read %ibytes, pid%i.RUNNING->BLOCKED, transition takes 10usecs (%i..%i)\n", globalClock, (int)capac, currProcess.pid, globalClock+1, globalClock+10);

        }   else if (strcmp(currAction.sysCallName, "write") == 0) {
            int devIndex        = find_deviceIndex(currAction.deviceName);          // Finds array index of the device (crashes if it doesnt exist)
            int rSpeed          =  devices[devIndex].readSpeed;                     // Dont need a double as its not used for the calculation
            double wSpeed       = (double) devices[devIndex].writeSpeed;
            double capac        = (double) currAction.capacity;
            
            currProcess.blockDuration   = (int) ceil(capac / wSpeed * 1000000);           // Ciel to round up (if the duration falls in between in certain cases)
            currProcess.readSpeed       = rSpeed;
            currProcess.busProgress     = -1;                                       // Set the process's busProgress to signify its waiting on the bus for IO
            strcpy(currProcess.waitingOnDevice, currAction.deviceName);
            currProcess.currActionIndex++;
            BlockedQueue_enqueue(currProcess);
            time_transition = 10;
            CPUState        = RUNNING_TO_BLOCKED;
            printf("@%08d    write %ibytes, pid%i.RUNNING->BLOCKED, transition takes 10usecs (%i..%i)\n", globalClock, (int)capac, currProcess.pid, globalClock+1, globalClock+10);
            //updateBus();
        }
        return;
    }
    currProcess.elapsedCPUTime++;
    cpuTime++;
    printf("@%08d    c\t\t\t\t%s(onCPU=%i)  (dur: %i)\n", globalClock, currProcess.processName, currProcess.elapsedCPUTime, currAction.duration); 
}


void tick_transition(void) {
    printf("@%08d    +\n", globalClock);    // Signify this tick is being used to transition between states
    time_transition--;

    if (time_transition == 0) {
        switch (CPUState) {
            case READY_TO_RUNNING:
                CPUState = WORKING;
                currProcess = readyQueue_dequeue(); // So time for that process only starts after transitioning
                printf("@%08d    pid%i now on CPU, gets new timequantum\t\t%s(onCPU=%i)\n", globalClock, currProcess.pid, currProcess.processName, currProcess.elapsedCPUTime);
                TQelapsed = 0;
                break;
            
            case RUNNING_TO_BLOCKED:
            case RUNNING_TO_SLEEPING:
                CPUState = IDLE;
                tick_idle();    // To allow READY->RUNNING on the same tick
                break;

            case BLOCKED_TO_READY:
                CPUState = IDLE;
                readyQueue_enqueue(nextOnREADY);
                tick_idle();    // On the same tick, initialise READY->RUNNING for the recently enqued process
                break;
            
            case RUNNING_TO_READY:
                CPUState = IDLE;
                printf("%i - CPU is now idle\n", globalClock);
                tick_idle();    // As soon as its gone from RUNNING->READY, start the process (on the same tick) to go from READY->RUNNING
            
            default:
                break;
        }
    }
}

void tick_blocked(void) {
    Process *queue = BlockedQueue.currBlocked;
    for (int i = 0; i < BlockedQueue.count_BLOCKED; i++) {
        //printf("PROCESS ID: %i (%s)     Number of spawned processes: %i     STATE: %d\n", queue[i].pid, queue[i].processName, queue[i].numOfSpawnedProcesses, queue[i].state);
        //printf("PID: pid%i (%s) Blocked Duration Remaining: %i\n", queue[i].pid, queue[i].processName, queue[i].blockDuration);
        //printf("Bus Progress: %i\n", queue[i].busProgress);
        // If the process is waiting / using the bus, incriment the waiting time it had spent

        // A processes waiting on the bus cannot be incrimented (only when on the bus it can be)
        if (queue[i].busProgress == -1) {
            queue[i].waitTime++;
            continue;
        }

        // If a process's IO has been completed -> Can be unblocked and the next process can use the bus
        else if (queue[i].busProgress == 1 && queue[i].blockDuration <= 0) {
            printf("@%08d    device.%s completes read/write, DATABUS is now idle\n", globalClock, DEVICE_USING_BUS);
            strcpy(DEVICE_USING_BUS, "");   // Device is no longer using the bus
            queue[i].busProgress = 2;       // Device is finished its task on the bus

            //updateBus();                    // Allow the next process to use the bus
            BlockedQueue.unblockIO[BlockedQueue.count_IO] = i;
            BlockedQueue.count_IO++;
            queue[i].state = WAITING_UNBLOCK;
            printf("IO PROCESS PID.%i ADDED TO UNBLOCK\n", queue[i].pid);
            // On the same tick, check now for unblocks
            tick_idle();
        }

        // For a waiting processes where all its children have finished
        else if (queue[i].state == WAITING && queue[i].numOfSpawnedProcesses == 0) {
            BlockedQueue.unblockWAITING[BlockedQueue.count_WAITING] = i;
            BlockedQueue.count_WAITING++;
            queue[i].state = WAITING_UNBLOCK;
            printf("WAITING PROCESS PID.%i ADDED TO UNBLOCK\n", queue[i].pid);
        }

        // For a sleeping processes that has finished its sleep time
        else if (queue[i].blockDuration <= 0 && queue[i].state == BLOCKED) {
            BlockedQueue.unblockSLEEPING[BlockedQueue.count_SLEEPING] = i;
            BlockedQueue.count_SLEEPING++;
            queue[i].state = WAITING_UNBLOCK;
            printf("SLEEPING PROCESS PID.%i ADDED TO UNBLOCK (index in blocked: %i)\n", queue[i].pid, i);
        }

        queue[i].blockDuration--;
    }
}


// --------------------------- EXECUTE --------------------------------------


void execute_commands(void) {
    globalClock = 0;    // Spawn first processes on tick 0
    cpuTime     = 0;    // No CPU time yet
    pid         = 0;    // PIDs start at 0
    printf("-------------------------------------------------------------------------\n\n");
    printf("@%08d    REBOOTING with timequantum=%i\n", globalClock, TIME_QUANTUM);

    // Initialise the first command in the system config as the first process and add it to the readyQueue
    initliasiseProcess(0, -1, 0);  // First process doesnt have a parent
    printf("@%08d    spawn '%s', pid%i.NEW->READY, transition takes 0usecs\n", globalClock, commands[0].name, 0);

    CPUState = READY_TO_RUNNING;
    time_transition = 5;
    printf("@%08d    pid%i.READY->RUNNING, transition takes 5usecs (%i..%i)\n", globalClock, 0, globalClock+1, globalClock+5);

    while (nprocesses > 0) { // While either the blocked or ready queue are not empty
        globalClock++;

        if (globalClock == 1000) {
            printf("code is fucked\n");
            exit(0);
        }

        // Tick blocked processed before processing any actions (to stop it incrimenting on the same tick its added)
        tick_blocked();

        // Send the current tick to the correct type
        switch (CPUState) {
            case SPAWN:
            case READY_TO_RUNNING:
            case RUNNING_TO_BLOCKED:
            case RUNNING_TO_READY:
            case RUNNING_TO_SLEEPING:
            case BLOCKED_TO_READY:
                tick_transition();
                break;

            case WORKING:
                tick_work();
                break;
            
            case IDLE:
                tick_idle();
                break;
            
            default:
                break;
        }
    }

    printf("@%08d    nprocesses=%i, SHUTDOWN\n", globalClock, nprocesses);
    printf("@%08d    %iusecs total system time, %iusecs onCPU by all processes, %i/%i -> %i%%\n", globalClock, globalClock, cpuTime, cpuTime, globalClock, (cpuTime*100 / globalClock)); // Integer division to truncate
}






//  ----------------------------------------------------------------------

#define CHAR_COMMENT                    '#'


// This function is from the lectures
void trim_line(char line[]) {
    int i = 0;
    while (line[i] != '\0') {
        if (line[i] == '\r' || line[i] == '\n') {   // Has support for windows-style line endings
            line[i] = '\0';
            break;
        }
        i++;
    }
}

void check_commandName(const char *cName) {
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(cName, commands[i].name) == 0) {
            printf("ERROR: Can't have two commands with the same name '%s'.\n", cName);
            exit(EXIT_FAILURE);
        }
    }
}


void read_sysconfig(char argv0[], char filename[]) {
    FILE *sysconf = fopen(filename, "r");   // Only reading the file
    char line[BUFFER_SIZE];

    if (sysconf == NULL) {
        printf("Cannot open system file '%s'. Please try again.\n", filename);
        exit(EXIT_FAILURE);
    }

    // Loop over every line in the system configuration file until the end
    while (fgets(line, sizeof(line), sysconf) != NULL) {
        // Skip commented lines & empty lines
        if (line[0] == CHAR_COMMENT || line[0] == '\0' || line[0] == '\n') { continue; }

        // Remove any '\n' or '\r' from the line
        trim_line(line);

        // Parsing device configuration
        if (strncmp(line, "device", 6) == 0) {
            char dName[MAX_DEVICE_NAME];
            int rSpeed;
            int wSpeed;
            if (sscanf(line, "device %s %iBps %iBps", dName, &rSpeed, &wSpeed) == 3) {
                strcpy(devices[DEVICE_COUNT].name, dName);
                devices[DEVICE_COUNT].readSpeed     = rSpeed;
                devices[DEVICE_COUNT].writeSpeed    = wSpeed;

            } else {
                printf("ERROR: Can't parse device configuration from the line: '%s'.\n", line);
                exit(EXIT_FAILURE);
            }
            // Incriment device count for correct assignments
            DEVICE_COUNT++;
        }

        // Parsing timequantum configuration
        else if (strncmp(line, "timequantum", 11) == 0) {
            int timeQ;
            if (sscanf(line, "timequantum %iusec", &timeQ) == 1) {
                TIME_QUANTUM = timeQ;

            } else {
                printf("ERROR: Can't parse timequantum configuration from the line: '%s'.\n", line);
                exit(EXIT_FAILURE);
            }
        }

        else {
            // Uncrecognisable line
            printf("ERROR: Unrecognisable line: '%s'.\n", line);
            exit(EXIT_FAILURE);
        }
    }

    // Close the file
    fclose(sysconf);
}

void read_commands(char argv0[], char filename[]) {
    FILE *cmds = fopen(filename, "r");
    char line[BUFFER_SIZE];

    if (cmds == NULL) {
        printf("ERROR: Cannot open command file '%s'. Please try again.\n", filename);
        exit(EXIT_FAILURE);
    }

    // Loop over every line in the command configuration file until the end
    while (fgets(line, sizeof(line), cmds) != NULL) {
        // Skip commented lines & empty lines
        if (line[0] == CHAR_COMMENT || line[0] == '\0' || line[0] == '\n') { continue; }

        // Remove any '\n' or '\r' from the line
        trim_line(line);

        // Parsing a new command
        if (line[0] != '\t') {
            char cName[MAX_COMMAND_NAME];
            if (sscanf(line, "%s", cName) == 1) {
                // Error check - for the case that the previous command has no system calls
                if (COMMAND_COUNT > 0 && commands[COMMAND_COUNT].numActions == 0) {
                    printf("ERROR: Command '%s' has no system calls/actions.\n", commands[COMMAND_COUNT].name);
                    exit(EXIT_FAILURE);
                }

                COMMAND_COUNT++;
                check_commandName(cName);  // Check if the name is not a repeat
                strcpy(commands[COMMAND_COUNT].name, cName);

            } else {
                printf("ERROR: Can't parse a command name from the line: '%s'.\n", line);
                exit(EXIT_FAILURE);
            }

        // Parsing a new system call for the current command
        } else {
            bool error = false;
            int duration;
            int capacity;
            int sleepDur;
            char sysCallName[MAX_SYSCALL_NAME];
            char spawnN[MAX_COMMAND_NAME];
            char deviceN[MAX_DEVICE_NAME];

            // Seperate each system call
            if (sscanf(line, "%iusecs %s", &duration, sysCallName) == 2) {
                SystemCall *currAction = &commands[COMMAND_COUNT].actions[commands[COMMAND_COUNT].numActions];

                if (strcmp(sysCallName, "read") == 0 || strcmp(sysCallName, "write") == 0) {
                    if (sscanf(line, "%iusecs %s %s %iB", &duration, sysCallName, deviceN, &capacity) == 4) {
                        currAction->duration        = duration;
                        currAction->capacity        = capacity;
                        strcpy(currAction->sysCallName, sysCallName);
                        strcpy(currAction->deviceName, deviceN);
                    } else { error = true; }

                } else if (strcmp(sysCallName, "sleep") == 0) {
                    if (sscanf(line, "%iusecs %s %iusecs ", &duration, sysCallName, &sleepDur) == 3) {
                        currAction->duration        = duration;
                        currAction->sleepDuration   = sleepDur;
                        strcpy(currAction->sysCallName, sysCallName);
                    } else { error = true; }
                
                } else if (strcmp(sysCallName, "spawn") == 0) {
                    if (sscanf(line, "%iusecs %s %s ", &duration, sysCallName, spawnN) == 3) {
                        currAction->duration        = duration;
                        strcpy(currAction->sysCallName, sysCallName);
                        strcpy(currAction->processName, spawnN);
                    } else { error = true; }

                } else if (strcmp(sysCallName, "wait") == 0 || strcmp(sysCallName, "exit") == 0) {
                    currAction->duration            = duration;
                    strcpy(currAction->sysCallName, sysCallName);

                } else {
                    // System call does not exist
                    error = true;
                }
                // Incriment the number of actions
                commands[COMMAND_COUNT].numActions++;

            } else {
                // Incorrectly typed line
                error = true;
            }

            if (error) {
                printf("ERROR: Can't parse a system call configuration from the line: '%s'.\n", line);
                exit(EXIT_FAILURE);
            }
        }
    }

    // Incriment the command count to match the number of commands
    COMMAND_COUNT++;

    // Close the commands configuration file
    fclose(cmds);
}


void _dump_systemConfig() {
    printf("\n\n\n\n\n---------------------- SYSTEM CONFIG DUMP ----------------------\n");
    printf("Time quantum: %i\n", TIME_QUANTUM);
    printf("Number of devices: %li (Actual: %i)\n\n", sizeof(devices)/sizeof(devices[0]), DEVICE_COUNT);

    for (int i = 0; i < DEVICE_COUNT; i++) {
        printf("DEVICE %i\n", i+1);
        printf("name: %s\n", devices[i].name);
        printf("rspeed: %i\n", devices[i].readSpeed);
        printf("wspeed: %i\n", devices[i].writeSpeed);
        printf("\n");
    }
}

void _dump_commands() {
    printf("\n\n\n\n\n---------------------- COMMAND CONFIG DUMP ----------------------\n");
    printf("Number of commands: %li (Actual: %i)\n\n", sizeof(commands)/sizeof(commands[0]), COMMAND_COUNT);

    for (int i = 0; i < COMMAND_COUNT; i++) {
        printf("COMMAND %i\n", i+1);
        printf("Name: %s\n", commands[i].name);
        printf("Num actions: %i\n", commands[i].numActions);

        for (int j = 0; j < commands[i].numActions; j++) {
            printf("\t");
            printf("%iusecs\t", commands[i].actions[j].duration);
            printf("%s\t", commands[i].actions[j].sysCallName);

            char sysCallName[MAX_SYSCALL_NAME];
            strcpy(sysCallName, commands[i].actions[j].sysCallName);
            if (strcmp(sysCallName, "exit") == 0 || strcmp(sysCallName, "wait") == 0) {
                printf("\n");
                continue;

            } else if (strcmp(sysCallName, "spawn") == 0) {
                printf("%s\n", commands[i].actions[j].processName);
                continue;

            } else if (strcmp(sysCallName, "sleep") == 0) {
                printf("%iusecs\n", commands[i].actions[j].sleepDuration);
                continue;

            } else if (strcmp(sysCallName, "read") == 0 || strcmp(sysCallName, "write") == 0) {
                printf("%s\t", commands[i].actions[j].deviceName);
                printf("%iB\n", commands[i].actions[j].capacity);
                continue;
            }
        }
        printf("\n");
    }
}

//  ----------------------------------------------------------------------




//  ----------------------------------------------------------------------

int main(int argc, char *argv[]) {
//  ENSURE THAT WE HAVE THE CORRECT NUMBER OF COMMAND-LINE ARGUMENTS
    if(argc != 3) {
        printf("Usage: %s sysconfig-file command-file\n", argv[0]);
        exit(EXIT_FAILURE);
    }

//  READ THE SYSTEM CONFIGURATION FILE
    read_sysconfig(argv[0], argv[1]);
    //_dump_systemConfig();

//  READ THE COMMAND FILE
    read_commands(argv[0], argv[2]);
    
    _dump_commands();

    //THIS IS FOR DEBUGGING PURPOSES:    -      NOTE: ARGC CHECK NEEDS TO BE CHANGED TO 1 AS WELL
    //char argv0[] = "myscheduler";
    //char sysname[] = "sysconfig.txt";
    //read_sysconfig(argv0, sysname);
    //char cmdname[] = "cmds.txt";
    //read_commands(argv0, cmdname);
    //_dump_commands();

//  EXECUTE COMMANDS, STARTING AT FIRST IN command-file, UNTIL NONE REMAIN
    execute_commands();

//  PRINT THE PROGRAM'S RESULTS
    printf("measurements  %i  %i\n", globalClock,  (cpuTime*100 / globalClock));    // Integer division to truncate

    exit(EXIT_SUCCESS);
}

//  vim: ts=8 sw=4