CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -O1
TARGET = MacsBugDemangler
SOURCE = MacsBugDemangler.c

# Default target (same as debug)
$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

# Clean target
clean:
	rm -f $(TARGET)

# Debug build (same as default)
debug: $(TARGET)

# Release build with optimization and no sanitizers
release: CFLAGS = -Wall -Wextra -g -std=c99 -O2 -DNDEBUG
release: $(TARGET)

# Phony targets
.PHONY: clean debug release