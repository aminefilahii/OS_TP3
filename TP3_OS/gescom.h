#ifndef GESCOM_H
#define GESCOM_H

void register_all_builtins(void);
int execute_commands_sequence(char *input_string);
void cleanup_shell_resources(void);

typedef int (*builtin_func_t)(int, char **);
void add_internal_cmd(const char *cmd_name, builtin_func_t handler);

#endif
