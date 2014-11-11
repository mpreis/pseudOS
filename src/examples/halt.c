/* halt.c

   Simple program to test whether running a user program works.
 	
   Just invokes a system call that shuts down the OS. */

#include <syscall.h>

int
main (void)
{
	// pseudOS: start
	char buffer[5]; 
	pid_t pid = 1;
	unsigned initial_size = 10;
	unsigned length = 1;
	unsigned position = 2;
	int fd = 1;

	buffer[0] = 'h';
	buffer[1] = 'a';
	buffer[2] = 'l';
	buffer[3] = 't';
	buffer[4] = '\0';
	
	//halt ();
	//exit (0);
	//exec (file);
	//wait (pid);
	//create (buffer, initial_size);
	//remove (buffer);
	open (buffer);
	//filesize (fd);
	//read (fd, buffer, length);
	//write (fd, buffer, length);
	//seek (fd, position);
	//tell (fd);
	//close (fd);
	// pseudOS: end

  //halt (); origin
  /* not reached */
}
