#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_ARGS 5
#define MAX_COMMAND_LENGTH 1000
#define MAX_NUMBER_OF_COMMANDS 20
#define MAX_BG_PROCESSES 100
// This KeyValuePair array holds all the commands
struct KeyValuePair
{
    char **cmd;      // Pointer to the command array
    int cmdLen;      // Length of the command array
    char *cmdSuffix; // This will determine the special character joiner
};

int isCommandValid;
int bgProcessArr[MAX_BG_PROCESSES];
int bgProcessCount = 0;
int isFgProcess = 0;

// Function to expand tilde character to user's home directory
char *expandTilde(const char *path)
{
    if (path[0] == '~')
    {
        const char *homeDir = getenv("HOME");
        char *expandedPath = malloc(strlen(homeDir) + strlen(path));
        strcpy(expandedPath, homeDir);
        strcat(expandedPath, path + 1); // Skip '~'
        return expandedPath;
    }
    else
    {
        return strdup(path);
    }
}

int isMultiCharOp(char c)
{
    return c == '>' || c == '&' || c == '|';
}

void addSpaces(char *command)
{
    // Length of the command
    size_t len = strlen(command);

    // Buffer to store the modified command
    char modified[MAX_COMMAND_LENGTH * 2]; // Assuming worst case scenario for length

    // Initialize the buffer index
    size_t j = 0;

    // Iterate through each character in the command
    for (size_t i = 0; i < len; i++)
    {
        char currentChar = command[i];
        // Check if the current character is a symbol
        if (currentChar == '#' || currentChar == '<' || currentChar == ';')
        {
            // Add a space before the symbol if necessary
            if (i > 0 && command[i - 1] != ' ')
            {
                modified[j++] = ' ';
            }
            // Add the symbol itself
            modified[j++] = currentChar;
            // Add a space after the symbol if necessary
            if (i < len - 1 && command[i + 1] != ' ')
            {
                modified[j++] = ' ';
            }
        }
        else if (isMultiCharOp(currentChar))
        {
            // Check if the current character is part of a multi-character operator
            if (i < len - 1 && command[i + 1] == currentChar)
            {
                // Multi-character operator found, add a space before and after if necessary
                if (i > 0 && command[i - 1] != ' ')
                {
                    modified[j++] = ' ';
                }
                modified[j++] = currentChar;
                modified[j++] = currentChar;
                // Skip the next character
                i++;
                // Add a space after the symbol if necessary
                if (i < len - 1 && command[i + 1] != ' ')
                {
                    modified[j++] = ' ';
                }
            }
            else
            {
                // Single character operator found, add spaces around it if necessary
                if (i > 0 && command[i - 1] != ' ')
                {
                    modified[j++] = ' ';
                }
                modified[j++] = currentChar;
                // Add a space after the symbol if necessary
                if (i < len - 1 && command[i + 1] != ' ')
                {
                    modified[j++] = ' ';
                }
            }
        }
        else
        {
            // Copy non-symbol characters as-is
            modified[j++] = currentChar;
        }
    }

    // Null-terminate the modified string
    modified[j] = '\0';

    // Copy the modified string back to the original command
    strcpy(command, modified);
}

void parseInput(char *input, struct KeyValuePair *keyValuePairs, int *keyValuePairSize)
{
    int argc = 0;
    keyValuePairs[*keyValuePairSize].cmd = malloc((MAX_ARGS + 1) * sizeof(char *));
    if (keyValuePairs[*keyValuePairSize].cmd == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    // Iterate through the command to divide the command based on spaces
    char *saveptr; // Pointer used by strtok_r for thread safety
    char *token = strtok_r(input, " ", &saveptr);
    while (token != NULL)
    {
        // Expland the path if it exists in the command
        if (strstr(token, "~/") != NULL)
        {
            token = expandTilde(token);
        }

        if (token[0] == '\"')
        {
            // Token starts with a quote, indicating the start of a quoted string
            char *endQuote = strchr(token + 1, '\"'); // Find the end quote
            if (endQuote == NULL)
            {
                // If end quote is not found, it means the quoted string spans multiple tokens
                // Concatenate tokens until the end quote is found
                char *concatToken = malloc(strlen(token) + 1); // Allocate memory for the concatenated token
                if (concatToken == NULL)
                {
                    perror("malloc");
                    exit(EXIT_FAILURE);
                }
                strcpy(concatToken, token + 1); // Copy the substring after the opening quote
                while (endQuote == NULL)
                {
                    token = strtok_r(NULL, " ", &saveptr);
                    if (token == NULL)
                    {
                        printf(stderr, "Syntax error: Unmatched double quote\n");
                        return;
                    }
                    if (strchr(token, '\"') != NULL)
                    {
                        endQuote = strchr(token, '\"');
                        token[endQuote - token] = '\0'; // Null-terminate the string at the end quote
                    }
                    concatToken = realloc(concatToken, strlen(concatToken) + strlen(token) + 1);
                    if (concatToken == NULL)
                    {
                        perror("realloc");
                        exit(EXIT_FAILURE);
                    }
                    strcat(concatToken, " ");
                    strcat(concatToken, token);
                }
                token = concatToken;
            }
            else
            {
                // Token is a single quoted string
                *endQuote = '\0'; // Null-terminate the string at the end quote
                // Remove the first character (quote) from the token
                memmove(token, token + 1, strlen(token)); // Shift the string one character to the left
            }
        }

        if (strcmp(token, "#") == 0 || strcmp(token, "|") == 0 || strcmp(token, ">>") == 0 || strcmp(token, ">") == 0 || strcmp(token, "<") == 0 || strcmp(token, "&&") == 0 || strcmp(token, "||") == 0 || strcmp(token, ";") == 0)
        {
            // Add NULL pointer to terminate the argument list
            keyValuePairs[*keyValuePairSize].cmd[argc] = NULL;
            keyValuePairs[*keyValuePairSize].cmdLen = argc;
            // check if the arguments are greater than 5 and less than 1, if yes then exit
            if (argc > MAX_ARGS || argc < 1)
            {
                if (*keyValuePairSize != 0 || (*keyValuePairSize == 0 && argc > MAX_ARGS))
                {
                    printf("Individual commands cannot be greater than 5 and less than 1 arguments\n");
                }
                isCommandValid = 0;
                break;
            }
            keyValuePairs[*keyValuePairSize].cmdSuffix = strdup(token);

            (*keyValuePairSize)++;
            argc = 0;
            // Dynamically assign value to (*keyValuePairSize)++;
            keyValuePairs[*keyValuePairSize].cmd = malloc((MAX_ARGS + 1) * sizeof(char *));
        }
        else
        {
            keyValuePairs[*keyValuePairSize].cmd[argc] = strdup(token);
            argc++;
        }
        token = strtok_r(NULL, " ", &saveptr);
    }

    keyValuePairs[*keyValuePairSize].cmd[argc] = NULL;
    keyValuePairs[*keyValuePairSize].cmdLen = argc;
    keyValuePairs[*keyValuePairSize].cmdSuffix = NULL;
    // check if the arguments are greater than 5 and less than 1, if yes then exit
    if (isCommandValid && (argc > MAX_ARGS || argc < 1))
    {
        if (*keyValuePairSize != 0 || (*keyValuePairSize == 0 && argc > MAX_ARGS))
        {
            printf("Individual commands cannot be greater than 5 and less than 1 arguments\n");
        }
        isCommandValid = 0;
    }
    (*keyValuePairSize)++;
}

void printKeyValuePair(struct KeyValuePair kvp)
{
    printf("cmdLen: %d\n", kvp.cmdLen);
    printf("cmdSuffix: %s\n", kvp.cmdSuffix);
    printf("cmd:");
    for (int i = 0; i < kvp.cmdLen; i++)
    {
        printf(" %s", kvp.cmd[i]);
    }
    printf("\n");
}

void openNewTerminal()
{
    pid_t pid = fork();

    if (pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
    {
        // Parent process does not wait for the child to complete immediately
        return;
    }
    else if (pid == 0)
    {
        // Launch a new instance of shell24 in a new Bash terminal
        execlp("x-terminal-emulator", "x-terminal-emulator", "-e", "./shell24", NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }
}

// This function will execute the command
void executeCommand(char *args[], int argc)
{
    pid_t pid = fork();

    if (pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
    {
        // Parent process waitinng for child to complete
        wait(NULL);
    }
    else if (pid == 0)
    {
        // Execute commands
        if (execvp(args[0], args) == -1)
        {
            perror("execvp");
        }
    }
}

// Checks if there is a combination of special characters used, If yes returns 0
int ifValidSpecialChar(struct KeyValuePair *keyValuePairs, int keyValuePairSize, char *specialChar)
{
    for (int i = 0; i < keyValuePairSize - 1; i++)
    {
        if (strcmp(keyValuePairs[i].cmdSuffix, specialChar) != 0)
        {
            // return 0 because it conatins combinations
            printf("Combination of special characters cannot be used %s and %s\n", specialChar, keyValuePairs[i].cmdSuffix);
            return 0;
        }
    }
    return 1;
}

// Concatenate contents of text files
void fileConcatenation(struct KeyValuePair *keyValuePairs, int keyValuePairSize, char *specialChar)
{
    if (!ifValidSpecialChar(keyValuePairs, keyValuePairSize, specialChar))
    {
        return;
    }

    // Open output file for writing concatenated contents
    FILE *outputFile = fopen("output.txt", "w");
    if (outputFile == NULL)
    {
        perror("Failed to open output file");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < keyValuePairSize; i++)
    {
        // Open input file for reading
        FILE *inputFile = fopen(keyValuePairs[i].cmd[0], "r");
        if (inputFile == NULL)
        {
            printf("Failed to open input file %s\n", keyValuePairs[i].cmd[0]);
            fclose(outputFile);
            remove("output.txt");
            return;
        }

        // Read contents of input file and write to output file
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), inputFile) != NULL)
        {
            fputs(buffer, outputFile);
        }

        // Close input file
        fclose(inputFile);

        // Close output file
        if (i != keyValuePairSize - 1)
        {
            // Add newline between concatenated files
            fputs(" ", outputFile);
        }
    }

    // Close output file
    fclose(outputFile);

    // Open output file to print concatenated contents
    FILE *resultFile = fopen("output.txt", "r");
    if (resultFile == NULL)
    {
        perror("Failed to open output file");
        exit(EXIT_FAILURE);
    }

    // Print concatenated contents
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), resultFile) != NULL)
    {
        printf("%s", buffer);
    }
    printf("\n");

    // Close output file
    fclose(resultFile);

    // Remove temporary output file
    remove("output.txt");
}

// Piping operation
void pipeOperation(struct KeyValuePair *keyValuePairs, int keyValuePairSize, char *specialChar)
{
    if (!ifValidSpecialChar(keyValuePairs, keyValuePairSize, specialChar))
    {
        return;
    }

    // Initialize pipes
    int pipes[keyValuePairSize - 1][2];
    for (int i = 0; i < keyValuePairSize - 1; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    // Execute commands
    for (int i = 0; i < keyValuePairSize; i++)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {
            // Child process
            if (i > 0)
            {
                // Connect input to previous pipe
                if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1)
                {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }
            if (i < keyValuePairSize - 1)
            {
                // Connect output to next pipe
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1)
                {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            // Close all pipe descriptors
            for (int j = 0; j < keyValuePairSize - 1; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Execute command
            if (execvp(keyValuePairs[i].cmd[0], keyValuePairs[i].cmd) == -1)
            {
                perror("execvp");
            }
        }
    }

    // Close all pipe descriptors in parent
    for (int i = 0; i < keyValuePairSize - 1; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for all child processes to finish
    for (int i = 0; i < keyValuePairSize; i++)
    {
        wait(NULL);
    }
}

// Redirection
void redirection(struct KeyValuePair *keyValuePairs, int keyValuePairSize, char *specialChar)
{

    // Open the file based on the redirection operator
    int fileDescriptor;
    if (strcmp(specialChar, ">") == 0)
    {
        fileDescriptor = open(keyValuePairs[1].cmd[0], O_WRONLY | O_CREAT | O_TRUNC, 0777);
    }
    else if (strcmp(specialChar, ">>") == 0)
    {
        fileDescriptor = open(keyValuePairs[1].cmd[0], O_WRONLY | O_CREAT | O_APPEND, 0777);
    }
    else if (strcmp(specialChar, "<") == 0)
    {
        fileDescriptor = open(keyValuePairs[1].cmd[0], O_RDONLY);
    }

    if (fileDescriptor == -1)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        // Child process

        // Redirect stdin or stdout to the file
        if (strcmp(specialChar, ">") == 0 || strcmp(specialChar, ">>") == 0)
        {
            if (dup2(fileDescriptor, STDOUT_FILENO) == -1)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(specialChar, "<") == 0)
        {
            if (dup2(fileDescriptor, STDIN_FILENO) == -1)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
        }

        // Close the file descriptor
        close(fileDescriptor);

        // Execute the command
        if (execvp(keyValuePairs[0].cmd[0], keyValuePairs[0].cmd) == -1)
        {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }

    // Close the file descriptor in the parent process
    close(fileDescriptor);

    // Wait for the child process to finish
    wait(NULL);
}

void conditionalExecution(struct KeyValuePair *keyValuePairs, int keyValuePairSize)
{
    int status; // Used to store the exit status of the executed commands

    // Iterate through each command in the key-value pairs
    for (int i = 0; i < keyValuePairSize; i++)
    {
        // Execute the command
        pid_t pid = fork();

        if (pid == -1)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {
            // Child process
            if (execvp(keyValuePairs[i].cmd[0], keyValuePairs[i].cmd) == -1)
            {
                perror("execvp");
            }
        }
        else
        {
            // Parent process
            waitpid(pid, &status, 0); // Wait for child process to complete
            if (WIFEXITED(status))
            {
                int exit_status = WEXITSTATUS(status);

                // Check for conditional execution operators
                if ((keyValuePairs[i].cmdSuffix != NULL) && (strcmp(keyValuePairs[i].cmdSuffix, "&&") == 0))
                {
                    // If the previous command succeeded, proceed to the next command
                    if (exit_status != 0)
                    {
                        i++; // Skip the next command
                    }
                }
                else if ((keyValuePairs[i].cmdSuffix != NULL) && strcmp(keyValuePairs[i].cmdSuffix, "||") == 0)
                {
                    // If the previous command failed, proceed to the next command
                    if (exit_status == 0)
                    {
                        i++; // Skip the next command
                    }
                }
            }
        }
    }
}

void sequentialExecution(struct KeyValuePair *keyValuePairs, int keyValuePairSize, char *specialChar)
{
    if (!ifValidSpecialChar(keyValuePairs, keyValuePairSize, specialChar))
    {
        return;
    }

    // Iterate through each command in the key-value pairs
    for (int i = 0; i < keyValuePairSize; i++)
    {
        pid_t pid = fork();

        if (pid == -1)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid > 0)
        {
            // Parent process waiting for child to complete
            int status;
            if (waitpid(pid, &status, 0) == -1)
            {
                perror("waitpid");
                exit(EXIT_FAILURE);
            }
        }
        else if (pid == 0)
        {
            // Execute commands
            executeCommand(keyValuePairs[i].cmd, keyValuePairs[i].cmdLen);
            // Exit the child process after command execution
            exit(EXIT_SUCCESS);
        }
    }
}

// Method to add a value to the array
void addToBgProcessArr(int value)
{
    if (bgProcessCount < MAX_BG_PROCESSES)
    {
        bgProcessArr[bgProcessCount++] = value;
    }
    else
    {
        printf("Error: Background process array is full.\n");
    }
}

// Method to read the last element of the array
int readLastBgProcess()
{
    if (bgProcessCount > 0)
    {
        return bgProcessArr[bgProcessCount - 1];
    }
    else
    {
        printf("Error: Background process array is empty.\n");
        return -999; // Return a default value indicating an error
    }
}

// Method to remove the last element of the array
void removeLastBgProcess()
{
    if (bgProcessCount > 0)
    {
        bgProcessCount--;
    }
    else
    {
        printf("Error: Background process array is empty.\n");
    }
}

void pushBackground(char *args[], int argc)
{
    // args[argc - 1] = NULL;
    pid_t pid = fork();

    if (pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
    {
        addToBgProcessArr(pid);
        // Set the process group ID of the current process to its own PID
        if (setpgid(pid, pid) == -1)
        {
            perror("setpgid");
            exit(EXIT_FAILURE);
        }
        printf("Program is running in the background with PID: %d\n", pid);
    }
    else if (pid == 0)
    {
        // Execute commands
        if (execvp(args[0], args) == -1)
        {
            perror("execvp");
        }
    }
}

void sigint_handler(int signum)
{
    // Handle SIGINT signal (Ctrl+C) in the parent process
    if (isFgProcess && kill(readLastBgProcess(), signum) == 0)
    {
        printf("Process with PID %d killed successfully", readLastBgProcess());
        removeLastBgProcess();
    }
}

int main()
{
    // Register SIGINT signal handler for the parent process
    if (signal(SIGINT, sigint_handler) == SIG_ERR)
    {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    // This will be the whole command as a string
    char command[MAX_COMMAND_LENGTH];

    while (1)
    {

        isCommandValid = 1;
        printf("shell24$ ");
        if (fgets(command, sizeof(command), stdin) == NULL)
        {
            perror("fgets");
            exit(EXIT_FAILURE);
        }
        // Remove newline character
        command[strcspn(command, "\n")] = '\0';
        // Add spaces in between commands if it does not exist
        addSpaces(command);
        // Initialize an array of KeyValuePair with max size 5
        struct KeyValuePair *keyValuePairs = malloc(MAX_NUMBER_OF_COMMANDS * sizeof(struct KeyValuePair));
        int keyValuePairSize = 0;
        // Parse input into arguments
        parseInput(command, keyValuePairs, &keyValuePairSize);
        if (!isCommandValid)
        {
            continue;
        }
        // Print each KeyValuePair
        // for (int i = 0; i < keyValuePairSize; i++) {
        //     printf("KeyValuePair %d:\n", i + 1);
        //     printKeyValuePair(keyValuePairs[i]);
        //     printf("\n");
        // }

        // Execute command
        if (strcmp(keyValuePairs[0].cmd[0], "newt") == 0)
        {
            // If there is junk values along with newt
            if (keyValuePairs[0].cmdLen > 1)
            {
                printf("Invalid Command\n");
            }
            else
            {
                printf("Creating a new shell24 session...\n");
                // Fork and execute a new instance of shell24
                openNewTerminal();
            }
        }
        else if ((keyValuePairSize == 1) && (keyValuePairs[0].cmdLen == 2) && (strcmp(keyValuePairs[0].cmd[1], "&") == 0))
        {
            // Push the process to the background
            pushBackground(keyValuePairs[0].cmd, keyValuePairs[0].cmdLen);
        }
        else if ((keyValuePairSize == 1) && (keyValuePairs[0].cmdLen == 1) && (strcmp(keyValuePairs[0].cmd[0], "fg") == 0))
        {
            isFgProcess = 1;
            waitpid(readLastBgProcess(), NULL, 0);
            isFgProcess = 0;
        }
        else if (keyValuePairs[0].cmdSuffix == NULL)
        {
            // There is only one command without any spacial characters
            executeCommand(keyValuePairs[0].cmd, keyValuePairs[0].cmdLen);
        }
        else if (strcmp(keyValuePairs[0].cmdSuffix, "#") == 0)
        {
            if (keyValuePairSize > 6)
            {
                printf("More than 5 operations are not allowed\n");
            }
            else
            {
                // Txt file concatenation upto 5 concatinations
                fileConcatenation(keyValuePairs, keyValuePairSize, "#");
            }
        }
        else if (strcmp(keyValuePairs[0].cmdSuffix, "|") == 0)
        {
            if (keyValuePairSize > 7)
            {
                printf("More than 6 pipes are not allowed\n");
            }
            else
            {
                // Implement code for piping
                pipeOperation(keyValuePairs, keyValuePairSize, "|");
            }
        }
        else if ((strcmp(keyValuePairs[0].cmdSuffix, ">>") == 0) || (strcmp(keyValuePairs[0].cmdSuffix, ">") == 0) || (strcmp(keyValuePairs[0].cmdSuffix, "<") == 0))
        {
            if (keyValuePairSize > 2)
            {
                printf("More than 2 commands not allowed for %s\n", keyValuePairs[0].cmdSuffix);
            }
            else
            {
                // Implement code for redirection
                redirection(keyValuePairs, keyValuePairSize, keyValuePairs[0].cmdSuffix);
            }
        }
        else if ((strcmp(keyValuePairs[0].cmdSuffix, "&&") == 0) || (strcmp(keyValuePairs[0].cmdSuffix, "||") == 0))
        {
            if (keyValuePairSize > 6)
            {
                printf("More than 5 conditional operations are not allowed\n");
            }
            else
            {
                // Conditional execution
                conditionalExecution(keyValuePairs, keyValuePairSize);
            }
        }
        else if (strcmp(keyValuePairs[0].cmdSuffix, ";") == 0)
        {
            if (keyValuePairSize > 5)
            {
                printf("More than 5 commands are not allowed\n");
            }
            else
            {
                // Sequential execution
                sequentialExecution(keyValuePairs, keyValuePairSize, ";");
            }
        }
    }

    return 0;
}