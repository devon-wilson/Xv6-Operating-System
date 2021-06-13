#ifdef CS333_P2
#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  uint starttime = uptime();

  int pid = fork();

  if (pid == 0)
  {
    //Child process
    exec(argv[1], &argv[1]);
    exit();
  } else
  {
    // Parent process
    wait();
  }

  uint stoptime = uptime();

  uint totaltime = stoptime - starttime;
  char* prefix = "";
  if (totaltime%1000 < 100)
  {
    prefix = "0";
  } else if (totaltime%1000 < 10)
  {
    prefix = "00";
  }

  printf(1, "%s ran in %d.%s%d seconds\n", argv[1], totaltime/1000, prefix, totaltime%1000);

  exit();
}
#endif