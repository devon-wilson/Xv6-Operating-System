#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "uproc.h"
#include "spinlock.h"

static char *states[] = {
[UNUSED]    "unused",
[EMBRYO]    "embryo",
[SLEEPING]  "sleep ",
[RUNNABLE]  "runble",
[RUNNING]   "run   ",
[ZOMBIE]    "zombie"
};

#ifdef CS333_P3
#define statecount NELEM(states)
#endif

#ifdef CS333_P3
// record with head and tail pointer for constant-time access to the beginning
// and end of a linked list of struct procs.  use with stateListAdd() and
// stateListRemove().
struct ptrs {
  struct proc* head;
  struct proc* tail;
};
#endif

static struct {
  struct spinlock lock;
  struct proc proc[NPROC];
#ifdef CS333_P3
  struct ptrs list[statecount];
#endif
#ifdef CS333_P4
  struct ptrs ready[MAXPRIO+1];
  uint PromoteAtTime;
#endif
} ptable;

// list management function prototypes
#ifdef CS333_P3
static void initProcessLists(void);
static void initFreeList(void);
static void stateListAdd(struct ptrs*, struct proc*);
static int  stateListRemove(struct ptrs*, struct proc* p);
static void assertState(struct proc*, enum procstate, const char *, int);
#endif // CS333_P3
#ifdef CS333_P4
static void printReadyLists();
static void printReadyList(struct proc *, int);
#endif // CS333_P4

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void* chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid) {
      return &cpus[i];
    }
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  int found = 0;
  #ifdef CS333_P3
  p = ptable.list[UNUSED].head;
  if (p != NULL)
  {
    found = 1;
  }
  #else
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  #endif
  if (!found) {
    release(&ptable.lock);
    return 0;
  }
  #ifdef CS333_P3
  if(stateListRemove(&ptable.list[UNUSED], p) == -1)
  {
    panic("failed to remove from UNUSED list in allocproc()");
  }
  assertState(p, UNUSED, __FUNCTION__, __LINE__);
  #endif
  p->state = EMBRYO;
  #ifdef CS333_P3
  stateListAdd(&ptable.list[EMBRYO], p);
  #endif
  p->pid = nextpid++;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    #ifdef CS333_P3
    if(stateListRemove(&ptable.list[EMBRYO], p) == -1)
    {
      panic("failed to remove from EMBRYO list after kernal stack allocation failure in allocproc()");
    }
    assertState(p, EMBRYO, __FUNCTION__, __LINE__);
    #endif
    p->state = UNUSED;
    #ifdef CS333_P3
    stateListAdd(&ptable.list[UNUSED], p);
    #endif
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  #ifdef CS333_P1
  p->start_ticks = ticks;
  #endif // CS333_P1

  #ifdef CS333_P2
  p->start_ticks = ticks;
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in = 0;
  #endif // CS333_P2

  #ifdef CS333_P4
  p->priority = MAXPRIO;
  p->budget = DEFAULT_BUDGET;
  #endif

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  #ifdef CS333_P3
  acquire(&ptable.lock);
  initProcessLists();
  initFreeList();
  #ifdef CS333_P4
  ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;
  #endif
  release(&ptable.lock);
  #endif

  p = allocproc();

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  #ifdef CS333_P2
  p->uid = UID_DEFAULT;
  p->gid = GID_DEFAULT;
  #endif // CS333_P2

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
  #ifdef CS333_P3
  if(stateListRemove(&ptable.list[EMBRYO], p) == -1)
  {
    panic("failed to remove from EMBRYO list in fork() after successful allocation in userinit()");
  }
  assertState(p, EMBRYO, __FUNCTION__, __LINE__);
  #endif
  p->state = RUNNABLE;
  #ifdef CS333_P4
  stateListAdd(&ptable.ready[p->priority], p);
  #elif CS333_P3
  stateListAdd(&ptable.list[RUNNABLE], p);
  #endif
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    #ifdef CS333_P3
    acquire(&ptable.lock);
    if(stateListRemove(&ptable.list[EMBRYO], np) == -1)
    {
      panic("failed to remove from EMBRYO list in fork() after page directory allocation failure");
    }
    assertState(np, EMBRYO, __FUNCTION__, __LINE__);
    #endif
    np->state = UNUSED;
    #ifdef CS333_P3
    stateListAdd(&ptable.list[UNUSED], np);
    release(&ptable.lock);
    #endif
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  #ifdef CS333_P2
  np->uid = curproc->uid;
  np->gid = curproc->gid;
  #endif // CS333_P2


  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
  #ifdef CS333_P3
  if(stateListRemove(&ptable.list[EMBRYO], np) == -1)
  {
    panic("failed to remove from EMBRYO on successful fork");
  }
  assertState(np, EMBRYO, __FUNCTION__, __LINE__);
  #endif
  np->state = RUNNABLE;
  #ifdef CS333_P4
  stateListAdd(&ptable.ready[np->priority], np);
  #elif CS333_P3
  stateListAdd(&ptable.list[RUNNABLE], np);
  #endif
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
#ifdef CS333_P3
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  for(int i = 1; i < 6; i++)
  {
    p = ptable.list[i].head;
    while (p != NULL)
    {
      if(p->parent == curproc)
      {
        p->parent = initproc;
        if(p->state == ZOMBIE)
          wakeup1(initproc);
      }
      p = p->next;
    }
  }

  #ifdef CS333_P4
  for(int i = MAXPRIO; i >= 0; i--)
  {
    p = ptable.ready[i].head;
    while(p != NULL)
    {
      if(p->parent == curproc)
      {
        p->parent = initproc;
        if(p->state == ZOMBIE)
          wakeup1(initproc);
      }
      p = p->next;
    }
  }
  #endif

  if(stateListRemove(&ptable.list[RUNNING], curproc) == -1)
  {
    panic("failed to remove from RUNNING list in exit()");
  }
  assertState(curproc, RUNNING, __FUNCTION__, __LINE__);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  stateListAdd(&ptable.list[ZOMBIE], curproc);
#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#else
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#endif

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
#ifdef CS333_P3
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(int i = 1; i < 6; i++)
    {
      p = ptable.list[i].head;
      while (p != NULL)
      {
        if(p->parent != curproc)
        {
          p = p->next;
          continue;
        }
        havekids = 1;
        if(p->state == ZOMBIE){
          // Found one.
          pid = p->pid;
          kfree(p->kstack);
          p->kstack = 0;
          freevm(p->pgdir);
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          if(stateListRemove(&ptable.list[ZOMBIE], p) == -1)
          {
            panic("failed to remove from ZOMBIE list in wait()");
          }
          assertState(p, ZOMBIE, __FUNCTION__, __LINE__);
          p->state = UNUSED;
          stateListAdd(&ptable.list[UNUSED], p);
          release(&ptable.lock);
          return pid;
        }
        p = p->next;
      }
    }

    #ifdef CS333_P4
    for(int i = MAXPRIO; i >= 0; i--)
    {
      p = ptable.ready[i].head;
      while(p != NULL)
      {
        if(p->parent != curproc)
        {
          p = p->next;
          continue;
        }
        havekids = 1;
        p = p->next;
      }
    }
    #endif

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
#else
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
#endif

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
#ifdef CS333_P3
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    #ifdef CS333_P4
    if(ticks >= ptable.PromoteAtTime)
    {
      for(int i = MAXPRIO - 1; i >= 0; i--)
      {
        p = ptable.ready[i].head;
        while(p != NULL)
        {
          p->budget = DEFAULT_BUDGET;
          if(stateListRemove(&ptable.ready[p->priority], p) == -1)
          {
            panic("Process could not be removed from ready list");
          }
          assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
          p->priority = p->priority + 1;
          stateListAdd(&ptable.ready[p->priority], p);
          p = p->next;
        }
      }

      // Promote in RUNNING list

      p = ptable.list[RUNNING].head;
      while(p != NULL)
      {
        if(p->priority < MAXPRIO)
        {
          p->budget = DEFAULT_BUDGET;
          p->priority = p->priority + 1;
        }
        p = p->next;
      }

      // Finally promote in SLEEPING list

      p = ptable.list[SLEEPING].head;
      while(p != NULL)
      {
        if(p->priority < MAXPRIO)
        {
          p->budget = DEFAULT_BUDGET;
          p->priority = p->priority + 1;
        }
        p = p->next;
      }
      ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;
    }

    for(int i = MAXPRIO; i >= 0; i--)
    {
      p = ptable.ready[i].head;
      if (p != NULL)
      {
        break;
      }
    }
    #else
    p = ptable.list[RUNNABLE].head;
    #endif
    if(p != NULL){
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      #ifdef CS333_P4
      if(stateListRemove(&ptable.ready[p->priority], p) == -1)
      {
        panic("failed to remove process we will run from runnable list in scheduler");
      }
      #else
      if(stateListRemove(&ptable.list[RUNNABLE], p) == -1)
      {
        panic("failed to remove process we will run from runnable list in scheduler");
      }
      #endif
      assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
      p->state = RUNNING;
      stateListAdd(&ptable.list[RUNNING], p);
      #ifdef CS333_P2
      p->cpu_ticks_in = ticks;
      #endif
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#else
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      #ifdef CS333_P2
      p->cpu_ticks_in = ticks;
      #endif
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#endif

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  #ifdef CS333_P2
  p->cpu_ticks_total = p-> cpu_ticks_total + (ticks - p->cpu_ticks_in);
  #endif
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
#ifdef CS333_P3
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  if(stateListRemove(&ptable.list[RUNNING], curproc) == -1)
  {
    panic("failed to remove from RUNNING list in yield()");
  }
  assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  curproc->state = RUNNABLE;
  #ifdef CS333_P4
  int newBudget = curproc->budget - (ticks - curproc->cpu_ticks_in);
  if(newBudget <= 0)
  {
    curproc->budget = DEFAULT_BUDGET;
    if(curproc->priority > 0)
    {
      curproc->priority = curproc->priority - 1;
    }
  }
  else
  {
    curproc->budget = newBudget;
  }
  stateListAdd(&ptable.ready[curproc->priority], curproc);
  #else
  stateListAdd(&ptable.list[RUNNABLE], curproc);
  #endif
  sched();
  release(&ptable.lock);
}
#else
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  curproc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}
#endif

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
#ifdef CS333_P3
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;

  if(stateListRemove(&ptable.list[RUNNING], p) == -1)
  {
    panic("failed to remove from RUNNING list in sleep()");
  }
  assertState(p, RUNNING, __FUNCTION__, __LINE__);
  p->state = SLEEPING;
  #ifdef CS333_P4
  int newBudget = p->budget - (ticks - p->cpu_ticks_in);
  if(newBudget <= 0)
  {
    p->budget = DEFAULT_BUDGET;
    if(p->priority > 0)
    {
      p->priority = p->priority - 1;
    }
  }
  else
  {
    p->budget = newBudget;
  }
  #endif
  stateListAdd(&ptable.list[SLEEPING], p);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#else
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#endif

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
#ifdef CS333_P3
static void
wakeup1(void *chan)
{
  struct proc *p;

  p = ptable.list[SLEEPING].head;
  while(p != NULL)
  {
    if(p->state == SLEEPING && p->chan == chan)
    {
      if(stateListRemove(&ptable.list[SLEEPING], p) == -1)
      {
        panic("failed to remove from SLEEPING list in wakeup1()");
      }
      assertState(p, SLEEPING, __FUNCTION__, __LINE__);
      p->state = RUNNABLE;
      #ifdef CS333_P4
      stateListAdd(&ptable.ready[p->priority], p);
      #else
      stateListAdd(&ptable.list[RUNNABLE], p);
      #endif
    }
    p = p->next;
  }
}
#else
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}
#endif

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
#ifdef CS333_P3
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(int i = 1; i < 6; i++)
  {
    p = ptable.list[i].head;
    while (p != NULL)
    {
      if(p->pid == pid)
      {
        p->killed = 1;
        // Wake process from sleep if necessary.
        if(p->state == SLEEPING)
        {
          if(stateListRemove(&ptable.list[SLEEPING], p) == -1)
          {
            panic("failed to remove from SLEEPING list in kill()");
          }
          assertState(p, SLEEPING, __FUNCTION__, __LINE__);
          p->state = RUNNABLE;
          #ifdef CS333_P4
          stateListAdd(&ptable.ready[p->priority], p);
          #else
          stateListAdd(&ptable.list[RUNNABLE], p);
          #endif
        }
        release(&ptable.lock);
        return 0;
      }
      p = p->next;
    }
  }

  #ifdef CS333_P4
  for(int i = MAXPRIO; i >= 0; i--)
  {
    p = ptable.ready[i].head;
    while (p != NULL)
    {
      if(p->pid == pid)
      {
        p->killed = 1;
        release(&ptable.lock);
        return 0;
      }
      p = p->next;
    }
  }
  #endif

  release(&ptable.lock);
  return -1;
}
#else
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
#endif

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

#if defined(CS333_P2)
void
procdumpP2P3P4(struct proc *p, char *state_string)
{
  int ppid;
  if (p->parent)
  {
    ppid = p->parent->pid;
  }else
  {
    ppid = p->pid;
  }

  char* proc_prefix = "";
  if ((ticks - p->start_ticks)%1000 < 100)
  {
    proc_prefix = "0";
  } else if ((ticks - p->start_ticks)%1000 < 10)
  {
    proc_prefix = "00";
  }

  char* cpu_prefix = "";
  if (p->cpu_ticks_total%1000 < 100)
  {
    cpu_prefix = "0";
  } else if (p->cpu_ticks_total%1000 < 10)
  {
    cpu_prefix = "00";
  }

  #ifdef CS333_P4
  cprintf("%d\t%s\t     %d\t\t%d\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d   ", p->pid, p->name, p->uid, p->gid, ppid, p->priority, (ticks - p->start_ticks)/1000, proc_prefix, (ticks - p->start_ticks)%1000, p->cpu_ticks_total/1000, cpu_prefix, p->cpu_ticks_total%1000, state_string, p->sz);
  #else
  cprintf("%d\t%s\t     %d\t\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d   ", p->pid, p->name, p->uid, p->gid, ppid, (ticks - p->start_ticks)/1000, proc_prefix, (ticks - p->start_ticks)%1000, p->cpu_ticks_total/1000, cpu_prefix, p->cpu_ticks_total%1000, state_string, p->sz);
  #endif
  return;
}
#elif defined(CS333_P1)
void
procdumpP1(struct proc *p, char *state_string)
{
  cprintf("%d\t%s\t     %d.%d\t%s\t%d\t", p->pid, p->name, (ticks - p->start_ticks)/1000, (ticks - p->start_ticks)%1000, state_string, p->sz);
  return;
}
#endif

void
procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

#if defined(CS333_P4)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P2)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P1)
#define HEADER "\nPID\tName         Elapsed\tState\tSize\t PCs\n"
#else
#define HEADER "\n"
#endif

  cprintf(HEADER);  // not conditionally compiled as must work in all project states

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

    // see TODOs above this function
    // P2 and P3 are identical and the P4 change is minor
#if defined(CS333_P2)
    procdumpP2P3P4(p, state);
#elif defined(CS333_P1)
    procdumpP1(p, state);
#else
    cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
#endif

    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
#ifdef CS333_P1
  cprintf("$ ");  // simulate shell prompt
#endif // CS333_P1
}

#if defined(CS333_P3)
// list management helper functions
static void
stateListAdd(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL){
    (*list).head = p;
    (*list).tail = p;
    p->next = NULL;
  } else{
    ((*list).tail)->next = p;
    (*list).tail = ((*list).tail)->next;
    ((*list).tail)->next = NULL;
  }
}
#endif

#if defined(CS333_P3)
static int
stateListRemove(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL || (*list).tail == NULL || p == NULL){
    return -1;
  }

  struct proc* current = (*list).head;
  struct proc* previous = 0;

  if(current == p){
    (*list).head = ((*list).head)->next;
    // prevent tail remaining assigned when we've removed the only item
    // on the list
    if((*list).tail == p){
      (*list).tail = NULL;
    }
    return 0;
  }

  while(current){
    if(current == p){
      break;
    }

    previous = current;
    current = current->next;
  }

  // Process not found. return error
  if(current == NULL){
    return -1;
  }

  // Process found.
  if(current == (*list).tail){
    (*list).tail = previous;
    ((*list).tail)->next = NULL;
  } else{
    previous->next = current->next;
  }

  // Make sure p->next doesn't point into the list.
  p->next = NULL;

  return 0;
}
#endif

#if defined(CS333_P3)
static void
initProcessLists()
{
  int i;

  for (i = UNUSED; i <= ZOMBIE; i++) {
    ptable.list[i].head = NULL;
    ptable.list[i].tail = NULL;
  }
#if defined(CS333_P4)
  for (i = 0; i <= MAXPRIO; i++) {
    ptable.ready[i].head = NULL;
    ptable.ready[i].tail = NULL;
  }
#endif
}
#endif

#if defined(CS333_P3)
static void
initFreeList(void)
{
  struct proc* p;

  for(p = ptable.proc; p < ptable.proc + NPROC; ++p){
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
  }
}
#endif

#if defined(CS333_P3)
// example usage:
// assertState(p, UNUSED, __FUNCTION__, __LINE__);
// This code uses gcc preprocessor directives. For details, see
// https://gcc.gnu.org/onlinedocs/cpp/Standard-Predefined-Macros.html
static void
assertState(struct proc *p, enum procstate state, const char * func, int line)
{
    if (p->state == state)
      return;
    cprintf("Error: proc state is %s and should be %s.\nCalled from %s line %d\n",
        states[p->state], states[state], func, line);
    panic("Error: Process state incorrect in assertState()");
}
#endif

#if defined(CS333_P3)
// Project 3/4 control sequence support
void
printList(int state)
{
  int count = 0;
  const int PER_LINE = 15;  // per line max on print
  const int PER_LINE_Z = (PER_LINE/2);  // zombie list has more chars per entry on print
  struct proc *p;
  static char *stateNames[] = {  // note: sparse array
    [RUNNABLE]  "Runnable",
    [SLEEPING]  "Sleep",
    [RUNNING]   "Running",
    [ZOMBIE]    "Zombie"
  };


  if (state < UNUSED || state > ZOMBIE) {
    cprintf("Invalid control sequence\n");
    cprintf("$ ");  // simulate shell prompt
    return;
  }

  acquire(&ptable.lock);
#ifdef DEBUG
  checkProcs(__FILE__, __FUNCTION__, __LINE__);
#endif
#ifdef CS333_P4
  if (state == RUNNABLE) {
    printReadyLists();
    release(&ptable.lock);
    cprintf("$ ");  // simulate shell prompt
    return;
  }
#endif
  cprintf("\n%s List Processes:\n", stateNames[state]);
  p = ptable.list[state].head;
  while (p != NULL) {
    if (p->state != state) {  // sanity check
      cprintf("Error: PID %d on %s list but should be on %s\n",
          p->pid, states[p->state], states[state]);
      panic("Corrupted list\n");
    }
    if (state == ZOMBIE)
      cprintf("(%d, %d)", p->pid,
          (p->parent) ? p->parent->pid : p->pid);
    else
      cprintf("%d", p->pid);
    p = p->next;
    cprintf("%s", p ? " -> " : "\n");
    if (p && (++count) %
        ((state == ZOMBIE) ? PER_LINE_Z : PER_LINE) == 0)
      cprintf("\n");
  }
  release(&ptable.lock);
  cprintf("$ ");  // simulate shell prompt
  return;
}

void
printFreeList(void)
{
  int count = 0;
  struct proc *p;

  acquire(&ptable.lock);
  p = ptable.list[UNUSED].head;
  while (p != NULL) {
    count++;
    p = p->next;
  }
  release(&ptable.lock);
  cprintf("\nFree List Size: %d processes\n", count);
  cprintf("$ ");  // simulate shell prompt
  return;
}

void
printListStats()
{
  int i, count, total = 0;
  struct proc *p;

  acquire(&ptable.lock);
  for (i=UNUSED; i<=ZOMBIE; i++) {
    count = 0;
    p = ptable.list[i].head;
    while (p != NULL) {
      count++;
      if(p->state != i) {
        cprintf("\nlist invariant failed: process %d has state %s but is on list %s\n",
            p->pid, states[p->state], states[i]);
      }
      p = p->next;
    }
    cprintf("\n%s list has ", states[i]);
    if (count < 10) cprintf(" ");  // line up columns. we know NPROC < 100
    cprintf("%d processes", count);
    total += count;
  }
  release(&ptable.lock);
  cprintf("\nTotal on lists is: %d. NPROC = %d. %s",
      total, NPROC, (total == NPROC) ? "Congratulations!" : "Bummer");
  cprintf("\n$ ");  // simulate shell prompt
  return;
}
#endif // CS333_P3

#ifdef CS333_P4
void
printReadyList(struct proc *p, int prio)
{
  const int PER_LINE = 15;  // per line max on print
  if (p == NULL) {
    cprintf("(NULL)\n");
    return;
  }
  int count = 0;
  do {
    cprintf("(%d", p->pid);
    cprintf(" -> %d)", p->budget);
    if(p->priority != prio) {
      cprintf("\nlist invariant failed: process %d has prio %d but is on runnable list %d\n",
          p->pid, p->priority, prio);
    }
    p = p->next;
    cprintf("%s", p ? " -> " : "\n");
    if (p && (++count) % PER_LINE == 0)
      cprintf("\n");
  } while (p != NULL);
}

void
printReadyLists()
{
  struct proc *p;

  cprintf("Ready List Processes:\n");
// this look must be changed based on MAX/MIN prio
  for (int i=MAXPRIO; i>=0; i--) {
    p = ptable.ready[i].head;
    if(p != NULL)
    {
      cprintf("%d: ", i);
      if(p->state != RUNNABLE) {
        cprintf("\nlist invariant failed: process %d has state %s but is on ready list\n",
            p->pid, states[p->state]);
      }
      printReadyList(p, i);
    }
  }
}

int
setpriority(int pid, int priority)
{
  acquire(&ptable.lock);

  struct proc * p = NULL;

  // Look through ready lists

  for(int i = MAXPRIO; i >= 0; i--)
  {
    p = ptable.ready[i].head;
    while(p != NULL)
    {
      if(p->pid == pid)
      {
        p->budget = DEFAULT_BUDGET;
        if(p->priority != priority)
        {
          if(stateListRemove(&ptable.ready[p->priority], p) == -1)
          {
            panic("Process could not be removed from ready list");
          }
          assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
          p->priority = priority;
          stateListAdd(&ptable.ready[priority], p);
          release(&ptable.lock);
          return 0;
        }
        else
        {
          release(&ptable.lock);
          return 0;
        }
      }
      p = p->next;
    }
  }

  // Next try running list

  p = ptable.list[RUNNING].head;
  while(p != NULL)
  {
    if(p->pid == pid)
    {
      p->budget = DEFAULT_BUDGET;
      p->priority = priority;
      release(&ptable.lock);
      return 0;
    }
    p = p->next;
  }

  // Finally try sleeping list

  p = ptable.list[SLEEPING].head;
  while(p != NULL)
  {
    if(p->pid == pid)
    {
      p->budget = DEFAULT_BUDGET;
      p->priority = priority;
      release(&ptable.lock);
      return 0;
    }
    p = p->next;
  }

  release(&ptable.lock);
  return -1;
}

int
getpriority(int pid)
{
  acquire(&ptable.lock);

  struct proc * p = NULL;

  // Look through ready lists

  for(int i = MAXPRIO; i >= 0; i--)
  {
    p = ptable.ready[i].head;
    while(p != NULL)
    {
      if(p->pid == pid)
      {
        int priority = p->priority;
        release(&ptable.lock);
        return priority;
      }
      p = p->next;
    }
  }

  // Search all except UNUSED lists

  for(int i = EMBRYO; i <= ZOMBIE; i++)
  {
    p = ptable.list[i].head;
    while(p != NULL)
    {
      if(p->pid == pid)
      {
        int priority = p->priority;
        release(&ptable.lock);
        return priority;
      }
      p = p->next;
    }
  }
  release(&ptable.lock);
  return -1;
}

#endif // CS333_P4

#ifdef CS333_P2
int
getprocs(uint max, struct uproc* table)
{
  acquire(&ptable.lock);
  int procnum = 0;
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC] && procnum < max; p++)
  {
    if(p->state != UNUSED && p->state != EMBRYO)
    {
      table[procnum].pid = p->pid;
      table[procnum].uid = p->uid;
      table[procnum].gid = p->gid;
      #ifdef CS333_P4
      table[procnum].priority = p->priority;
      #endif

      if(p->parent)
      {
        table[procnum].ppid = p->parent->pid;
      } else
      {
        table[procnum].ppid = p->pid;
      }

      table[procnum].elapsed_ticks = ticks - p->start_ticks;
      table[procnum].CPU_total_ticks = p->cpu_ticks_total;
      safestrcpy(table[procnum].state, states[p->state], STRMAX);
      table[procnum].size = p->sz;
      safestrcpy(table[procnum].name, p->name, STRMAX);

      procnum++;
    }
  }
  release(&ptable.lock);

  return procnum;
}
#endif // CS333_P2

