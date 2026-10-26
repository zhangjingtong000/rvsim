# RISC-V 模拟器构建
CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c11
LDFLAGS ?=
SRCDIR   = src
OBJDIR   = build
TARGET   = $(OBJDIR)/rvsim

SRCS = $(SRCDIR)/main.c $(SRCDIR)/exec.c $(SRCDIR)/csr.c $(SRCDIR)/bus.c $(SRCDIR)/loader.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR)/include -c -o $@ $<

clean:
	rm -rf $(OBJDIR)

.PHONY: all clean