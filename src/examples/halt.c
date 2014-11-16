/* halt.c

   Simple program to test whether running a user program works.
 	
   Just invokes a system call that shuts down the OS. */

#include <syscall.h>

int
main (void)
{
	// pseudOS: start
	char buffer[5]; 
	char bufcon[5]; 
	char bufcon2[3]; 
	// pid_t pid = 1;
	unsigned initial_size = 10;
	// unsigned length = 1;
	// unsigned position = 2;
	// int fd = 1;

	buffer[0] = 't';
	buffer[1] = 'e';
	buffer[2] = 's';
	buffer[3] = 't';
	buffer[4] = '\0';

	bufcon[0] = '1';
	bufcon[1] = '2';
	bufcon[2] = '3';
	bufcon[3] = '4';
	bufcon[4] = '\0';
	
	
	//halt ();
	//exit (0);
	//exec (file);
	//wait (pid);
	create (buffer, initial_size);
	//remove (buffer);
	int fd = open (buffer);
	// filesize (fd);
	write (fd, bufcon, 5);
	read (fd, bufcon2, 3);
	printf(" --- USERPROG: %s \n", bufcon);
	//seek (fd, position);
	//tell (fd);
	close (fd);
	// pseudOS: end

  	halt (); //origin
  /* not reached */
}
