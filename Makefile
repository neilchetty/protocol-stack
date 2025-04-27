CC = gcc
CFLAGS = -g -Wall -Wextra -Wno-unused-variable -I.
LDFLAGS = -pthread

TARGET = protocol_stack

BUILDDIR = build

SRCS =  main.c \
		src/physical-impl.c \
		src/data-link-impl.c \
		src/network-impl.c \
		src/transport-impl.c \
		src/application-impl.c \
		thread-pool-src/thpool.c

OBJS = $(patsubst %.c,$(BUILDDIR)/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking..."
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

$(BUILDDIR)/%.o: %.c
	@echo "Compiling $< -> $@"
	@mkdir -p $(@D) # Create the directory for the object file if it doesn't exist (@D gets the directory part of $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up build artifacts..."
	rm -rf $(BUILDDIR) $(TARGET) # Remove the entire build directory and the target
	@echo "Clean complete."

ARGS ?= nic1 nic2
run: all
	@echo "Running: ./$(TARGET) $(ARGS)"
	./$(TARGET) $(ARGS)

.PHONY: all clean run
