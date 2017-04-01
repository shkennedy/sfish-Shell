#ifndef SFISH_H
#define SFISH_H

#include <errno.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 15
#define TIME_SIZE 6

enum colors {BLACK = 0, B_BLACK, RED, B_RED, GREEN, B_GREEN, 
YELLOW, B_YELLOW, BLUE, B_BLUE, MAGENTA, B_MAGENTA, CYAN, B_CYAN, WHITE, B_WHITE};

char *open_tags[16] = {
    "\e[0;30m", // Black
    "\e[1;30m", 
    "\e[0;31m", // Red
    "\e[1;31m",
    "\e[0;32m", // Green
    "\e[1;32m",
    "\e[0;33m", // Yellow
    "\e[1;33m",
    "\e[0;34m", // Blue
    "\e[1;34m",
    "\e[0;35m", // Magenta
    "\e[1;35m",
    "\e[0;36m", // Cyan
    "\e[1;36m",
    "\e[0;37m", // White
    "\e[1;37m"
};

char *HELP_MENU = "\nsfish bash, version 1-release (x86_64-pc-linux-gnu)\n\
bg [PID|JID] - resume stopped background job with $PID|$JID\n\
cd [] [-] [DIR] - change current directory\n\
chclr [SETTING] [COLOR] [BOLD] - change color of prompt elements\n\
chpmt [SETTING] [TOGGLE] - change display of prompt elements\n\
disown [PID|JID] - remove job with $PID|$JID from job list\n\
exit - exit sfish\n\
fg [PID|JID] - brings background job with $PID|$JID to foreground\n\
jobs - print list of current jobs\n\
kill [SIGNAL] [PID|JID] - send $SIGNAL to job with $PID|$JID\n\
pwd - print present working directory\n\
prt - print last return value\n";

char *INFO_MENU = \
"\n----Info----\n\
help\n\
prt\n\
----CTRL---\n\
cd\n\
chclr\n\
chpmt\n\
pwd\n\
exit\n\
----Job Control----\n\
bg\n\
fg\n\
disown\n\
jobs\n\
kill\n\
---Number of Commands Run----\n";


struct exec {
    pid_t pid;
    int argc;
    char *argv[MAX_ARGS];
    int srcfd;
    int desfd;
    int errfd;
    struct exec *next;
};

struct job {
    int jid;
    pid_t pid;
    char *cmd;
    char *status;
    bool fg;
    int nexec;
    char time[TIME_SIZE];
    struct exec *exec_head;
    struct job *next;
};

enum status {RUNNING = 0, STOPPED};
char *exec_status[2] = {"Running", "Stopped"};

struct builtin {
    char label[5];
    int(func*)(int, char**);
};
struct builtin builtins[12];

#endif

// #define SIGHUP       	1
// #define SIGINT       	2
// #define SIGQUIT      	3
// #define SIGILL       	4
// #define SIGTRAP      	5
// #define SIGABRT      	6
// #define SIGIOT       	6
// #define SIGBUS       	7
// #define SIGFPE       	8
// #define SIGKILL      	9
// #define SIGUSR1     	10
// #define SIGSEGV     	11
// #define SIGUSR2     	12
// #define SIGPIPE     	13
// #define SIGALRM     	14
// #define SIGTERM     	15
// #define SIGSTKFLT   	16
// #define SIGCHLD     	17
// #define SIGCONT     	18
// #define SIGSTOP     	19
// #define SIGTSTP     	20
// #define SIGTTIN     	21
// #define SIGTTOU     	22
// #define SIGURG      	23
// #define SIGXCPU     	24
// #define SIGXFSZ     	25
// #define SIGVTALRM   	26
// #define SIGPROF     	27
// #define SIGWINCH    	28
// #define SIGIO       	29
// #define SIGPWR      	30  
// #define SIGSYS      	31