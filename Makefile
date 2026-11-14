CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_GNU_SOURCE -Iinclude
LDFLAGS ?= -pthread
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := warehouse

.PHONY: all clean run interactive test tsan

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

build/%.o: src/%.c include/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

run: $(BIN)
	./$(BIN) --mode=auto --producers=3 --consumers=4 --duration=5

interactive: $(BIN)
	./$(BIN) --mode=interactive

test: $(BIN)
	./tests/stress.sh

# Build with ThreadSanitizer to prove there are no data races.
tsan: clean
	$(MAKE) CFLAGS="$(CFLAGS) -g -O1 -fsanitize=thread" LDFLAGS="$(LDFLAGS) -fsanitize=thread"
	./$(BIN) --mode=auto --duration=3 --quiet

clean:
	rm -rf build $(BIN)
