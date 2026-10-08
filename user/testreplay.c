#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(void)
{
  int parent_pid = getpid();
  printf("\n[testreplay] Starting demo under parent PID %d\n", parent_pid);

  int pid = fork();
  if (pid < 0) {
    printf("[testreplay] fork failed!\n");
    exit(1);
  }

  if (pid == 0) {
    // Child process
    int child_pid = getpid();
    printf("[testreplay:child %d] Child running, opening 'replay_test.txt'...\n", child_pid);

    int fd = open("replay_test.txt", O_CREATE | O_WRONLY);
    if (fd >= 0) {
      write(fd, "Flight recorder test data\n", 26);
      close(fd);
      printf("[testreplay:child %d] File written and closed.\n", child_pid);
    } else {
      printf("[testreplay:child %d] open failed!\n", child_pid);
    }

    printf("[testreplay:child %d] Exiting with status 0.\n", child_pid);
    exit(0);
  } else {
    // Parent process
    printf("[testreplay:parent %d] Forked child PID %d. Waiting for child...\n", parent_pid, pid);
    int status;
    wait(&status);
    printf("[testreplay:parent %d] Child reaped. Demo completed!\n\n", parent_pid);
    printf("Now try running:\n");
    printf("  replay %d\n", parent_pid);
    printf("  replay %d\n", pid);
    printf("  replay -t %d\n", parent_pid);
    printf("  replay list\n\n");
    exit(0);
  }
}
