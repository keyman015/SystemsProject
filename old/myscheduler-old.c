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




/*

QUESTIONS & TODO LIST:
-> Are r/w speeds needed to be stored in 32 or 64-bit values?
-> 

*/

int cpuTime = 0;
int globalClock = 0;
int pid = 1;


// Process Structure

struct sysCall {    // Not all values will have a value, some may be NULL
    int duration;
    char sysCallName[MAX_SYSCALL_NAME];
    char deviceName[MAX_DEVICE_NAME];
    char processName[MAX_PROCESS_NAME];
    long long int capacity;
    long long int sleepDuration;
};

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
    long long startBlockTime;
    char waitingOnDevice[MAX_DEVICE_NAME];
    int blockDuration;
    struct sysCall processSyscalls[MAX_SYSCALLS_PER_PROCESS];
} Process;

struct {
    char name[MAX_DEVICE_NAME];
    long long int readSpeed;    // 64-bit values
    long long int writeSpeed;
} devices[MAX_DEVICES];



struct {
    char name[MAX_COMMAND_NAME];
    int numActions;
    struct sysCall actions[MAX_SYSCALLS_PER_PROCESS];   // Array of all system call actions (in order)
} commands[MAX_COMMANDS];

#define MAX_PROCESS 50

// READY QUEUE

Process readyQueue[MAX_PROCESS];
int count = 0;

void enqueue(Process p) {
    if (count < MAX_PROCESS) {
        readyQueue[count] = p;
        count++;
    }
}

Process dequeue() {
    if (count > 0) {
        Process front = readyQueue[0];

        for (int i = 0; i < count; i++) {
            readyQueue[i] = readyQueue[i + 1];
        }

    count--;

    return front;
    } else {
        Process error;
        error.pid = -1;
        return error;
    }
}

// BLOCKED QUEUE

Process blockedQueue[MAX_PROCESS];
int blockedCount = 0;

void enqueueBlocked(Process p) {
    if (blockedCount < MAX_PROCESS) {
        blockedQueue[blockedCount] = p;
        blockedCount++;
    }
}

void dequeueBlocked() {
    if (blockedCount > 0) {
        for (int i = 0; i < blockedCount; i++) {
            blockedQueue[i] = blockedQueue[i + 1];
        }
        blockedCount--;
    }
}

void checkBlockedProcesses() {
    for (int i = 0; i < blockedCount; i++) {
        printf("Remaining: %i\n", blockedQueue[i].blockDuration);
        blockedQueue[i].blockDuration--;
        if (blockedQueue[i].blockDuration <= 0) {
            globalClock += 10; // Blocked -> Ready
            printf("GLOBAL TIME: %i\n", globalClock);
            blockedQueue[i].state = READY;
            enqueue(blockedQueue[i]);
            dequeueBlocked();
        }
    }


}

void initliasiseProcess() {
    Process p;
    p.state = READY;
    p.blockDuration = 0;
    strcpy(p.processName, commands[0].name);
    p.currentActionIndex = 0;
    p.pid = pid;
    p.elapsedCPUTime = 0;
    strcpy(p.waitingOnDevice, "");
    p.startBlockTime = 0;
    for (int i = 0; i < MAX_SYSCALLS_PER_PROCESS; i++) {
        p.processSyscalls[i] = commands[0].actions[i];
    }

    pid++;


    enqueue(p);
    printf("First Process Enqueued\n");
}



void execute_commands(void) {
    printf("GLOBAL TIME: %i\n", globalClock);
    // Initialise first process and add it to the readyQueue
    initliasiseProcess();

    while (count > 0 || blockedCount > 0) { // While either the blocked or ready queue are not empty
    printf("COUNT: %i   BLOCKED: %i\n", count, blockedCount);
        if (count > 0) {
            
            Process currentProcess = dequeue();
            printf("System Call: %s\n", currentProcess.processSyscalls[0].sysCallName);
            printf("System Call: %s\n", currentProcess.processSyscalls[1].sysCallName);
            globalClock += 5; // Ready -> Running
            printf("GLOBAL TIME: %i\n", globalClock);

            printf("Duration: %i\n", currentProcess.processSyscalls[currentProcess.currentActionIndex].duration);
            printf("System Call: %s\n", currentProcess.processSyscalls[currentProcess.currentActionIndex].sysCallName);



            // HANDLE TIME QUANTUM / DURATION ON CPU
            if (currentProcess.processSyscalls[currentProcess.currentActionIndex].duration - currentProcess.elapsedCPUTime > TIME_QUANTUM) {
                globalClock += TIME_QUANTUM;
                cpuTime += TIME_QUANTUM;
                currentProcess.elapsedCPUTime += TIME_QUANTUM;
                currentProcess.processSyscalls[currentProcess.currentActionIndex].duration -= TIME_QUANTUM;
                globalClock += 10; // Running -> Ready
                enqueue(currentProcess);
                continue;
            } else {
                int remainingTime = currentProcess.processSyscalls[currentProcess.currentActionIndex].duration - currentProcess.elapsedCPUTime;
                printf("System Call: %s\n", currentProcess.processSyscalls[currentProcess.currentActionIndex].sysCallName);
                globalClock += remainingTime;
                printf("GLOBAL TIME: %i\n", globalClock);
                cpuTime += remainingTime;
                currentProcess.elapsedCPUTime += remainingTime;
            }

            printf("Sleep Duration: %lli\n", currentProcess.processSyscalls[currentProcess.currentActionIndex].sleepDuration);
            if (strcmp(currentProcess.processSyscalls[currentProcess.currentActionIndex].sysCallName, "sleep") == 0) {
                globalClock += 10; // Running -> Blocked
                currentProcess.state = BLOCKED;
                currentProcess.blockDuration += currentProcess.processSyscalls[currentProcess.currentActionIndex].sleepDuration - 10; // Includes State transition
                currentProcess.currentActionIndex++;
                enqueueBlocked(currentProcess);
                printf("actionIndex = %i\n", currentProcess.currentActionIndex);
            } else if (strcmp(currentProcess.processSyscalls[currentProcess.currentActionIndex].sysCallName, "exit") == 0) {
                currentProcess.state = TERMINATED;
            } else if (strcmp(currentProcess.processSyscalls[currentProcess.currentActionIndex].sysCallName, "read") == 0) {
                globalClock += 10;
                
            } else if (strcmp(currentProcess.processSyscalls[currentProcess.currentActionIndex].sysCallName, "write") == 0) {
                globalClock += 10;
            } else if (strcmp(currentProcess.processSyscalls[currentProcess.currentActionIndex].sysCallName, "spawn") == 0) {
                globalClock += 10;
                
            } else if (strcmp(currentProcess.processSyscalls[currentProcess.currentActionIndex].sysCallName, "wait") == 0) {
                globalClock += 10;
                
            }



        }
        checkBlockedProcesses();
        globalClock++;
        printf("GLOBAL TIME: %i\n", globalClock);
        //sleep(1);
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
        int state = -1;

        // If a line starts with a tab, then the line is a part of the last command, else its a new command
        if (line[0] == '\t') {
            index_action++;                                 // A new action, increasing the counter
            state = 0;
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
                    if (strcmp(token, "exit") == 0 || strcmp(token, "wait") == 0) { state = 1; }
                    break;

                case 2: // deviceName OR processName OR nothing (exit)
                    char sysCallName[MAX_SYSCALL_NAME];     // TEMP VALUE FOR LESS CLUTTER
                    strcpy(sysCallName, commands[index_currCMD].actions[index_action].sysCallName);

                    if (strcmp(sysCallName, "read") == 0 || strcmp(sysCallName, "write") == 0) { 
                        strcpy(commands[index_currCMD].actions[index_action].deviceName, token);

                    } else if (strcmp(sysCallName, "spawn") == 0) {
                        strcpy(commands[index_currCMD].actions[index_action].processName, token);
                        state = 1;
                    
                    } else if (strcmp(sysCallName, "sleep") == 0) {  // NOTE: COULD REMOVE THIS ONE BECAUSE ONLY ONE LEFT
                        commands[index_currCMD].actions[index_action].sleepDuration = getNum(token, "usecs");
                        state = 1;
                    }
                    break;

                case 3: // Capacity for read/write
                    commands[index_currCMD].actions[index_action].capacity = getNum(token, "B");
                    state = 1;
                    break;
            }

            // Move to the next token (word)
            token = strtok(NULL, " \t");
            index_word++;
        }

        // For the case of an incorrectly typed command configuration file
        if (state <= 0) {
            printf("There wasn't enough info for command '%s', action number '%i'.\n", commands[index_currCMD].name, index_action+1);
            printf("~~~~~~~~~~~~~~~~ Please fix the file and retry. ~~~~~~~~~~~~~~~~\n");
            break;
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
//   _dump_systemConfig();

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
