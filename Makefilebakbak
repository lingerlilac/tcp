objects =  receivea.o

helloworld: $(objects)
	gcc -o receivea $(objects)  -lpthread -L/usr/lib/mysql -lmysqlclient
receivea.o: receivea.c hash_lin.h
	gcc -c receivea.c  -lpthread 
clean:
	rm -f *.o receivea