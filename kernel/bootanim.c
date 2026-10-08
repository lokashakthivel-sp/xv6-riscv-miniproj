// kernel/bootanim.c

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "bootanim.h"

// Busy-wait delay. Runs a no-op loop for `n` iterations.
// At QEMU speed ~100000 iterations ≈ a small visible pause.
static void
boot_delay(int n)
{
  volatile int i;
  for(i = 0; i < n; i++)
    ;
}

// Print one boot log line with a delay before it appears.
// tag   : TAG_OK / TAG_WARN / TAG_FAIL / TAG_INFO
// msg   : the message string
static void
boot_line(const char *tag, const char *msg)
{
  boot_delay(3000000);   // pause before each line appears
  printk("%s %s\n", tag, msg);
}

// Called from main.c after all subsystems init.
// Prints the animated boot sequence then the splash screen.
void
boot_animate(int free_pages, int num_inodes, int init_pid)
{
  // Clear screen
  printk("\033[2J\033[H");

  printk(COLOR_BOLD "\nxv6 RISC-V — Booting...\n" COLOR_RESET);

  // -- Memory
  boot_delay(2000000);
  printk(TAG_OK " Initializing memory...          ");
  boot_delay(1500000);
  printk(COLOR_GREEN "%d pages free\n" COLOR_RESET, free_pages);

  // -- Virtual memory
  boot_line(TAG_OK, "Setting up virtual memory...    done");

  // -- Filesystem
  boot_delay(2000000);
  printk(TAG_OK " Mounting filesystem...          ");
  boot_delay(1500000);
  printk(COLOR_GREEN "%d inodes\n" COLOR_RESET, num_inodes);

  // -- Trap vectors
  boot_line(TAG_OK, "Installing trap vectors...      done");

  // -- Scheduler
  boot_line(TAG_OK, "Starting process scheduler...   done");

  // -- Swap (xv6 has none — intentional WARN)
  boot_line(TAG_WARN, "No swap space configured");

  // -- Interrupts
  boot_line(TAG_OK, "Enabling interrupts...          done");

  // -- Init process
  boot_delay(2000000);
  printk(TAG_OK " Launching init...               ");
  boot_delay(1500000);
  printk(COLOR_GREEN "pid %d\n" COLOR_RESET, init_pid);

  boot_delay(3000000);

  // ── Splash Screen ─────────────────────────────────────────
  printk("\033[2J\033[H");   // clear screen again

//   printk(COLOR_CYAN COLOR_BOLD);
//   printk("  ██╗  ██╗██╗   ██╗ ██████╗\n");
//   printk("  ╚██╗██╔╝██║   ██║██╔════╝\n");
//   printk("   ╚███╔╝ ██║   ██║███████╗\n");
//   printk("   ██╔██╗ ╚██╗ ██╔╝██╔═══██╗\n");
//   printk("  ██╔╝ ██╗ ╚████╔╝ ╚██████╔╝\n");
//   printk("  ╚═╝  ╚═╝  ╚═══╝   ╚═════╝\n");
//   printk(COLOR_RESET);

  printk(COLOR_BOLD "RISC-V xv6 OS\n" COLOR_RESET);

  boot_delay(4000000);   // let them read the splash
}