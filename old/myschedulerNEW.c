#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
int     COMMAND_COUNT = 0;
char    DEVICE_USING_BUS[MAX_DEVICE_NAME] = ""; // Nothing is using the bus currently




/*

QUESTIONS & TODO LIST:
-> Are r/w speeds needed to be stored in 32 or 64-bit values?
-> Ensure total number of non-terminated processes (including blocked) <= MAX_RUNNING_PROCESSES & error handling in such cases it does happen
-> error cases for dequeue (pid = -1)
-> pid counting starts at 0 not 1!!!!

*/

int cpuTime = 0;
int globalClock = 0;
int pid = 0;                                    // Start pid from 0
//int tempCount = 0;


// Process Structure

typedef struct {    // Not all values will have a value, some may be NULL
    int duration;
    char sysCallName[MAX_SYSCALL_NAME];
    char deviceName[MAX_DEVICE_NAME];
    char processName[MAX_PROCESS_NAME];
    long long int capacity;
    int sleepDuration;
} sysCall;

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
    sysCall processSyscalls[MAX_SYSCALLS_PER_PROCESS];
    int busProgress;    // 0 for bus not needed, -1 for bus is needed, 1 for process is using bus, 2 for process has finished with bus
    int readSpeed;
    int waitTime;
    int parent; // Pointer to the parent (space efficient) - MUST USE VOID *
    int numOfSpawnedProcesses;
    int tempElapsed;
} Process;

struct {
    char name[MAX_DEVICE_NAME];
    long long int readSpeed;    // 64-bit values
    long long int writeSpeed;
} devices[MAX_DEVICES];

struct {
    char name[MAX_COMMAND_NAME];
    int numActions;
    sysCall actions[MAX_SYSCALLS_PER_PROCESS];   // Array of all system call actions (in order)
} commands[MAX_COMMANDS];

struct {
    int count_BLOCKED;
    int count_waitingUNBLOCK;
    Process currBlocked[MAX_RUNNING_PROCESSES];
    int waitingUnblock[MAX_RUNNING_PROCESSES];
} BlockedQueue;

int count_READY = 0;
Process readyQueue[MAX_RUNNING_PROCESSES];
CPUStates CPUState = IDLE;
int time_transition = 0;
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

void initliasiseProcess(int cmdIndex, int parentPid) {
    Process p;
    p.state = NEW;
    p.blockDuration = 0;
    p.currActionIndex = 0;
    p.elapsedCPUTime = 0;
    p.numOfSpawnedProcesses = 0;
    p.pid = pid;
    p.parent = parentPid;
    p.busProgress = 0;
    p.tempElapsed = 0;
    strcpy(p.processName, commands[cmdIndex].name);
    strcpy(p.waitingOnDevice, "");

    // Copy all the system calls from system configuration for a given command
    for (int i = 0; i < MAX_SYSCALLS_PER_PROCESS; i++) { p.processSyscalls[i] = commands[cmdIndex].actions[i]; }
    pid++;  // Incriment the next available pid
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

void BlockedQueue_dequeueByIndex(int index) {
    nextOnREADY = BlockedQueue.currBlocked[index];

    for (int i = index; i < BlockedQueue.count_BLOCKED - 1; i++) {
        BlockedQueue.currBlocked[i] = BlockedQueue.currBlocked[i + 1];
    }

    // Remove the top element from waitingforublock queue
    for (int i = 0; i < BlockedQueue.count_waitingUNBLOCK - 1; i++) {
        BlockedQueue.waitingUnblock[i] = BlockedQueue.waitingUnblock[i + 1];
    }

    BlockedQueue.count_BLOCKED--;
    BlockedQueue.count_waitingUNBLOCK--;
}

// Updates what process/device now has access to the bus (if its free)
void updateBus(void) {
    // For the case that the bus is still being used -> Cant update
    if (strcmp(DEVICE_USING_BUS, "") != 0) { return; }

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

    if (index != -1) {
        strcpy(DEVICE_USING_BUS, BlockedQueue.currBlocked[index].waitingOnDevice);
        BlockedQueue.currBlocked[index].busProgress = 1;    // Next process is now using the bus (IO incriments only while IDLE)
    }
}

void tick_bus(void) {
    
}


void tick_idle(void) {
    // Unblock any processes finished
    if (BlockedQueue.count_waitingUNBLOCK > 0) {
        BlockedQueue_dequeueByIndex(BlockedQueue.waitingUnblock[0]);
        CPUState = BLOCKED_TO_READY;
        time_transition = 10;

        printf("@%08d    pid%i.BLOCKED->READY, transition takes 10usecs (%i..%i)\n", globalClock, nextOnREADY.pid, globalClock+1, globalClock+10);
        return;
    }

    // Commence any pending I/O -> The bus must be in need first
    if (strcmp(DEVICE_USING_BUS, "") != 0) {
        tick_bus();
    }

    // Commence/resume the next READY process
    if (count_READY > 0) {
        CPUState = READY_TO_RUNNING;
        time_transition = 5;

        printf("@%08d    pid%i.READY->RUNNING, transition takes 5usecs (%i..%i)\n", globalClock, readyQueue[0].pid, globalClock+1, globalClock+5);
        return;
    }

    printf("@%08d    Idle\n", globalClock); // remain idle if nothing else
}

int TQelapsed = 0;
void tick_work(void) {
    printf("@%08d    c\n", globalClock);                                            // Signify this tick is being used for work
    //printf("Process Name: %s\n", currProcess.processName);
    //printf("Process Duration Reamining: %i\n", currProcess.processSyscalls[currProcess.currActionIndex].duration);
    //printf("Process ElapsedCpu time: %i\n", currProcess.elapsedCPUTime);
    sysCall currAction = currProcess.processSyscalls[currProcess.currActionIndex];
    currProcess.elapsedCPUTime++;
    cpuTime++;
    TQelapsed++;

    if (TQelapsed == TIME_QUANTUM) {
        printf("@%08d    Time quantum expired, pid%i.RUNNING->READY, transition takes 10usecs (%i..%i)\n", globalClock, currProcess.pid, globalClock+1, globalClock+10);
        //currProcess.processSyscalls[currProcess.currActionIndex].duration -= TIME_QUANTUM;
        readyQueue_enqueue(currProcess);
        time_transition = 10;
        CPUState = RUNNING_TO_READY;
        TQelapsed = 0;
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
                time_transition = -1;   // Intermediate state (to keep consistent with answer timings)
                // The rest of the variables are set on the next tick (to keep consitent with answers)

        }   else if (strcmp(currAction.sysCallName, "exit") == 0) {
                CPUState = IDLE;
                printf("@%08d    Exit, pid%i.RUNNING->EXIT, transition takes 0usecs\n",globalClock+1, currProcess.pid);
        
        }   else if (strcmp(currAction.sysCallName, "spawn") == 0) {
            int cmdIndex = find_commandIndex(currAction.processName);
            initliasiseProcess(cmdIndex, currProcess.pid);                          // Requires no time from NEW -> READY
            currProcess.currActionIndex++;
            readyQueue_enqueue(currProcess);
            CPUState = SPAWN;                                                       // Spawn is processed on the next tick seperately
        
        }   else if (strcmp(currAction.sysCallName, "wait") == 0) {
            //currProcess.waiting = 1; <-- REDUNDANT
            currProcess.state = WAITING;                                            // Signify the process is waiting
            BlockedQueue_enqueue(currProcess);
            time_transition = 10;
            CPUState = RUNNING_TO_BLOCKED;
        
        }   else if (strcmp(currAction.sysCallName, "read") == 0) {
            int devIndex        = find_deviceIndex(currAction.deviceName);          // Finds array index of the device (crashes if it doesnt exist)
            double rSpeed       = (double) devices[devIndex].readSpeed;             // Dont need double as its not used in duration calculation
            double capac        = (double) currAction.capacity;

            currProcess.blockDuration   = (int) ceil( capac / rSpeed * 1000000 );   // Ciel to round up (if the duration falls in between in certain cases)
            currProcess.readSpeed       = (int) rSpeed;
            currProcess.busProgress     = -1;                                       // Set the process's busProgress to signify its waiting on the bus for IO
            strcpy(currProcess.waitingOnDevice, currAction.deviceName);
            currProcess.currActionIndex++;
            BlockedQueue_enqueue(currProcess);
            updateBus();
            time_transition = 10;
            CPUState        = RUNNING_TO_BLOCKED;
        
        }   else if (strcmp(currAction.sysCallName, "write") == 0) {
            int devIndex        = find_deviceIndex(currAction.deviceName);          // Finds array index of the device (crashes if it doesnt exist)
            int rSpeed          =  devices[devIndex].readSpeed;                     // Dont need a double as its not used for the calculation
            double wSpeed       = (double) devices[devIndex].writeSpeed;
            double capac        = (double) currAction.capacity;

            currProcess.blockDuration   = (int) ceil( capac / wSpeed * 1000000 );   // Ciel to round up (if the duration falls in between in certain cases)
            currProcess.readSpeed       = rSpeed;
            currProcess.busProgress     = -1;                                       // Set the process's busProgress to signify its waiting on the bus for IO
            strcpy(currProcess.waitingOnDevice, currAction.deviceName);
            currProcess.currActionIndex++;
            BlockedQueue_enqueue(currProcess);
            updateBus();
            time_transition = 10;
            CPUState        = RUNNING_TO_BLOCKED;
        }
    } 
}


void tick_transition(void) {
    if (CPUState == RUNNING_TO_SLEEPING && time_transition == -1) {
        currProcess.blockDuration = currProcess.processSyscalls[currProcess.currActionIndex].sleepDuration;
        currProcess.currActionIndex++;
        BlockedQueue_enqueue(currProcess); 
        time_transition = 10;
        printf("@%08d    Sleep %i , pid%i.RUNNING->SLEEPING, transition takes 10usecs (%i..%i)\n", globalClock, currProcess.blockDuration, currProcess.pid, globalClock+1, globalClock+10);
        return;

    } else if (CPUState == SPAWN) {
        time_transition = 10;
        CPUState = RUNNING_TO_READY;    // Put the current process (the parent) back onto READY queue (already done in the previous tick)
        printf("@%08d    Spawn '%s', pid%i.NEW->READY, transition takes 0usecs\n",globalClock, currProcess.processSyscalls[currProcess.currActionIndex-1].processName, pid);
        printf("@%08d    pid%i.RUNNING->READY, transition takes 10usecs (%i..%i)\n", globalClock, currProcess.pid, globalClock+1, globalClock+10);
        return;
    }

    time_transition--;

    printf("@%08d    +\n", globalClock);    // Signify this tick is being used to transition between states

    if (time_transition == 0) {
        switch (CPUState) {
            case READY_TO_RUNNING:
                CPUState = WORKING;
                currProcess = readyQueue_dequeue(); // So time for that process only starts after transitioning
                printf("@%08d    pid%i now on CPU, gets new timequantum\n", globalClock, currProcess.pid);
                printf("Process Duration Reamining: %i\n", currProcess.processSyscalls[currProcess.currActionIndex].duration);
                break;
            
            case RUNNING_TO_BLOCKED:
            case RUNNING_TO_SLEEPING:
                CPUState = IDLE; // Not sure on this
                tick_idle(); // To allow READY->RUNNING on the same tick
                break;

            case BLOCKED_TO_READY:
                CPUState = IDLE; // not sure on this
                readyQueue_enqueue(nextOnREADY);
                tick_idle();    // On the same tick, initialise READY->RUNNING for the recently enqued process
                break;
            
            case RUNNING_TO_READY:
                CPUState = IDLE;
                printf("CPU is now idle\n");
                tick_idle();    // As soon as its gone from RUNNING->READY, start the process (on the same tick) to go from READY->RUNNING
                //exit(0);
            
            default:
                break;
        }
        return;
    }
}

void tick_blocked(void) {
    Process *queue = BlockedQueue.currBlocked;
    for (int i = 0; i < BlockedQueue.count_BLOCKED; i++) {
        //printf("PID: pid%i (%s) Blocked Duration Remaining: %i\n", queue[i].pid, queue[i].processName, queue[i].blockDuration);
        // If the process is waiting / using the bus, incriment the waiting time it had spent
        if (queue[i].busProgress == -1) {
            queue[i].waitTime++;
            continue;
        }

        // If a process's IO has been completed -> Can be unblocked and the next process can use the bus
        if (queue[i].busProgress == 1 && queue[i].blockDuration == 0) {
            strcpy(DEVICE_USING_BUS, "");   // Device is no longer using the bus
            queue[i].busProgress = 2;       // Device is finished its task on the bus

            updateBus();                    // Allow the next process to use the bus
            BlockedQueue.waitingUnblock[BlockedQueue.count_waitingUNBLOCK] = i;
            BlockedQueue.count_waitingUNBLOCK++;
            continue;
        }

        // If a regular block has finished its duration OR if a WAITING process has finished waiting, it can be unblocked
        if ((queue[i].blockDuration <= 0 && queue[i].state == BLOCKED) || (queue[i].state == WAITING && queue[i].numOfSpawnedProcesses == 0)) {
            BlockedQueue.waitingUnblock[BlockedQueue.count_waitingUNBLOCK] = i;
            BlockedQueue.count_waitingUNBLOCK++;
            queue[i].state = WAITING_UNBLOCK;
        }

        queue[i].blockDuration--;
    }
}





int tempElapsed = 0;
void execute_commands(void) {
    printf("-------------------------------------------------------------------------\n\n");
    printf("@%08d    REBOOTING with timequantum=%i\n", globalClock, TIME_QUANTUM);

    // Initialise the first command in the system config as the first process and add it to the readyQueue
    initliasiseProcess(0, -1);  // First process doesnt have a parent
    printf("@%08d    spawn '%s', pid%i.NEW->READY, transition takes 0usecs\n", globalClock, commands[0].name, 0);

    CPUState = READY_TO_RUNNING;
    time_transition = 5;
    globalClock = 1;            // Start the CPU on the first tick (not on the zeroth)
    printf("@%08d    pid%i.READY->RUNNING, transition takes 5usecs (1..5)\n", globalClock, 0);

    while (CPUState != IDLE || count_READY > 0 || BlockedQueue.count_BLOCKED > 0 || BlockedQueue.count_waitingUNBLOCK > 0) { // While either the blocked or ready queue are not empty
        if (globalClock == 9000) {
            exit(0);
        }

        tick_blocked();         // To stop time moving forward for a blocked process on the same tick its added to the queue

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

        globalClock++;
        //sleep(1);
    }

    printf("@%08d    nprocesses=0, SHUTDOWN\n", globalClock);
    printf("@%08d    %iusecs total system time, %iusecs onCPU by all processes, %i/%i -> %i%%\n", globalClock, globalClock, cpuTime, cpuTime, globalClock, (cpuTime*100 / globalClock)); // Integer division to truncate
}






//  ----------------------------------------------------------------------

#define CHAR_COMMENT                    '#'


void trim_line(char line[]) {
    // Following code is from lectures
    int i = 0;

    while (line[i] != '\0') {
        if (line[i] == '\r' || line[i] == '\n') {   // Has support for windows-style line endings
            line[i] = '\0';
            break;
        }
        i++;
    }
}

long long int getNum(char word[], char text[]) {
    // Removes 'text' from 'word' and then extracts the number from the remaining 'word'
    char *textPos = strstr(word, text);

    if (textPos != NULL) {              // The text exists within the word
        *textPos = '\0';
    }

    return atoi(word);   // Converts the number to long long int for long read/write speeds
}



void read_sysconfig(char argv0[], char filename[]) {
    FILE *sysconf = fopen(filename, "r");   // Only reading the file
    char line[BUFFER_SIZE];

    if (sysconf == NULL) {
        printf("Cannot open system file '%s'. Please try again.\n", filename);
        exit(EXIT_FAILURE);
    }

    while (fgets(line, sizeof(line), sysconf) != NULL) {    // Loops over every line in the file
        if (line[0] == CHAR_COMMENT) { continue; }          // Skips commented lines

        trim_line(line);    // Removes any '\n' or '\r' from the line
//        printf("                  NEW LINE!             \n");
        char *token = strtok(line, " \t");  // Tokenises the line by spaces and tabs
        int state = -1;                     // 0 for 'timequantum', 1 for 'device', 2 for the rspeed, 3 for wspeed (-ve for intermediate states)

        while (token != NULL) {
//            printf("token: %s\n", token);
            switch (state) {
                case 0 :
                    int qnum = (int) getNum(token, "usec");   // Only 32-bit int is needed
                    TIME_QUANTUM = qnum;
                    state = -2;     // Signifies there is no more information to get
//                    printf("NEW TIME QUANTUM! (%i)\n", qnum);
                    break;
                
                case 1 :
                    strcpy(devices[DEVICE_COUNT].name, token);
                    state = 2;       // Move on to read speed
                    break;
                
                case 2 :
                    long long int num = getNum(token, "Bps");
                    devices[DEVICE_COUNT].readSpeed = num;
                    state = 3;       // Move on to write speed
                    break;

                case 3 :
                    num = getNum(token, "Bps");
                    devices[DEVICE_COUNT].writeSpeed = num;
                    DEVICE_COUNT++; // New device added
                    state = -2;     // Signifies there is no more information to get
//                    printf("NEW DEVICE ADDED!\n");
                    break;
            }
            
            if (strcmp(token, "device") == 0) {
                state = 1;          // Ready to add new device
            } else if (strcmp(token, "timequantum") == 0) {
                state = 0;          // Ready for time quantum update
            }

            // Move to the next token
            token = strtok(NULL, " \t");
        }

        // For the case of an incorrectly typed system configuration file
        if (state != -2) {
            printf("There wasn't enough info for one of the lines in the system config file.");
            printf("~~~~~~~~~~~~~~~~ Please fix the file and retry. ~~~~~~~~~~~~~~~~\n");
            break;
        }
    }

    // Close the file
    fclose(sysconf);
}




void read_commands(char argv0[], char filename[]) {
    FILE *cmds = fopen(filename, "r");
    char line[BUFFER_SIZE];

    if (cmds == NULL) {
        printf("Cannot open command file '%s'. Please try again.\n", filename);
        exit(EXIT_FAILURE);
    }


    while (fgets(line, sizeof(line), cmds) != NULL) {       // Loops over every line in the file
        if (line[0] == CHAR_COMMENT) { continue; }          // Skips commented lines

//        printf("                  NEW LINE!             \n");
        trim_line(line);                                    // Removes any '\n' or '\r' from the line
        char *token = strtok(line, " \t");                  // Tokenises the line by spaces and tabs
        int index_word = 0;                                 // Indexing starts from 0 (max is 3)
        int index_currCMD = COMMAND_COUNT - 1;
        int index_action;
        int finished = -1;

        // If a line starts with a tab, then the line is a part of the last command, else its a new command
        if (line[0] == '\t') {
            index_action++;                                 // A new action, increasing the counter
            finished = 0;
//            printf("ANOTHER ACTION TO ADD...\n");

        } else {
            strcpy(commands[COMMAND_COUNT].name, token);    // Since the current token is the command name
            COMMAND_COUNT++;                                // New command added
            index_action = -1;                              // Resets action counter so a new tabbed line can incriment it to zero
//            printf("A NEW COMMAND TO ADD...\n");
            continue;                                        // Look for next line to add actions to the new command
        }

        // ONLY ACTIONS (NON-COMMAND NAMES) PASS THIS POINT AND ARE PROCESSED
        while (token != NULL) {                             // Loops over all words in the line
//            printf("token: %s\t\t\t-> WORD INDEX: %i\n", token, index_word);
            switch (index_word) {
                case 0: // Duration of action
                    commands[index_currCMD].actions[index_action].duration = (int) getNum(token, "usecs"); // 32-bit int only
                    break;
                
                case 1: // sysCallName
                    strcpy(commands[index_currCMD].actions[index_action].sysCallName, token);
                    
                    // No further information is needed from 'exit' OR 'wait' system call
                    if (strcmp(token, "exit") == 0 || strcmp(token, "wait") == 0) { finished = 1; }
                    break;

                case 2: // deviceName OR processName OR nothing (exit)
                    char sysCallName[MAX_SYSCALL_NAME];     // TEMP VALUE FOR LESS CLUTTER
                    strcpy(sysCallName, commands[index_currCMD].actions[index_action].sysCallName);

                    if (strcmp(sysCallName, "read") == 0 || strcmp(sysCallName, "write") == 0) { 
                        strcpy(commands[index_currCMD].actions[index_action].deviceName, token);

                    } else if (strcmp(sysCallName, "spawn") == 0) {
                        strcpy(commands[index_currCMD].actions[index_action].processName, token);
                        finished  = 1;
                    
                    } else if (strcmp(sysCallName, "sleep") == 0) {  // NOTE: COULD REMOVE THIS ONE BECAUSE ONLY ONE LEFT ---------------------------------------
                        commands[index_currCMD].actions[index_action].sleepDuration = (int) getNum(token, "usecs");
                        finished  = 1;
                    }
                    break;

                case 3: // Capacity for read/write
                    commands[index_currCMD].actions[index_action].capacity = getNum(token, "B");
                    finished  = 1;
                    break;
            }

            // Move to the next token (word)
            token = strtok(NULL, " \t");
            index_word++;
        }

        // For the case of an incorrectly typed command configuration file
        if (finished <= 0) {
            printf("There wasn't enough info for command '%s', action number '%i'.\n", commands[index_currCMD].name, index_action+1);
            printf("~~~~~~~~~~~~~~~~ Please fix the file and retry. ~~~~~~~~~~~~~~~~\n");
            break; // Break from reading the file, since it wont process properly anyways
        } 

        // Set number of actions after they've all been added (add one to account for index offset)
        commands[index_currCMD].numActions = index_action + 1;
    }


    fclose(cmds);
}


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
        printf("name: %s\n", commands[i].name);
        printf("num actions: %i\n", commands[i].numActions);

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
                printf("%i\n", commands[i].actions[j].sleepDuration);
                continue;

            } else if (strcmp(sysCallName, "read") == 0 || strcmp(sysCallName, "write") == 0) {
                printf("%s\t", commands[i].actions[j].deviceName);
                printf("%lli\n", commands[i].actions[j].capacity);
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

//  EXECUTE COMMANDS, STARTING AT FIRST IN command-file, UNTIL NONE REMAIN
    execute_commands();

//  PRINT THE PROGRAM'S RESULTS
    printf("measurements  %i  %i\n", globalClock,  (cpuTime*100 / globalClock));    // Integer division to truncate

    exit(EXIT_SUCCESS);
}

//  vim: ts=8 sw=4
