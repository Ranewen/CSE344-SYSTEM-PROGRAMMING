#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int print_operation(char *argv[])
{
	printf("%s <filename> <num-bytes> [x]\n", argv[0]);
	printf("   where x is only checked for existence\n");
	return 0;
}

/* Main Function */
int main(int argc, char *argv[])
{
	int file_desc;
	int flags;
	int i;
	long num_bytes;
	char *file_name;

	if (argc < 3 || argc > 4) 
	{
		print_operation(argv);
		return 1;
	}
	/* By default, the program should open the file with the  O_APPENDflag */
	flags = (O_RDWR | O_CREAT);
	if (argc == 3) 
	{
		flags |= (O_APPEND);
	}

	file_name = argv[1];
	num_bytes = atol(argv[2]);
	file_desc = open(file_name, flags, S_IRUSR | S_IWUSR);
	
	/* Error file */ 
	if (file_desc == -1) {
		printf("Could not open specified filename (%s)\n", file_name);
		print_operation(argv);
		return 1;
	}
	
	/* if   a   third   command-line   argument   (x)   is   supplied */
	for (i = 0; i < num_bytes; i++) 
	{
		if (argc == 3) 
		{
			write(file_desc, "i", 1);
		} 
		else 
		{
			lseek(file_desc, 0, SEEK_END);
			write(file_desc, "i", 1);
		}
	}

	close(file_desc);
	return 0;
}
