
/*modified: for addtional system call function*/

#include <stdio.h>
#include <string.h>
#include <syscall.h>

int 
str_to_int(char *str)
{
    int sum = 0;

    for(int i = 0; i < strlen(str); i++){
        sum = sum * 10 + (str[i] - '0');
    }

    return sum;
}

int
main (int argc, char *argv[])
{
    int a = str_to_int(argv[1]);
    int b = str_to_int(argv[2]);
    int c = str_to_int(argv[3]);
    int d = str_to_int(argv[4]);

    printf("%d ", fibonacci(a));
    printf("%d\n", max_of_four_int(a, b, c, d));
    
    return EXIT_SUCCESS;
}