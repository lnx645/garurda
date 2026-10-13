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
       $(SRCDIR)/resolver.c \
       $(SRCDIR)/checker.c \
       $(SRCDIR)/codegen.c \
       $(SRCDIR)/runtime.c

OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

.PHONY: all clean run test sanitize

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Built $(TARGET)"

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(wildcard $(INCDIR)/*.h)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)
	rm -f $(TESTDIR)/hello $(TESTDIR)/ifelse $(TESTDIR)/loop \
	      $(TESTDIR)/loops $(TESTDIR)/numbers $(TESTDIR)/factorial

run: all
	./$(TARGET) run $(TESTDIR)/hello.ga

test: all
	./run_tests.sh

# Build with AddressSanitizer + UBSan and run the test suite against it,
# so the "ASAN/UBSAN bersih" claim in README stays verifiable.
sanitize:
	@mkdir -p $(BINDIR)
	$(CC) -Wall -Wextra -std=c11 -g -O1 -fsanitize=address,undefined \
		-fno-omit-frame-pointer -I$(INCDIR) $(SRCS) -o $(BINDIR)/gar_san $(LDFLAGS)
	@echo "Built $(BINDIR)/gar_san (ASan+UBSan)"
	GAR=$(BINDIR)/gar_san ./run_tests.sh

# Inspect the generated assembly for a program:  make asm FILE=testdata/loop.ga
asm: all
	GAR_KEEP_ASM=1 ./$(TARGET) build $(FILE)
	@echo "--- assembly ---"
	@cat /tmp/gar_$$.asm 2>/dev/null || ls -t /tmp/gar_*.asm | head -1 | xargs cat

# Auto-generated header dependencies (so editing a .h rebuilds its users).
-include $(DEPS)
