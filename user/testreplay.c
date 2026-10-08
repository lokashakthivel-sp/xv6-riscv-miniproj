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

    int fd = open("replay_test.txt", O_CREATE | O_RDWR);
    if (fd >= 0) {
      // 1. Small write: 20 bytes = 160 bits (<= 512 bits, should NOT be logged)
      write(fd, "small write (160b)\n", 19);

      // 2. Large write: 100 bytes = 800 bits (> 512 bits, SHOULD be logged)
      char bigbuf[100];
      for (int i = 0; i < 99; i++) bigbuf[i] = 'A';
      bigbuf[99] = '\n';
      write(fd, bigbuf, 100);

      close(fd);
    }

    // 3. Open for reading: test first read vs second read
    fd = open("replay_test.txt", O_RDONLY);
    if (fd >= 0) {
      char buf[32];
      // 1st read on fd: SHOULD be logged
      read(fd, buf, sizeof(buf));
      // 2nd read on fd: should NOT be logged
      read(fd, buf, sizeof(buf));
      close(fd);
    }

    printf("[testreplay:child %d] Writes and reads completed. Exiting.\n", child_pid);
    exit(0);
  } else {
    // Parent process
    printf("[testreplay:parent %d] Forked child PID %d. Waiting for child...\n", parent_pid, pid);
    int status;
    wait(&status);
    printf("[testreplay:parent %d] Child reaped. Demo completed!\n\n", parent_pid);
    printf("Now try running:\n");
    printf("  replay %d\n", pid);
    printf("  replay -t %d\n\n", parent_pid);
    exit(0);
  }
}
