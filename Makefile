CC?=gcc

all: serialtest

.PHONY : all

serialtest: serialtest.o
	$(CC) --static -pthread -o $@ $<

clean:
	rm -f *.o

install: all
	sudo cp ./serialtest /usr/bin/
