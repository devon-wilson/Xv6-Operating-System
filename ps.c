#ifdef CS333_P2
#include "types.h"
#include "user.h"
#include "uproc.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(2, "usage: pass in max number of processes to list...\n");
    exit();
  }

  uint numprocs = atoi(argv[1]);

  if(numprocs == 0)
  {
    printf(2, "Please only pass in an integer value.\n");
    exit();
  }

  struct uproc *p = malloc(sizeof(struct uproc) * numprocs);
  int procs = getprocs(numprocs, p);

  #ifdef CS333_P4
  printf(1, "\nPID\tName\t         UID\tGID\tPPID\tPRIO\tElapsed\tCPU\tState\tSize\n");
  #else
  printf(1, "\nPID\tName\t         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\n");
  #endif

  for (int i = 0; i < procs; i++)
  {
    char* proc_prefix = "";
    if (p[i].elapsed_ticks%1000 < 100)
    {
      proc_prefix = "0";
    } else if (p[i].elapsed_ticks%1000 < 10)
    {
      proc_prefix = "00";
    }

    char* cpu_prefix = "";
    if (p[i].CPU_total_ticks%1000 < 100)
    {
      cpu_prefix = "0";
    } else if (p[i].CPU_total_ticks%1000 < 10)
    {
      cpu_prefix = "00";
    }
    #ifdef CS333_P4
    printf(1, "%d\t%s\t\t %d\t%d\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\n", p[i].pid, p[i].name, p[i].uid, p[i].gid, p[i].ppid, p[i].priority, p[i].elapsed_ticks/1000, proc_prefix, p[i].elapsed_ticks%1000, p[i].CPU_total_ticks/1000, cpu_prefix, p[i].CPU_total_ticks%1000, p[i].state, p[i].size);
    #else
    printf(1, "%d\t%s\t\t %d\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\n", p[i].pid, p[i].name, p[i].uid, p[i].gid, p[i].ppid, p[i].elapsed_ticks/1000, proc_prefix, p[i].elapsed_ticks%1000, p[i].CPU_total_ticks/1000, cpu_prefix, p[i].CPU_total_ticks%1000, p[i].state, p[i].size);
    #endif
  }

  free(p);
  exit();
}
#endif // CS333_P2