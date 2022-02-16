/* 
* Author: Matthew Rost
* Class: CS 344 - Operating Systems I
* Project 3: Small Shell
* Date 2/9/2022
* 
* Description: My own implementation of a small shell in C. This
* implements a few features of well-known shells, like bash. This
* provides input for running commands, handles blank lines and
* comments, provides expansion for the variable $$, executes exit,
* cd, and status via built in code, executes other commands with
* exec functions, supports input/output redirection, supports running
* commands in the foreground or background, and implements custom
* handlers for 2 signals, SIGINT and SIGSTP.
*/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


// Global to keep track of foreground mode.
int foreground;


/*
Structure: Command

Description: Stores attributes of a command entered into the shell.
This is then parsed thru parseInput. The command is then attempted
to be executed.
*/
struct command
{
	char* command;
	char* arguments[512];
	char* inputFile;
	char* outputFile;
	int ampersand;

};


/*
Structure: Processes

Description: Stores process information including background PIDs,
a count of background PID's, and the most recent status.
*/
struct processes
{
	// maybe store every process run here
	int* backgroundPids[500];
	int pidCount;
	int status;
	int signal;

	int* exitedPids[500];
	int exitedCount;
};


/*
Function: handle_SIGTSTP

Description: Turns on and off foreground only mode using SIGTSTP command.
This is ctrl+z.
*/
void handle_SIGTSTP(int signo)
{
	// Turn on foreground mode.
	if (foreground == 0)
	{
		char* message = "Entering foreground-only mode";
		write(STDOUT_FILENO, message, 29);
		foreground = 1;
	}

	// Turn off foreground mode.
	else 
	{
		char* message = "Exiting foreground-only mode";
		write(STDOUT_FILENO, message, 28);
		foreground = 0;
	}

}


/*
Function: exitShell

Description: Exits the program, this makes sure that all currently
running background processes are killed to clean up before ending
the program.

Input: processes structure
Output: NULL
*/
void exitShell(struct processes* processes)
{
	int i;

	// Loop over every background process we created and attempt to kill the PID.
	for (i = 0; i < processes->pidCount; i++)
	{
		kill(processes->backgroundPids[i], SIGKILL);
	}

	return;
}


/*
Function: cdShell

Description: Performs shell change directory action. If no arguments
are entered, this returns home. If there is an argument this attempts
to cd to the entered argument. If it is not there, this returns an
error message. Either an absolute path or relative path is functional.

Input: command structure
Output: NULL
*/
void cdShell(struct command* input)
{
	if (input->arguments[1] == NULL)
	{
		// "HOME" variable contains the pathname of user's login directory
		chdir(getenv("HOME")); //Needed getenv to get the HOME variable
	}
	else
	{
		int changeDir = chdir(input->arguments[1]);

		// https://edstem.org/us/courses/16718/discussion/1078120 This post
		// helped me realize that when chdir == -1 there was an error.
		if (changeDir == -1)
		{
			printf("Error finding this directory.\n");
			fflush(stdout);
		}
	}
}


/*
Function: statusShell

Description: Retrieves the exit value of the most recent foreground process and prints
it for the user. If this is run before any foreground command is run, this will return
exit status 0. The three built in commands (exitShell, cdShell, and statusShell) will
not affect the statuses that are returned.

Input: processes structure
Output: NULL
*/
void statusShell(struct processes* processes)
{
	if (processes->status == 0)
	{
		printf("exit value 0\n");
		fflush(stdout);
	}
	else if (processes->status == 1)
	{
		printf("exit value 1\n");
		fflush(stdout);
	}
	else // I use 2 for when a process is terminated by a signal.
	{
		printf("terminated by signal %d\n", processes->signal);
		fflush(stdout);
	}
}


/*
Function: otherCommands

Description: When a built in command is not used, this function is called.
This uses fork(), exec(), and waitpid() to tell the operating system what
to do. Ending one of these commands with & runs it in background mode if
we do not have foreground only mode on.

Input: command structure; processes structure
Output: NULL
*/
void otherCommands(struct command* command, struct processes* processes)
{
	int childStatus; // Keeps track of a child processes' status
	int sourceFD;    // Used to change input file
	int targetFD;    // Used to change output file


	// Very heavily based off of Module 5: processes and I/O
	if (command->inputFile != NULL)
	{
		sourceFD = open(command->inputFile, O_RDONLY);

		if (sourceFD == -1)
		{
			// Error opening the file, create an error message.
			processes->status = 1;
			printf("Cannot open %s for input.\n", command->inputFile);
			fflush(stdout);
			return;
		}
	}
	if (command->outputFile != NULL)
	{
		targetFD = open(command->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (sourceFD == -1)
		{
			// Error opening the file
			processes->status = 1;
			return;
		}
	}

	pid_t newPid = fork();

	// If this is a background process we need to make source and target "/dev/null/" if
	// they were not specified
	if (command->ampersand == 1)
	{
		if (command->outputFile == NULL)
		{
			sourceFD = open("/dev/null/", O_RDONLY);
		}
		if (command->inputFile == NULL)
		{
			targetFD = open("/dev/null/", O_WRONLY | O_CREAT | O_TRUNC, 0644);
		}
	}

	// Set signals to ignore.
	struct sigaction SIGINT_action = { 0 }, SIGTSTP_action = { 0 };

	SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

	SIGTSTP_action.sa_handler = SIG_IGN;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	// Error forking, change status and return
	if (newPid < 0)
	{
		processes->status = 1;
		printf("fork() failed.");
		fflush(stdout);
		return;
	}
	// Child Process
	else if (newPid == 0)
	{
		// Very heavily based off of Module 5: processes and I/O
		// and module on signal API

		// If this is a foreground process we want SIGINT to work to stop the process
		if (command->ampersand == 0)
		{
			SIGINT_action.sa_handler = SIG_DFL;
			sigfillset(&SIGINT_action.sa_mask);
			SIGINT_action.sa_flags = 0;
			sigaction(SIGINT, &SIGINT_action, NULL);
		}

		// Use dup 2 from processes and I/O if we have an input file.
		if (command->inputFile != NULL)
		{
			int resultInput = dup2(sourceFD, 0);

			// Error
			if (resultInput == -1)
			{
				processes->status = 1;
				return;
			}
		}

		// Use dup 2 from processes and I/O if we have an output file.
		if (command->outputFile != NULL)
		{
			int resultOutput = dup2(targetFD, 1);

			// Error
			if (resultOutput == -1)
			{
				processes->status = 1;
				return;
			}
		}

		// Reference: https://stackoverflow.com/questions/27541910/how-to-use-execvp
		// The next two lines are very similar to what was shown on this page.
		if (execvp(command->command, command->arguments) == -1)
		{
			printf("Command not found.\n");
			processes->status = 1;
			fflush(stdout);
		}

		// Close input
		if (command->inputFile != NULL)
		{
			fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
		}

		// Close output
		if (command->outputFile != NULL)
		{
			fcntl(targetFD, F_SETFD, FD_CLOEXEC);
		}

		// If we get down to this point we must exit the child
		exit(1);
	}
	// Parent Process
	else
	{
		// Close Onput
		if (command->inputFile != NULL)
		{
			close(sourceFD);
		}
		// Close Output
		if (command->outputFile != NULL)
		{
			close(targetFD);
		}
		
		// For Foreground Processes
		if (command->ampersand == 0)
		{
			// Turn on SIGTSTP to use handle_SIGTSTP
			SIGTSTP_action.sa_handler = handle_SIGTSTP;
			sigfillset(&SIGTSTP_action.sa_mask);
			SIGTSTP_action.sa_flags = SA_RESTART;
			sigaction(SIGTSTP, &SIGTSTP_action, NULL);

			waitpid(newPid, &childStatus, 0);

			// If a child process has been terminated, run thru this to update status
			if (WIFSIGNALED(childStatus) != 0)
			{
				// Reference: https://edstem.org/us/courses/16718/discussion/1079841
				// Guided by Philip Peiffer's answer to this ed question.
				printf("Child terminated with signal %d\n", WTERMSIG(childStatus));
				processes->signal = WTERMSIG(childStatus);
				processes->status = 2;
				fflush(stdout);
			}
			// If a child process has ended, update status
			else
			{
				// Reference: Process API - Monitoring Child Processes module
				processes->status = WEXITSTATUS(childStatus);
			}
		}
		// Background Processes
		else
		{
			// Print new background process PID and store the PID to the array.
			printf("Background process is %d\n", newPid);
			fflush(stdout);
			int backgroundIndex = processes->pidCount;
			processes->backgroundPids[backgroundIndex] = newPid;
			processes->pidCount++;

			// Turn on handle_SIGTSTP handler.
			SIGTSTP_action.sa_handler = handle_SIGTSTP;
			sigfillset(&SIGTSTP_action.sa_mask);
			SIGTSTP_action.sa_flags = SA_RESTART;
			sigaction(SIGTSTP, &SIGTSTP_action, NULL);
		}
		return;
	}
}


/*
Function: parseInput

Description: Used to take a user's input and build a command struct
to send to the pertinant command functions. This also turns $$ into
the PID of the process running smallsh.

Input: char* input; processes structure
Output: int (0 or 1)
*/
int parseInput(char* input, struct processes* processes)
{
	// Copy input to save it when we tokenize.
	int len = 2049;
	char* inputCopy = (char*)calloc(len, sizeof(char));
	char* inputPID;
	strcpy(inputCopy, input);

	// Tokenize input to see if it has a space, is blank, or starts with #
	char* startToken = strtok(input, " ");
	int tokenCount = 0;

	// Constant values to compare strings against.
	char* blank = "\n";
	char* comment = "#";
	char* exit = "exit";
	char* cd = "cd";
	char* inputFile = "<";
	char* outputFile = ">";
	char* ampersand = "&";
	char* status = "status";
	char* dollars = "$$";

	// Handle blanks and comments
	if (strcmp(input, blank) == 0)
	{
		printf("\n");
		fflush(stdout);
		return 0;
	}
	else if (startToken == NULL)
	{
		return 0;
	}
	else if (strncmp(input, comment, 1) == 0)
	{
		fflush(stdout);
		return 0;
	}

	// This section is for converting the $$ in the input to the PID.
	inputPID = (char*)calloc(len, sizeof(char));
	int pid = getpid();
	char pidString[6];

	// Reference: https://www.delftstack.com/howto/c/how-to-convert-an-integer-to-a-string-in-c/
	// Used this to learn about sprintf()
	sprintf(pidString, "%d", pid);

	// No $$ are in the input
	if (strstr(inputCopy, dollars) == NULL)
	{
		strcpy(inputPID, inputCopy);
	}

	// $$ are in the input, we need to build our string to have the PID
	while (strstr(inputCopy, dollars) != NULL)
	{
		// Reference: https://www.tutorialspoint.com/c_standard_library/c_function_strstr.htm
		// This was used to learn about strstr() used for finding substring

		char* substring = (char*)calloc(len, sizeof(char));
		strcpy(substring, strstr(inputCopy, dollars));
		char* copyToken = strtok(inputCopy, "$$");

		// Add the initial token and then the PID to the inputPID string
		strcat(inputPID, copyToken);
		strcat(inputPID, pidString);
		char* substringToken = strtok(substring, "$$");

		// Need this if because without it we add some bad characters to our input.
		if (substringToken != NULL)
		{
			// Add the substring with $$ removed to inputPID
			strcat(inputPID, substringToken);
		}
		free(substring);
	}

	// Tokenizing inputPID to determine command structure parts
	char* token = strtok(inputPID, " ");

	// Initialize struct, set background to 0.
	struct command* currentCommand = malloc(sizeof(struct command));
	currentCommand->ampersand = 0;

	// Set command
	currentCommand->command = calloc(strlen(token) + 1, sizeof(char));
	strcpy(currentCommand->command, token);

	// Add command to args
	currentCommand->arguments[tokenCount] = calloc(strlen(token) + 1, sizeof(char));
	strcpy(currentCommand->arguments[tokenCount], token);
	tokenCount++;

	token = strtok(NULL, " ");

	// While tokenizing
	while (token != NULL)
	{
		// Set input file
		if (strcmp(token, inputFile) == 0)
		{
			token = strtok(NULL, " ");
			currentCommand->inputFile = calloc(strlen(token) + 1, sizeof(char));
			strcpy(currentCommand->inputFile, token);
			token = strtok(NULL, " ");
		}

		// Set output file
		else if (strcmp(token, outputFile) == 0)
		{
			token = strtok(NULL, " ");
			currentCommand->outputFile = calloc(strlen(token) + 1, sizeof(char));
			strcpy(currentCommand->outputFile, token);
			token = strtok(NULL, " ");
		}

		// Set background status
		else if (strcmp(token, ampersand) == 0)
		{
			token = strtok(NULL, " ");
			if (token == NULL)
			{
				// If foreground only mode is on, we ignore this.
				if (foreground == 0)
				{
					currentCommand->ampersand = 1;
				}
			}
		}

		// Add to arguments
		else {
			currentCommand->arguments[tokenCount] = calloc(strlen(token) + 1, sizeof(char));
			strcpy(currentCommand->arguments[tokenCount], token);
			token = strtok(NULL, " ");
			tokenCount++;
		}
	}

	// Free the memory
	free(inputPID);
	free(inputCopy);

	// Determine which command to use
	if (strcmp(currentCommand->command, exit) == 0)
	{
		exitShell(processes);
		return 1;
	}
	else if (strcmp(currentCommand->command, cd) == 0)
	{
		cdShell(currentCommand);
		return 0;
	}
	else if (strcmp(currentCommand->command, status) == 0)
	{
		statusShell(processes);
		return 0;
	}
	else {
		otherCommands(currentCommand, processes);
		return 0;
	}


	return 0;
}


/*
Function: shell

Description: Used to prompt a user for input while the user has not used the exit command.
This also checks for when background processes are ended or terminated and prints that
information out to the user.
*/
void shell()
{
	// Reference: https://c-for-dummies.com/blog/?p=1112 
	// This reference helped me learn how to use getline, I did something extremely similar

	int loop = 0;    // Bool to keep track of if the user has exited the shell.
	char* input;
	int len = 2049;

	// Build processes struct to keep track of background processes and status updates
	struct processes* currentProcesses = malloc(sizeof(struct processes));
	currentProcesses->status = 0;


	while (loop == 0)
	{
		// Print prompt for user and use getline to store an input
		printf(": ");
		fflush(stdout);

		input = (char*)malloc(len * sizeof(char));
		getline(&input, &len, stdin);
		// Remove \n from the end of the string
		input = strtok(input, "\n");

		// Send to the parseInput function to determine which command to run
		loop = parseInput(input, currentProcesses);
		free(input);

		int i;
		int backgroundStatus;

		// Loop over background pid's to see if they have been terminated or completed. Used the
		// Process API - Monitoring Child Processes module for this
		for (i = 0; i < currentProcesses->pidCount; i++)
		{
			if (waitpid(currentProcesses->backgroundPids[i], &backgroundStatus, WNOHANG) > 0)
			{
				if (WIFSIGNALED(backgroundStatus) != 0)
				{
					// https://edstem.org/us/courses/16718/discussion/1079841
					printf("Process %d terminated with signal %d\n", currentProcesses->backgroundPids[i],  WTERMSIG(backgroundStatus));
					fflush(stdout);

					currentProcesses->signal = WTERMSIG(backgroundStatus);
					currentProcesses->status = 2;
				}
				else
				{
					// https://canvas.oregonstate.edu/courses/1884946/pages/exploration-process-api-monitoring-child-processes?module_item_id=21835973
					currentProcesses->status = WEXITSTATUS(backgroundStatus);
					printf("Process %d ended with status %d\n", currentProcesses->backgroundPids[i], WEXITSTATUS(backgroundStatus));
					fflush(stdout);
				}

			}
		}

	}

	// Free memory of the processes and return to main to exit
	free(currentProcesses);
	return;
}


/*
Function: main

Description: Set foreground boolean and initialize structure of signal actions.
Runs the shell function to prompt user for inputs.

Output: int
*/
int main(void)
{
	// Global to keep track of foreground only mode.
	foreground = 0;

	// Module on signal API used to reference on how to creat signals and
	// set handlers
	struct sigaction SIGINT_action = { 0 }, SIGTSTP_action = { 0 };

	SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	// Run Shell
	shell();

	// Exit Program
	return 0;
}