/* Solution to shell project
 * Author: Sherri Goings
 * Last Modified: 1/18/2014
 */

#include    <stdlib.h>
#include    <stdio.h>
#include    <unistd.h>
#include    <string.h>
#include    <sys/types.h>
#include    <sys/wait.h>
#include    <fcntl.h>

char** readLineOfWords();
void printListOfWords(char**);
int processCommand(char**, int*, int*, int*, int*);
int fileRedirect(int*, char*, int, char*);
int isValidWord(char*);

int main()
{
  printf("enter a shell command (e.g. ls): ");
  fflush(stdout);
  char** words = readLineOfWords();

  // read lines from shell until encounter EOF 
  while (words) {
    int shellWait = 1;                // should shell process wait for child to finish
    int curIndex = 0;                 // index into line of words
    int redirInpFD = -1;              // file descriptor of input if redirected
    int redirOutFD = -1;              // file descriptor of output if redirected
    int prepipe[2] = {-1,-1};         // holds files of pipe that comes before command being processed
    int postpipe[2] = {-1,-1};        // holds files of pipe that comes after command being processed
    pid_t pid;                        // pid of most recent forked child process
    
    // loop through line of input one command at a time, curIndex will be the index of the first
    // word in each command or the index of the final NULL in words after the last command is processed
    while (words[curIndex] != NULL) {

      // process command starting at current index, returns number of words that were in the command
      // or -1 if error in command
      int commandLength = processCommand(&words[curIndex], &shellWait, &redirInpFD, &redirOutFD, postpipe);
      if (commandLength == -1) break;

      // split into parent and child processes
      pid = fork();     
  
      if (pid == 0) { // child

         // make any necessary changes to stdin/out for file redirects or pipes
        if (redirInpFD >= 0) {
          dup2(redirInpFD, 0);
        }
        if (redirOutFD >= 0) {
          dup2(redirOutFD, 1);
        }

        // if pipe before/after close appropriate ends of pipe before exec
        if (postpipe[0]>=0) {
          close(postpipe[0]);
        }
        if (prepipe[0]>=0) {
          close(prepipe[1]);
        }
        
        // execute command and check if succeeded
        int success = execvp(words[curIndex], &words[curIndex]);
        if (success == -1) {
          printf("ERROR: Unable to execute command: %s\n", words[curIndex]);
          fflush(stdout);
          return 1;
        }
       }

      // must be in parent if get here!
      // no need for 'else' because in above 'if' child either exec'd or returned -1

      // reset file descriptors for redirection
      redirInpFD = -1;
      redirOutFD = -1;

      // if there was a pipe before the most recent child forked, need to close both sides 
      if (prepipe[0]>=0) {
        close(prepipe[0]);
        close(prepipe[1]);
      }

      // if there is a pipe after the most recent child forked, 
      if (postpipe[0]>=0) {
        // copy it over to become the pipe before the next child forked
        prepipe[0] = postpipe[0];
        prepipe[1] = postpipe[1];

        // redirect the next child's input to come from the pipe
        redirInpFD = prepipe[0];

        // reset to start with no pipe after the next child forked
        postpipe[0] = postpipe[1] = -1;
      }

      // update curIndex to start of next command 
      curIndex += commandLength;
    }

    // if no &, wait for last child to finish
    if (shellWait) {
      waitpid(pid, NULL, 0);
    }
   
    printf("enter a shell command (e.g. ls): ");
    fflush(stdout);
    words = readLineOfWords();
  }
  printf("\n");
  return 0;
}

/*
 * loops through each word in current command until reaches end of entire line (NULL or &) or 
 * a pipe operator, checking for any errors in the command and setting up redirects as needed.
 * Note that the last 4 arguments are used to set the appropriate values instead of making them global
 * variables or returning a struct.  The one exception is inRedirectFD may already be set if the 
 * current command was preceded by a pipe.
 * args: pointer to first word in this command in list of words, reference to wait to be set if an
 *       & is found, reference to redirect input file descriptor, reference to redirect output file
 *       descriptor, reference to pipe that may follow this command
 * return: -1 if any error occurs, otherwise index of next token after the end of current command
 */
int processCommand(char** words, int* wait, int* inRedirectFD, int* outRedirectFD, int* postPipe) {
  int i=0;
  while (words[i] != NULL) {

    // check to make sure first word of command is a valid word and not an operator
    if (i==0 && !isValidWord(words[0])) {
      printf("ERROR: command cannot begin with an operator\n");
      return -1;
    }
    
    // handle file redirection, note that the i++ here is in addition to the i++ at the
    // end of the loop in order to skip the filename that follows the redirect operator
    if (strcmp(words[i], "<")==0) {
      if (fileRedirect(inRedirectFD, "<", O_RDONLY, words[i+1] )==-1) {
        return -1;
      }
      words[i] = NULL;
      i++;
    }
    else if (strcmp(words[i], ">")==0) {
       if (fileRedirect(outRedirectFD, ">", O_WRONLY|O_CREAT, words[i+1] )==-1) {
        return -1;
      }
      words[i] = NULL;
      i++;
    }

    // handle pipes
    else if (strcmp(words[i], "|")==0) {

      // if already found a file redirect for output, error
      if (*outRedirectFD >= 0) {
        printf("ERROR: cannot both redirect and pipe output\n");
        return -1;
      }

      // if pipe is not followed by a valid word, error
      if (words[i+1]==NULL || !isValidWord(words[i+1])) {
        printf("ERROR: | must be followed by command\n");
        return -1;
      }

       // create pipe, set this command to redirect output to pipe write
      pipe(postPipe);
      *outRedirectFD = postPipe[1];

      // replace | with NULL in array, return index of next token (should be next cmd)
      words[i] = NULL;
      return i+1;
    }

    // handle &, if not last token return -1 for error, 
    // otherwise replace with NULL and return this final index
    else if (strcmp(words[i], "&")==0) {
      if (words[i+1] != NULL) {
        printf("ERROR: illegal use of &\n");
        return -1;
      }
      else {
        *wait = 0;
        words[i] = NULL;
        return i;
      }
    }
    
    // if current token is none of the operators, check that is a valid word
    else if (!isValidWord(words[i])) {
      printf("ERROR: %s contains invalid characters\n", words[i]);
      return -1;
    }

    i++;
  }
  return i;
}

/* 
 * checks if a file redirect is valid and if so opens file and sets FD to open file descriptor
 * Args: reference to file descriptor, string that is either ">" or "<" to indicate input or
 *       output file redirection, flags for opening file, filename to attempt to open
 * Return: -1 if any error occurred with the redirect, 0 if succeeded
 */
int fileRedirect(int* FD, char* inOut, int flags, char* fileName) {
  // causes error if FD already has valid value, from another </> or a |
  if (*FD >= 0) {
    printf("ERROR: cannot redirect from multiple sources\n");
    return -1;
  }
  // causes error if redirect operator is not followed by a valid filename
  else if (fileName == NULL || !isValidWord(fileName)) {
    printf("ERROR: %s must be followed by filename\n", inOut);
    return -1;
  }
  // open file for reading, error if unsuccessful, otherwise set inRedirectFD to open file
  else {
    *FD = open(fileName, flags, 0644);
    if (*FD == -1) {
      printf("ERROR: unable to open file: %s\n", fileName);
      return -1;
    }
  }
  return 0;
}

/* 
 * checks each char in a single word to determine if it is in the valid set of chars
 * args: word to check
 * return: 0 if invalid char is found, 1 if entire word is valid
 */
int isValidWord(char* check) {
  int i;
  char c;
  for (i=0; check[i] != '\0'; i++) {
    c = check[i];
    if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') 
        && c!='-' && c!='_' && c!= '.' && c!='/') {
      return 0;
    }
  }
  return 1;
}

/* 
 * reads a single line from terminal and parses it into an array of tokens/words by 
 * splitting the line on spaces.  Adds NULL as final token 
 * args: None
 * return: array of tokens/words
 */
char** readLineOfWords() {

  // A line may be at most 100 characters long, which means longest word is 100 chars, 
  // and max possible tokens is 51 as must be space between each
  size_t MAX_WORD_LENGTH = 100;
  size_t MAX_NUM_WORDS = 51;

  // allocate memory for array of array of characters (list of words)
  char** words = (char**) malloc( MAX_NUM_WORDS * sizeof(char*) );
  int i;
  for (i=0; i<MAX_NUM_WORDS; i++) {
    words[i] = (char*) malloc( MAX_WORD_LENGTH );
  }

  // read actual line of input from terminal
  int bytes_read;
  char *buf;
  buf = (char*) malloc( MAX_WORD_LENGTH+1 );
  bytes_read = getline(&buf, &MAX_WORD_LENGTH, stdin);

  // check if EOF (or other error)
  if (bytes_read == -1) return NULL;
 
  // take each word from line and add it to next spot in list of words
  i=0;
  char* word = (char*) malloc( MAX_WORD_LENGTH );
  word = strtok(buf, " \n");
  while (word != NULL && i<MAX_NUM_WORDS) {
    strcpy(words[i++], word);
    word = strtok(NULL, " \n");
  }

  // check if we quit because of going over allowed word limit
  if (i == MAX_NUM_WORDS) {
    printf( "WARNING: line contains more than %d words!\n", (int)MAX_NUM_WORDS ); 
  } 
  else
    words[i] = NULL;
  
  // return the list of words
  return words;
}

/*
 * prints each token in the array in the format [tok1, tok2, ...]
 * args: array of words 
 * return: void
 */
void printListOfWords(char** words) {
  int i=0; 
  printf("\n[ ");
  while (words[i] != NULL) {
    printf("%s, ", words[i++]);
  }
  printf("]");
  printf("\n\n");
}
