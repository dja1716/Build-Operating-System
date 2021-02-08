#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <machine/trapframe.h>
#include "opt-A2.h"

#if OPT_A2
// create a copy of the current process
// return 0 for the child process
// return PID of the child process for the parent
int sys_fork(struct trapframe* tf, pid_t* retval) {


    // create process structure
    struct proc* childProc = proc_create_runprogram(curproc->p_name);

    if (childProc == NULL) {
        DEBUG(DB_SYSCALL, "Syscall: sys_fork error\n");
        return ENOMEM;
    }

    // create address space and copy
    spinlock_acquire(&childProc->p_lock);
    int AsCopyTemp = as_copy(curproc_getas(), &(childProc->p_addrspace));
    spinlock_release(&childProc->p_lock);

    if (AsCopyTemp) {
        DEBUG(DB_SYSCALL, "Syscall: sys_fork error\n");
        proc_destroy(childProc);
        return ENOMEM;
    }

    struct trapframe* temptf = (struct trapframe*)kmalloc(sizeof(struct trapframe));
    if (temptf == NULL) {
        DEBUG(DB_SYSCALL, "Syscall: sys_fork error\n");
        as_destroy(childProc->p_addrspace);
        proc_destroy(childProc);
        return ENOMEM;
    }

    *temptf = *tf;
    lock_acquire(curproc->p_A2lock);
    childProc->p_parent = curproc;
    array_add(curproc->p_children, childProc, NULL);
    lock_release(curproc->p_A2lock);

    // time to build parent child relationship
    // do thread_fork
    int thread_fork_temp = thread_fork(curthread->t_name, childProc, enter_forked_process, temptf, 0);

    if (thread_fork_temp) {
        DEBUG(DB_SYSCALL, "Syscall: sys_fork error\n");
        as_destroy(childProc->p_addrspace);
        proc_destroy(childProc);
        kfree(temptf);
        return thread_fork_temp;
    }

    *retval = childProc->p_pid;

    return 0;
}
#endif






  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);


#if OPT_A2
  // A2a start
  p->p_alive = false;
  p->p_exit_Status = _MKWAIT_EXIT(exitcode);

  struct proc* parent = p->p_parent;
  lock_acquire(p->p_A2lock);
  cv_broadcast(p->p_cv, p->p_A2lock);
  lock_release(p->p_A2lock);
  if (parent == NULL) {
      proc_destroy(p);
  }


  // A2a end


#else

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
#endif
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */

#if OPT_A2
    * retval = curproc->p_pid;
#else
  *retval = 1;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  // A2 begin
#if OPT_A2
  struct proc* parentProc = curproc;
  struct proc* childProc;
  for (unsigned int i = 0; i < array_num(parentProc->p_children); i++) {
      struct proc* tempProc = array_get(parentProc->p_children, i);
      if (pid == tempProc->p_pid) {
          childProc = array_get(parentProc->p_children, i);
          break;
      }
  }
  if (childProc == NULL) {
      return ECHILD;
  }

  lock_acquire(childProc->p_A2lock);
  while (childProc->p_alive) {
      cv_wait(childProc->p_cv, childProc->p_A2lock);
  }
  lock_release(childProc->p_A2lock);
  exitstatus = childProc->p_exit_Status;
  // A2 end

#else

  exitstatus = 0;
#endif


  /* for now, just pretend the exitstatus is 0 */

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}


