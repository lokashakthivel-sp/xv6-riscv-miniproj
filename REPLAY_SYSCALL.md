# Replay System Call & Flight Recorder in xv6-riscv

## 1. Overview & Motivation

The **Replay System Call (`replaylog`)** and its accompanying kernel subsystem introduce an in-kernel **Flight Recorder** to xv6-riscv. 

In operating systems and systems programming, a *flight recorder* (or execution trace logger) monitors and records critical, non-deterministic system events—such as process creation, program executions, file operations, and terminations—into a dedicated memory buffer.

### Key Objectives:
1. **Process Activity Auditing:** Track what processes ran, what programs they executed (`exec`), what files they opened, read, or wrote to, and how they exited.
2. **Post-Mortem & Timeline Reconstruction:** Allow system administrators and developers to inspect past process trees and timelines, even after processes have terminated.
3. **Low Overhead & Safe Logging:** Maintain a fixed-size circular ring buffer in kernel memory protected by spinlocks, with zero dynamic memory allocations during runtime.
4. **Intelligent Noise Filtering:** Prevent log flooding by recording only the first read per file descriptor and filtering out small writes (e.g., writes $\le$ 64 bits / 8 bytes).

---

## 2. Architecture & Data Structures

```
 +-------------------------------------------------------------------+
 |                        USER SPACE                                 |
 |                                                                   |
 |   $ replay <pid>          $ replay -t <pid>       $ testreplay    |
 |         \                        /                     |          |
 |          +----------+-----------+                      |          |
 |                     |                                  |          |
 |             replaylog(pid, buf, max)             read / write /   |
 |                     |                            fork / exec      |
 +---------------------|----------------------------------|----------+
                       | ecall (trap)                     |
 +---------------------|----------------------------------|----------+
 |                     v                                  v          |
 |              sys_replaylog()                     Kernel Hooks     |
 |                     |                         (kfork, kexec, etc) |
 |                     v                                  |          |
 |               replay_get()                             |          |
 |                     |                                  v          |
 |                     |                           replay_record()   |
 |                     |                                  |          |
 |                     |        +---------------------+   |          |
 |                     +<-------| replay_log          |<--+          |
 |          copyout()           | (Ring Buffer: 512)  |              |
 |                              | Spinlock protected  |              |
 |                              +---------------------+              |
 |                        KERNEL SPACE                               |
 +-------------------------------------------------------------------+
```

### 2.1 The Event Structure (`struct replay_event`)
Defined in [`kernel/replay.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/replay.h):

```c
struct replay_event {
  int pid;             // Process ID that performed the action
  int event_type;      // REPLAY_EVENT_* (1-8)
  uint ticks;          // Kernel uptime tick timestamp
  int arg1;            // child pid (fork), fd (open/close/read/write), exit code (exit/wait)
  int arg2;            // flags/omode (open), bytes (read/write), exit status (wait)
  char name[16];       // Process name (e.g., "sh", "testreplay")
  char detail[32];     // Target file/program path or child process name
};
```

### 2.2 Event Types
| Event Constant | Value | Trigger Point | `arg1` | `arg2` | `detail` |
|---|---|---|---|---|---|
| `REPLAY_EVENT_FORK` | 1 | `kfork()` | Child PID | `0` | Child proc name |
| `REPLAY_EVENT_EXEC` | 2 | `kexec()` | `argc` | `0` | Executable path |
| `REPLAY_EVENT_OPEN` | 3 | `sys_open()` | Assigned `fd` | Open flags (`omode`) | Target file path |
| `REPLAY_EVENT_CLOSE` | 4 | `sys_close()` | Closed `fd` | `0` | `""` |
| `REPLAY_EVENT_EXIT` | 5 | `kexit()` | Exit status code | `0` | `""` |
| `REPLAY_EVENT_WAIT` | 6 | `kwait()` | Reaped child PID | Child exit status | Child proc name |
| `REPLAY_EVENT_READ` | 7 | `sys_read()` (first read) | `fd` | Bytes read | `""` |
| `REPLAY_EVENT_WRITE` | 8 | `sys_write()` (> 8 bytes) | `fd` | Bytes written | `""` |

### 2.3 The Ring Buffer (`replay_log`)
Defined in [`kernel/replay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/replay.c):
- Holds up to `MAX_REPLAY_EVENTS` (512) items.
- Managed by:
  - `head`: index pointing to where the next event will be written (`(head + 1) % 512`).
  - `total`: lifetime counter of all events since boot.
  - `lock`: spinlock (`struct spinlock`) preventing race conditions between concurrent cores or interrupts.
- When `total >= MAX_REPLAY_EVENTS`, older events are overwritten in FIFO order.

---

## 3. Comprehensive File-by-File Breakdown

### Summary of Changes
- **New Files (4):** [`kernel/replay.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/replay.h), [`kernel/replay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/replay.c), [`user/replay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/user/replay.c), [`user/testreplay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/user/testreplay.c).
- **Modified Kernel Files (8):** [`kernel/defs.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/defs.h), [`kernel/main.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/main.c), [`kernel/proc.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/proc.h), [`kernel/proc.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/proc.c), [`kernel/exec.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/exec.c), [`kernel/sysfile.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/sysfile.c), [`kernel/syscall.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/syscall.h), [`kernel/syscall.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/syscall.c).
- **Modified User/Build Files (3):** [`user/user.h`](file:///d:/sem5/os/xv6-riscv-miniproj/user/user.h), [`user/usys.pl`](file:///d:/sem5/os/xv6-riscv-miniproj/user/usys.pl), [`Makefile`](file:///d:/sem5/os/xv6-riscv-miniproj/Makefile).

---

### Detailed File Explanations

#### 1. [`kernel/replay.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/replay.h) `[ADDED]`
- **Purpose:** Defines public constants and data contracts for the replay subsystem.
- **Why Added:** Both kernel and user-space programs need the exact definition of `struct replay_event`, event codes (`REPLAY_EVENT_*`), and buffer size limit (`MAX_REPLAY_EVENTS`).

#### 2. [`kernel/replay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/replay.c) `[ADDED]`
- **Purpose:** Implements the core flight recorder logic and the system call handler.
- **Key Functions:**
  - `replay_init()`: Initializes the spinlock and resets head/total counters.
  - `replay_record(pid, name, event_type, arg1, arg2, detail)`: Safely acquires `replay_log.lock`, writes to `events[head]`, stamps current `ticks`, updates `head = (head + 1) % MAX_REPLAY_EVENTS`, increments `total`, and releases lock.
  - `replay_get(pid, dst_addr, max_events)`: Reads events chronologically. If `pid != -1`, it filters entries matching only that PID. Uses `copyout(p->pagetable, p->sz, ...)` to safely transfer data into the calling process's user memory.
  - `sys_replaylog()`: The syscall entry point. Uses `argint()` and `argaddr()` to read arguments from trapframe registers (`a0`, `a1`, `a2`) and dispatches to `replay_get()`.

#### 3. [`kernel/defs.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/defs.h) `[MODIFIED]`
- **Modification:** Added declarations for replay functions:
  ```c
  void   replay_init(void);
  void   replay_record(int, const char*, int, int, int, const char*);
  int    replay_get(int, uint64, int);
  uint64 sys_replaylog(void);
  ```
- **Why Modified:** In C, files calling `replay_record()` and `replay_init()` require visible function prototypes to compile cleanly without implicit declaration warnings/errors.

#### 4. [`kernel/syscall.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/syscall.h) & [`kernel/syscall.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/syscall.c) `[MODIFIED]`
- **Modification in `syscall.h`:** Assigned system call number:
  ```c
  #define SYS_replaylog 23
  ```
- **Modification in `syscall.c`:**
  - Declared `extern uint64 sys_replaylog(void);`.
  - Registered `[SYS_replaylog] = sys_replaylog` in the `syscalls[]` dispatch array.
- **Why Modified:** Enables the xv6 syscall dispatcher to map syscall ID 23 to `sys_replaylog()`.

#### 5. [`kernel/main.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/main.c) `[MODIFIED]`
- **Modification:** Added call to `replay_init()` during boot:
  ```c
  virtio_disk_init(); // emulated hard disk
  replay_init();      // flight recorder log buffer
  userinit();         // first user process
  ```
- **Why Modified:** The spinlock and ring buffer must be initialized on CPU 0 before any user processes or fork/exec operations occur.

#### 6. [`kernel/proc.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/proc.h) `[MODIFIED]`
- **Modification:** Added field to `struct proc`:
  ```c
  uint fd_read_logged; // Bitmask: bit i set if first read on fd i was logged
  ```
- **Why Modified:** Each process maintains open file descriptors (`ofile[NOFILE]`, where `NOFILE = 16`). Reading frequently (e.g., character by character or in blocks) would instantly flood the 512-entry log buffer. A 16-bit mask tracks per-FD read state.

#### 7. [`kernel/proc.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/proc.c) `[MODIFIED]`
- **Modifications & Rationale:**
  - **`allocproc()` & `freeproc()`:** Reset `p->fd_read_logged = 0;` so new processes or recycled slots have a clean bitmask.
  - **`kfork()`:** Records `REPLAY_EVENT_FORK`:
    ```c
    replay_record(p->pid, p->name, REPLAY_EVENT_FORK, pid, 0, np->name);
    ```
    Logs parent PID, child PID, and child process name.
  - **`kexit()`:** Records `REPLAY_EVENT_EXIT`:
    ```c
    replay_record(p->pid, p->name, REPLAY_EVENT_EXIT, status, 0, "");
    ```
    Captures the exit code right before the process transitions to `ZOMBIE`.
  - **`kwait()`:** Records `REPLAY_EVENT_WAIT`:
    ```c
    replay_record(p->pid, p->name, REPLAY_EVENT_WAIT, pid, child_xstate, child_name);
    ```
    Records when a parent reaps a child, logging the reaped child's PID and its exit status.

#### 8. [`kernel/exec.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/exec.c) `[MODIFIED]`
- **Modification in `kexec()`:**
  ```c
  replay_record(p->pid, p->name, REPLAY_EVENT_EXEC, argc, 0, path);
  ```
- **Why Modified:** Records when a process replaces its address space with an executable, logging `argc` and the binary path (e.g. `/sh`, `/ls`).

#### 9. [`kernel/sysfile.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/sysfile.c) `[MODIFIED]`
- **Modifications & Rationale:**
  - **`sys_open()`:**
    ```c
    if (fd >= 0 && fd < NOFILE)
      myproc()->fd_read_logged &= ~(1 << fd);
    replay_record(myproc()->pid, myproc()->name, REPLAY_EVENT_OPEN, fd, omode, path);
    ```
    Clears the read-log bit for this `fd` (in case the slot was reused) and logs the filename opened.
  - **`sys_close()`:**
    ```c
    if (fd >= 0 && fd < NOFILE)
      myproc()->fd_read_logged &= ~(1 << fd);
    replay_record(myproc()->pid, myproc()->name, REPLAY_EVENT_CLOSE, fd, 0, "");
    ```
    Resets the read-log bit and records the closure.
  - **`sys_read()` (Noise-filtered logging):**
    ```c
    int ret = fileread(f, p, n);
    if (ret >= 0 && fd >= 0 && fd < NOFILE) {
      struct proc *pr = myproc();
      if (!(pr->fd_read_logged & (1 << fd))) {
        pr->fd_read_logged |= (1 << fd);
        replay_record(pr->pid, pr->name, REPLAY_EVENT_READ, fd, ret, "");
      }
    }
    ```
    Only the **first** read operation on a descriptor is recorded. Subsequent reads on the same open descriptor are suppressed.
  - **`sys_write()` (Threshold-filtered logging):**
    ```c
    int ret = filewrite(f, p, n);
    // Log write only if greater than 64 bits (64 bits = 8 bytes)
    if (ret > 0 && fd >= 0 && ret > 8) {
      replay_record(myproc()->pid, myproc()->name, REPLAY_EVENT_WRITE, fd, ret, "");
    }
    ```
    Small writes (such as single newline characters or small terminal control sequences $\le 8$ bytes) do not overwhelm the buffer. Only substantive payloads ($> 8$ bytes / $> 64$ bits) are logged.

#### 10. [`user/user.h`](file:///d:/sem5/os/xv6-riscv-miniproj/user/user.h) & [`user/usys.pl`](file:///d:/sem5/os/xv6-riscv-miniproj/user/usys.pl) `[MODIFIED]`
- **`user.h`:** Added prototype: `int replaylog(int, void *, int);`.
- **`usys.pl`:** Added `entry("replaylog");`, which generates the assembly shim:
  ```assembly
  .global replaylog
  replaylog:
    li a7, SYS_replaylog
    ecall
    ret
  ```
- **Why Modified:** Allows any C program in user space to call `replaylog()` like standard POSIX-style functions.

#### 11. [`user/replay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/user/replay.c) `[ADDED]`
- **Purpose:** User-facing interactive command-line utility to analyze and display flight recorder data.
- **Commands & Features:**
  - `replay list`: Scans all events, deduplicates PIDs, displays each process's event count, name, and final status (`active` or `exited (code)`).
  - `replay <pid>`: Reconstructs a formatted, colorized chronological timeline for that specific PID.
  - `replay -t <pid>`: **Tree mode**: Reconstructs a hierarchical timeline, nesting child process events directly under parent `fork` events with tree branch graphics (`└── [child pid ...]`).
  - `replay all`: Dumps every global event in raw chronological sequence.
- **Visuals:** Uses ANSI escape sequences (Green for fork/active, Cyan for exec/open/read/write, Yellow for wait, Red for exit).

#### 12. [`user/testreplay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/user/testreplay.c) `[ADDED]`
- **Purpose:** Automated test and verification suite.
- **Workflow:**
  1. Parent records its PID and forks a child.
  2. Child creates and opens `replay_test.txt`.
  3. Child performs a 19-byte write (demo small write) and a 100-byte write (demo large write $> 8$ bytes).
  4. Child opens `replay_test.txt` for reading and performs two reads in sequence to verify that only the first read is captured.
  5. Child exits.
  6. Parent waits for the child and exits.
  7. Instructs user on the exact `replay` commands to verify output.

#### 13. [`Makefile`](file:///d:/sem5/os/xv6-riscv-miniproj/Makefile) `[MODIFIED]`
- **Modifications:**
  - Added `$K/replay.o` to `OBJS` to compile the kernel module into `kernel.bin`.
  - Added `$U/_replay` and `$U/_testreplay` to `UPROGS` to build the user binaries and bundle them into the file system image (`fs.img`).

---

## 4. End-to-End Execution Flow

When a user executes `replay 3`:

```
User Program                   Kernel System Call                 Hardware / Trap
------------                   ------------------                 ---------------
main() in user/replay.c
  |
  +--> replaylog(3, buf, 512)
         |
         +--> usys.S:
              a0 = 3
              a1 = buf
              a2 = 512
              a7 = SYS_replaylog (23)
              ecall -----------------------------------------> Trap to kernel
                                                                 |
                                                               usertrap()
                                                                 |
                                                               syscall()
                                                                 |
       sys_replaylog() <-----------------------------------------+
         |
         +--> argint(0, &pid), argaddr(1, &ubuf), argint(2, &max)
         +--> replay_get(3, ubuf, 512)
                |
                +--> acquire(&replay_log.lock)
                +--> Iterate circular buffer from oldest to newest
                +--> Filter events where ev.pid == 3
                +--> copyout(p->pagetable, p->sz, ubuf, &ev, sizeof(ev))
                +--> release(&replay_log.lock)
                +--> return copied_count
                                                                 |
User space resumes <---------------------------------------------+ sret
  |
  +--> Formats timestamps, ANSI colors, details
  +--> Prints reconstructed timeline to console
```

---

## 5. Summary Table

| Category | File | Change | Primary Function / Reason |
|---|---|---|---|
| **Kernel Subsystem** | [`kernel/replay.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/replay.h) | Added | Declares event struct, event IDs (1-8), buffer limits |
| **Kernel Subsystem** | [`kernel/replay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/replay.c) | Added | Buffer storage, concurrency locks, recording, extraction, `copyout` |
| **Kernel Header** | [`kernel/defs.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/defs.h) | Modified | Prototypes for `replay_init`, `replay_record`, `sys_replaylog` |
| **Process State** | [`kernel/proc.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/proc.h) | Modified | Adds `fd_read_logged` bitmask to `struct proc` |
| **Process Lifecycle** | [`kernel/proc.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/proc.c) | Modified | Hooks `fork`, `wait`, `exit` and initializes read mask |
| **Binary Execution** | [`kernel/exec.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/exec.c) | Modified | Hooks `exec` to log program path and argument count |
| **File I/O Hooks** | [`kernel/sysfile.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/sysfile.c) | Modified | Hooks `open`, `close`, first `read`, and writes $> 8$ bytes |
| **Kernel Boot** | [`kernel/main.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/main.c) | Modified | Invokes `replay_init()` on CPU 0 before starting scheduler |
| **Syscall Registry** | [`kernel/syscall.h`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/syscall.h) | Modified | Assigns syscall number `SYS_replaylog 23` |
| **Syscall Dispatch** | [`kernel/syscall.c`](file:///d:/sem5/os/xv6-riscv-miniproj/kernel/syscall.c) | Modified | Connects syscall table entry to `sys_replaylog` handler |
| **User API** | [`user/user.h`](file:///d:/sem5/os/xv6-riscv-miniproj/user/user.h) | Modified | User-level prototype for `replaylog(int, void *, int)` |
| **Assembly Shim** | [`user/usys.pl`](file:///d:/sem5/os/xv6-riscv-miniproj/user/usys.pl) | Modified | Generates assembly stub with `ecall` |
| **User CLI** | [`user/replay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/user/replay.c) | Added | CLI tool with `list`, `<pid>`, `-t <pid>`, `all` modes |
| **Test Program** | [`user/testreplay.c`](file:///d:/sem5/os/xv6-riscv-miniproj/user/testreplay.c) | Added | Demonstration and validation test for all logged events |
| **Build System** | [`Makefile`](file:///d:/sem5/os/xv6-riscv-miniproj/Makefile) | Modified | Links `replay.o` into kernel and builds user tools in `UPROGS` |
