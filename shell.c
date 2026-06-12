/*
------------------------------------------------------
Description :
    Entry point for Mini Shell. Contains main(),
    global variable definitions, and signal handlers
    for SIGINT (Ctrl+C), SIGTSTP (Ctrl+Z), and
    SIGCHLD (background child exit notification).
------------------------------------------------------
*/

#include "shell.h"

/* ---- Global variable definitions ---- */
char  *external_cmd[256];           /* External commands loaded from External.txt  */
pid_t  child_pid    = -1;           /* PID of current foreground child; -1 if none */
char   g_prompt[256] = "Zereth_SHELL ";/* Active prompt string                     */
int    g_last_status = 0;           /* $? — exit status of last command            */
JOB   *job_list     = NULL;         /* Head of job linked list                     */
int    g_job_counter = 0;           /* Monotonically increasing job number seed    */

/* ------------------------------------------------------------------ */
/*  signal_handler — SIGINT and SIGTSTP                                */
/* ------------------------------------------------------------------ */
void signal_handler(int signum)
{
    if (signum == SIGINT)
    {
        /* Ctrl+C: if no foreground child, just reprint prompt */
        if (child_pid <= 0)
        {
            printf("\n%s", g_prompt);
            fflush(stdout);
        }
        /* If a foreground child exists, the kernel delivers SIGINT to it */
    }
    else if (signum == SIGTSTP)
    {
        /* Ctrl+Z: stop the foreground child and add it to the job list */
        if (child_pid > 0)
        {
            /* Send SIGSTOP to freeze the foreground child */
            kill(child_pid, SIGSTOP);

            /*
             * We cannot know the exact command string here,
             * so we record a placeholder; def.c records it properly
             * before forking.  The job entry is updated there.
             * This handler just ensures the PID is captured if
             * something slips through.
             */
            JOB *j = search_job_by_pid(child_pid);
            if (j == NULL)
            {
                /* Fallback: add with unknown command */
                add_job(child_pid, "<unknown>", JOB_STOPPED);
            }
            else
            {
                j->status = JOB_STOPPED;
            }

            printf("\n[%d] Stopped\n", (j ? j->job_no : g_job_counter));
            child_pid = -1;         /* Detach from foreground tracking */
        }
        else
        {
            /* No foreground child — suppress Ctrl+Z and reprint prompt */
            printf("\n%s", g_prompt);
        }
        fflush(stdout);
    }
}

/* ------------------------------------------------------------------ */
/*  sigchld_handler — reap finished background children               */
/* ------------------------------------------------------------------ */
void sigchld_handler(int sig_num)
{
    (void)sig_num;   /* suppress unused-parameter warning */

    int   wstatus;
    pid_t pid;

    /*
     * WNOHANG: non-blocking — reap any child that has already finished
     * without suspending the shell.
     */
    while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0)
    {
        /* Find the matching job and mark it done */
        JOB *j = search_job_by_pid(pid);
        if (j != NULL)
        {
            j->status = JOB_DONE;

            /* Capture exit code for $? only if it was foreground */
            if (WIFEXITED(wstatus))
                g_last_status = WEXITSTATUS(wstatus);

            printf("\n[%d] Done    %s\n", j->job_no, j->cmd);
            fflush(stdout);

            /* Remove from list */
            delete_job(j->job_no);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    /* Load external commands from External.txt */
    extract_external_commands(external_cmd);

    /* Register signal handlers */
    signal(SIGINT,  signal_handler);   /* Ctrl+C */
    signal(SIGTSTP, signal_handler);   /* Ctrl+Z */
    signal(SIGCHLD, sigchld_handler);  /* Background child finished */

    char prompt_str[256] = "Zereth_SHELL$: ";  /* PS1 buffer               */
    char input_cmd[1024];                  /* Raw input buffer          */

    system("clear");

    /* Hand off to the main read-eval-print loop in def.c */
    scan_input(prompt_str, input_cmd);

    return 0;
}
