# HT-Compass - build with:  make        (needs gcc, flex, bison)
CC      ?= gcc
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter
LDLIBS  := -lm

SRC_DIR := src
BLD_DIR := build

SRCS := common json llm advisor ast astutil depend transform runner verify optimize sema analysis profiler contention topology codegen report main
OBJS := $(addprefix $(BLD_DIR)/,$(addsuffix .o,$(SRCS))) \
        $(BLD_DIR)/parser.tab.o $(BLD_DIR)/lexer.o

all: htcompiler

htcompiler: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BLD_DIR):
	mkdir -p $(BLD_DIR)

# --- generated front end ---
# With flex+bison installed the scanner/parser are generated from src/.
# Without them (or with USE_GENERATED=1) the pre-generated copies in generated/ are used.
HAVE_TOOLS := $(shell command -v bison >/dev/null 2>&1 && command -v flex >/dev/null 2>&1 && echo yes)
ifeq ($(HAVE_TOOLS),yes)
ifndef USE_GENERATED
GENERATE := 1
endif
endif

ifdef GENERATE
$(BLD_DIR)/parser.tab.c $(BLD_DIR)/parser.tab.h: $(SRC_DIR)/parser.y | $(BLD_DIR)
	bison -d -o $(BLD_DIR)/parser.tab.c $<

$(BLD_DIR)/lexer.c: $(SRC_DIR)/lexer.l $(BLD_DIR)/parser.tab.h | $(BLD_DIR)
	flex -o $@ $<
else
$(BLD_DIR)/parser.tab.c $(BLD_DIR)/parser.tab.h: generated/parser.tab.c generated/parser.tab.h | $(BLD_DIR)
	cp generated/parser.tab.c generated/parser.tab.h $(BLD_DIR)/

$(BLD_DIR)/lexer.c: generated/lexer.c $(BLD_DIR)/parser.tab.h | $(BLD_DIR)
	cp generated/lexer.c $@
endif

# refresh generated/ after editing src/lexer.l or src/parser.y (needs flex+bison)
regen: | $(BLD_DIR)
	mkdir -p generated
	bison -d -o generated/parser.tab.c $(SRC_DIR)/parser.y
	flex -o generated/lexer.c $(SRC_DIR)/lexer.l

# generated C is compiled without our strict warnings
$(BLD_DIR)/parser.tab.o: $(BLD_DIR)/parser.tab.c
	$(CC) -std=gnu11 -O2 -g -I$(SRC_DIR) -I$(BLD_DIR) -w -c $< -o $@

$(BLD_DIR)/lexer.o: $(BLD_DIR)/lexer.c
	$(CC) -std=gnu11 -O2 -g -I$(SRC_DIR) -I$(BLD_DIR) -w -c $< -o $@

HEADERS := $(wildcard $(SRC_DIR)/*.h)

$(BLD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BLD_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(BLD_DIR) -c $< -o $@

unit: $(BLD_DIR)/json.o
	$(CC) $(CFLAGS) -I$(SRC_DIR) tests/unit/json_test.c $(BLD_DIR)/json.o -o $(BLD_DIR)/json_test && $(BLD_DIR)/json_test

test: htcompiler unit
	./tests/run_tests.sh

test-frontend: htcompiler
	python3 frontend/test_server.py

# open http://127.0.0.1:8765 (needs python3; the compiler itself does not)
serve: htcompiler
	python3 frontend/server.py

clean:
	rm -rf $(BLD_DIR) htcompiler *.o

.PHONY: all unit test test-frontend serve regen clean
