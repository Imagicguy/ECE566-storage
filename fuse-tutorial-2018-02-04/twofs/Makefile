twofs: bbfs.c config.h params.h log.o
	cc -D_FILE_OFFSET_BITS=64 -o twofs bbfs.c log.o `pkg-config fuse --cflags --libs`
log.o: log.c log.h
	cc -c -D_FILE_OFFSET_BITS=64 log.c `pkg-config fuse --cflags --libs`
clean:
	rm *~ twofs 
