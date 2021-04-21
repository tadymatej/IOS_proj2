COMPILE_OPTIONS=-std=gnu99 -Wall -Wextra -pedantic -pthread #-Werror
PROJECT_NAME=proj2
CC=gcc

.PHONY: clear clean

all: main
	
main:	$(PROJECT_NAME).c
	$(CC) $(PROJECT_NAME).c $(COMPILE_OPTIONS) -o $(PROJECT_NAME)

clear: clean

clean: 
	rm $(PROJECT_NAME)
	#rm -r $(BUILD_FOLDER)

zip_project:
	zip -r proj2.zip *.c *.h Makefile
