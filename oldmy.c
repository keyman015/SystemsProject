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
#define MAX_PROCESS_NAME                15  // CHANGE THIS FOR LONGER PROCESS NAMES (include space for null-byte)

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



// Structures to be used

typedef struct {                // Not all values will have a value, some may be NULL
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
    WAITING                     // Waiting for child processes to finish
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
    int busProgress;            // 0 for bus not needed, -1 for bus is needed, 1 for process is using bus, 2 for process has finished with bus
    long long int readSpeed;
    int waitTime;
    int parent;                 // Pointer to the parent (space efficient) - MUST USE VOID *
    int numOfSpawnedProcesses;
    int spawned;
} Process;

struct {
    char name[MAX_DEVICE_NAME];
    long long int readSpeed;    // 64-bit values for added security
    long long int writeSpeed;
} devices[MAX_DEVICES];

struct {
    char name[MAX_COMMAND_NAME];
    int numActions;
    SystemCall actions[MAX_SYSCALLS_PER_PROCESS];   // Array of all system call actions (in order)
} commands[MAX_COMMANDS];

struct {
    int count_BLOCKED;
    int count_IO;               // These 3 counts are for waiting for ublock queues
    int count_WAITING;
    int count_SLEEPING;
    Process currBlocked[MAX_RUNNING_PROCESSES];
    int unblockIO[MAX_RUNNING_PROCESSES];
    int unblockWAITING[MAX_RUNNING_PROCESSES];
    int unblockSLEEPING[MAX_RUNNING_PROCESSES];
} BlockedQueue;

//  -------------------------------------------------------------------------------------------------

// General variables to be used globally
Process readyQueue[MAX_RUNNING_PROCESSES];
Process currProcess;
Process nextOnREADY;
CPUStates CPUState  = IDLE;
int time_transition = 0;
int count_READY     = 0;
int nprocesses      = 0;
int idle_cycleState = 1;        // For which step the idle ticks are in for the current cycle (starts on step 1 - from specifications)
int TQelapsed       = 0;
int pid;                        // Next available PID to be used
int cpuTime;
int globalClock;

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

void readyQueue_enqueue(Process p) {
    readyQueue[count_READY] = p;
    count_READY++;
    p.state = READY;
}

Process readyQueue_dequeue(void) {
    Process front = readyQueue[0];
    for (int i = 0; i < count_READY; i++) {
        readyQueue[i] = readyQueue[i + 1];
    }

    count_READY--;
    front.state = RUNNING;
    return front;
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
    time_transition = TIME_CORE_STATE_TRANSITIONS;
}

void initliasiseProcess(int cmdIndex, int parentPid, int spawned) {
    if (nprocesses >= MAX_RUNNING_PROCESSES) {
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

// A function prototype, for the case that the bus is updated, an idle tick can be processed on the same tick
void tick_idle();

// Updates what process/device now has access to the bus (if its free)
bool updateBus(void) {
    // For the case that the bus is still being used -> Cant update
    if (strcmp(DEVICE_USING_BUS, "") != 0) { return false; }

    long long int max_rSpeed  = -1;   // Read speed
    int max_wTime               = -1;   // Wait time (if the read speed is the same, it then compares wait times)
    int index                   = -1;   // Index of the next possible process to use the bus
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
        BlockedQueue.currBlocked[index].busProgress = 1;    // Next process is now using the bus (IO incriments only while IDLE)
        BlockedQueue.currBlocked[index].blockDuration += TIME_ACQUIRE_BUS; // IO doesnt incriment during transitions (must counter-act)
        // On the same tick the bus is updated, another idle tick is processed
        tick_idle();
        return true;
    }

    // Bus is NOT updated
    return false;
}


//  -------------------------- DIFFERENT TICK TYPES --------------------------------------------

void tick_idle(void) {
    // Unblock SLEEPING processes
    if (idle_cycleState == 1 && BlockedQueue.count_SLEEPING > 0) {
        int index_to_unblock = BlockedQueue.unblockSLEEPING[0];
        for (int i = 0; i < BlockedQueue.count_SLEEPING - 1; i++) {
            BlockedQueue.unblockSLEEPING[i] = BlockedQueue.unblockSLEEPING[i+1];
        }

        BlockedQueue.count_SLEEPING--;
        BlockedQueue_dequeueIndex(index_to_unblock);
        idle_cycleState++;
        return;

    } else if (idle_cycleState <= 1) { idle_cycleState++; }


    // Unblock WAITING processes
    if (idle_cycleState == 2 && BlockedQueue.count_WAITING > 0) {
        int index_to_unblock = BlockedQueue.unblockWAITING[0];
        for (int i = 0; i < BlockedQueue.count_WAITING - 1; i++) {
            BlockedQueue.unblockWAITING[i] = BlockedQueue.unblockWAITING[i+1];
        }

        BlockedQueue.count_WAITING--;
        BlockedQueue_dequeueIndex(index_to_unblock);
        idle_cycleState++;
        return;

    } else if (idle_cycleState <= 2) { idle_cycleState++; }


    // Unblock IO processes
    if (idle_cycleState == 3 && BlockedQueue.count_IO > 0) {
        int index_to_unblock = BlockedQueue.unblockIO[0];
        for (int i = 0; i < BlockedQueue.count_IO - 1; i++) {
            BlockedQueue.unblockIO[i] = BlockedQueue.unblockIO[i+1];
        }

        BlockedQueue.count_IO--;
        BlockedQueue_dequeueIndex(index_to_unblock);
        idle_cycleState++;
        return;

    } else if (idle_cycleState <= 3) { idle_cycleState++; }


    // Commence any pending I/O -> The bus must be in need first
    if (idle_cycleState == 4 && strcmp(DEVICE_USING_BUS, "") == 0) {
        idle_cycleState++;
        // If the bus is updated, consider the current idle tick complete and return (if not conintue)
        if (updateBus()) { return; }

    } else if (idle_cycleState <= 4) { idle_cycleState++; }


    // Commence/resume the next READY process
    if ( idle_cycleState == 5 && count_READY > 0) {
        CPUState = READY_TO_RUNNING;
        time_transition = TIME_CONTEXT_SWITCH;

        idle_cycleState = 1;   // Back to the start of the cycle
        return;

    }


    //Remain idle if there is nothing else to do in the cycle
    idle_cycleState = 1;   // Reset the cycle for the next idle tick
}


void tick_work(void) {
    // The current system call to be processed
    SystemCall currAction = currProcess.processSyscalls[currProcess.currActionIndex];
    TQelapsed++;

    // For when time quantum expires on the current tick (no more work can be done)
    if (TQelapsed == TIME_QUANTUM) {
        // On the same work tick, time quantum has expired, so it must be added back to the READY queue
        currProcess.elapsedCPUTime++;
        cpuTime++;

        readyQueue_enqueue(currProcess);
        time_transition = TIME_CORE_STATE_TRANSITIONS;
        CPUState = RUNNING_TO_READY;
        TQelapsed = 0;
        // Time limit reached, cannot do any more work
        return;
    }

    // For when the system call has been on the CPU for long enough and it can be processed
    if (currAction.duration - currProcess.elapsedCPUTime <= 0) {
        // Reset the Timequantum elapsed for the next system call to be processed
        TQelapsed = 0;

        // SLEEP system call
        if (strcmp(currAction.sysCallName, "sleep") == 0) {
                CPUState = RUNNING_TO_SLEEPING;
                currProcess.blockDuration = currProcess.processSyscalls[currProcess.currActionIndex].sleepDuration;
                currProcess.currActionIndex++;
                BlockedQueue_enqueue(currProcess); 
                time_transition = TIME_CORE_STATE_TRANSITIONS;

        // EXIT system call
        }   else if (strcmp(currAction.sysCallName, "exit") == 0) {
            // The counter for the number of children processes a certain process has MUST BE DECREASED for wait checks
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
            CPUState = IDLE;
            nprocesses--;
            tick_idle();
        
        // SPAWN system call
        }   else if (strcmp(currAction.sysCallName, "spawn") == 0) {
            int cmdIndex = find_commandIndex(currAction.processName);
            initliasiseProcess(cmdIndex, currProcess.pid, 1);                          // Requires no time from NEW -> READY
            currProcess.numOfSpawnedProcesses++;                                    // Keep track of the number of spawned processes
            currProcess.currActionIndex++;
            time_transition = TIME_CORE_STATE_TRANSITIONS;
            readyQueue_enqueue(currProcess);
            CPUState = RUNNING_TO_READY;    // Put the current process (the parent) back onto READY queue (already done in the previous tick)

        // WAIT system call
        }   else if (strcmp(currAction.sysCallName, "wait") == 0) {
            // WAIT but with NO children
            if (currProcess.numOfSpawnedProcesses == 0) {
                currProcess.currActionIndex++;
                readyQueue_enqueue(currProcess);
                time_transition = TIME_CORE_STATE_TRANSITIONS;
                CPUState = RUNNING_TO_READY;

            // WAIT but with children
            } else {
                currProcess.state = WAITING;    // Signify the process is waiting
                currProcess.currActionIndex++;
                BlockedQueue_enqueue(currProcess);
                time_transition = TIME_CORE_STATE_TRANSITIONS;
                CPUState = RUNNING_TO_BLOCKED;
            }
        
        // READ system call
        }   else if (strcmp(currAction.sysCallName, "read") == 0) {
            int devIndex        = find_deviceIndex(currAction.deviceName);          // Finds array index of the device (crashes if it doesnt exist)
            double rSpeed       = (double) devices[devIndex].readSpeed;             // Dont need double as its not used in duration calculation
            double capac        = (double) currAction.capacity;

            currProcess.blockDuration   = (int) ceil(capac / rSpeed * 1000000);     // Ciel to round up (if the duration falls in between in certain cases)
            currProcess.readSpeed       = devices[devIndex].readSpeed;              // Retain the 64-bit value
            currProcess.busProgress     = -1;                                       // Set the process's busProgress to signify its waiting on the bus for IO
            strcpy(currProcess.waitingOnDevice, currAction.deviceName);
            currProcess.currActionIndex++;
            BlockedQueue_enqueue(currProcess);
            time_transition = TIME_CORE_STATE_TRANSITIONS;
            CPUState        = RUNNING_TO_BLOCKED;

        // WRITE system call
        }   else if (strcmp(currAction.sysCallName, "write") == 0) {
            int devIndex        = find_deviceIndex(currAction.deviceName);          // Finds array index of the device (crashes if it doesnt exist)
            double wSpeed       = (double) devices[devIndex].writeSpeed;
            double capac        = (double) currAction.capacity;
            
            currProcess.blockDuration   = (int) ceil(capac / wSpeed * 1000000);     // Ciel to round up (if the duration falls in between in certain cases)
            currProcess.readSpeed       = devices[devIndex].readSpeed;              // Retain the 64-bit value
            currProcess.busProgress     = -1;                                       // Set the process's busProgress to signify its waiting on the bus for IO
            strcpy(currProcess.waitingOnDevice, currAction.deviceName);
            currProcess.currActionIndex++;
            BlockedQueue_enqueue(currProcess);
            time_transition = TIME_CORE_STATE_TRANSITIONS;
            CPUState        = RUNNING_TO_BLOCKED;
        }
        return;
    }

    // Incriment the CPU time after the work is done
    currProcess.elapsedCPUTime++;
    cpuTime++;
}


void tick_transition(void) {
    time_transition--;

    // The transition between states finishes on this tick, so perform the system call (or move on)
    if (time_transition == 0) {
        switch (CPUState) {
            case READY_TO_RUNNING:
                CPUState = WORKING;
                currProcess = readyQueue_dequeue(); // So time for that process only starts after transitioning
                TQelapsed = 0;
                break;
            
            case RUNNING_TO_BLOCKED:
            case RUNNING_TO_SLEEPING:
                CPUState = IDLE;
                tick_idle();                        // To allow READY->RUNNING on the same tick
                break;

            case BLOCKED_TO_READY:
                CPUState = IDLE;
                readyQueue_enqueue(nextOnREADY);
                tick_idle();                        // On the same tick, initialise READY->RUNNING for the recently enqued process
                break;
            
            case RUNNING_TO_READY:
                CPUState = IDLE;
                tick_idle();                        // As soon as its gone from RUNNING->READY, start the process (on the same tick) to go from READY->RUNNING
            
            default:
                break;
        }
    }
}


// Incriment any BLOCKED processes (SLEEPING, WAITING, IO) forward in time
void tick_blocked(void) {
    Process *queue = BlockedQueue.currBlocked;
    for (int i = 0; i < BlockedQueue.count_BLOCKED; i++) {
        // A processes waiting on the bus cannot be incrimented (only when on the bus it can be)
        if (queue[i].busProgress == -1) {
            queue[i].waitTime++;
            continue;
        }

        // If a process's IO has been completed -> Can be unblocked and the next process can use the bus
        else if (queue[i].busProgress == 1 && queue[i].blockDuration <= 0) {
            strcpy(DEVICE_USING_BUS, "");   // Device is no longer using the bus
            queue[i].busProgress = 2;       // Device is finished its task on the bus

            BlockedQueue.unblockIO[BlockedQueue.count_IO] = i;
            BlockedQueue.count_IO++;
            queue[i].state = WAITING_UNBLOCK;
            // On the same tick, check now for unblocks
            tick_idle();
        }

        // For a waiting processes where all its children have finished
        else if (queue[i].state == WAITING && queue[i].numOfSpawnedProcesses == 0) {
            BlockedQueue.unblockWAITING[BlockedQueue.count_WAITING] = i;
            BlockedQueue.count_WAITING++;
            queue[i].state = WAITING_UNBLOCK;
        }

        // For a sleeping processes that has finished its sleep time
        else if (queue[i].blockDuration <= 0 && queue[i].state == BLOCKED) {
            BlockedQueue.unblockSLEEPING[BlockedQueue.count_SLEEPING] = i;
            BlockedQueue.count_SLEEPING++;
            queue[i].state = WAITING_UNBLOCK;
        }

        queue[i].blockDuration--;
    }
}


// --------------------------- EXECUTE --------------------------------------


void execute_commands(void) {
    globalClock = 0;    // Spawn first processes on tick 0
    cpuTime     = 0;    // No CPU time yet
    pid         = 0;    // PIDs start at 0

    // Initialise the first command in the system config as the first process and add it to the readyQueue
    initliasiseProcess(0, -1, 0);  // First process doesnt have a parent

    CPUState = READY_TO_RUNNING;
    time_transition = TIME_CONTEXT_SWITCH;

    // Loop until there are no processes left
    while (nprocesses > 0) {
        // Incriment global clock for every tick
        globalClock++;

        // Tick blocked processed before processing any actions (to stop a blocked process from incrimenting on the same tick its added)
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
}




//  ------------------------------- FILE READING ------------------------------

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

// This function checks for correct name lengths & repeated names
void check_commandName(const char *cName) {
    // Check for too long of a name (strlen doesnt include null-byte, so check has to be >=)
    if (strlen(cName) >= MAX_PROCESS_NAME) {
        printf("ERROR: Command name '%s' is too long, the limit is currently %i.\n", cName, MAX_PROCESS_NAME);
        exit(EXIT_FAILURE);
    }

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
        printf("ERROR: Cannot open system file '%s'. Please try again.\n", filename);
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
            long long int rSpeed;
            long long int wSpeed;
            if (sscanf(line, "device %s %lliBps %lliBps", dName, &rSpeed, &wSpeed) == 3) {
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
            // Check if the following command name to be parsed isnt past the length limit
            if (commands[COMMAND_COUNT].numActions >= MAX_SYSCALLS_PER_PROCESS) {
                printf("ERROR: Too many system calls for command/process '%s'. Limit is currently %i\n", commands[COMMAND_COUNT].name, MAX_SYSCALLS_PER_PROCESS);
                exit(EXIT_FAILURE);
            }

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


//  ------------------------------ DUMP THE CONTENTS OF THE FILES READ ----------------------------------------

void _dump_systemConfig() {
    printf("\n\n\n\n\n---------------------- SYSTEM CONFIG DUMP ----------------------\n");
    printf("Time quantum: %i\n", TIME_QUANTUM);
    printf("Number of devices: %li (Actual: %i)\n\n", sizeof(devices)/sizeof(devices[0]), DEVICE_COUNT);

    for (int i = 0; i < DEVICE_COUNT; i++) {
        printf("DEVICE %i\n", i+1);
        printf("name: %s\n", devices[i].name);
        printf("rspeed: %lli\n", devices[i].readSpeed);
        printf("wspeed: %lli\n", devices[i].writeSpeed);
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

//  READ THE COMMAND FILE
    read_commands(argv[0], argv[2]);

//  DUMP FILE CONTENTS
    //_dump_systemConfig();
    //_dump_commands();

//  EXECUTE COMMANDS, STARTING AT FIRST IN command-file, UNTIL NONE REMAIN
    execute_commands();

//  PRINT THE PROGRAM'S RESULTS
    printf("measurements  %i  %i\n", globalClock,  (cpuTime*100 / globalClock));    // Integer division to truncate

    exit(EXIT_SUCCESS);
}