CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L
relay_client: relay_client.c third_party/monocypher.c
	$(CC) $(CFLAGS) -o $@ relay_client.c third_party/monocypher.c
clean:
	rm -f relay_client
