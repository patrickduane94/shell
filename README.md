Smallsh
Smallsh is a simple shell program that allows you to run commands in both interactive and non-interactive modes. It supports basic command execution, redirection of input/output, and background processing.

Features
Interactive mode: Allows you to enter commands interactively.
Non-interactive mode: Execute commands from a specified file.
Command execution with support for redirection (<, >, >>).
Background process execution using &.
Built-in commands: cd and exit.
Shell variables: $$, $?, and $!.
Shell Variables
$$: The process ID of the smallsh shell.
$?: The exit status of the last foreground command.
$!: The process ID of the most recent background process.
Compilation
To compile the shell program, use the following command:

sh
Copy code
gcc -std=c99 -o smallsh smallsh.c
Usage
Interactive Mode
To run smallsh in interactive mode, simply execute the compiled binary without any arguments:

sh
Copy code
./smallsh
You will see the prompt >_ indicating that the shell is ready to accept commands.

Non-Interactive Mode
To run smallsh in non-interactive mode, provide a file containing the commands as an argument:

sh
Copy code
./smallsh commands.txt
In this mode, smallsh will read and execute the commands from the specified file.

Special Shell Variables
$$: The process ID of the smallsh shell.

sh
Copy code
echo $$
This command will print the process ID of the smallsh shell.

$?: The exit status of the last foreground command.

sh
Copy code
ls
echo $?
The second command will print the exit status of the ls command.

$!: The process ID of the most recent background process.

sh
Copy code
sleep 10 &
echo $!
The second command will print the process ID of the sleep 10 background process.
