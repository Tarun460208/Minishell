/*
------------------------------------------------------
Description :
    Contains scan_input() — the main read-eval-print
    loop — and get_command(), which extracts the first
    token from an input line.

    scan_input handles:
      • PS1= prompt changes
      • Background execution  (cmd &)
      • Foreground execution  (regular commands)
      • Dispatching to execute_internal_commands()
        or execute_external_commands()
------------------------------------------------------
*/

#include "shell.h"

/* ------------------------------------------------------------------ */
/*  get_command — extract the first word from input_string            */
/* ------------------------------------------------------------------ */
char *get_command(char *input_string)
{
    static char cmd_buf[256];

    int i = 0;
    while (input_string[i] != '\0' &&
           input_string[i] != ' '  &&
           input_string[i] != '\t' &&
           i < (int)(sizeof(cmd_buf) - 1))
    {
        cmd_buf[i] = input_string[i];
        i++;
    }
    cmd_buf[i] = '\0';

    return cmd_buf;
}

/* ------------------------------------------------------------------ */
/*  scan_input — REPL: read → classify → execute → repeat            */
/* ------------------------------------------------------------------ */
void scan_input(char *prompt, char *input_string)
{
    while (1)
    {
        /* Keep the global prompt in sync for signal_handler */
        strncpy(g_prompt, prompt, sizeof(g_prompt) - 1);
        g_prompt[sizeof(g_prompt) - 1] = '\0';

        printf("%s", prompt);
        fflush(stdout);

        /* Read a line; treat EOF (Ctrl+D) as graceful exit */
        if (fgets(input_string, 1024, stdin) == NULL)
        {
            printf("\n");
            exit(0);
        }

        /* Strip trailing newline / carriage return */
        int len = strlen(input_string);
        while (len > 0 &&
               (input_string[len-1] == '\n' || input_string[len-1] == '\r'))
        {
            input_string[--len] = '\0';
        }

        if (len == 0)
            continue;   /* skip blank lines */

        /* ---- PS1= prompt change ---- */
        if (strncmp(input_string, "PS1=", 4) == 0)
        {
            if (input_string[4] == '\0' || input_string[4] == ' ')
            {
                printf(ANSI_COLOR_RED
                       "Error: invalid prompt value\n"
                       ANSI_COLOR_RESET);
                continue;
            }
            strncpy(prompt, input_string + 4, 255);
            prompt[255] = '\0';
            /* Append a trailing space for readability */
            int plen = strlen(prompt);
            if (prompt[plen - 1] != ' ')
            {
                prompt[plen]     = ' ';
                prompt[plen + 1] = '\0';
            }
            continue;
        }

        /* ---- Detect background operator '&' at end of line ---- */
        int background = 0;
        if (len > 0 && input_string[len-1] == '&')
        {
            background = 1;
            /* Strip the '&' and any trailing spaces */
            input_string[--len] = '\0';
            while (len > 0 && input_string[len-1] == ' ')
                input_string[--len] = '\0';
        }

        /* ---- Classify the command ---- */
        char *cmd = get_command(input_string);
        int   type = check_command_type(cmd);

        /* ----------------------------------------------------------------
         * Special echo variants: echo $$, echo $?, echo $SHELL
         * These are handled before the normal BUILTIN path so they always
         * take priority over the external 'echo' in External.txt.
         * ---------------------------------------------------------------- */
        if (strcmp(cmd, "echo") == 0)
        {
            echo_special(input_string);
            continue;
        }

        /* ---- fg, bg, jobs — job-control builtins ---- */
        if (strcmp(cmd, "jobs") == 0)
        {
            builtin_jobs();
            continue;
        }

        if (strcmp(cmd, "fg") == 0)
        {
            int jnum = 1;   /* default to job 1 if no argument given */
            if (len > 3)
                jnum = atoi(input_string + 3);
            builtin_fg(jnum);
            continue;
        }

        if (strcmp(cmd, "bg") == 0)
        {
            int jnum = 1;
            if (len > 3)
                jnum = atoi(input_string + 3);
            builtin_bg(jnum);
            continue;
        }

        /* ---- BUILTIN commands (cd, pwd, exit, …) ---- */
        if (type == BUILTIN)
        {
            execute_internal_commands(input_string);
            continue;
        }

        /* ---- EXTERNAL commands ---- */
        if (type == EXTERNAL)
        {
            pid_t pid = fork();   /* Create child process */

            if (pid > 0)
            {
                /* Parent */
                if (background)
                {
                    /* Register the job before the child can finish */
                    JOB *j = add_job(pid, input_string, JOB_RUNNING);
                    printf("[%d] %d\n", j->job_no, (int)pid);
                    /* Do NOT wait — let SIGCHLD handler reap it */
                }
                else
                {
                    /* Foreground: track PID for Ctrl+C / Ctrl+Z */
                    child_pid = pid;
                    int wstatus;
                    waitpid(pid, &wstatus, WUNTRACED);  /* Wait, or until stopped */

                    if (WIFSTOPPED(wstatus))
                    {
                        /* Child was stopped by Ctrl+Z */
                        JOB *j = add_job(pid, input_string, JOB_STOPPED);
                        printf("\n[%d]  Stopped    %s\n",
                               j->job_no, input_string);
                    }
                    else if (WIFEXITED(wstatus))
                    {
                        g_last_status = WEXITSTATUS(wstatus);
                    }
                    else if (WIFSIGNALED(wstatus))
                    {
                        g_last_status = 1;
                    }

                    child_pid = -1;
                }
            }
            else if (pid == 0)
            {
                /* Child: restore default signal behaviour */
                signal(SIGINT,  SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);

                /* Replace child image with command */
                execute_external_commands(input_string);
                exit(1);   /* reached only on execvp failure */
            }
            else
            {
                perror(ANSI_COLOR_RED "fork" ANSI_COLOR_RESET);
            }
            continue;
        }

        /* ---- Unknown command ---- */
        printf(ANSI_COLOR_RED "%s: command not found\n" ANSI_COLOR_RESET, cmd);
        g_last_status = 127;
    }
}
