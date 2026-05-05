#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "gescom.h"
#include "creme.h"

static int application_running = 1;

int builtin_exit(int ac, char **av) {
    application_running = 0;
    return 1;
}

int builtin_cd(int ac, char **av) {
    const char *dir = (ac > 1) ? av[1] : getenv("HOME");
    if (dir) {
        if (chdir(dir) != 0) {
            perror("Erreur cd");
        }
    }
    return 1;
}

int builtin_pwd(int ac, char **av) {
    char p[1024];
    if (getcwd(p, sizeof(p))) printf("%s\n", p);
    return 1;
}

int builtin_vers(int ac, char **av) {
    printf("biceps (Nouvelle Edition) v4.0\n");
    return 1;
}

int builtin_net(int ac, char **av) {
    if (ac < 2) {
        printf("Action reseau manquante. (start, stop, list, ls, get, message)\n");
        return 1;
    }
    if (strcmp(av[1], "start") == 0 && ac > 2) beuip_start(av[2]);
    else if (strcmp(av[1], "stop") == 0) beuip_stop();
    else if (strcmp(av[1], "list") == 0) listeElts();
    else if (strcmp(av[1], "ls") == 0 && ac > 2) beuip_ls(av[2]);
    else if (strcmp(av[1], "get") == 0 && ac > 3) beuip_get(av[2], av[3]);
    else if (strcmp(av[1], "message") == 0 && ac > 3) {
        char txt[1000] = "";
        for (int i=3; i<ac; i++) { strcat(txt, av[i]); if (i<ac-1) strcat(txt, " "); }
        if (strcmp(av[2], "all") == 0) commande('5', txt, NULL);
        else commande('4', txt, av[2]);
    } else {
        printf("Commande beuip non reconnue ou arguments manquants.\n");
    }
    return 1;
}

int main(void) {
    signal(SIGINT, SIG_IGN);
    
    add_internal_cmd("exit", builtin_exit);
    add_internal_cmd("cd", builtin_cd);
    add_internal_cmd("pwd", builtin_pwd);
    add_internal_cmd("vers", builtin_vers);
    add_internal_cmd("beuip", builtin_net);

    char hfile[512] = "";
    if (getenv("HOME")) {
        snprintf(hfile, sizeof(hfile), "%s/.biceps_history", getenv("HOME"));
        read_history(hfile);
    }

    char prompt[100];
    char host[50];
    if (gethostname(host, 50) != 0) {
        strcpy(host, "host");
    }
    snprintf(prompt, sizeof(prompt), "[%s@%s] ~> ", getenv("USER") ? getenv("USER") : "user", host);

    while (application_running) {
        char *line = readline(prompt);
        if (!line) break;
        if (strlen(line) > 0) add_history(line);
        
        char *seq_save;
        char *cmd_unit = strtok_r(line, ";", &seq_save);
        while (cmd_unit) {
            execute_commands_sequence(cmd_unit);
            cmd_unit = strtok_r(NULL, ";", &seq_save);
            if (!application_running) break;
        }
        free(line);
    }

    if (strlen(hfile) > 0) write_history(hfile);
    beuip_stop();
    cleanup_shell_resources();
    return 0;
}