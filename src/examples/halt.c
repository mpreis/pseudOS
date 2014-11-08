/* halt.c

   Simple program to test whether running a user program works.
 	
   Just invokes a system call that shuts down the OS. */

#include <syscall.h>

int
main (void)
{
	// pseudOS: start
	char *file = (void *)0;
	void *buffer = "abc";
	pid_t pid = 1;
	unsigned initial_size = 10;
	unsigned length = 3;
	unsigned position = 2;
	int fd = 1;

	//halt ();
	//exit (0);
	//exec (file);
	//wait (pid);
	//create (file, initial_size);
	//remove (file);
	//open (file);
	//filesize (fd);
	//read (fd, buffer, length);
	write (fd, buffer, length);
	//seek (fd, position);
	//tell (fd);
	//close (fd);
	// pseudOS: end

  //halt (); origin
  /* not reached */
}
