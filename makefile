CC = gcc
CFLAGS = -std=c11 -ggdb
OUT = as6.o
SRC = as6.c

all:	sysSemaphores

sysSemaphores:
	$(CC) -o $(OUT) $(SRC) $(CFLAGS)
	chmod 775 $(OUT)

clean:
	rm -f $(OUT)
