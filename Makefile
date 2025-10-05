########################################################################
####################### Makefile Template ##############################
########################################################################

# Compiler settings - Can be customized.
CC = gcc
CFLAGS = -std=c99 -Wall -Wpedantic
LDFLAGS = -lgccjit

# Visibility for later
ifeq ($(OS), Windows_NT)
	SPECIAL_FLAGS = -O0 -g -fsanitize=undefined -fsanitize-trap=all
else
	SPECIAL_FLAGS = -O0 -g -fsanitize=undefined,address
endif


# Makefile settings - Can be customized.
ifeq ($(OS), Windows_NT)
	APPNAME = wyrt.exe
else
	APPNAME = wyrt
endif
EXT = .c
SRCDIR = ./src
OBJDIR = ./obj

############## Do not change anything from here downwards! #############
SRC = $(wildcard $(SRCDIR)/*$(EXT))
OBJ = $(SRC:$(SRCDIR)/%$(EXT)=$(OBJDIR)/%.o)
DEP = $(OBJ:$(OBJDIR)/%.o=%.d)

########################################################################
####################### Targets beginning here #########################
########################################################################

all: $(APPNAME)

release:
	$(CC) -O3 $(CFLAGS) -Werror -o $(APPNAME)_Release $(SRC) $(LDFLAGS) 

run: all
	@./$(APPNAME)

debug:
	$(CC) -O0 -g $(CFLAGS) -o $(APPNAME)_Debug $(SRC) $(LDFLAGS) 

test: test_runner release $(APPNAME)
	@./test_runner

test_runner: test_runner.c test_manifest
	$(CC) test_runner.c -o test_runner

# Builds the app
$(APPNAME): $(OBJ)
	@echo Linking...
	@$(CC) $(SPECIAL_FLAGS) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

# Creates the dependecy rules
%.d: $(SRCDIR)/%$(EXT)
	@$(CC) $(SPECIAL_FLAGS) $(CFLAGS) $< -MM -MT $(@:%.d=$(OBJDIR)/%.o) >$@

# Includes all .h files
-include $(DEP)

# Building rule for .o files and its .c/.cpp in combination with all .h
$(OBJDIR)/%.o: $(SRCDIR)/%$(EXT) | $(OBJDIR)
	@echo Compiling...
	@$(CC) $(SPECIAL_FLAGS) $(CFLAGS) -o $@ -c $<

$(OBJDIR):
	@mkdir $@
