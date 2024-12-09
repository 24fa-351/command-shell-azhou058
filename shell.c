#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <ctype.h>

#define MAX_INPUT 1024
#define MAX_ARGS 100
#define ENV_VARS_SIZE 100

char *env_vars[ENV_VARS_SIZE][2];
int env_var_count = 0;

void parse_command(char *input, char **commands);
int is_internal_command(char *cmd);
void execute_internal(char *cmd, char **args);
void execute_external(char *cmd, char **args);
void handle_pipe(char *input);
void find_absolute_path(char *cmd, char *result);
void set_variable(char *name, char *value);
void unset_variable(char *name);
char *get_variable_value(char *name);
void replace_env_vars(char **args);

void parse_command(char *input, char **commands)
{
    char *token = strtok(input, "|");
    int increment = 0;
    while (token != NULL)
    {
        commands[increment++] = token;
        token = strtok(NULL, "|");
    }
    commands[increment] = NULL;
}

void handle_pipe(char *input)
{
    char *commands[MAX_ARGS];
    parse_command(input, commands);

    int increment = 0;
    while (commands[increment] != NULL)
    {
        char *args[MAX_ARGS];
        int argc = 0;
        char *cmd = strtok(commands[increment], " ");

        while (cmd != NULL)
        {
            args[argc++] = cmd;
            cmd = strtok(NULL, " ");
        }
        args[argc] = NULL;

        replace_env_vars(args);

        if (is_internal_command(args[0]))
        {
            execute_internal(args[0], args);
        }
        else
        {
            execute_external(args[0], args);
        }
        increment++;
    }
}

int is_internal_command(char *cmd)
{
    return (strcmp(cmd, "cd") == 0 || strcmp(cmd, "pwd") == 0 || strcmp(cmd, "set") == 0 || strcmp(cmd, "unset") == 0);
}

void execute_internal(char *cmd, char **args)
{
    if (strcmp(cmd, "cd") == 0)
    {
        if (args[1] != NULL)
        {
            if (chdir(args[1]) != 0)
            {
                perror("cd");
            }
        }
    }
    else if (strcmp(cmd, "pwd") == 0)
    {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            printf("%s\n", cwd);
        }
        else
        {
            perror("pwd");
        }
    }
    else if (strcmp(cmd, "set") == 0)
    {
        if (args[1] != NULL && args[2] != NULL)
        {
            set_variable(args[1], args[2]);
        }
        else
        {
            printf("Usage: set <variable> <value>\n");
        }
    }
    else if (strcmp(cmd, "unset") == 0)
    {
        if (args[1] != NULL)
        {
            unset_variable(args[1]);
        }
        else
        {
            printf("Usage: unset <variable>\n");
        }
    }
}

void execute_external(char *cmd, char **args)
{
    int is_input_redirect = 0;
    int input_file_descriptor = -1;
    
    for (int increment = 0; args[increment] != NULL; increment++)
    {
        if (strcmp(args[increment], "<") == 0)
        {
            is_input_redirect = 1;
            if (args[increment + 1] != NULL)
            {
                input_file_descriptor = open(args[increment + 1], O_RDONLY);
                if (input_file_descriptor == -1)
                {
                    perror("open");
                    return;
                }
                args[increment] = NULL;
            }
            break;
        }
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        if (is_input_redirect)
        {
            if (dup2(input_file_descriptor, STDIN_FILENO) == -1)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(input_file_descriptor);
        }

        if (execvp(cmd, args) == -1)
        {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        wait(NULL);

        printf("\n");
    }
}

void find_absolute_path(char *cmd, char *result)
{
    char *path_env = getenv("PATH");
    char path[1024];
    char *dir = strtok(path_env, ":");

    while (dir != NULL)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, cmd);
        if (access(path, X_OK) == 0)
        {
            strcpy(result, path);
            return;
        }
        dir = strtok(NULL, ":");
    }
    strcpy(result, cmd); 
}

void set_variable(char *name, char *value)
{
    for (int increment = 0; increment < env_var_count; increment++)
    {
        if (strcmp(env_vars[increment][0], name) == 0)
        {
            free(env_vars[increment][1]);
            env_vars[increment][1] = strdup(value);
            return;
        }
    }

    if (env_var_count < ENV_VARS_SIZE)
    {
        env_vars[env_var_count][0] = strdup(name);
        env_vars[env_var_count][1] = strdup(value);
        env_var_count++;
    }
    else
    {
        printf("Error: Cannot set more than %d environment variables.\n", ENV_VARS_SIZE);
    }
}

void unset_variable(char *name)
{
    for (int increment = 0; increment < env_var_count; increment++)
    {
        if (strcmp(env_vars[increment][0], name) == 0)
        {
            free(env_vars[increment][0]);
            free(env_vars[increment][1]);

            for (int second_increment = increment; second_increment < env_var_count - 1; second_increment++)
            {
                env_vars[second_increment][0] = env_vars[second_increment + 1][0];
                env_vars[second_increment][1] = env_vars[second_increment + 1][1];
            }
            env_var_count--;
            return;
        }
    }
    printf("Error: Variable '%s' not found.\n", name);
}


char *get_variable_value(char *name)
{
    char *env_value = getenv(name);
    if (env_value) {
        return env_value;
    }

    for (int increment = 0; increment < env_var_count; increment++)
    {
        if (strcmp(env_vars[increment][0], name) == 0)
        {
            return env_vars[increment][1];
        }
    }

    return NULL;
}


void replace_env_vars(char **args)
{
    for (int increment = 0; args[increment] != NULL; increment++)
    {
        char *var_start = strchr(args[increment], '$');
        if (var_start)
        {
            char var_name[100];
            int second_increment = 0;
            for (int count = 1; var_start[count] != '\0' && isalpha(var_start[count]); count++)
            {
                var_name[second_increment++] = var_start[count];
            }
            var_name[second_increment] = '\0';

            char *var_value = get_variable_value(var_name);
            if (var_value)
            {
                char *new_arg = malloc(strlen(var_value) + 1);
                strcpy(new_arg, var_value);
                args[increment] = new_arg;
            }
            else
            {
                args[increment] = strdup("");
            }
        }
    }
}
int main()
{
    char input[MAX_INPUT];
    char *commands[MAX_ARGS];

    while (1)
    {
        printf("xsh# ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            break;

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0)
            break;

        parse_command(input, commands);

        handle_pipe(input);
    }

    return 0;
}