all:
	$(CC) -o spockfs -Wall -Werror -O3 -g `pkg-config --cflags fuse` spockfs.c `pkg-config --libs fuse` -lcurl
