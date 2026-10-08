#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "replay.h"

// Reserved memory region: circular ring buffer for the flight recorder log
static struct {
  struct spinlock lock;
  struct replay_event events[MAX_REPLAY_EVENTS];
  int head;   // index of next write
  int total;  // total lifetime events recorded
} replay_log;

void
replay_init(void)
{
  initlock(&replay_log.lock, "replay");
  replay_log.head = 0;
  replay_log.total = 0;
}

void
replay_record(int pid, const char *name, int event_type, int arg1, int arg2, const char *detail)
{
  acquire(&replay_log.lock);

  int idx = replay_log.head;
  struct replay_event *ev = &replay_log.events[idx];

  ev->pid = pid;
  ev->event_type = event_type;
  ev->ticks = ticks;
  ev->arg1 = arg1;
  ev->arg2 = arg2;

  if (name)
    safestrcpy(ev->name, name, sizeof(ev->name));
  else
    ev->name[0] = '\0';

  if (detail)
    safestrcpy(ev->detail, detail, sizeof(ev->detail));
  else
    ev->detail[0] = '\0';

  replay_log.head = (replay_log.head + 1) % MAX_REPLAY_EVENTS;
  replay_log.total++;

  release(&replay_log.lock);
}

int
replay_get(int pid, uint64 dst_addr, int max_events)
{
  struct proc *p = myproc();
  int copied = 0;

  if (max_events <= 0)
    return 0;

  acquire(&replay_log.lock);
  int total = replay_log.total;
  int count = (total < MAX_REPLAY_EVENTS) ? total : MAX_REPLAY_EVENTS;
  int start = (total < MAX_REPLAY_EVENTS) ? 0 : replay_log.head;
  release(&replay_log.lock);

  for (int i = 0; i < count && copied < max_events; i++) {
    int idx = (start + i) % MAX_REPLAY_EVENTS;
    struct replay_event ev;

    acquire(&replay_log.lock);
    ev = replay_log.events[idx];
    release(&replay_log.lock);

    if (pid == -1 || ev.pid == pid) {
      if (dst_addr != 0) {
        if (copyout(p->pagetable, p->sz, dst_addr + (uint64)copied * sizeof(struct replay_event),
                    (char *)&ev, sizeof(struct replay_event)) < 0) {
          return -1;
        }
      }
      copied++;
    }
  }

  return copied;
}

uint64
sys_replaylog(void)
{
  int pid;
  uint64 ubuf;
  int max_events;

  argint(0, &pid);
  argaddr(1, &ubuf);
  argint(2, &max_events);

  return replay_get(pid, ubuf, max_events);
}
