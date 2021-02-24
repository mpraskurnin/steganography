all: process

process: process.c
	gcc -g -o process crc.c zutil.c helper.c process.c -lz

clean: 
	rm process
