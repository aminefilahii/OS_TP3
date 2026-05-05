#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include "gescom.h"

#define MAX_ARGS 128
#define MAX_BUILT 15

typedef struct {
    char name[32];
    builtin_func_t fun;
} internal_cmd_t;

static internal_cmd_t builtins[MAX_BUILT];
static int builtins_count = 0;

void add_internal_cmd(const char *cmd_name, builtin_func_t handler) {
    if (builtins_count < MAX_BUILT) {
        strncpy(builtins[builtins_count].name, cmd_name, 31);
        builtins[builtins_count].name[31] = '\0';
        builtins[builtins_count].fun = handler;
        builtins_count++;
    }
}

static int run_internal(int ac, char **av) {
    for (int i = 0; i < builtins_count; i++) {
        if (strcmp(av[0], builtins[i].name) == 0) {
            builtins[i].fun(ac, av);
            return 1;
        }
    }
    return 0;
}

static void apply_redir_and_clean(int *argc, char **argv) {
    int i = 0;
    while (i < *argc) {
        int is_redir = 0;
        int fd = -1;
        int dup_target = -1;
        
        if (strcmp(argv[i], "<") == 0) {
            fd = open(argv[i+1], O_RDONLY);
            dup_target = STDIN_FILENO;
            is_redir = 1;
        } else if (strcmp(argv[i], ">") == 0) {
            fd = open(argv[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup_target = STDOUT_FILENO;
            is_redir = 1;
        } else if (strcmp(argv[i], ">>") == 0) {
            fd = open(argv[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            dup_target = STDOUT_FILENO;
            is_redir = 1;
        } else if (strcmp(argv[i], "2>") == 0) {
            fd = open(argv[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup_target = STDERR_FILENO;
            is_redir = 1;
        } else if (strcmp(argv[i], "2>>") == 0) {
            fd = open(argv[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            dup_target = STDERR_FILENO;
            is_redir = 1;
        }

        if (is_redir) {
            if (fd >= 0) {
                dup2(fd, dup_target);
                close(fd);
            } else {
                perror("Erreur lors de la redirection");
                exit(1);
            }
            // Shift array
            for (int j = i; j < *argc - 2; j++) {
                argv[j] = argv[j+2];
            }
            *argc -= 2;
            argv[*argc] = NULL;
        } else {
            i++;
        }
    }
}

static int exec_single_cmd(char *cmd) {
    char *argv[MAX_ARGS];
    int argc = 0;
    char *saveptr;
    char *token = strtok_r(cmd, " \t\n", &saveptr);
    
    while (token != NULL && argc < MAX_ARGS - 1) {
        argv[argc++] = token;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }
    argv[argc] = NULL;
    
    if (argc == 0) return 0;
    
    apply_redir_and_clean(&argc, argv);
    
    if (run_internal(argc, argv)) {
        exit(0);
    }
    
    execvp(argv[0], argv);
    fprintf(stderr, "Erreur d'execution : commande introuvable (%s)\n", argv[0]);
    exit(1);
}

int execute_commands_sequence(char *input_string) {
    char *cmds[32];
    int cmd_count = 0;
    char *save_pipe;
    
    char *token = strtok_r(input_string, "|", &save_pipe);
    while (token != NULL && cmd_count < 32) {
        cmds[cmd_count++] = token;
        token = strtok_r(NULL, "|", &save_pipe);
    }
    
    if (cmd_count == 0) return 0;
    
    if (cmd_count == 1) {
        // Parse into argv for internal check without fork
        char *cmd_cpy = strdup(cmds[0]);
        char *argv[MAX_ARGS];
        int argc = 0;
        char *sptr;
        char *t = strtok_r(cmd_cpy, " \t\n", &sptr);
        while (t && argc < MAX_ARGS - 1) { argv[argc++] = t; t = strtok_r(NULL, " \t\n", &sptr); }
        argv[argc] = NULL;
        
        if (argc > 0) {
            int is_built = 0;
            for (int i = 0; i < builtins_count; i++) {
                if (strcmp(argv[0], builtins[i].name) == 0) { is_built = 1; break; }
            }
            if (is_built) {
                int s_in = dup(0), s_out = dup(1), s_err = dup(2);
                apply_redir_and_clean(&argc, argv);
                run_internal(argc, argv);
                dup2(s_in, 0); close(s_in);
                dup2(s_out, 1); close(s_out);
                dup2(s_err, 2); close(s_err);
                free(cmd_cpy);
                return 0;
            }
        }
        free(cmd_cpy);
    }

    int p_fd[2];
    int in_fd = 0;
    pid_t pids[32];
    
    for (int i = 0; i < cmd_count; i++) {
        if (i < cmd_count - 1) pipe(p_fd);
        
        pids[i] = fork();
        if (pids[i] == 0) {
            signal(SIGINT, SIG_DFL);
            if (in_fd != 0) { dup2(in_fd, 0); close(in_fd); }
            if (i < cmd_count - 1) { dup2(p_fd[1], 1); close(p_fd[1]); close(p_fd[0]); }
            exec_single_cmd(cmds[i]);
        }
        
        if (in_fd != 0) close(in_fd);
        if (i < cmd_count - 1) { close(p_fd[1]); in_fd = p_fd[0]; }
    }
    
    int status;
    for (int i = 0; i < cmd_count; i++) waitpid(pids[i], &status, 0);
    return WEXITSTATUS(status);
}

void cleanup_shell_resources(void) {
    // Aucune liberation requise car on n'alloue pas de memoire dynamiquement pour le parser.
}
