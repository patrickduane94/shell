/* Sources referenced outside of class materials include (so far):
Redirection in C: https://www.youtube.com/watch?v=5fnVr-zH-SE&t=834s
Processes: https://www.youtube.com/watch?v=1R9h-H2UnLs 
pid_t to char* conversion: https://stackoverflow.com/questions/251900/how-to-convert-a-pid-t
Many C functions: https://www.tutorialspoint.com/index.htm
And here: https://linux.die.net/
*/ 

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h> 
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <time.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

int last_fg_exit_status = 0;
pid_t child_pid;
int child_process_id;
int child_status;
int bg_process; // 1 if process is running in background, 0 if not
int bg_operator = 0; // 1 if background operator & is last word, 0 if not
int command_args_index = 0; // index of the current argument in the command

void sigint_handler(int sig) {}
void sigtstp_handler(int sig) {}

char *command_args[MAX_WORDS]; // will store commands after removing operators
char *words[512];
size_t wordsplit(char const *line);
char *expand(char const *word);

void short_sleep() {
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = 100000000L; // 100 milliseconds
    nanosleep(&req, &rem);
}

// Look for background processes in the same group as parent which are ready to be cleaned up
// WNOHANG is used to avoid blocking
// Print specified messages to stderr
void manage_bg_processes() {
    pid_t id;
    int status;

    while ((id = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFSIGNALED(status)) {
            fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t)id, WTERMSIG(status));
        }
        if (WIFEXITED(status)) {
            fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t)id, WEXITSTATUS(status));
        }
        if (WIFSTOPPED(status)) {
            kill(id, SIGCONT);
            fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t)id);
        }
    }
  fflush(stderr);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, SIG_IGN);

    FILE *input = stdin;
    char *input_fn = "(stdin)";
    if (argc == 2) {
        input_fn = argv[1];
        input = fopen(input_fn, "re");
        if (!input) err(1, "%s", input_fn);
    } else if (argc > 2) {
        errx(1, "too many arguments");
    }

    char *line = NULL;
    size_t n = 0;
    for (;;) {
        short_sleep();
        manage_bg_processes();

        if (input == stdin) {
            // Print prompt in interactive mode
            char *prompt = getenv("PS1");
            if (prompt == NULL) {
                prompt = "";
            }
            fprintf(stderr, "%s", prompt);
        }

        ssize_t line_len = getline(&line, &n, input);
        if (line_len < 0) {
            exit(0);
        }

        size_t nwords = wordsplit(line);
        for (size_t i = 0; i < nwords; i++) {
            char *exp_word = expand(words[i]);
            free(words[i]);
            words[i] = exp_word;
        }

        // Check if background operator is last word
        if (nwords > 0 && strcmp(words[nwords - 1], "&") == 0) {
            bg_operator = 1;
            free(words[nwords - 1]); // free the memory for "&"
            words[nwords - 1] = NULL; // nullify the last word
            nwords--; // decrement the number of words
        } else {
            bg_operator = 0;
        }

        if (nwords == 0) {
            fprintf(stderr, "continue");
            continue;
        }

        bg_process = 1;

        for (size_t i = 0; i < nwords; i++) {
            if (strcmp(words[0], "exit") == 0) {
                bg_process = 0;
                if (nwords == 2) {
                    // If int, exit on that int (0 was not being recognized as int)
                    if ((atoi(words[1])) || (strcmp(words[1], "0") == 0)) {
                        last_fg_exit_status = atoi(words[1]);
                        exit(atoi(words[1]));
                    } else {
                        fprintf(stderr, "exit: %s: numeric argument required\n", words[1]);
                    }
                } else if (nwords == 1) {
                    exit(last_fg_exit_status);
                } else {
                    fprintf(stderr, "exit: too many arguments\n");
                }
            }
            if (strcmp(words[0], "cd") == 0) {
                bg_process = 0;
                if (nwords == 2) {
                    // If two arguments, attempt to change directory to that argument words[1]
                    if (chdir(words[1]) != 0) {
                        fprintf(stderr, "cd: %s: No such file or directory\n", words[1]);
                        break;
                    }
                } else if (nwords == 1) {
                    // If only cd entered, go to home directory
                    const char *home = getenv("HOME");
                    if (home == NULL) {
                        fprintf(stderr, "cd: No such file or directory\n");
                        break;
                    } else {
                        if (chdir(home) != 0) {
                            fprintf(stderr, "cd: %s: No such file or directory\n", home);
                            break;
                        }
                    }
                } else {
                    fprintf(stderr, "cd: too many arguments\n");
                }
                break;
            }
        }

        if (bg_process == 1) {
            child_pid = fork();
            signal(SIGINT, sigint_handler);
            signal(SIGTSTP, SIG_IGN);

            switch (child_pid) {
                case -1:
                    fprintf(stderr, "fork failed\n");
                    exit(1);

                case 0: // Child process
                    for (size_t i = 0; i < nwords; i++) {
                        if (strcmp(words[i], ">") == 0) {
                            const char *write_file = words[i + 1];
                            // Attempt to open with necessary flags or create with 0777 permissions if it doesn't exist
                            int file_desc = open(write_file, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                            // If open failed or duplicating file descriptor for stdout failed, print error message
                            if ((file_desc == -1) || (dup2(file_desc, STDOUT_FILENO) == -1)) {
                                fprintf(stderr, "Redirect failed\n");
                                exit(1);
                            }
                            close(file_desc);
                            // Increment i to skip filename
                            i++;
                        } else if (strcmp(words[i], ">>") == 0) {
                            const char *append_file = words[i + 1];
                            int file_desc = open(append_file, O_WRONLY | O_CREAT | O_APPEND, 0777);
                            if ((file_desc == -1) || (dup2(file_desc, STDOUT_FILENO) == -1)) {
                                fprintf(stderr, "Redirect failed\n");
                                exit(1);
                            }
                            close(file_desc);
                            i++;
                        } else if (strcmp(words[i], "<") == 0) {
                            const char *read_file = words[i + 1];
                            int file_desc = open(read_file, O_RDONLY);
                            if ((file_desc == -1) || (dup2(file_desc, STDIN_FILENO) == -1)) {
                                fprintf(stderr, "Redirect failed\n");
                                exit(1);
                            }
                            close(file_desc);
                            i++;
                        } else {
                            command_args[command_args_index] = words[i];
                            command_args_index++;
                        }
                    }
                    // Null terminate commands before passing to execvp
                    command_args[command_args_index] = NULL;
                    if (execvp(command_args[0], command_args) == -1) {
                        fprintf(stderr, "execvp failed\n");
                        exit(1);
                    }

                default: // Parent process
                    command_args_index = 0;
                    child_process_id = child_pid;
                    if (bg_operator == 0) {
                        // If & not present, perform blocking wait
                        waitpid(child_pid, &child_status, 0);
                        // If child terminated by signal, set child status accordingly
                        if (WIFSIGNALED(child_status)) {
                            last_fg_exit_status = 128 + WTERMSIG(child_status);
                        }
                        // Set child status to last foreground process exit status
                        if (WIFEXITED(child_status)) {
                            last_fg_exit_status = WEXITSTATUS(child_status);
                        }
                        if (WIFSTOPPED(child_status)) {
                            // Send SIGCONT to stopped child process
                            kill(child_pid, SIGCONT);
                            waitpid(child_pid, &child_status, WUNTRACED);
                            fprintf(stderr, "Child process %d stopped. Continuing.\n", child_pid);
                        }
                    } else {
                        // Non-blocking wait for background process
                        waitpid(child_pid, &child_status, WNOHANG);
                    }
            }
        }
      fflush(stdout); // Ensure all output is printed before the next prompt
      fflush(stderr);
    }
}

char *words[MAX_WORDS] = {0};

size_t wordsplit(char const *line) {
    size_t wlen = 0;
    size_t wind = 0;

    char const *c = line;
    for (; *c && isspace(*c); ++c);

    for (; *c;) {
        if (wind == MAX_WORDS) break;
        if (*c == '#') goto exit;
        for (; *c && !isspace(*c); ++c) {
            if (*c == '\\') ++c;
            void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
            if (!tmp) err(1, "realloc");
            words[wind] = tmp;
            words[wind][wlen++] = *c;
            words[wind][wlen] = '\0';
        }
        ++wind;
        wlen = 0;
        for (; *c && isspace(*c); ++c);
    }
exit:
    return wind;
}

char param_scan(char const *word, char const **start, char const **end) {
    static char const *prev;
    if (!word) word = prev;

    char ret = 0;
    *start = 0;
    *end = 0;
    for (char const *s = word; *s && !ret; ++s) {
        s = strchr(s, '$');
        if (!s) break;
        switch (s[1]) {
        case '$':
        case '!':
        case '?':
            ret = s[1];
            *start = s;
            *end = s + 2;
            break;
        case '{':;
            char *e = strchr(s + 2, '}');
            if (e) {
                ret = s[1];
                *start = s;
                *end = e + 1;
            }
            break;
        }
    }
    prev = *end;
    return ret;
}

char *build_str(char const *start, char const *end) {
    static size_t base_len = 0;
    static char *base = 0;

    if (!start) {
        /* Reset; new base string, return old one */
        char *ret = base;
        base = NULL;
        base_len = 0;
        return ret;
    }
    /* Append [start, end) to base string
     * If end is NULL, append whole start string to base string.
     * Returns a newly allocated string that the caller must free.
     */
    size_t n = end ? end - start : strlen(start);
    size_t newsize = sizeof *base * (base_len + n + 1);
    void *tmp = realloc(base, newsize);
    if (!tmp) err(1, "realloc");
    base = tmp;
    memcpy(base + base_len, start, n);
    base_len += n;
    base[base_len] = '\0';

    return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string
 * Returns a newly allocated string that the caller must free
 */
char *expand(char const *word) {
    char const *pos = word;
    char const *start;
    char const *end;
    char c = param_scan(pos, &start, &end);
    build_str(NULL, NULL);
    build_str(pos, start);
    while (c) {
        if (c == '!') {
            // Compose string
            char child_pid_char[20];
            snprintf(child_pid_char, 20, "%d", child_process_id);
            build_str(child_pid_char, NULL);
        }
        else if (c == '$') {
            int pid = getpid();
            char fg_pid_char[8];
            snprintf(fg_pid_char, sizeof(fg_pid_char), "%d", pid);
            build_str(fg_pid_char, NULL);
        }
        else if (c == '?') {
            char fg_exit_status_char[8];
            snprintf(fg_exit_status_char, sizeof(fg_exit_status_char), "%d", last_fg_exit_status);
            build_str(fg_exit_status_char, NULL);
        }
        else if (c == '{') {
            size_t i = 1;
            while (start + 2 + i < end - 1 && start[2 + i] != '}') {
                i++;
            }
            char param_char[i + 1];
            strncpy(param_char, start + 2, i);
            param_char[i] = '\0';
            char *param_char_value = getenv(param_char);
            if (param_char_value == NULL) {
                param_char_value = "";
            }
            build_str(param_char_value, NULL);
        }
        pos = end;
        c = param_scan(pos, &start, &end);
        build_str(pos, start);
    }
    return build_str(start, NULL);
}