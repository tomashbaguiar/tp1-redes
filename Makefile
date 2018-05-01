PROGRAMS := dcc023c3
CC := gcc
FLAGS := -Wall -Werror -pedantic -Wextra -fno-stack-protector -pthread

all: $(PROGRAMS)

dcc023c3: dcc023c3.c
	$(CC) $(FLAGS) dcc023c3.c -o dcc023c2

clean: 
	rm -rf dcc023c2
