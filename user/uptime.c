#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void) {
  uint ticks;

  // Call the uptime system call
  ticks = uptime();

  // Print the number of ticks (uptime)
  printf("Uptime in ticks: %d\n", ticks);
  
  exit(0);
}

