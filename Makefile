COMPILE_OPTIONS=-std=gnu99 -Wall -Wextra -Werror -pedantic -pthread
PROJECT_NAME=proj2
CC=gcc

.PHONY: clear clean

all: main
	
main:	$(PROJECT_NAME).c
	$(CC) $(PROJECT_NAME).c $(COMPILE_OPTIONS) -o $(PROJECT_NAME)

clear: clean

clean: 
	-rm proj2.zip 
	-rm -f proj2
	-rm -f proj2.out

pack: clean
	zip -r proj2.zip proj2.c Makefile
