Mini Shell (msh)
=================

Introduction
------------

By definition, the Operating System (OS) acts as an interface between the user and the hardware. For the user the interface can be given either in form of Graphical User Interface (GUI) or Command Line Interface (CLI). Windows OS is a typical example for GUI example. Before the GUI supported by operating system, it was mainly operated by CLI. It was given different names in different Operating Systems. Some popular examples being DOS prompt of Windows or BASH prompt of Linux.

These interfaces typically run in a text window, allowing the user to type commands which cause actions. Upon receiving commands the OS will parse the commands for its correctness and invoke appropriate application to perform a specific action. These action can range from opening a file to terminating an application. In case of Linux the BASH shell reads commands from a file, called a script. Like all Unix shells, it supports piping and variables as well, and eventually provides an output.

This project implements a small educational shell called `msh` that mimics a subset of BASH behavior using standard Linux system calls and basic IPC (signals).

Features
--------

- Prompt shown as `msh > ` by default
- Prompt can be customized using `PS1` environment variable or by entering `PS1=NEW_PROMPT` (no spaces around `=`)
- Execute external commands using `fork` + `execvp` (parent waits for child)
- Built-in commands: `exit`, `cd`, `pwd`, `echo`
- Special variables supported in `echo`: `$?` (last command exit status), `$$` (msh PID), `$SHELL` (path to msh executable)
- Signal handling: Ctrl-C (SIGINT) and Ctrl-Z (SIGTSTP) forwarded to foreground child; if no foreground job, Ctrl-C just re-displays the prompt

Limitations / Notes
-------------------

- This is a minimal educational shell: no job control list, no piping or redirection, no command history.
- Assignments like `VAR=VALUE` set environment variables only if entered without spaces (e.g., `PS1=my>`). `PS1 = my>` will be treated as a normal command.

cd and OLDPWD behaviour
-----------------------

- `cd -` is supported and switches to the previous working directory (the value of `OLDPWD`). After a successful `cd -` the new working directory is printed, matching common shell behavior.
- To avoid the error `cd: OLDPWD not set` when the shell starts, `msh` initializes `PWD` and `OLDPWD` to the current working directory at startup. Once you change directories, `OLDPWD` is updated on successful `cd` calls.

Examples:

- `pwd` — prints current working directory
- `cd /tmp` — change to /tmp
- `cd -` — return to previous directory and print it

Building
--------

This project compiles on Linux (or WSL) using `gcc` and `make`.

Build steps:

```powershell
# from the repository root
make
```

This will produce the `msh` executable.

Running
-------

Start the shell:

```powershell
./msh
```

Examples:

- Change prompt:
  - `PS1=prompt>`  (no spaces around `=`)
- Builtins:
  - `cd /tmp`
  - `pwd`
  - `exit`
  - `echo $?`
  - `echo $$`
  - `echo $SHELL`
- External commands:
  - `ls -la`
  - `sleep 10` then press `Ctrl-C` to send SIGINT to the child
  - `sleep 10` then press `Ctrl-Z` to send SIGTSTP to the child

 
- Pipelining and redirection
- Background jobs and job list
- Tab completion

Enjoy!

