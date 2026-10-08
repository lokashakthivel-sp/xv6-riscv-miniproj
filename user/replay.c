#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/replay.h"
#include "user/user.h"

#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RED    "\033[31m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_RESET  "\033[0m"

static struct replay_event events[MAX_REPLAY_EVENTS];

static void
print_event(struct replay_event *ev, const char *prefix)
{
  switch (ev->event_type) {
  case REPLAY_EVENT_FORK:
    printf("%s[tick %d] " ANSI_GREEN "forked" ANSI_RESET " child pid %d (%s)\n",
           prefix, ev->ticks, ev->arg1, ev->detail);
    break;
  case REPLAY_EVENT_EXEC:
    printf("%s[tick %d] " ANSI_CYAN "exec" ANSI_RESET " \"%s\"\n",
           prefix, ev->ticks, ev->detail);
    break;
  case REPLAY_EVENT_OPEN:
    printf("%s[tick %d] " ANSI_CYAN "opened" ANSI_RESET " \"%s\" (fd=%d, flags=0x%x)\n",
           prefix, ev->ticks, ev->detail, ev->arg1, ev->arg2);
    break;
  case REPLAY_EVENT_CLOSE:
    printf("%s[tick %d] closed fd=%d\n",
           prefix, ev->ticks, ev->arg1);
    break;
  case REPLAY_EVENT_WAIT:
    printf("%s[tick %d] " ANSI_YELLOW "waited" ANSI_RESET " on child pid %d (status %d)\n",
           prefix, ev->ticks, ev->arg1, ev->arg2);
    break;
  case REPLAY_EVENT_EXIT:
    printf("%s[tick %d] " ANSI_RED "exited" ANSI_RESET " with status %d\n",
           prefix, ev->ticks, ev->arg1);
    break;
  case REPLAY_EVENT_READ:
    printf("%s[tick %d] " ANSI_CYAN "read" ANSI_RESET " fd=%d (%d bytes, first read)\n",
           prefix, ev->ticks, ev->arg1, ev->arg2);
    break;
  case REPLAY_EVENT_WRITE:
    printf("%s[tick %d] " ANSI_CYAN "write" ANSI_RESET " fd=%d (%d bytes, %d bits)\n",
           prefix, ev->ticks, ev->arg1, ev->arg2, ev->arg2 * 8);
    break;
  default:
    printf("%s[tick %d] event type=%d (arg1=%d, arg2=%d)\n",
           prefix, ev->ticks, ev->event_type, ev->arg1, ev->arg2);
    break;
  }
}

// List all unique PIDs recorded in the flight recorder
static void
cmd_list(int total)
{
  int seen_pids[64];
  int num_pids = 0;

  printf(ANSI_BOLD "\nprocesses:\n" ANSI_RESET);
  printf("pid\tname\t\t\tevents\tstatus\n");
  printf("--------------------------------------------\n");

  for (int i = 0; i < total; i++) {
    int pid = events[i].pid;
    int already = 0;
    for (int j = 0; j < num_pids; j++) {
      if (seen_pids[j] == pid) {
        already = 1;
        break;
      }
    }
    if (!already && num_pids < 64) {
      seen_pids[num_pids++] = pid;

      // Count events and determine last known status for this PID
      int count = 0;
      char name[16] = "unknown";
      int exited = 0;
      int exit_code = 0;

      for (int k = 0; k < total; k++) {
        if (events[k].pid == pid) {
          count++;
          if (events[k].name[0] != '\0')
            strcpy(name, events[k].name);
          if (events[k].event_type == REPLAY_EVENT_EXIT) {
            exited = 1;
            exit_code = events[k].arg1;
          }
        }
      }

      if (exited) {
        printf("%d\t%s\t\t\t%d\t" ANSI_RED "exited (%d)" ANSI_RESET "\n",
               pid, name, count, exit_code);
      } else {
        printf("%d\t%s\t\t\t%d\t" ANSI_GREEN "active" ANSI_RESET "\n",
               pid, name, count);
      }
    }
  }

  printf("\nRun: 'replay <pid>' to view timeline for a specific process.\n\n");
}

// Reconstruct timeline for a specific PID, including nested child activity
static void
cmd_replay_pid(int target_pid, int total, int tree_mode)
{
  char proc_name[16] = "unknown";
  int pid_events = 0;

  for (int i = 0; i < total; i++) {
    if (events[i].pid == target_pid) {
      pid_events++;
      if (events[i].name[0] != '\0')
        strcpy(proc_name, events[i].name);
    }
  }

  if (pid_events == 0) {
    printf("No replay logs found for PID %d.\n", target_pid);
    cmd_list(total);
    return;
  }

  printf(ANSI_BOLD "\npid %d: %s\n" ANSI_RESET,
         target_pid, proc_name);

  for (int i = 0; i < total; i++) {
    if (events[i].pid == target_pid) {
      print_event(&events[i], "  ");

      // If this was a FORK and tree mode is enabled, print child timeline indented
      if (tree_mode && events[i].event_type == REPLAY_EVENT_FORK) {
        int child_pid = events[i].arg1;
        int child_found = 0;
        for (int c = 0; c < total; c++) {
          if (events[c].pid == child_pid) {
            if (!child_found) {
              printf(ANSI_YELLOW "    └── [child pid %d]\n" ANSI_RESET, child_pid);
              child_found = 1;
            }
            print_event(&events[c], "        ");
          }
        }
      }
    }
  }

  printf("\n");
}

// Dump all events across the whole system
static void
cmd_all(int total)
{
  printf(ANSI_BOLD "\n%d total\n" ANSI_RESET, total);
  for (int i = 0; i < total; i++) {
    printf("[%d: %s] ", events[i].pid, events[i].name);
    print_event(&events[i], "");
  }
  printf("\n");
}

int
main(int argc, char *argv[])
{
  int n = replaylog(-1, events, MAX_REPLAY_EVENTS);
  if (n <= 0) {
    printf("Replay log is currently empty.\n");
    exit(0);
  }

  if (argc < 2) {
    printf("Usage:\n");
    printf("  replay <pid>      Reconstruct human-readable timeline for process <pid>\n");
    printf("  replay -t <pid>   Tree timeline with nested child process activity\n");
    printf("  replay list       List all processes in the flight recorder\n");
    printf("  replay all        Dump all global events chronologically\n");
    cmd_list(n);
    exit(0);
  }

  if (strcmp(argv[1], "list") == 0) {
    cmd_list(n);
  } else if (strcmp(argv[1], "all") == 0) {
    cmd_all(n);
  } else if (strcmp(argv[1], "-t") == 0) {
    if (argc < 3) {
      printf("Usage: replay -t <pid>\n");
      exit(1);
    }
    int target_pid = atoi(argv[2]);
    cmd_replay_pid(target_pid, n, 1);
  } else {
    int target_pid = atoi(argv[1]);
    cmd_replay_pid(target_pid, n, 1);
  }

  exit(0);
}
