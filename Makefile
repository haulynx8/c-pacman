CC ?= gcc
CFLAGS = -Wall -Wextra -Iinclude -std=c99 -g
SRC = src/main.c src/manifest.c src/deps.c src/build.c src/lockfile.c src/semver.c src/registry.c src/audit.c src/clean.c src/update.c
OBJ = $(SRC:.c=.o)
TARGET = cpm

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	@mkdir -p bin
	$(CC) $(CFLAGS) tests/test_semver.c src/semver.c src/manifest.c -o bin/test_semver
	./bin/test_semver

clean:
	rm -f src/*.o bin/* $(TARGET)
	rm -rf .cpm cpm.lock

.PHONY: all test clean
