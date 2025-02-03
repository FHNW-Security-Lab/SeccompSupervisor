CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = 

# Targets
TARGETS = supervisor user-tool test-server test-client

# Default target
all: $(TARGETS)

# Rules for building
supervisor: supervisor.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

user-tool: user-tool.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test-server: test-server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test-client: test-client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Clean rule
clean:
	rm -f $(TARGETS) *.o /tmp/user_tool.sock

# Run tests
test: all
	@echo "Starting test sequence..."
	./user-tool & 
	sleep 1
	./test-server &
	sleep 1
	./supervisor ./test-client
	pkill -f test-server
	pkill -f user-tool

.PHONY: all clean test
