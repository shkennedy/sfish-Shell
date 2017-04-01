#include "sfish.h"

#define MAX_EXECS 10
#define PROMPT_SIZE 256
#define HOSTNAME_SIZE 112
#define PWD_SIZE 160
#define CLOSE_TAG "\x1b[0m"

// Environment variables
struct job *jobs_head;
char last_dir[256];
int last_return;
int cmd_count;
pid_t stored_pid;

// Prompt settings
char *user;
char *user_color = "\e[0;37m"; // Default white non-bold
char *machine;
char *machine_color = "\e[0;37m"; // Default white non-bold
char *pwd;
bool user_tag = false;
bool mach_tag = false;

char *var_cat(char *buf, int nvar, ...) {
    va_list vars;
    va_start(vars, nvar);
    for (; nvar > 0; --nvar) {
        strcat(buf, va_arg(vars, char*));
    }
    va_end(vars);
    return buf;
}

void s_print(int fd, const char *format, int nvar, ...) {
    // Count vars in format
    int i = 0, j;

    // Initialize va_list
    va_list vars;
    va_start(vars, nvar);

    // Make variable length string on stack for print
    char str[1000];
    memset(str, 0, 1000);

    i = j = 0;
    while (format[i] != '\0') {
        if (format[i] == '%') {
            // Concat substring up to '%'
            strncat(str, (format + i - j), j);
            // Concat var
            if (format[i + 1] == 's')
                strcat(str, va_arg(vars, const char*));
            else {
                int num = va_arg(vars, int), numcpy = num, numlen = 0;
                while (numcpy != 0) {
                    numcpy /= 10;
                    ++numlen;
                }
                char numstr[numlen + 1];
                memset(numstr, 0, numlen + 1);
                if (num == 0) {
                    numstr[0] = '0';
                }
                while (num != 0) {
                    numstr[--numlen] = num % 10 + '0';
                    num /= 10;
                }
                strcat(str, numstr);
            }
            ++i;
            j = 0;
        } else {
            ++j;
        }
        ++i;
    }
    strncat(str, (format + i - j), j);

    // Close va_list
    va_end(vars);

    // Fork
    write(fd, str, strlen(str));
}

void update_pwd() {
    getcwd(pwd, PWD_SIZE);
    
    // Handle home shortening
    char *home = getenv("HOME");
    int home_len = strlen(home);
    if (strncmp(pwd, home, home_len) == 0) {
        char dir_temp[PWD_SIZE];
        strcpy(dir_temp, pwd + home_len);
        pwd[0] = '~';
        strcpy(pwd + 1, dir_temp);   
    }
}

int sf_help(int argc, char **argv) {
    // Print help menu
    s_print(STDOUT_FILENO, HELP_MENU, 0);
    return 0;
}

int sf_help_caller(int count, int key) {
    rl_on_new_line();
    sf_help(0, NULL);
    return 0;
}

int sf_info(int count, int key) {
    // Print info menu
    rl_on_new_line();
    s_print(STDOUT_FILENO, INFO_MENU, 0);
    s_print(STDOUT_FILENO, "%d\n----Process Table----\nPGID    PID    CMD\n", 1, cmd_count);
    struct job *cursor = jobs_head;
    while (cursor != NULL) {
        s_print(STDOUT_FILENO, "%d %d %s\n", 3,
        cursor->pid, cursor->pid, cursor->cmd);
        cursor = cursor->next;
    }
    return 0;
}

void sf_exit(int argc, char **argv) {
    struct job *cursor = jobs_head;
    while (cursor != NULL) {
        kill(-cursor->pid, SIGTERM);
        cursor = cursor->next;
    }
    exit(EXIT_SUCCESS);
}

int sf_cd(int argc, char **argv) {
    char *path = argv[1];

    // Save old pwd
    char prev_pwd[PWD_SIZE];
    strcpy(prev_pwd, pwd);

    // Make new changable path
    char new_path[PWD_SIZE], *new_path_ptr = new_path;
    memset(new_path, 0, PWD_SIZE);

    if (argc == 1 || strncmp(path, "~", 1) == 0) {
        strcpy(new_path_ptr ,getenv("HOME"));
        if (argc != 1 && strcmp(path, "~") != 0) {
            size_t homelen = strlen(new_path_ptr);
            strncpy(new_path_ptr + homelen, path + 1, strlen(path) - 1);
        }
    } else if (strcmp(path, "-") == 0) {
        if (strlen(last_dir) != 0) {
            if (strncmp(last_dir, "~", 1) == 0) {
                strcpy(new_path_ptr ,getenv("HOME"));
                size_t homelen = strlen(new_path_ptr);
                strncpy(new_path_ptr + homelen, last_dir + 1, 
                strlen(last_dir) - 1);
            } else {
                new_path_ptr = last_dir;
            }
        } else {
            new_path_ptr = pwd;
        }
    } else {
        strcpy(new_path_ptr, path);
    }

    if (chdir(new_path_ptr) == -1) {
        s_print(STDERR_FILENO, "sfish: cd: %s: No such directory\n", 1, argv[1]);
        return 1;
    }
    update_pwd();
    strcpy(last_dir, prev_pwd);

    return 0;
}

int sf_pwd(int argc, char **argv) {
    s_print(STDOUT_FILENO, "%s\n", 1, pwd);
    return 0;
}

int sf_prt(int argc, char **argv) {
    if (last_return < 100 || last_return > -20)
        s_print(STDOUT_FILENO, "%d\n", 1, last_return);
    else 
        s_print(STDOUT_FILENO, "No commands have been run.\n", 0);
    return 0;
}

int sf_chpmt(int argc, char **argv) {
    int val;
    
    // Check field and value
    if (argc != 3 || ((val = atoi(argv[2])) != 0 && val != 1)) {
        s_print(STDERR_FILENO, "chpmt: Invalid input\n", 0);
        return 1;
    }

    // Set new value
    if (strcmp(argv[1], "user") == 0) {
        user_tag = val;
    } else if (strcmp(argv[1], "machine") == 0) {
        mach_tag = val;
    } else {
        s_print(STDERR_FILENO, "chpmt: Invalid input\n", 0);
        return 1;
    }
    return 0;
}

int sf_chclr(int argc, char **argv) {
    char **setting, *color;
    int bold;
    
    // Check for valid input
    if (argc != 4 || (strcmp(argv[3], "0") != 0 && 
    strcmp(argv[3], "1") != 0)) {
        s_print(STDERR_FILENO, "chclr: Invalid input\n", 0);
        return 1;
    }

    bold = atoi(argv[3]);

    // Check setting
    if (strcmp(argv[1], "user") == 0) {
        setting = &user_color;
    } else if (strcmp(argv[1], "machine") == 0) {
        setting = &machine_color;
    } else {
        s_print(STDERR_FILENO, "chclr: Invalid input\n", 0);
        return 1;
    }

    // Check color
    if (strcmp(argv[2], "blue") == 0) {
        color = open_tags[bold + BLUE];
    } else if (strcmp(argv[2], "black") == 0) {
        color = open_tags[bold + BLACK];
    } else if (strcmp(argv[2], "cyan") == 0) {
        color = open_tags[bold + CYAN];
    } else if (strcmp(argv[2], "green") == 0) {
        color = open_tags[bold + GREEN];
    } else if (strcmp(argv[2], "magenta") == 0) {
        color = open_tags[bold + MAGENTA];
    } else if (strcmp(argv[2], "red") == 0) {
        color = open_tags[bold + RED];
    } else if (strcmp(argv[2], "white") == 0) {
        color = open_tags[bold + WHITE];
    } else if (strcmp(argv[2], "yellow") == 0) {
        color = open_tags[bold + YELLOW];
    } else {
        s_print(STDERR_FILENO, "chclr: Invalid input\n", 0);
        return 1;
    }

    // Set color
    *setting = color;

    return 0;
}

void free_job(struct job *done_job) {
    struct exec *cursor = done_job->exec_head, *temp;
    int i;
    while (cursor != NULL) {
        for (i = 0; i < MAX_ARGS; ++i) {
            if (cursor->argv[i] == NULL)
                break;
            free(cursor->argv[i]);
        }
        temp = cursor->next;
        free(cursor);
        cursor = temp;
    }
    free(done_job->cmd);
    free(done_job);
}

void add_job(struct job *new_job) {
    struct job *cursor = jobs_head, *prev_job = NULL;
    // Find lowest available jid
    if (cursor == NULL || cursor->jid > 1) {
        new_job->jid = 1;
        new_job->next = jobs_head;
        jobs_head = new_job;
    } else {
        while (cursor != NULL) {
            if (prev_job != NULL && cursor->jid > prev_job->jid + 1) {
                new_job->jid = prev_job->jid + 1;
                prev_job->next = new_job;
                new_job->next = cursor;
                return;
            }
            prev_job = cursor;
            if (cursor->next == NULL) {
                cursor->next = new_job;
                new_job->next = NULL;
                break;
            }
            cursor = cursor->next;
        }
        if (new_job->jid == 0) {
            new_job->jid = cursor->jid + 1;
        }
    }
}

struct job* find_job(pid_t pid, bool jid) {
    if (pid < 1)
        return NULL;
    struct job *cursor = jobs_head;
    while (cursor != NULL) {
        if (jid && cursor->jid == pid) {
            break;
        } else if (!jid && cursor->pid == pid) {
            break;
        }
        cursor = cursor->next;
    }
    return cursor;
}

void remove_job(struct job *dead_job) {
    struct job *cursor = jobs_head, *prev = NULL;
    printf("pid: %d\n", getpid());
    if (dead_job == NULL)
        return;
    while (cursor != dead_job) {
        prev = cursor;
        cursor = cursor->next; 
    }
    if (prev != NULL) {
        prev->next = cursor->next;
    } else {
        jobs_head = cursor->next;
    }
    free_job(dead_job);
}   

int print_jobs(int argc, char **argv) {
    struct job *cursor = jobs_head;
    while (cursor != NULL) {
        if (strcmp(cursor->exec_head->argv[0], "jobs") != 0) {
            s_print(STDOUT_FILENO, "[%d]    %s    %d    %s\n", 4, 
            cursor->jid, cursor->status, cursor->pid, cursor->cmd);
        }
        cursor = cursor->next;
    }
    return 1;
}

int sf_kill(int argc, char **argv) {
    if (argc < 2)
        return 1;
    struct job *res_job;
    pid_t jpid; 
    int signal;
    // JID
    if (strncmp(argv[1], "%", 1) == 0) {
        jpid = atoi(argv[1] + 1);
        res_job = find_job(jpid, true);
    }
    // PID
    else {
        jpid = atoi(argv[1]);
        res_job = find_job(jpid, false);
    }
    if (res_job == NULL) {
        s_print(STDERR_FILENO, "kill: invalid job identifier\n", 0);
        return 1;
    }
    // Get signal
    if (argc == 2)
        signal = 15;
    else 
        signal = atoi(argv[2]);
    if (signal < 1 || signal > 31) {
        s_print(STDERR_FILENO, "kill: invalid signal\n", 0);
        return 1;
    }

    // Block signals
    int prev_errno = errno;
    sigset_t all_mask, prev_mask;
    sigemptyset(&all_mask);
    sigfillset(&all_mask);
    sigprocmask(SIG_BLOCK, &all_mask, &prev_mask);

    kill(-res_job->pid, signal);
    
    s_print(STDOUT_FILENO, "[%d] %d sent signal %d\n", 3,
    res_job->jid, res_job->pid, signal);
    
    // Unblock signals
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    errno = prev_errno;

    return 0;
}

int sf_fg(int argc, char **argv) {
    if (argc != 2)
        return 1;
    // JID
    pid_t jpid;
    struct job *new_fg;
    if (strncmp(argv[1], "%", 1) == 0) {
        jpid = atoi(argv[1] + 1);
        new_fg = find_job(jpid, true);
    }
    // PID
    else {
        jpid = atoi(argv[1]);
        new_fg = find_job(jpid, false);
    }
    if (new_fg == NULL) {
        s_print(STDERR_FILENO, "fg: invalid input\n", 0);
        return 1;
    }
    int status, prev_errno = errno;
    new_fg->fg = true;
    if (waitpid(new_fg->pid, &status, WUNTRACED) < 0) {
        errno = prev_errno;
    }
    return 0;
}

int sf_bg(int argc, char **argv) {
    if (argc != 2)
        return 1;
    struct job *res_job;
    pid_t jpid; 
    if (strncmp(argv[1], "%", 1) == 0) {
        jpid = atoi(argv[1] + 1);
        res_job = find_job(jpid, true);
    }
    // PID
    else {
        jpid = atoi(argv[1]);
        res_job = find_job(jpid, false);
    }
    if (res_job == NULL || res_job->status == exec_status[RUNNING]) {
        s_print(STDERR_FILENO, "bg: invalid input\n", 0);
        return 1;
    }
    kill(-res_job->pid, SIGCONT);
    res_job->status = exec_status[RUNNING];
    return 0;
}

int sf_disown(int argc, char **argv) {
    if (argc > 2) {
        s_print(STDERR_FILENO, "disown: invalid input\n", 0);
        return 1;
    }
    struct job *cursor = jobs_head;
    pid_t jpid;
    if (argv[1] == NULL) {
        while (cursor != NULL) {
            remove_job(cursor);
            cursor = cursor->next;
        }
    } 
    // JID
    if (strncmp(argv[1], "%", 1) == 0) {
        jpid = atoi(argv[1] + 1);
        cursor = find_job(jpid, true);
    }
    // PID 
    else {
        jpid = atoi(argv[1]);
        cursor = find_job(jpid, false);
    }
    if (cursor == NULL)
        return 1;
    remove_job(cursor);
    return 0;
}

void* get_builtin(char *cmd, bool *mproc) {
    bool *mp;
    if (mproc != NULL)
        mp = mproc;
    else {
        bool mpro;
        mp = &mpro;
    }
    if (strcmp(cmd, "help") == 0) {
        *mp = false; 
        return &sf_help;
    }
    if (strcmp(cmd, "exit") == 0) {
        *mp = true;
        return &sf_exit;
    }
    if (strcmp(cmd, "cd") == 0) {
        *mp = true;
        return &sf_cd;
    }
    if (strcmp(cmd, "pwd") == 0) {
        *mp = false; 
        return &sf_pwd;
    }
    if (strcmp(cmd, "prt") == 0) {
        *mp= false; 
        return &sf_prt;
    }
    if (strcmp(cmd, "chpmt") == 0) {
        *mp = true;
        return &sf_chpmt;
    }
    if (strcmp(cmd, "chclr") == 0) {
        *mp = true;
        return &sf_chclr;
    }
    if (strcmp(cmd, "jobs") == 0) {
        *mp = false;
        return &print_jobs;
    }
    if (strcmp(cmd, "fg") == 0) {
        *mp = true;
        return &sf_fg;
    }
    if (strcmp(cmd, "bg") == 0) {
        *mp = true;
        return &sf_bg;
    }    
    if (strcmp(cmd, "kill") == 0) {
        *mp = true;
        return &sf_kill;
    }
    if (strcmp(cmd, "disown") == 0) {
        *mp = true;
        return &sf_disown;
    }
    return NULL;
}

void make_prompt(char* prompt) {
    // Fix sfish prompt
    memset(prompt, 0, PROMPT_SIZE);
    strcat(prompt, "sfish");

    if (user == NULL) {
        user = getenv("USER");
    }
    if (strlen(machine) == 0) {
        gethostname(machine, HOSTNAME_SIZE);
    }
    if (strlen(pwd) == 0) {
        update_pwd();
    }

    // Add user and/or machine
    if (user_tag) { 
        var_cat(prompt, 4, "-", user_color, user, CLOSE_TAG);
        if (mach_tag) {
            var_cat(prompt, 5, "@", machine_color, machine, CLOSE_TAG, ":");
        } else {
            strcat(prompt, ":");
        }
    } else if (mach_tag) {
        var_cat(prompt, 5, "-", machine_color, machine, CLOSE_TAG, ":");
    } else {
        strcat(prompt, ":");
    }

    // Add pwd
    var_cat(prompt, 3, "[", pwd, "]>");
}

bool check_exec(char *exec) {
    if (get_builtin(exec, NULL) != NULL) {
        return true;
    }
    bool valid = false;
    char *path_buf = calloc(PWD_SIZE, sizeof(char));
    struct stat stats;
    // Direct location
    if (strstr(exec, "/") != NULL) {
        var_cat(path_buf, 3, pwd, "/", exec);
        if (stat(path_buf, &stats) == -1) {
            valid = true;
        }
    }

    // Unspecified location
    else {
        char path_list[PWD_SIZE], *path_ptr = path_list, *cur_dir;
        strcpy(path_list, getenv("PATH"));
        while ((cur_dir = strsep(&path_ptr, ":")) != NULL) {
            var_cat(path_buf, 3, cur_dir, "/", exec);  
            if (stat(path_buf, &stats) != -1) {
                valid = true;
            }
            memset(path_buf, 0, PWD_SIZE);
        }
    }
    free(path_buf);
    if (!valid) {
        s_print(STDERR_FILENO, "No command '%s' found\n", 1, exec);
    }
    return valid;
} 

bool make_args(char *cmd, int *argc, char **argv) {
    *argc = 0;
    char cmd_cpy[strlen(cmd) + 1], *arg, *argsec;
    strcpy(cmd_cpy, cmd);
    int delim_ind = 0;
    
    // exe arg0 arg1<file0>file1
    while ((argsec = strsep(&cmd, "<>")) != NULL) {
        if (cmd != NULL)
            delim_ind = cmd - argsec - 1;
        else
            delim_ind = 0;
        while ((arg = strsep(&argsec, " ")) != NULL) {
            if (strlen(arg) != 0) {
                argv[*argc] = calloc(strlen(arg) + 1, sizeof(char));
                strcpy(argv[(*argc)++], arg);
            }   
        }
        if (delim_ind > 0) {
            if (cmd_cpy[delim_ind] == '>' || cmd_cpy[delim_ind] == '<') {
                argv[*argc] = calloc(2, sizeof(char));
                argv[(*argc)++][0] = cmd_cpy[delim_ind];
            } 
        }
    }  
    
    
    // Set rest of argv NULL
    int i;
    for (i = *argc; i < MAX_ARGS; ++i) {
        argv[i] = NULL;
    }
    
    // Check for background flag
    if (*argc != 0) {
        int amperloc = strlen(argv[*argc - 1]) - 1;
        if (strcmp(argv[*argc - 1], "&") == 0) {
            --(*argc);
            free(argv[*argc]);
            argv[*argc] = NULL;
        } else if (argv[*argc - 1][amperloc] == '&') {
            argv[*argc - 1][amperloc] = '\0';
        } else {
            return true;
        }
        return false;
    }
    return true;
}

int make_job(char *input, struct job **new_job) {
    // Create new_job
    (*new_job) = calloc(1, sizeof(struct job));
    (*new_job)->cmd = input;
    (*new_job)->fg = true;
    (*new_job)->status = exec_status[RUNNING];
    
    // Copy input to sep
    char cmd[strlen(input) + 1], *cmdp = cmd;
    strcpy(cmd, input);
    
    if (strlen(input) == 0) {
        free_job(*new_job);
        return 0;
    }

    struct exec *cursor = calloc(1, sizeof(struct exec));

    // Separate by pipe
    char *exec_str;
    while ((exec_str = strsep(&cmdp, "|")) != NULL) {
        if (strlen(exec_str) == 0) {
            s_print(STDERR_FILENO, "Invalid command\n", 0);
            free_job(*new_job);
            return 0;
        }
        // Make new node 
        if ((*new_job)->exec_head == NULL) {
            (*new_job)->exec_head = cursor;
        } else {
            cursor->next = calloc(1, sizeof(struct exec));
            cursor = cursor->next;
        }
        cursor->srcfd = cursor->desfd = cursor->errfd = -1;
        ++(*new_job)->nexec;
        
        // Fill new args
        if (make_args(exec_str, &cursor->argc, cursor->argv) == false) {
            (*new_job)->fg = false;
        }

        // Check again for bad input
        if (cursor->argv[0] == NULL || check_exec(cursor->argv[0]) == false) {
            free_job(*new_job);
            return 0;
        }
        
        // Check for redirection, consume from args
        bool non_args = false;
        char fpb[PWD_SIZE], *fp = fpb;
        memset(fp, 0, PWD_SIZE);
        char cwdb[PWD_SIZE], *cwdp = cwdb;
        getcwd(cwdp, PWD_SIZE);
        for (int i = 1; i < cursor->argc - 1; ++i) {
            var_cat(fp, 3, cwdp, "/", cursor->argv[i + 1]);
            if (strcmp(cursor->argv[i], "<") == 0) {
                if ((cursor->srcfd = open(fp, O_RDONLY)) == -1) {
                    s_print(STDERR_FILENO, "Error opening file '%s'\n", 1,
                    cursor->argv[i + 1]);
                    free_job(*new_job);
                    return 0;
                }
                non_args = true;
            } else if (strcmp(cursor->argv[i], ">") == 0) {
                cursor->desfd = 
                open(fp, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | 
                S_IWGRP | S_IWUSR);
                non_args = true;
            } else if (strcmp(cursor->argv[i], "2>") == 0) {
                cursor->errfd = 
                open(fp, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | 
                S_IWGRP | S_IWUSR);
                non_args = true;
            } else if (strcmp(cursor->argv[i], ">>") == 0) {
                cursor->desfd = 
                open(fp, O_RDWR | O_APPEND | O_CREAT , S_IRUSR | S_IRGRP | 
                S_IWGRP | S_IWUSR);
                non_args = true;
            }
            if (non_args) {
                free(cursor->argv[i]);
                cursor->argv[i] = NULL;
            }
            memset(fp, 0, PWD_SIZE);
        }
    }
    return (*new_job)->nexec;
}

void setup_files(struct exec *exec, int *pipes, int npipes, int execn) {
    // Redirect
    if (exec->srcfd != -1) {
        dup2(exec->srcfd, STDIN_FILENO);
        close(exec->srcfd);
    }
    if (exec->desfd != -1) {
        dup2(exec->desfd, STDOUT_FILENO);
        close(exec->desfd);
    }
    if (exec->errfd != -1) {
        dup2(exec->errfd, STDERR_FILENO);
        close(exec->errfd);
    }
    // Pipe
    int pipeind = execn << 1;
    if (pipeind <= npipes - 2) {
        dup2(pipes[pipeind + 1], STDOUT_FILENO);
    }
    if (pipeind > 0) {
        dup2(pipes[pipeind - 2], STDIN_FILENO);
    }
    for (int i = 0; i < npipes; ++i) {
        close(pipes[i]);
    }
}

void start_job(struct job *new_job) {
    struct exec *cursor = new_job->exec_head;

    setpgid(0, 0);

    // Overwrite sigchld_handler
    signal(SIGCHLD, NULL);

    // Make pipes
    int npipes = (new_job->nexec - 1) << 1, 
    *pipes = calloc(npipes, sizeof(int));
    for (int i = 0; i < npipes; i += 2) {
        if (pipe(pipes + i) > 0) {
            s_print(STDERR_FILENO, "Error creating pipes\n", 0);             }
    }

    // Fork for all execs
    int execn = 0;
    int (*func)(int, char**);
    while (cursor != NULL) {
        // Exec
        if ((cursor->pid = fork()) == 0) {
            setpgid(0, getpgid(getpid()));
            // Set redirection
            setup_files(cursor, pipes, npipes, execn);
            // Builtin
            if ((func = get_builtin(cursor->argv[0], NULL)) != NULL) {
                (*func)(cursor->argc, cursor->argv);
                exit(EXIT_SUCCESS);
            }
            // Exec
            else {
                if(execvp(cursor->argv[0], cursor->argv)) {
                    s_print(STDERR_FILENO, "%s: command not found\n", 1, 
                    cursor->argv[0]);
                    exit(EXIT_SUCCESS);        
                }
                // Invalid exec
                else {
                    s_print(STDERR_FILENO, "%s: command not found\n", 1, 
                    cursor->argv[0]);
                    exit(EXIT_SUCCESS);
                }
            } 
        } 
        // Job parent
        else {
            // Close used pipes
            if (pipes[0] != -1) {
                int pipeind = execn << 1;
                if (pipeind <= npipes - 2) {
                    close(pipes[pipeind + 1]);
                }
                if (pipeind > 0) {
                    close(pipes[pipeind - 2]);
                }
            }
            int status, prev_errno = errno;
            if (waitpid(cursor->pid, &status, 0) < 0) {
                 errno = prev_errno;
             }
         }
        cursor = cursor->next;
        ++execn;
    }
}

void init_job_handlers() {
    signal(SIGCHLD, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    rl_command_func_t sf_info;
    rl_command_func_t sf_help_caller;
    rl_command_func_t storepid_handler;
    rl_command_func_t getpid_handler;
    rl_bind_keyseq("\\C-p", NULL);
    rl_bind_keyseq("\\C-h", NULL);
    rl_bind_keyseq("\\C-b", NULL);
    rl_bind_keyseq("\\C-g", NULL);
}

void eval_cmd(char *input) {
    struct job *new_job;

    // Create job and and check for no command
    if (!make_job(input, &new_job)) {
        return;
    }

    // Check if job is main process builtin
    bool mproc;
    int (*func)(int, char**) = get_builtin(new_job->exec_head->argv[0], &mproc);
    if (func != NULL && mproc) {
        (*func)(new_job->exec_head->argc, new_job->exec_head->argv);
        free_job(new_job);
        return;
    }

    // Add job to job list
    add_job(new_job);

    // Fork for execs and wait if needed
    if ((new_job->pid = fork()) == 0) {
        init_job_handlers();
        start_job(new_job);
        exit(EXIT_SUCCESS);
    } 
    
    // Foreground: wait for job to end
    if (new_job->fg) {
        int status, prev_errno = errno;
        if (waitpid(new_job->pid, &status, WUNTRACED) == -1) {
            errno = prev_errno;
        } 
        // Reap was successful: remove job from list
        else {
            remove_job(new_job);
        }
        last_return = status;
    } else {
        s_print(STDOUT_FILENO, "[%d]  %d\n", 2, new_job->jid, new_job->pid);
    } 
}

int storepid_handler(int count, int key) {
    if (jobs_head != NULL)
        stored_pid = jobs_head->pid;
    else
        stored_pid = -1;
    return 0;
}

int getpid_handler(int count, int key) {
    s_print(STDOUT_FILENO, "\n", 0);
    rl_on_new_line();
    struct job *stored_job;
    if (stored_pid != -1) {
        stored_job = find_job(stored_pid, false);
        if (stored_job == NULL) {
            s_print(STDERR_FILENO, "SPID is not set\n", 0);
            stored_pid = -1;
            return 1;    
        }
        if (stored_job->fg) {
            s_print(STDOUT_FILENO, "[%d] %d stopped by signal 19\n", 2,
            stored_job->jid, stored_job->pid);
            kill(-stored_job->pid, SIGSTOP);
            stored_job->status = exec_status[STOPPED];
        } else {
            s_print(STDOUT_FILENO, "[%d] %d stopped by signal 15\n", 2,
            stored_job->jid, stored_job->pid);
            kill(-stored_job->pid, SIGTERM);
        }
    } else {
        s_print(STDERR_FILENO, "SPID is not set\n", 0);
        return 1;
    }
    return 0;
}

void sigint_handler(int sig) {
    int prev_errno = errno;
    sigset_t all_mask, prev_mask;

    // Block signals
    sigemptyset(&all_mask);
    sigfillset(&all_mask);
    sigprocmask(SIG_BLOCK, &all_mask, &prev_mask);

    //Tell foreground job to interrupt
    struct job *cursor = jobs_head;
    while (cursor != NULL) {
        if (cursor->fg) {
            kill(-cursor->pid, SIGINT);
            break;
        }
        cursor = cursor->next;
    }

    // Unblock signals
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    errno = prev_errno;
} 

void sigtstp_handler(int sig) {
    int prev_errno = errno;
    sigset_t all_mask, prev_mask;
    
    // Block signals
    sigemptyset(&all_mask);
    sigfillset(&all_mask);
    sigprocmask(SIG_BLOCK, &all_mask, &prev_mask);

    struct job *cursor = jobs_head;
    while (cursor != NULL) {
        if (cursor->fg) {
            kill(-cursor->pid, SIGTSTP);
            cursor->status = exec_status[STOPPED];
            cursor->fg = false;
            break;
        }
        cursor = cursor->next;
    }

    // Unblock signals
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    errno = prev_errno;
}

void sigchld_handler(int sig) {
    int prev_errno = errno, status;
    sigset_t all_mask, prev_mask;
    pid_t pid;

    // Check if responsible child is background
    struct job *job_cursor = jobs_head;
    while (job_cursor != NULL) {
        job_cursor = job_cursor->next;
    }

    printf("In sigchld\n");
    
    // Block signals
    sigemptyset(&all_mask);
    sigfillset(&all_mask);
    sigprocmask(SIG_BLOCK, &all_mask, &prev_mask);

    // Check status of signaling child
    struct job *signaled_job;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        signaled_job = find_job(pid, false);
        if (WIFSTOPPED(status)) {
            signaled_job->status = exec_status[STOPPED];
        } else if (WIFCONTINUED(status)) {
            signaled_job->status = exec_status[RUNNING];
        } else if (WIFSIGNALED(status)) {
            remove_job(signaled_job);
        }
    }

    // Unblock signals
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    errno = prev_errno;
}

void init_handlers() {
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    rl_command_func_t sf_info;
    rl_command_func_t sf_help_caller;
    rl_command_func_t storepid_handler;
    rl_command_func_t getpid_handler;
    rl_bind_keyseq("\\C-p", sf_info);
    rl_bind_keyseq("\\C-h", sf_help_caller);
    rl_bind_keyseq("\\C-b", storepid_handler);
    rl_bind_keyseq("\\C-g", getpid_handler);
}

int main(int argc, char** argv) {
    //DO NOT MODIFY THIS. If you do you will get a ZERO.
    rl_catch_signals = 0;
    //This is disable readline's default signal handlers, since you are going
    //to install your own.
    init_handlers();
    printf("pid: %d\n", getpid());

    last_return = -1;
    cmd_count = 0;
    pwd = calloc(PWD_SIZE, sizeof(char));
    machine = calloc(HOSTNAME_SIZE, sizeof(char));
    char *prompt = calloc(PROMPT_SIZE, sizeof(char));
    make_prompt(prompt);

    char *test1 = calloc(100, 1);
    strcpy(test1, "cd ../testexecs");
    eval_cmd(test1);

    char *cmd;
    while((cmd = readline(prompt)) != NULL) {
        eval_cmd(cmd);
        make_prompt(prompt);
        ++cmd_count;
    }

    free(pwd);
    free(machine);
    free(prompt);

    return EXIT_SUCCESS;
}
