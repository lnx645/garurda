# ============================================================
# GarudaLang (GaLang) Compiler — Makefile
# Author: GalangTeam
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lm

SRCDIR  = src
INCDIR  = include
OBJDIR  = obj
BINDIR  = bin
TESTDIR = testdata

TARGET  = $(BINDIR)/gar

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/ast.c \
       $(SRCDIR)/lexer.c \
       $(SRCDIR)/parser.c \
       $(SRCDIR)/checker.c \
       $(SRCDIR)/codegen.c \
       $(SRCDIR)/runtime.c

OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Built $(TARGET)"

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)

run: all
	./$(TARGET) run $(TESTDIR)/hello.ga

test: all
	@echo "Running tests..."
	./$(TARGET) check $(TESTDIR)/hello.ga
