/**********************************************************************
gtthread_mutex.c.  

This file contains the implementation of the mutex subset of the
gtthreads library.  The locks can be implemented with a simple queue.
 **********************************************************************/

/*
  Include as needed
*/


#include "gtthread.h"

extern sigset_t vtalrm;

/*
  The gtthread_mutex_init() function is analogous to
  pthread_mutex_init with the default parameters enforced.
  There is no need to create a static initializer analogous to
  PTHREAD_MUTEX_INITIALIZER.
 */
int gtthread_mutex_init(gtthread_mutex_t* mutex){
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  steque_init(mutex);
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  return 0;
}

/*
  The gtthread_mutex_lock() is analogous to pthread_mutex_lock.
  Returns zero on success.
 */
int gtthread_mutex_lock(gtthread_mutex_t* mutex){
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  if (steque_isempty(mutex)) {
    steque_enqueue(mutex, gtthread_self());
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return 0;
  }

  if (steque_front(mutex) == gtthread_self()) {
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return 0;
  }

  steque_enqueue(mutex, gtthread_self());
  while (steque_front(mutex) != gtthread_self()) {
    gtthread_yield();
  }

  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  return 0;
}

/*
  The gtthread_mutex_unlock() is analogous to pthread_mutex_unlock.
  Returns zero on success.
 */
int gtthread_mutex_unlock(gtthread_mutex_t *mutex){
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  
  if (steque_isempty(mutex)) {
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return -1;
  }

  if (steque_front(mutex) != gtthread_self()) {
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return -1;
  }

  steque_pop(mutex);
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  return 0;
}

/*
  The gtthread_mutex_destroy() function is analogous to
  pthread_mutex_destroy and frees any resourcs associated with the mutex.
*/
int gtthread_mutex_destroy(gtthread_mutex_t *mutex){
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  steque_destroy(&mutex);
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
}
