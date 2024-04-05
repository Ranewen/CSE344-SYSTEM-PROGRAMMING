all: appendMeMore

appendMeMore: appendMeMore.o
	gcc appendMeMore.o -o appendMeMore

appendMeMore.o: appendMeMore.c
	gcc -c -Wall -pedantic-errors  appendMeMore.c

clean:
	rm -rf *o appendMeMore