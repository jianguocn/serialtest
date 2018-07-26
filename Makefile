CC?=gcc

all: serialtest

.PHONY : all

serialtest:
	sudo $(CC) --static -pthread -o /usr/bin/serialtest serialtest.c crc16.c
