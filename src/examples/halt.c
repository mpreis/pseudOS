/* halt.c

   Simple program to test whether running a user program works.
 	
   Just invokes a system call that shuts down the OS. */

#include <syscall.h>
   
#include "tests/lib.h"
#include "tests/main.h"

int
main (void)
{
  	halt (); //origin
  /* not reached */
}
