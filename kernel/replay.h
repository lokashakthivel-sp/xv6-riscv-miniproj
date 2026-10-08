#ifndef _KERNEL_REPLAY_H_
#define _KERNEL_REPLAY_H_

#define REPLAY_EVENT_FORK  1
#define REPLAY_EVENT_EXEC  2
#define REPLAY_EVENT_OPEN  3
#define REPLAY_EVENT_CLOSE 4
#define REPLAY_EVENT_EXIT  5
#define REPLAY_EVENT_WAIT  6

#define MAX_REPLAY_EVENTS 512

struct replay_event {
  int pid;             // Process ID that performed the action
  int event_type;      // REPLAY_EVENT_*
  uint ticks;          // Kernel uptime tick timestamp
  int arg1;            // child pid (fork), fd (open/close), exit code (exit/wait)
  int arg2;            // flags/omode (open)
  char name[16];       // Process name
  char detail[32];     // Target file/program path or child name
};

#endif
