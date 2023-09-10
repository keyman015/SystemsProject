#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
//  you may need other standard header files


//  CITS2002 Project 1 2023
//  Student1:   23340022            Cameron O'Neill
//  Student2:   23424609            Akhil Gorasia


//  myscheduler (v1.0)
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
-> Ensure total number of non-terminated processes (including blocked) <= MAX_RUNNING_PROCESSES

*/

int cpuTime = 0;
int globalClock = 0;
int pid = 1;                                    // Start pid from 1 (pid 0 reserved for "init" - i.e. the first spawned process's parent is 0)



// Process Structure

typedef struct {    // Not all values will have a value, some may be NULL
    int duration;
    char sysCallName[MAX_SYSCALL_NAME];
    char deviceName[MAX_DEVICE_NAME];
    char processName[MAX_PROCESS_NAME];
    long long int capacity;
    long long int sleepDuration;
} sysCall;

typedef enum {
    NEW,
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
} ProcessState;

typedef struct {
    int pid;
    ProcessState state;
    char processName[MAX_COMMAND_NAME];
    int currentActionIndex;
    long long elapsedCPUTime;
    char waitingOnDevice[MAX_DEVICE_NAME];
    int blockDuration;
    sysCall processSyscalls[MAX_SYSCALLS_PER_PROCESS];
    int busProgress;    // 0 for bus not needed, -1 for bus is needed, 1 for process is using bus, 2 for process has finished with bus
    int readSpeed;
    int waitTime;
    int parent; // Pointer to the parent (space efficient) - MUST USE VOID *
    int numOfSpawnedProcesses;
    int waiting;
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


// READY QUEUE

Process readyQueue[MAX_RUNNING_PROCESSES];
int count_READY = 0;

void enqueue(Process p) {
    if (count_READY < MAX_RUNNING_PROCESSES) {
        readyQueue[count_READY] = p;
        count_READY++;
    }
}

Process dequeue() {
    if (count_READY > 0) {
        Process front = readyQueue[0];

        for (int i = 0; i < count_READY; i++) {
            readyQueue[i] = readyQueue[i + 1];
        }

    count_READY--;

    return front;
    } else {
        Process error;
        error.pid = -1;
        return error;
    }
}

// BLOCKED QUEUE

Process blockedQueue[MAX_RUNNING_PROCESSES];
int count_BLOCKED = 0;

void enqueueBlocked(Process p) {
    if (count_BLOCKED < MAX_RUNNING_PROCESSES) {
        blockedQueue[count_BLOCKED] = p;
        count_BLOCKED++;
    }
}



void updateBus() {
    // For the case that the bus is still being used -> Cant update
    if (strcmp(DEVICE_USING_BUS, "") != 0) { return; }

    int max_rSpeed  = -1;   // Read speed
    int max_wTime   = -1;   // Wait time
    int index       = -1;   // Index of the next possible process to use the bus
    for (int i = 0; i < count_BLOCKED; i++) {
        if (blockedQueue[i].busProgress == -1) { // Make sure its waiting for the bus
            if (blockedQueue[i].readSpeed > max_rSpeed || (blockedQueue[i].readSpeed == max_rSpeed && blockedQueue[i].waitTime > max_wTime)) {
                // Update the new maximums
                max_rSpeed= blockedQueue[i].readSpeed;
                max_wTime= blockedQueue[i].waitTime;
                index = i;
            }
        }
    }

    if (index != -1) {
        strcpy(DEVICE_USING_BUS, blockedQueue[index].waitingOnDevice);
        blockedQueue[index].busProgress = 1;    // Now using the bus
        globalClock += 20;  // Databus transfer time
        printf("BUS UPDATED!!!!!!! NEW DEVICE '%s' with processes '%s'\n",DEVICE_USING_BUS, blockedQueue[index].processName);
        return;             // Bus updated
    }
}

// This function will dequeue a process from a specific index in blockedQueue
void dequeueBlockedByIndex(int index) {
    for (int i = index; i < count_BLOCKED - 1; i++) {
        blockedQueue[i] = blockedQueue[i + 1];
    }
    count_BLOCKED--;
}


void checkBlockedProcesses() {
    int indices_to_dequeue[count_BLOCKED]; // An array to store indices of items to dequeue.
    int dequeue_count = 0;
    printf("DEVICE USING BUS: '%s'\n",DEVICE_USING_BUS);

    for (int i = 0; i < count_BLOCKED; i++) {
        printf("Remaining: %i\n", blockedQueue[i].blockDuration);
        printf("In use (bus progress): %i\n", blockedQueue[i].busProgress);

        // If the process is waiting / using the bus, incriment the waiting time it had spent
        if (blockedQueue[i].busProgress == -1) {
            blockedQueue[i].waitTime++;
            continue;
        }

        if (blockedQueue[i].busProgress == 1 && blockedQueue[i].blockDuration <= 0) {
            globalClock += 10; // Blocked -> Ready
            strcpy(DEVICE_USING_BUS, "");       // Device is no longer using the bus
            blockedQueue[i].busProgress = 2;    // Device is finished its task on the bus

            updateBus();
            enqueue(blockedQueue[i]);

            indices_to_dequeue[dequeue_count] = i;  // Store the index to dequeue later
            dequeue_count++;
            continue;
        }

        printf("Spawned Processes: %i\n", blockedQueue[i].numOfSpawnedProcesses);

        if (blockedQueue[i].waiting == 1 && blockedQueue[i].numOfSpawnedProcesses > 0) {
            continue;
        }

        if (blockedQueue[i].blockDuration <= 0) {
            blockedQueue[i].state = READY;
            printf("BLOCKED -> READY: %s\n", blockedQueue[i].processName);
            enqueue(blockedQueue[i]);

            indices_to_dequeue[dequeue_count] = i;
            dequeue_count++;
        }

        blockedQueue[i].blockDuration--;
    }

    // Now, dequeue all the processes that need to be dequeued
    for (int i = 0; i < dequeue_count; i++) {
        globalClock += 10;
        dequeueBlockedByIndex(indices_to_dequeue[i]);
    }
}





int find_commandIndex(const char pName[]) {
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(pName, commands[i].name) == 0) {
            return i;
            break;
        }
    }

    return 0; // No command exists with that name (return 0 by default)
}

int find_deviceIndex(const char dName[]) {
    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (strcmp(dName, devices[i].name) == 0) {
            return i;
            break;
        }
    }

    return 0; // No device exists with that name (return 0 by default)
}

void initliasiseProcess(int cmdIndex, int parentPid) {
    Process p;
    p.state = READY;
    p.blockDuration = 0;
    p.currentActionIndex = 0;
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

    enqueue(p);
    printf("Process '%s' enqueued onto READY queue.\n", p.processName);
}


int tempElapsed = 0;
void execute_commands(void) {
    printf("-------------------------------------------------------------------------\n");
    printf("GLOBAL TIME: %i\n", globalClock);
    // Initialise the first command in the system config as the first process and add it to the readyQueue
    initliasiseProcess(0, -1); // First process doesnt have a parent

    while (count_READY > 0 || count_BLOCKED > 0) { // While either the blocked or ready queue are not empty
    //sleep(1);
    printf("\nCOUNT: %i   BLOCKED: %i\n", count_READY, count_BLOCKED);
        if (count_READY > 0) {
            
            Process currProcess = dequeue();
            printf("Process Ready -> Running: %s\n", currProcess.processName);
            sysCall currAction = currProcess.processSyscalls[currProcess.currentActionIndex];
            //printf("System Call: %s\n", currProcess.processSyscalls[0].sysCallName);
            //printf("System Call: %s\n", currProcess.processSyscalls[1].sysCallName);
            globalClock += 5; // Ready -> Running
            //printf("GLOBAL TIME: %i\n", globalClock);

            printf("System Call: %s\n", currAction.sysCallName);
            printf("Duration: %i\n", currProcess.processSyscalls[currProcess.currentActionIndex].duration);
            

            int remainingSyscallDuration = currAction.duration - currProcess.elapsedCPUTime;            
            printf("REMAINING Call Duration: %i\n", remainingSyscallDuration);
                //printf("ELAP Before: %lli\n", currProcess.elapsedCPUTime);
            // HANDLE TIME QUANTUM / DURATION ON CPU
            if (remainingSyscallDuration >= TIME_QUANTUM) {
                //printf("ELAP: %lli\n", currProcess.elapsedCPUTime);
                globalClock += TIME_QUANTUM;
                //printf("GLOBAL TIME TQ: %i\n", globalClock);
                cpuTime += TIME_QUANTUM;
                currProcess.tempElapsed += TIME_QUANTUM;
                printf("TEMPELAP After: %i\n", currProcess.tempElapsed);
                currProcess.processSyscalls[currProcess.currentActionIndex].duration -= TIME_QUANTUM;
                printf("Duration After: %i\n", currAction.duration);
                //exit(0);
                if (count_READY > 0) {
                    globalClock += 10; // Running -> Ready
                    //printf("GLOBAL TIME: %i\n", globalClock);
                    enqueue(currProcess);
                    continue;
                } else {
                    enqueue(currProcess);
                    globalClock -= 5; // to counter the ready to running change
                    continue;
                }
                
            } else {
                //int remainingTime = currAction.duration;
                //printf("System Call: %s\n", currAction.sysCallName);
                printf("Globaltime: '%i'\n",globalClock);
                globalClock += remainingSyscallDuration;
                currProcess.tempElapsed += remainingSyscallDuration;
                //printf("GLOBAL TIME NONTQ: %i\n", globalClock);
                cpuTime += remainingSyscallDuration;
                currProcess.elapsedCPUTime += currProcess.tempElapsed;
                //printf("FINAL ELAPSED CPU: %lli\n", currProcess.elapsedCPUTime);
                remainingSyscallDuration = 0;
                currProcess.tempElapsed = 0;
                printf("CPU TIME AFTER LAST REMAINAING DURATION: %i\n", cpuTime);
            }
            


            printf("System Call (AFTER TQ): %s\n", currAction.sysCallName);
            //exit(0);
            //printf("Sleep Duration: %lli\n", currProcess.processSyscalls[currProcess.currentActionIndex].sleepDuration);
            if (strcmp(currAction.sysCallName, "sleep") == 0) {
                globalClock += 10; // Running -> Blocked
                currProcess.state = BLOCKED;
                currProcess.blockDuration += currAction.sleepDuration - 10; // Includes State transition
                currProcess.currentActionIndex++;
                enqueueBlocked(currProcess);
                printf("actionIndex = %i\n", currProcess.currentActionIndex);


            } else if (strcmp(currAction.sysCallName, "exit") == 0) {
                currProcess.state = TERMINATED;
                int found = -1;
                if (currProcess.parent != -1) {
                    printf("IF STATEMENT CALLED\n"); 
                    //int found = 0;
                    for (int i = 0; i < count_BLOCKED; i++) {
                        if ((currProcess.parent == blockedQueue[i].pid)) {
                            blockedQueue[i].numOfSpawnedProcesses--;
                            found = 0;
                            break;
                        }
                    }
                    if (found == -1) {
                        for (int i = 0; i < count_READY; i++) {
                            if (currProcess.parent == readyQueue[i].pid) {
                                readyQueue[i].numOfSpawnedProcesses--;
                                break;
                            }
                        }
                    }
                    
                }


            } else if (strcmp(currAction.sysCallName, "read") == 0) {
                globalClock         += 10; // Running -> Blocked
                currProcess.state   = BLOCKED;
                int devIndex        = find_deviceIndex(currAction.deviceName); // Finds array index of the device (returns 0 if it doesnt exist)
                double rSpeed       = (double)devices[devIndex].readSpeed;    // Need double as its used in calculations
                double capac        = (double)currAction.capacity;
                printf("Device name: %s\n", currAction.deviceName);
                
                //rSpeed in Bps, capacity in B and duration needs to be in usec -> Times duration by 10^6
                currProcess.blockDuration = (int) ceil( capac / rSpeed * 1000000 );   // Ciel to round up (if the duration falls in between in certain cases)
                currProcess.readSpeed = (int) rSpeed;
                strcpy(currProcess.waitingOnDevice, currAction.deviceName);

                currProcess.currentActionIndex++;
                currProcess.busProgress = -1;   // Signifies its not finished with / waiting for bus
                strcpy(currProcess.waitingOnDevice, currAction.deviceName);
                enqueueBlocked(currProcess);
                updateBus();

                printf("Block Duration: %i\n", currProcess.blockDuration);
                printf("Read Speed: %f\n",rSpeed);
                printf("Capacity: %lli\n",currAction.capacity);


            } else if (strcmp(currAction.sysCallName, "write") == 0) {
                globalClock         += 10; // Running -> Blocked
                currProcess.state   = BLOCKED;
                int devIndex        = find_deviceIndex(currAction.deviceName);  // Finds array index of the device (returns 0 if it doesnt exist)
                int rSpeed          =  devices[devIndex].readSpeed;             // Dont need double as its not used in duration calculation
                double wSpeed       = (double) devices[devIndex].writeSpeed;
                double capac        = (double) currAction.capacity;

                currProcess.blockDuration = (int) ceil( capac / wSpeed * 1000000 );   // Ciel to round up (if the duration falls in between in certain cases)
                currProcess.readSpeed = rSpeed;
                strcpy(currProcess.waitingOnDevice, currAction.deviceName);

                currProcess.currentActionIndex++;
                currProcess.busProgress = -1;   // Signifies its not finished with / waiting for bus
                strcpy(currProcess.waitingOnDevice, currAction.deviceName);
                enqueueBlocked(currProcess);
                updateBus();


            } else if (strcmp(currAction.sysCallName, "spawn") == 0) {
                printf("SPAWN ENTERED\n");
                globalClock += 20; // Running -> Ready x 2 (1x for child process, 1x for parent process)
                printf("First Process: %s\n", currAction.processName);
                int cmdIndex = find_commandIndex(currAction.processName);   // If the name doesnt exist then 0 (default) is returned
                initliasiseProcess(cmdIndex, currProcess.pid);  //Keeps track of the process that spawned it by giving it the pointer of the parent process

                currProcess.currentActionIndex++;
                currProcess.numOfSpawnedProcesses++;
                printf("CURRENT PROCESS NUM OF SPAWNED: %i\n", currProcess.numOfSpawnedProcesses);
                enqueue(currProcess);
                //printf("Current process has been enqueued\n");
                //printf("Count after spawn: %i\n", count_READY);
                //printf("First Process: %s   Second Process: %s\n", readyQueue[0].processName, readyQueue[1].processName);


            } else if (strcmp(currAction.sysCallName, "wait") == 0) {
                globalClock += 10;
                currProcess.waiting = 1;
                currProcess.currentActionIndex++;
                enqueueBlocked(currProcess);
            }



        }
        checkBlockedProcesses();
        globalClock++;
        printf("GLOBAL TIME: %i\n", globalClock);
        sleep(1);   // Delay the execution (for easier debugging)
    }

    printf("TOTAL TIME: %i\n", globalClock);
    printf("TOTAL CPU TIME: %i\n", cpuTime);
    printf("CPU Percentage: %i/%i = %i%%\n", cpuTime, globalClock, (int)ceil((double)cpuTime / globalClock * 100)); // Need to confirm how its meant to be rounded

    
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
//    printf("\n\n\n~~~~~~~~NEXT IS COMMANDS FILE~~~~~~~~~~\n\n\n");   // to help differentiate

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
                    
                    } else if (strcmp(sysCallName, "sleep") == 0) {  // NOTE: COULD REMOVE THIS ONE BECAUSE ONLY ONE LEFT
                        commands[index_currCMD].actions[index_action].sleepDuration = getNum(token, "usecs");
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
                printf("%lli\n", commands[i].actions[j].sleepDuration);
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
    printf("measurements  %i  %i\n", 0, 0);

    exit(EXIT_SUCCESS);
}

//  vim: ts=8 sw=4
