# Mini Shell

A Linux command interpreter written in C, supporting builtin commands, external command execution, job control, and n-stage pipes — built with a clean 5-file architecture.

**Author:** Tarun Gaurav
**Language:** C (C99)
**Platform:** Linux (Ubuntu / Debian)

---

## Table of Contents

- [Project Structure](#project-structure)
- [Architecture](#architecture)
- [Flow Diagram](#flow-diagram)
- [Features](#features)
- [Data Structures](#data-structures)
- [Build & Run](#build--run)
- [Demo Output](#demo-output)
- [Signal Handling](#signal-handling)
- [File Reference](#file-reference)

---

## Project Structure

```
MiniShell/
│
├── shell.c          →  main(), global variable definitions, signal handlers
├── def.c            →  scan_input() REPL loop, get_command(), PS1 handling
├── command.c        →  all command logic (builtins, job control, linked list, execvp)
├── shell.h          →  shared macros, JOB typedef, extern declarations, prototypes
└── External.txt     →  newline-separated list of recognised external commands
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                        shell.c                          │
│  main()  ·  signal_handler()  ·  sigchld_handler()      │
│  Globals: external_cmd[], child_pid, g_last_status,      │
│           job_list, g_job_counter, g_prompt              │
└───────────────────────┬─────────────────────────────────┘
                        │ calls
                        ▼
┌─────────────────────────────────────────────────────────┐
│                        def.c                            │
│  scan_input()   — REPL: read → classify → dispatch      │
│  get_command()  — extract first token from input line   │
└───────┬─────────────────────────────────┬───────────────┘
        │ builtin                         │ external
        ▼                                 ▼
┌───────────────────────┐     ┌───────────────────────────┐
│      command.c        │     │        command.c           │
│                       │     │                            │
│  execute_internal_    │     │  execute_external_         │
│  commands()           │     │  commands()                │
│   · cd / pwd / exit   │     │   · fork() + execvp()      │
│   · true / false      │     │   · n-pipe chaining        │
│                       │     │   · background (&)         │
│  echo_special()       │     └───────────────────────────┘
│   · echo $$           │
│   · echo $?           │     ┌───────────────────────────┐
│   · echo $SHELL       │     │      command.c             │
│                       │     │   Job linked-list          │
│  builtin_jobs()       │     │                            │
│  builtin_fg(N)        │     │  add_job()                 │
│  builtin_bg(N)        │     │  delete_job()              │
└───────────────────────┘     │  search_job()              │
                              │  search_job_by_pid()       │
                              │  display_jobs()            │
                              └───────────────────────────┘
```

---

## Flow Diagram

```
                          ┌─────────────┐
                          │   main()    │
                          │  shell.c    │
                          └──────┬──────┘
                                 │ extract_external_commands()
                                 │ register SIGINT / SIGTSTP / SIGCHLD
                                 ▼
                          ┌─────────────┐
                     ┌───▶│ scan_input()│◀──────────────────────┐
                     │    │   def.c     │                       │
                     │    └──────┬──────┘                       │
                     │           │ fgets()                      │
                     │           ▼                              │
                     │    ┌─────────────┐   empty              │
                     │    │  Read line  │──────────────────────▶│ (loop)
                     │    └──────┬──────┘                       │
                     │           │                              │
                     │           ▼                              │
                     │    ┌─────────────┐   PS1=xxx            │
                     │    │  PS1 check  │──────────────────────▶│ update prompt
                     │    └──────┬──────┘                       │
                     │           │                              │
                     │           ▼                              │
                     │    ┌─────────────┐   ends with &        │
                     │    │  '&' detect │──────────────────────▶│ background=1
                     │    └──────┬──────┘                       │
                     │           │                              │
                     │           ▼                              │
                     │    ┌─────────────────────────────┐       │
                     │    │     get_command()            │       │
                     │    │  extract first token         │       │
                     │    └──────────────┬──────────────┘       │
                     │                   │                      │
                     │         ┌─────────┴──────────┐           │
                     │         ▼                    ▼           │
                     │   echo / jobs          check_command_    │
                     │   fg / bg              type()            │
                     │         │              BUILTIN│EXTERNAL  │
                     │         │                     │          │
                     │   ┌─────┴──────┐    ┌─────────┴────────┐ │
                     │   │echo_special│    │    fork()         │ │
                     │   │builtin_jobs│    └────────┬─────────┘ │
                     │   │builtin_fg  │             │           │
                     │   │builtin_bg  │    ┌────────┴─────────┐ │
                     │   └─────┬──────┘    │ background?       │ │
                     │         │           └──┬────────────┬──┘ │
                     │         │           yes│            │no   │
                     │         │       ┌──────┘      ┌────┘     │
                     │         │       ▼             ▼          │
                     │         │  add_job()     waitpid()       │
                     │         │  print [N] PID WUNTRACED       │
                     │         │  (SIGCHLD reaps)               │
                     │         │              │                  │
                     └─────────┴──────────────┴──────────────────┘
                                           loop
```

### Job State Machine

```
                   cmd &                 fg N
                ┌──────────┐        ┌──────────┐
                │          ▼        │          ▼
           ┌────┴──────────────────────────────────┐
  fork() ──▶│          RUNNING                     │
           └────┬──────────────────────────────────┘
                │                       │
              Ctrl+Z                  exits
           kill(SIGSTOP)          (SIGCHLD fires)
                │                       │
                ▼                       ▼
           ┌─────────┐            ┌──────────┐
           │ STOPPED │            │   DONE   │
           └────┬────┘            └──────────┘
                │ bg N                delete_job()
                │ kill(SIGCONT)
                └──────────────────▶ RUNNING
```

---

## Features

### Builtin Commands

| Command        | Description                              |
|----------------|------------------------------------------|
| `cd [path]`    | Change directory; bare `cd` goes to HOME |
| `pwd`          | Print working directory                  |
| `exit`         | Exit the shell                           |
| `true`         | Set `$?` to 0                            |
| `false`        | Set `$?` to 1                            |
| `echo $$`      | Print shell PID                          |
| `echo $?`      | Print last command exit status           |
| `echo $SHELL`  | Print `$SHELL` environment variable      |
| `echo <text>`  | Print arbitrary text                     |
| `jobs`         | List all background/stopped jobs         |
| `fg N`         | Bring job N to foreground                |
| `bg N`         | Resume stopped job N in background       |
| `PS1=<str>`    | Change the prompt string                 |

### External Commands

Any command listed in `External.txt` is executed via `fork()` + `execvp()`. The file ships with 150+ standard Linux utilities (`ls`, `cat`, `grep`, `sleep`, `date`, `ps`, etc.).

### Pipes

N-stage pipes are supported:

```bash
ls -l | grep .c | wc -l
cat shell.h | grep typedef | head -5
```

### Background Execution

```bash
sleep 100 &        # runs in background, prints [1] PID
sleep 200 &        # [2] PID
jobs               # list both
fg 1               # bring job 1 to foreground
bg 1               # resume job 1 in background after Ctrl+Z
```

---

## Data Structures

### JOB Node (singly linked list)

```c
typedef struct node
{
    int          job_no;     /* Job number shown to user: [1], [2], … */
    pid_t        pid;        /* PID of the background/stopped process  */
    char         cmd[256];   /* Command string as typed by the user    */
    int          status;     /* JOB_RUNNING | JOB_STOPPED | JOB_DONE  */
    struct node *link;       /* Pointer to next node                   */
} JOB;
```

### Global State (defined in `shell.c`)

```c
char  *external_cmd[256];    /* commands loaded from External.txt      */
pid_t  child_pid;            /* foreground child PID (-1 if none)      */
char   g_prompt[256];        /* active prompt string                   */
int    g_last_status;        /* $? exit status of last command         */
JOB   *job_list;             /* head of job linked list                */
int    g_job_counter;        /* monotonically increasing job number    */
```

---

## Build & Run

### Prerequisites

```bash
gcc --version   # GCC 7+ required for C99
```

### Compile

```bash
gcc -Wall -Wextra -o minishell shell.c def.c command.c
```

### Run

```bash
./minishell
```

> `External.txt` must be present in the same directory as the binary.

---

## Demo Output

### Special Variables

```
Minishell$ echo $$
4823

Minishell$ false
Minishell$ echo $?
1

Minishell$ true
Minishell$ echo $?
0

Minishell$ echo $SHELL
/bin/bash

Minishell$ notacommand
notacommand: command not found
Minishell$ echo $?
127
```

### Builtin Commands

```
Minishell$ pwd
/home/tarun/MiniShell

Minishell$ cd /tmp
Minishell$ pwd
/tmp

Minishell$ cd
Minishell$ pwd
/home/tarun

Minishell$ PS1=dev@box:~$
dev@box:~$ pwd
/home/tarun

dev@box:~$ exit
Exiting Minishell...
```

### Background Jobs

```
Minishell$ sleep 30 &
[1] 5021

Minishell$ sleep 60 &
[2] 5034

Minishell$ jobs
[1]  Running    sleep 30
[2]  Running    sleep 60

Minishell$ ls
command.c  def.c  minishell  shell.c  shell.h

[1] Done    sleep 30
```

### Job Control — fg / bg

```
Minishell$ sleep 100 &
[1] 4521

Minishell$ sleep 200 &
[2] 4544

Minishell$ jobs
[1]  Running    sleep 100
[2]  Running    sleep 200

Minishell$ fg 1
sleep 100
^Z
[1]  Stopped    sleep 100

Minishell$ jobs
[1]  Stopped    sleep 100
[2]  Running    sleep 200

Minishell$ bg 1
[1] sleep 100 &

Minishell$ jobs
[1]  Running    sleep 100
[2]  Running    sleep 200
```

### Pipes

```
Minishell$ ls -l | grep .c
-rw-r--r-- 1 tarun tarun 14330 Jun 12 command.c
-rw-r--r-- 1 tarun tarun  5980 Jun 12 def.c
-rw-r--r-- 1 tarun tarun  8210 Jun 12 shell.c

Minishell$ ls -l | grep .c | wc -l
3

Minishell$ cat shell.h | grep typedef
typedef struct node
```

### Signal Handling

```
Minishell$ sleep 100
^C                          ← Ctrl+C kills foreground child

Minishell$ sleep 100
^Z
[1]  Stopped    sleep 100   ← Ctrl+Z suspends and adds to job list

Minishell$ jobs
[1]  Stopped    sleep 100

Minishell$
^C                          ← Ctrl+C at idle: just reprints prompt
Minishell$
```

---

## Signal Handling

| Signal    | Source     | Behaviour                                                    |
|-----------|------------|--------------------------------------------------------------|
| `SIGINT`  | `Ctrl+C`   | Kills foreground child; reprints prompt if no child running  |
| `SIGTSTP` | `Ctrl+Z`   | Stops foreground child via `SIGSTOP`; inserts into job list  |
| `SIGCHLD` | kernel     | Non-blocking reap with `waitpid(WNOHANG)`; prints `Done`     |

---

## File Reference

### `shell.h`
Single header included by all three `.c` files. Defines:
- Macros: `BUILTIN`, `EXTERNAL`, `NO_COMMAND`, `JOB_RUNNING`, `JOB_STOPPED`, `JOB_DONE`, ANSI colour codes
- `JOB` struct typedef
- `extern` declarations for all globals
- All function prototypes

### `shell.c`
- `main()` — initialisation, signal registration, entry into REPL
- `signal_handler()` — handles `SIGINT` and `SIGTSTP`
- `sigchld_handler()` — reaps finished background children

### `def.c`
- `scan_input()` — full REPL loop: reads input, detects `&`, dispatches commands
- `get_command()` — extracts first whitespace-delimited token from input

### `command.c`
- `check_command_type()` — returns `BUILTIN`, `EXTERNAL`, or `NO_COMMAND`
- `extract_external_commands()` — loads `External.txt` into `external_cmd[]`
- `execute_internal_commands()` — handles `cd`, `pwd`, `exit`, `true`, `false`
- `echo_special()` — handles `echo $$`, `echo $?`, `echo $SHELL`, plain echo
- `builtin_jobs()` / `builtin_fg()` / `builtin_bg()` — job control
- `add_job()` / `delete_job()` / `search_job()` / `search_job_by_pid()` / `display_jobs()` — linked list
- `execute_external_commands()` — `fork()` + `execvp()` with n-pipe support
