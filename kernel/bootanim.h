// kernel/bootanim.h
// ANSI color codes work in QEMU's terminal output
#define COLOR_GREEN  "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RED    "\033[31m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_BOLD   "\033[1m"
#define COLOR_RESET  "\033[0m"

#define TAG_OK   COLOR_GREEN  "[  OK  ]" COLOR_RESET
#define TAG_WARN COLOR_YELLOW "[ WARN ]" COLOR_RESET
#define TAG_FAIL COLOR_RED    "[ FAIL ]" COLOR_RESET
#define TAG_INFO COLOR_CYAN   "[ INFO ]" COLOR_RESET