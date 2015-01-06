all:
	$(CC) -o spockfs -Wall -Werror -O3 -g `pkg-config --cflags fuse` `curl-config --cflags` spockfs.c `pkg-config --libs fuse` `curl-config --libs`
