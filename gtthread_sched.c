/**********************************************************************
gtthread_sched.c.  

This file contains the implementation of the scheduling subset of the
gtthreads library.  A simple round-robin queue should be used.
 **********************************************************************/
/*
  Include as needed
*/

#include "gtthread.h"
#include <stdbool.h>

/* 
   Students should define global variables and helper functions as
   they see fit.
 */
enum GTTHREAD_STATE {
  GTTHREAD_NORMAL,
  GTTHREAD_DEAD
};

typedef struct gtthread{
  gtthread_t joining_id;
  ucontext_t *uct;
  enum GTTHREAD_STATE state;
  void *retval;
}gtthread;

sigset_t vtalrm;
struct itimerval *timer;
steque_t *wait_queue;
steque_t *dead_queue;
gtthread *current;
gtthread_t mainThread = 0;

/*
  where the thread actually starts from
*/
void lazy_start(void *(* start_routine)(void *), void *args) {
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  current->retval = (*start_routine)(args);
  gtthread_exit(current->retval);
}

/*
  get handle of gtthread by its thread address.
*/
gtthread *get_thread(gtthread_t thread) {

  steque_node_t *node_t = NULL;

  if (!steque_isempty(wait_queue))
    node_t = wait_queue->front;
  while (NULL != node_t) {
    gtthread *gt = (gtthread *)node_t->item;
    if (gtthread_equal(thread, &(*gt)))
      return  gt;
    node_t = node_t->next;
  }

  if (!steque_isempty(dead_queue))
    node_t = dead_queue->front;
  while (NULL != node_t) {
    gtthread *gt = (gtthread *)node_t->item;    
    if (gtthread_equal(thread, &(*gt)))
      return  gt;
    node_t = node_t->next;
  }

  return NULL;
}
/*
  schedule next thread to run
*/
void turn_next(int sig) {
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  gtthread *prev = current;
  steque_enqueue(wait_queue, current);
  while(!steque_isempty(wait_queue)) {
    current = steque_pop(wait_queue);
    if (current->state == GTTHREAD_NORMAL) 
      break;
  }
  if (current->state != GTTHREAD_NORMAL)
    return;
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  
  if (swapcontext(prev->uct, current->uct) == -1){
    perror("swapcontext");
    exit(-1);
  }
}

/*
  release thread heap resouces
*/
void release_thread(gtthread *gt) {
  free(gt->uct->uc_stack.ss_sp);
  free(gt->uct);
  gt->uct = NULL;
  gt->state = GTTHREAD_DEAD;
  gt->joining_id = 0;
  steque_enqueue(dead_queue, gt);
}

/*
  The gtthread_init() function does not have a corresponding pthread equivalent.
  It must be called from the main thread before any other GTThreads
  functions are called. It allows the caller to specify the scheduling
  period (quantum in micro second), and may also perform any other
  necessary initialization.  If period is zero, then thread switching should
  occur only on calls to gtthread_yield().

  Recall that the initial thread of the program (i.e. the one running
  main() ) is a thread like any other. It should have a
  gtthread_t that clients can retrieve by calling gtthread_self()
  from the initial thread, and they should be able to specify it as an
  argument to other GTThreads functions. The only difference in the
  initial thread is how it behaves when it executes a return
  instruction. You can find details on this difference in the man page
  for pthread_create.
 */
void gtthread_init(long period){
  wait_queue = (steque_t *)malloc(sizeof(steque_t));
  dead_queue = (steque_t *)malloc(sizeof(steque_t));
  steque_init(wait_queue);
  steque_init(dead_queue);

  gtthread *gt = (gtthread *)malloc(sizeof(gtthread));
  gt->joining_id = 0;
  gt->retval = 0;
  gt->state = GTTHREAD_NORMAL;
  gt->uct = (ucontext_t *)malloc(sizeof(ucontext_t));
  memset(gt->uct, 0, sizeof(ucontext_t));
  if (getcontext(gt->uct) == -1) { 
    perror("getcontext");
    exit(-1);
  }

  mainThread = &(*gt);
  current = gt;

  struct sigaction act;
  sigemptyset(&vtalrm);
  sigaddset(&vtalrm, SIGVTALRM);
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);

  if (period <= 0)
    return;
  timer = (struct itimerval*)malloc(sizeof(struct itimerval));
  timer->it_value.tv_sec = timer->it_interval.tv_sec = 0;
  timer->it_value.tv_usec = timer->it_interval.tv_usec = period;

  setitimer(ITIMER_VIRTUAL, timer, NULL);
  memset(&act, 0, sizeof(act));
  act.sa_handler = &turn_next;
  if (sigaction(SIGVTALRM, &act, NULL) < 0) {
    perror("sigaction");
    exit(-1);
  }
}

/*
  The gtthread_create() function mirrors the pthread_create() function,
  only default attributes are always assumed.
 */
int gtthread_create(gtthread_t *thread,
		    void *(*start_routine)(void *),
		    void *arg){
    sigprocmask(SIG_BLOCK, &vtalrm, NULL);
    gtthread *gt = (gtthread *)malloc(sizeof(gtthread));
    gt->joining_id = 0;
    gt->retval = NULL;
    gt->state = GTTHREAD_NORMAL;
    gt->uct = (ucontext_t *)malloc(sizeof(ucontext_t));
    memset(gt->uct, 0, sizeof(ucontext_t));
    if (getcontext(gt->uct) == -1) {
      perror("getcontext"); 
      exit(-1);
    }
    gt->uct->uc_stack.ss_sp = (char *)malloc(SIGSTKSZ);
    gt->uct->uc_stack.ss_size = SIGSTKSZ;

    makecontext(gt->uct, lazy_start, 2, start_routine, arg);
    steque_enqueue(wait_queue, gt);

    *thread = &(*gt);
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return 0;
}

/*
  The gtthread_join() function is analogous to pthread_join.
  All gtthreads are joinable.
 */
int gtthread_join(gtthread_t thread, void **status){
  
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);

  gtthread *gt = get_thread(thread);
  bool error = gtthread_equal(&(*current), thread) || (gt == NULL) || gtthread_equal(gt->joining_id, &(*current));
  if (error)
    return -1;

  current->joining_id = thread;
  while (gt->state == GTTHREAD_NORMAL) {
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    gtthread_yield();
    sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  }

  if (status == NULL)
    return 0;

  if (gt->state == GTTHREAD_DEAD)
    *status = gt->retval;

  return 0;
}

/*
  The gtthread_exit() function is analogous to pthread_exit.
 */
void gtthread_exit(void* retval){
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);

  if (steque_isempty(wait_queue))
    exit(retval);

  if (gtthread_equal(&(*current), mainThread)) {
    while (!steque_isempty(wait_queue)) {
      sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
      gtthread_yield();
      sigprocmask(SIG_BLOCK, &vtalrm, NULL);
    }
    exit(current->retval);
  }

  gtthread *prev = current;
  while (!steque_isempty(wait_queue)) {
    current = steque_pop(wait_queue); 
    if (current->state == GTTHREAD_NORMAL)
      break;
  }
  if (current->state != GTTHREAD_NORMAL) {
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    exit((int)retval);
  }

  prev->retval = retval;
  release_thread(prev);

  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  setcontext(current->uct);
}

/*
  The gtthread_yield() function is analogous to pthread_yield, causing
  the calling thread to relinquish the cpu and place itself at the
  back of the schedule queue.
 */
void gtthread_yield(void){
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  turn_next(SIGVTALRM);
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
}

/*
  The gtthread_yield() function is analogous to pthread_equal,
  returning zero if the threads are the same and non-zero otherwise.
 */
int  gtthread_equal(gtthread_t t1, gtthread_t t2){
  return t1 == t2;
}

/*
  The gtthread_cancel() function is analogous to pthread_cancel,
  allowing one thread to terminate another asynchronously.
 */
int  gtthread_cancel(gtthread_t thread){
  if (gtthread_equal(thread, &(*current)))
    gtthread_exit(current->retval);

  gtthread *gt = get_thread(thread);
  if (gt == NULL || gt->state != GTTHREAD_NORMAL) {
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return -1;
  }

  release_thread(gt);
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  
  return 0;
}

/*
  Returns calling thread.
 */
gtthread_t gtthread_self(void){
  return &(*current);
}