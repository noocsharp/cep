INC = -IwsServer/include
LIB = -LwsServer -lws

server: main.o cep.o cep.h
	$(CC) main.o cep.o -o server $(LIB)

main.o: main.c
	$(CC) -c main.c -o main.o $(INC)

cep.o: cep.c cep.h
	$(CC) -c cep.c -o cep.o $(INC)

.PHONY: wsServer
wsServer:
	$(MAKE) -C wsServer

clean:
	rm *.o server
