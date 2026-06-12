/*
------------------------------------------------------
Description :
    Header file for Mini Shell. Contains all macro
    definitions, type definitions, extern declarations,
    and function prototypes shared across shell.c,
    def.c, and command.c.
------------------------------------------------------
*/

#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

/* ---- Command type identifiers ---- */
#define BUILTIN     1
#define EXTERNAL    2
#define NO_COMMAND  3

/* ---- Job status flags ---- */
#define JOB_RUNNING  0
#define JOB_STOPPED  1
#define JOB_DONE     2

/* ---- ANSI colour codes ---- */
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

/* ---- Job linked-list node ---- */
typedef struct node
{
    int          job_no;       /* Job number shown to user: [1], [2], … */
    pid_t        pid;          /* PID of the background/stopped process  */
    char         cmd[256];     /* Command string as typed by the user     */
    int          status;       /* JOB_RUNNING | JOB_STOPPED | JOB_DONE   */
    struct node *link;         /* Pointer to next node                    */
} JOB;

/* ---- Global variables (defined in shell.c, used across all files) ---- */
extern char  *external_cmd[];   /* External command list loaded from External.txt */
extern pid_t  child_pid;        /* PID of the current foreground child            */
extern char   g_prompt[256];    /* Current prompt string (for signal_handler)     */
extern int    g_last_status;    /* Exit status of the last finished command ($?)  */
extern JOB   *job_list;         /* Head of the job linked list                    */
extern int    g_job_counter;    /* Auto-incrementing job number                   */

/* ---- def.c ---- */
void scan_input(char *prompt, char *input_string);
char *get_command(char *input_string);

/* ---- command.c : classification ---- */
int  check_command_type(char *command);
void extract_external_commands(char **external_commands);

/* ---- command.c : execution ---- */
void execute_internal_commands(char *input_string);
void execute_external_commands(char *input_string);

/* ---- command.c : special echo variants ---- */
void echo_special(char *input_string);

/* ---- command.c : job-control builtins ---- */
void builtin_jobs(void);
void builtin_fg(int job_no);
void builtin_bg(int job_no);

/* ---- command.c : linked-list helpers ---- */
JOB *add_job(pid_t pid, const char *cmd, int status);
void delete_job(int job_no);
JOB *search_job(int job_no);
JOB *search_job_by_pid(pid_t pid);
void display_jobs(void);

/* ---- shell.c : signal handling ---- */
void signal_handler(int sig_num);
void sigchld_handler(int sig_num);

#endif
