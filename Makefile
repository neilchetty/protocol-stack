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
	$(CC) $(OBJS) -o $(BUILDDIR)/$(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"
	@echo "To See Usage Run: ./$(BUILDDIR)/$(TARGET)"

$(BUILDDIR)/%.o: %.c
	@echo "Compiling $< -> $@"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up build artifacts..."
	rm -rf $(BUILDDIR)
	@echo "Clean complete."

.PHONY: all clean
