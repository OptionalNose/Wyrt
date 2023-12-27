########################################################################
####################### Makefile Template ##############################
########################################################################

# Compiler settings - Can be customized.
CC = clang
CFLAGS = -std=c99 -fcolor-diagnostics `llvm-config --cflags`
LDFLAGS = -lm -lc `llvm-config --ldflags --libs all`

# Visibility for later
SPECIAL_FLAGS = -O0 -g -fsanitize=undefined,address


# Makefile settings - Can be customized.
APPNAME = wyrt
EXT = .c
SRCDIR = ./src
OBJDIR = ./obj

############## Do not change anything from here downwards! #############
SRC = $(wildcard $(SRCDIR)/*$(EXT))
OBJ = $(SRC:$(SRCDIR)/%$(EXT)=$(OBJDIR)/%.o)
DEP = $(OBJ:$(OBJDIR)/%.o=%.d)


# UNIX-based OS variables & settings
RM = rm
DELOBJ = $(OBJ)
# Windows OS variables & settings
DEL = del
EXE = .exe
WDELOBJ = $(SRC:$(SRCDIR)/%$(EXT)=$(OBJDIR)\\%.o)

########################################################################
####################### Targets beginning here #########################
########################################################################

all: $(APPNAME)

release:
	$(CC) -O3 $(CFLAGS) $(LDFLAGS) -o $(APPNAME)_Release $(SRC)

run: all
	@./$(APPNAME)

debug:
	$(CC) -O0 -g $(CFLAGS) $(LDFLAGS) -o $(APPNAME)_Debug $(SRC)

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

################### Cleaning rules for Unix-based OS ###################
# Cleans complete project
.PHONY: clean
clean:
	$(RM) $(DELOBJ) $(DEP) $(APPNAME) $(APPNAME)_Release

# Cleans only all files with the extension .d
.PHONY: cleandep
cleandep:
	$(RM) $(DEP)

#################### Cleaning rules for Windows OS #####################
# Cleans complete project
.PHONY: cleanw
cleanw:
	$(DEL) $(WDELOBJ) $(DEP) $(APPNAME)$(EXE)

# Cleans only all files with the extension .d
.PHONY: cleandepw
cleandepw:
	$(DEL) $(DEP)
