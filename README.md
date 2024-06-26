## Mini Shell Features

### Background Execution
- Allows executing commands in the background using the `&` symbol at the end of a command.
- Command syntax: `<command> &`

### Foreground Execution
- Supports bringing background processes to the foreground using the `fg` command.
- Command syntax: `fg`

### New Shell Instance
- Provides functionality to open a new instance of the shell within the current shell.
- Command syntax: `newt`

### File Concatenation
- Concatenates contents of text files into a single output file.
- Supported command syntax: `cat <file1> # <file2> # ...`

### Pipe Operation
- Executes piped commands, allowing the output of one command to serve as input to the next.
- Supported command syntax: `<command1> | <command2> | ...`

### Redirection
- Supports redirection of standard input and output to and from files.
- Supported redirection operators: `>`, `>>`, `<`

### Conditional Execution
- Executes commands conditionally based on the success or failure of previous commands.
- Supported conditional operators: `&&`, `||`

### Sequential Execution
- Executes commands sequentially, one after another, regardless of the success or failure of previous commands.
- Supported command syntax: `<command1> ; <command2> ; ...`

### Signal Handling (Ctrl+C)
- Handles SIGINT signal (Ctrl+C) to terminate background processes when running in foreground mode.
