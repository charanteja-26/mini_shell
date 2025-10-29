#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>

volatile sig_atomic_t fg_pid = 0;

static void sigint_handler(int signo) {
    if (fg_pid > 0) {
        kill(fg_pid, SIGINT);
    } else {
        /* write newline to keep prompt tidy */
        const char nl = '\n';
        write(STDOUT_FILENO, &nl, 1);
    }
}

static void sigtstp_handler(int signo) {
    if (fg_pid > 0) {
        kill(fg_pid, SIGTSTP);
        char buf[128];
        int n = snprintf(buf, sizeof(buf), "Stopped child pid: %d\n", (int)fg_pid);
        if (n > 0) write(STDOUT_FILENO, buf, (size_t)n);
    } else {
        const char nl = '\n';
        write(STDOUT_FILENO, &nl, 1);
    }
}

/* Read link to /proc/self/exe to get executable path; fallback to argv[0] not available here */
static void get_shell_path(char *buf, size_t size) {
    ssize_t r = readlink("/proc/self/exe", buf, size - 1);
    if (r <= 0) {
        /* fallback */
        buf[0] = '\0';
    } else {
        buf[r] = '\0';
    }
}

static void print_prompt() {
    char *ps1 = getenv("PS1");
    if (ps1 && ps1[0] != '\0') {
        printf("%s", ps1);
    } else {
        printf("msh > ");
    }
    fflush(stdout);
}

/* trim leading and trailing whitespace and newline */
static char *trim(char *s) {
    if (!s) return s;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    return s;
}

/* Check for simple assignment VAR=VALUE with no spaces around '=' */
static int handle_assignment(char *line) {
    char *eq = strchr(line, '=');
    if (!eq) return 0;
    /* Ensure no spaces around '=' and that the token has no spaces */
    /* If there's a space before or after '=', don't treat as assignment */
    if (eq != line && (*(eq - 1) == ' ' || *(eq - 1) == '\t')) return 0;
    if (*(eq + 1) == ' ' || *(eq + 1) == '\t') return 0;
    /* Ensure entire line contains no spaces (we require VAR=VALUE with no spaces anywhere) */
    for (char *p = line; *p; ++p) {
        if (*p == ' ' || *p == '\t') return 0;
    }
    /* Split */
    *eq = '\0';
    char *name = line;
    char *value = eq + 1;
    if (!name || !name[0]) return 0;
    /* Validate name: starts with letter or underscore */
    if (!((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) return 0;
    for (char *p = name + 1; *p; ++p) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return 0;
    }
    setenv(name, value, 1);
    return 1;
}

int main(int argc, char **argv) {
    /* Set up signal handlers */
    struct sigaction sa_int, sa_tstp;
    memset(&sa_int, 0, sizeof(sa_int));
    memset(&sa_tstp, 0, sizeof(sa_tstp));
    sa_int.sa_handler = sigint_handler;
    sa_tstp.sa_handler = sigtstp_handler;
    /* Don't restart syscalls so that getline can be interrupted */
    sigaction(SIGINT, &sa_int, NULL);
    sigaction(SIGTSTP, &sa_tstp, NULL);

    char shell_path[PATH_MAX];
    get_shell_path(shell_path, sizeof(shell_path));
    if (shell_path[0] != '\0') {
        /* set $SHELL to point to this executable path (so echo $SHELL prints it) */
        setenv("SHELL", shell_path, 1);
    }

    /* Initialize PWD and OLDPWD so `cd -` has a sensible value on first use. */
    char init_cwd[PATH_MAX];
    if (getcwd(init_cwd, sizeof(init_cwd)) != NULL) {
        /* Ensure PWD reflects current directory */
        setenv("PWD", init_cwd, 1);
        /* If OLDPWD is unset, initialize it to the current directory so `cd -` won't error immediately */
        if (!getenv("OLDPWD")) {
            setenv("OLDPWD", init_cwd, 1);
        }
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    int last_status = 0;

    while (1) {
        /* Print prompt */
        print_prompt();

        errno = 0;
        nread = getline(&line, &len, stdin);
        if (nread == -1) {
            if (feof(stdin)) {
                /* Ctrl-D or EOF: exit shell */
                printf("\n");
                break;
            }
            if (errno == EINTR) {
                /* Interrupted by signal; re-display prompt */
                continue;
            }
            perror("getline");
            continue;
        }

        /* Remove trailing newline and trim */
        if (nread > 0 && line[nread - 1] == '\n') line[nread - 1] = '\0';
        char *cmdline = trim(line);
        if (!cmdline || cmdline[0] == '\0') {
            /* empty input: show prompt again */
            continue;
        }

        /* Assignment handling: PS1=NEW_PROMPT style without spaces */
        if (handle_assignment(cmdline)) {
            continue;
        }

        /* Tokenize */
        const int MAX_TOK = 256;
        char *tokens[MAX_TOK];
        int t = 0;
        char *saveptr = NULL;
        char *tok = strtok_r(cmdline, " \t", &saveptr);
        while (tok && t < MAX_TOK - 1) {
            tokens[t++] = tok;
            tok = strtok_r(NULL, " \t", &saveptr);
        }
        tokens[t] = NULL;
        if (t == 0) continue;

        /* Builtins */
        if (strcmp(tokens[0], "exit") == 0) {
            free(line);
            exit(0);
        }
        else if (strcmp(tokens[0], "cd") == 0) {
            char *dir = NULL;
            char oldcwd[PATH_MAX] = "";
            int print_new = 0;

            /* capture current working directory (if possible) */
            if (getcwd(oldcwd, sizeof(oldcwd)) == NULL) {
                oldcwd[0] = '\0';
            }

            if (t >= 2) {
                /* support `cd -` to go to OLDPWD */
                if (strcmp(tokens[1], "-") == 0) {
                    char *oldd = getenv("OLDPWD");
                    if (!oldd) {
                        fprintf(stderr, "cd: OLDPWD not set\n");
                        last_status = 1;
                        continue;
                    }
                    dir = oldd;
                    print_new = 1; /* print new cwd after successful chdir */
                } else {
                    dir = tokens[1];
                }
            } else {
                dir = getenv("HOME");
            }
            if (!dir) dir = "/";

            if (chdir(dir) != 0) {
                perror("cd");
                last_status = 1;
            } else {
                /* Update OLDPWD to previous cwd only on success */
                if (oldcwd[0] != '\0') {
                    setenv("OLDPWD", oldcwd, 1);
                }
                /* Update PWD to new cwd */
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    setenv("PWD", cwd, 1);
                    if (print_new) printf("%s\n", cwd);
                }
                last_status = 0;
            }
            continue;
        }
        else if (strcmp(tokens[0], "pwd") == 0) {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                perror("pwd");
                last_status = 1;
            } else {
                printf("%s\n", cwd);
                last_status = 0;
            }
            continue;
        }
        else if (strcmp(tokens[0], "echo") == 0) {
            /* print arguments with variable substitution for $? $$ $SHELL and env vars */
            for (int i = 1; i < t; ++i) {
                char *a = tokens[i];
                if (a[0] == '$') {
                    if (strcmp(a, "$?") == 0) {
                        printf("%d", last_status);
                    } else if (strcmp(a, "$$") == 0) {
                        printf("%d", (int)getpid());
                    } else if (strcmp(a, "$SHELL") == 0) {
                        char *sh = getenv("SHELL");
                        if (sh) printf("%s", sh);
                    } else {
                        /* other env var */
                        char *name = a + 1;
                        char *val = getenv(name);
                        if (val) printf("%s", val);
                    }
                } else {
                    printf("%s", a);
                }
                if (i < t - 1) printf(" ");
            }
            printf("\n");
            last_status = 0;
            continue;
        }

        /* External command: fork and exec */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            last_status = 1;
            continue;
        }
        if (pid == 0) {
            /* Child: restore default signal actions */
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            /* exec */
            execvp(tokens[0], tokens);
            /* If execvp returns, it's an error */
            fprintf(stderr, "%s: command not found\n", tokens[0]);
            _exit(127);
        } else {
            fg_pid = pid;
            int status = 0;
            /* Wait for child to change state (exit or stopped) */
            if (waitpid(pid, &status, WUNTRACED) == -1) {
                perror("waitpid");
                last_status = 1;
            } else {
                if (WIFEXITED(status)) {
                    last_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    last_status = 128 + WTERMSIG(status);
                } else if (WIFSTOPPED(status)) {
                    /* child was stopped (SIGTSTP) */
                    printf("Stopped child pid: %d\n", (int)pid);
                    last_status = 0;
                }
            }
            fg_pid = 0;
        }
    }

    free(line);
    return 0;
}
