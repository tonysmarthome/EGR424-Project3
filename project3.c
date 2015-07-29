#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/lm3s6965.h"
#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "driverlib/interrupt.h"
#include "rit128x96x4.h"
#include "scheduler.h"



void main(void)
{
  unsigned i;

  
  periphs_init();
  // Create all the threads and allocate a stack for each one
  for (i=0; i < NUM_THREADS; i++) {
    // Mark thread as runnable
    threads[i].active = 1;

    // Allocate stack
    threads[i].stack = (char *)malloc(STACK_SIZE) + STACK_SIZE;
    if (threads[i].stack == 0) {
      iprintf("Out of memory\r\n");
      exit(1);
    }

    // After createThread() executes, we can execute a longjmp()
    // to threads[i].state and the thread will begin execution
    // at threadStarter() with its own stack.
    createThread(threads[i].state, threads[i].stack);
  }

  // Initialize the global thread lock
  // lock_init(&threadlock);
  lock_init(&OLED_lock);
  lock_init(&UART0_lock);
  lock_init(&UART1_lock);
  lock_init(&LED_lock);

  iprintf("Num of threads: %d\n", NUM_THREADS);

  // Start running coroutines
  //scheduler();
  currThread = -1;
  yield();

  // If scheduler() returns, all coroutines are inactive and we return
  // from main() hence exit() should be called implicitly (according to
  // ANSI C). However, TI's startup_gcc.c code (ResetISR) does not
  // call exit() so we do it manually.
  exit(0);
}

void lock_init(lock_t* lock)
{
    lock->lock_state = 1; // initialize lock as released
    lock->lock_count = 0; // initialize no lock
    lock->lock_owner = -1; // not an owner

}

unsigned lock_acquire(lock_t* lock)
{
    if(lock->lock_state == 0 && lock->lock_owner == currThread)
    {
        // If already locked, increment lock count
        lock->lock_count ++;
    }

        asm volatile("MOV 	r1, #0\n"
                     "LDREX 	r2, [r0]\n"	// R2: Lock value
                     "CMP 	r2, r1\n"	// Is it 0? y:locked
                     "ITT 	NE\n"		// 
                     "STREXNE 	r2, r1, [r0]\n" // if not locked try to claim it R2: 0 if success, 1 if failure
                     "CMPNE 	r2, #1\n"	// check success
                     "BEQ 	1f\n");		// branch if lock was already 0
    // If this code executes, it did not branch
        lock->lock_count++ ;  // increment lock count
        lock->lock_state = 0; // locked
        lock->lock_owner = currThread; // owner
        asm volatile("MOV 	R0, #1\n"	// Indicate success, return 1
                     "BX 	LR\n"		// Branch out of function
                     "1:\n"			// label to branch to if we did not get the lock
                     "CLREX\n"			// clear exclusive access
                     "MOV 	R0, #0\n"	// indicate failure
                     "BX 	LR\n");

    // return 1; // always succeeds
}

void lock_release(lock_t* lock)
{
    lock->lock_count --;  // decrement lock count

    if(lock->lock_count == 0)
    {
        // really unlocked, release the lock
        lock->lock_state = 1;
        lock->lock_owner = -1;
    }
}


// This function is called from within user thread context. It executes
// a jump back to the scheduler. When the scheduler returns here, it acts
// like a standard function return back to the caller of yield().
void yield(void)
{
    iprintf("In yield\n");
    // call scheduler
    asm volatile ("svc #100");
}

// This is the starting point for all threads. It runs in user thread
// context using the thread-specific stack. The address of this function
// is saved by createThread() in the LR field of the jump buffer so that
// the first time the scheduler() does a longjmp() to the thread, we
// start here.
void threadStarter(void)
{
  // Call the entry point for this thread. The next line returns
  // only when the thread exits.
  (*(threadTable[currThread]))();

  // Do thread-specific cleanup tasks. Currently, this just means marking
  // the thread as inactive. Do NOT free the stack here because we're
  // still using it! Remember, this function runs in user thread context.
  threads[currThread].active = 0;

  // This yield returns to the scheduler and never returns back since
  // the scheduler identifies the thread as inactive.
  yield();
}

void scheduler_handler(void)
{
    iprintf("in temp schedule");
    unsigned i;


  
    do {
        // It's kinda inefficient to call setjmp() every time through this
        // loop, huh? I'm sure your code will be better.
        if (save_registers(scheduler_buf)==0) {

            // We saved the state of the scheduler, now find the next
            // runnable thread in round-robin fashion. The 'i' variable
            // keeps track of how many runnable threads there are. If we
            // make a pass through threads[] and all threads are inactive,
            // then 'i' will become 0 and we can exit the entire program.
            i = NUM_THREADS;
            do {
                // Round-robin scheduler
                if (++currThread == NUM_THREADS) {
                    currThread = 0;
                }
                
                if (threads[currThread].active) {
                    restore_registers(threads[currThread].savedregs);
                } else {
                    i--;
                }
            } while (i > 0);

            // No active threads left. Leave the scheduler, hence the program.
            return;

        } else {
            // yield() returns here. Did the thread that just yielded to us exit? If
            // so, clean up its entry in the thread table.
            if (! threads[currThread].active) {
                free(threads[currThread].stack - STACK_SIZE);
            }
        }
    } while (1);
}


void systick_init(void)
{
    NVIC_ST_CTRL_R = NVIC_ST_CTRL_CLK_SRC |
        NVIC_ST_CTRL_INTEN |
        NVIC_ST_CTRL_ENABLE;
    // Fire every 1 second, with 8MHz clock
    NVIC_ST_RELOAD_R = 0x1F40;
}

void periphs_init(void)
{
    // Set the clocking to run directly from the crystal.
    SysCtlClockSet(SYSCTL_SYSDIV_1 | SYSCTL_USE_OSC | SYSCTL_OSC_MAIN |
                   SYSCTL_XTAL_8MHZ);

    // Initialize the OLED display and write status.
    RIT128x96x4Init(1000000);
    RIT128x96x4StringDraw("Scheduler Demo",       20,  0, 15);

    // Enable the peripherals used by this example.
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

    // Set GPIO A0 and A1 as UART pins.
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    // Configure the UART for 115,200, 8-N-1 operation.
    UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200,
                        (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                         UART_CONFIG_PAR_NONE));
    systick_init();
    IntMasterEnable();
    
}

int save_registers(unsigned* buffer)
{
    // Takes Place of setjmp
    // Saves the current environment
    asm volatile ("mrs r12,psp\n"
                  "stmea r0!, {r4-r12}");
    // return 0 when setting up the buffer
    return 0;
    
}

void restore_registers(unsigned* buffer)
{
    // Takes place of longjmp
    // Restores the the environment previously saved b
    // save_registers()
    // branch back with the process stack,
    // returning to thread mode

    asm volatile ("ldr r1, [r0] \n"
                  "add r0, r0, #4\n"
                  "msr psp, r1\n"
                  "ldm r0, {r4-r12}\n"
                  "movw lr, 0xfffd\n"
                  "movt lr, 0xffff\n"
                  "bx lr");
}




/*
 * Compile with:
 * ${CC} -o lockdemo.elf -I${STELLARISWARE} -L${STELLARISWARE}/driverlib/gcc 
 *     -Tlinkscript.x -Wl,-Map,lockdemo.map -Wl,--entry,ResetISR 
 *     lockdemo.c create.S threads.c startup_gcc.c syscalls.c rit128x96x4.c 
 *     -ldriver
 */

