#include <stdio.h>
#include "scheduler.h"

void thread1(void)
{
  while (1) {
    if (lock_acquire(&threadlock))
    {
        lock_acquire(&threadlock);
        // Simulate code that is occasionally interrupted
        iprintf("lock cnt: %i\n", threadlock.lock_count);
        iprintf("THIS IS T");
        yield(); // context switch "interrupt"
        iprintf("HREAD NU");
        yield(); // context switch "interrupt"
        iprintf("MBER 1\r\n");
        iprintf("lock cnt: %i\n", threadlock.lock_count);
        lock_release(&threadlock);
         lock_release(&threadlock);
        iprintf("lock cnt: %i\n", threadlock.lock_count);
    }
    yield();
  }
}

void thread2(void)
{
  while (1) {
    if (lock_acquire(&threadlock)) {
      // Simulate code that is occasionally interrupted
      iprintf("this is t");
          yield(); // context switch "interrupt"
      iprintf("hread number 2\r\n");

      lock_release(&threadlock);
    }
    yield();
  }
}