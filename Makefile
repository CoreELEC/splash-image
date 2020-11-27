LDFLAGS+=-Wl,-Bstatic -lspng_static -lz -Wl,-Bdynamic -lc -lm -pthread
OBJS = splash-image.o splash-timer.o

splash-image: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f splash-image
	rm -f *.o
