/*
------------------------------------------------------
Project     : Mini Shell
File        : command.c
Author      : Tarun Gaurav (Zereth)
Description :
    Central command module.  Contains:

    1. Builtin command table and classification
       check_command_type()
       extract_external_commands()

    2. Builtin execution
       execute_internal_commands()  — exit, cd, pwd
       echo_special()               — echo $$, $?, $SHELL

    3. Job-control builtins
       builtin_jobs()
       builtin_fg()
       builtin_bg()

    4. Job linked-list helpers
       add_job()
       delete_job()
       search_job()
       search_job_by_pid()
       display_jobs()

    5. External command execution
       execute_external_commands()  — fork + execvp, n-pipe support
------------------------------------------------------
*/

#include "shell.h"

/* ================================================================== */
/*  1. BUILTIN TABLE AND CLASSIFICATION                                */
/* ================================================================== */

/* Recognised builtin command names */
static const char *builtins[] = {
    /* Standard builtins */
    "echo", "cd", "pwd", "exit",
    /* Job control */
    "jobs", "fg", "bg",
    /* Others commonly treated as builtins */
    "printf", "read", "pushd", "popd", "dirs", "let", "eval",
    "set", "unset", "export", "declare", "typeset", "readonly",
    "getopts", "source", "exec", "shopt", "caller", "true", "false",
    "type", "hash", "bind", "help",
    NULL
};

/*
 * check_command_type
 * Returns BUILTIN, EXTERNAL, or NO_COMMAND.
 */
int check_command_type(char *command)
{
    /* Check builtin table */
    for (int i = 0; builtins[i] != NULL; i++)
    {
        if (strcmp(command, builtins[i]) == 0)
            return BUILTIN;
    }

    /* Check external commands loaded from External.txt */
    for (int i = 0; external_cmd[i] != NULL; i++)
    {
        if (strcmp(command, external_cmd[i]) == 0)
            return EXTERNAL;
    }

    return NO_COMMAND;
}

/*
 * extract_external_commands
 * Reads External.txt line-by-line and populates external_commands[].
 */
void extract_external_commands(char **external_commands)
{
    FILE *fp = fopen("External.txt", "r");
    if (fp == NULL)
    {
        perror(ANSI_COLOR_RED
               "Error: could not open External.txt"
               ANSI_COLOR_RESET);
        return;
    }

    char line[256];
    int  index = 0;

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        /* Strip trailing newline / carriage return */
        int len = strlen(line);
        while (len > 0 &&
               (line[len-1] == '\n' || line[len-1] == '\r'))
        {
            line[--len] = '\0';
        }

        if (len == 0)
            continue;   /* skip blank lines */

        /* Allocate and store the command name */
        external_commands[index] = (char *)malloc(len + 1);
        if (external_commands[index] == NULL)
        {
            perror("malloc");
            break;
        }
        strcpy(external_commands[index], line);
        index++;
    }

    external_commands[index] = NULL;   /* NULL-terminate the list */
    fclose(fp);
}


/* ================================================================== */
/*  2. BUILTIN EXECUTION                                               */
/* ================================================================== */

/*
 * execute_internal_commands
 * Handles: exit, cd, pwd
 */
void execute_internal_commands(char *input_string)
{
    /* ---- exit ---- */
    if (strcmp(input_string, "exit") == 0)
    {
        printf(ANSI_COLOR_YELLOW "Exiting Minishell...\n" ANSI_COLOR_RESET);
        exit(0);
    }

    /* ---- cd ---- */
    else if (strncmp(input_string, "cd", 2) == 0)
    {
        char *path = NULL;

        if (input_string[2] == '\0' || input_string[2] == ' ')
        {
            /* "cd" alone or "cd <path>" */
            path = (input_string[2] == ' ')
                       ? (input_string + 3)
                       : getenv("HOME");
        }

        if (path == NULL || path[0] == '\0')
            path = getenv("HOME");

        if (chdir(path) != 0)
            perror(ANSI_COLOR_RED "cd" ANSI_COLOR_RESET);
    }

    /* ---- pwd ---- */
    else if (strcmp(input_string, "pwd") == 0)
    {
        char buf[1024];
        if (getcwd(buf, sizeof(buf)) != NULL)
            printf("%s\n", buf);
        else
            perror(ANSI_COLOR_RED "pwd" ANSI_COLOR_RESET);
    }

    /* ---- true / false ---- */
    else if (strcmp(input_string, "true") == 0)
    {
        g_last_status = 0;
    }
    else if (strcmp(input_string, "false") == 0)
    {
        g_last_status = 1;
    }
}

/*
 * echo_special
 * Handles the three special echo variants:
 *   echo $$     — print shell PID
 *   echo $?     — print exit status of last command
 *   echo $SHELL — print value of $SHELL env variable
 *
 * Falls back to printf() for all other "echo <text>" inputs.
 */
void echo_special(char *input_string)
{
    /* Skip past "echo" and any whitespace */
    char *arg = input_string + 4;
    while (*arg == ' ' || *arg == '\t')
        arg++;

    /* echo $$ — shell PID */
    if (strcmp(arg, "$$") == 0)
    {
        printf("%d\n", (int)getpid());
        return;
    }

    /* echo $? — last exit status */
    if (strcmp(arg, "$?") == 0)
    {
        printf("%d\n", g_last_status);
        return;
    }

    /* echo $SHELL — SHELL environment variable */
    if (strcmp(arg, "$SHELL") == 0)
    {
        char *shell = getenv("SHELL");
        printf("%s\n", shell ? shell : "/bin/sh");
        return;
    }

    /* General echo: print everything after "echo " */
    if (*arg != '\0')
        printf("%s\n", arg);
    else
        printf("\n");
}


/* ================================================================== */
/*  3. JOB-CONTROL BUILTINS                                           */
/* ================================================================== */

/*
 * builtin_jobs
 * Prints all currently tracked jobs (Running or Stopped).
 */
void builtin_jobs(void)
{
    if (job_list == NULL)
    {
        /* No jobs */
        return;
    }
    display_jobs();
}

/*
 * builtin_fg
 * Brings job job_no to the foreground.
 *
 * Steps:
 *   1. Find the job in the list.
 *   2. Send SIGCONT to resume it if stopped.
 *   3. Set child_pid and waitpid (WUNTRACED) to block the shell.
 *   4. Remove from job list if it exited normally.
 */
void builtin_fg(int job_no)
{
    JOB *j = search_job(job_no);
    if (j == NULL)
    {
        printf(ANSI_COLOR_RED
               "fg: %d: no such job\n"
               ANSI_COLOR_RESET, job_no);
        return;
    }

    /* Print the command being brought forward */
    printf("%s\n", j->cmd);

    /* Continue stopped process */
    kill(j->pid, SIGCONT);
    j->status = JOB_RUNNING;

    /* Track as foreground child */
    child_pid = j->pid;

    int wstatus;
    /* Wait for child termination or another stop */
    waitpid(j->pid, &wstatus, WUNTRACED);

    if (WIFSTOPPED(wstatus))
    {
        /* Stopped again (another Ctrl+Z) */
        j->status = JOB_STOPPED;
        printf("\n[%d]  Stopped    %s\n", j->job_no, j->cmd);
    }
    else
    {
        /* Exited — capture status and remove from list */
        if (WIFEXITED(wstatus))
            g_last_status = WEXITSTATUS(wstatus);

        delete_job(job_no);
    }

    child_pid = -1;
}

/*
 * builtin_bg
 * Resumes job job_no in the background.
 *
 * Steps:
 *   1. Find the job.
 *   2. Send SIGCONT to resume it.
 *   3. Mark it RUNNING and leave it in the job list.
 */
void builtin_bg(int job_no)
{
    JOB *j = search_job(job_no);
    if (j == NULL)
    {
        printf(ANSI_COLOR_RED
               "bg: %d: no such job\n"
               ANSI_COLOR_RESET, job_no);
        return;
    }

    /* Continue stopped process in background */
    kill(j->pid, SIGCONT);
    j->status = JOB_RUNNING;

    printf("[%d] %s &\n", j->job_no, j->cmd);
}


/* ================================================================== */
/*  4. JOB LINKED-LIST HELPERS                                        */
/* ================================================================== */

/*
 * add_job
 * Allocates a new JOB node and appends it to job_list.
 * Returns a pointer to the newly created node.
 */
JOB *add_job(pid_t pid, const char *cmd, int status)
{
    JOB *new_node = (JOB *)malloc(sizeof(JOB));
    if (new_node == NULL)
    {
        perror("malloc");
        return NULL;
    }

    g_job_counter++;
    new_node->job_no = g_job_counter;
    new_node->pid    = pid;
    new_node->status = status;
    strncpy(new_node->cmd, cmd, sizeof(new_node->cmd) - 1);
    new_node->cmd[sizeof(new_node->cmd) - 1] = '\0';
    new_node->link   = NULL;

    /* Append at end of list */
    if (job_list == NULL)
    {
        job_list = new_node;
    }
    else
    {
        JOB *temp = job_list;
        while (temp->link != NULL)
            temp = temp->link;
        temp->link = new_node;
    }

    return new_node;
}

/*
 * delete_job
 * Removes the node with the matching job_no from job_list and frees it.
 */
void delete_job(int job_no)
{
    JOB *prev = NULL;
    JOB *curr = job_list;

    while (curr != NULL)
    {
        if (curr->job_no == job_no)
        {
            if (prev == NULL)
                job_list = curr->link;   /* removing head */
            else
                prev->link = curr->link;

            free(curr);
            return;
        }
        prev = curr;
        curr = curr->link;
    }
}

/*
 * search_job
 * Returns a pointer to the JOB node with the matching job_no,
 * or NULL if not found.
 */
JOB *search_job(int job_no)
{
    JOB *temp = job_list;
    while (temp != NULL)
    {
        if (temp->job_no == job_no)
            return temp;
        temp = temp->link;
    }
    return NULL;
}

/*
 * search_job_by_pid
 * Returns a pointer to the JOB node with the matching pid,
 * or NULL if not found.  Used by signal handlers.
 */
JOB *search_job_by_pid(pid_t pid)
{
    JOB *temp = job_list;
    while (temp != NULL)
    {
        if (temp->pid == pid)
            return temp;
        temp = temp->link;
    }
    return NULL;
}

/*
 * display_jobs
 * Prints all jobs in the list in the format:
 *   [1] Running   sleep 100 &
 *   [2] Stopped   vim file.c
 */
void display_jobs(void)
{
    JOB *temp = job_list;
    while (temp != NULL)
    {
        const char *status_str;
        switch (temp->status)
        {
            case JOB_RUNNING:  status_str = "Running "; break;
            case JOB_STOPPED:  status_str = "Stopped "; break;
            case JOB_DONE:     status_str = "Done    "; break;
            default:           status_str = "Unknown "; break;
        }
        printf("[%d]  %s   %s\n", temp->job_no, status_str, temp->cmd);
        temp = temp->link;
    }
}


/* ================================================================== */
/*  5. EXTERNAL COMMAND EXECUTION                                     */
/* ================================================================== */

/*
 * execute_external_commands
 * Called from inside the child process (after fork()).
 * Tokenises input_string, handles n-pipe chaining, and calls execvp().
 *
 * Supports:
 *   ls -l
 *   date
 *   cat file.txt
 *   sleep 20        (foreground)
 *   sleep 20 &      (background — '&' stripped in def.c before this call)
 *   ls -l | grep .c | wc -l   (n-pipe)
 */
void execute_external_commands(char *input_string)
{
    char input_copy[1024];
    strncpy(input_copy, input_string, sizeof(input_copy) - 1);
    input_copy[sizeof(input_copy) - 1] = '\0';

    /* ---- Count pipe operators ---- */
    int pipe_count = 0;
    for (int i = 0; input_copy[i] != '\0'; i++)
        if (input_copy[i] == '|') pipe_count++;

    int cmd_count = pipe_count + 1;

    /* ---- No pipe: simple execvp ---- */
    if (cmd_count == 1)
    {
        char *args[128];
        int   argc = 0;
        char  seg_copy[1024];
        strncpy(seg_copy, input_string, sizeof(seg_copy) - 1);
        seg_copy[sizeof(seg_copy) - 1] = '\0';

        char *word = strtok(seg_copy, " \t");
        while (word != NULL && argc < 127)
        {
            args[argc++] = word;
            word = strtok(NULL, " \t");
        }
        args[argc] = NULL;

        /* Replace child image with command */
        execvp(args[0], args);

        /* execvp only returns on error */
        perror(ANSI_COLOR_RED "execvp" ANSI_COLOR_RESET);
        exit(1);
    }

    /* ---- n-pipe logic ---- */

    /* Split input into pipe segments */
    char *segments[64];
    int   seg_idx = 0;
    char *token = strtok(input_copy, "|");
    while (token != NULL && seg_idx < 64)
    {
        segments[seg_idx++] = token;
        token = strtok(NULL, "|");
    }

    /* Create all pipe file-descriptor pairs upfront */
    int pipefds[2 * pipe_count];
    for (int i = 0; i < pipe_count; i++)
    {
        if (pipe(pipefds + i * 2) < 0)
        {
            perror("pipe");
            exit(1);
        }
    }

    for (int i = 0; i < cmd_count; i++)
    {
        /* Tokenise this segment */
        char *args[128];
        int   argc = 0;
        char *word = strtok(segments[i], " \t");
        while (word != NULL && argc < 127)
        {
            args[argc++] = word;
            word = strtok(NULL, " \t");
        }
        args[argc] = NULL;

        /* Create child process */
        pid_t pid = fork();
        if (pid == 0)
        {
            /* Child: wire stdin from previous pipe */
            if (i > 0)
                dup2(pipefds[(i - 1) * 2], STDIN_FILENO);

            /* Wire stdout to next pipe */
            if (i < cmd_count - 1)
                dup2(pipefds[i * 2 + 1], STDOUT_FILENO);

            /* Close all pipe fds in child */
            for (int j = 0; j < 2 * pipe_count; j++)
                close(pipefds[j]);

            /* Replace child image with command */
            execvp(args[0], args);
            perror(ANSI_COLOR_RED "execvp" ANSI_COLOR_RESET);
            exit(1);
        }
        else if (pid < 0)
        {
            perror("fork");
            exit(1);
        }
    }

    /* Parent of pipeline: close all pipe fds */
    for (int i = 0; i < 2 * pipe_count; i++)
        close(pipefds[i]);

    /* Wait for child termination of all pipeline stages */
    for (int i = 0; i < cmd_count; i++)
        wait(NULL);

    exit(0);   /* Pipeline wrapper child exits cleanly */
}
