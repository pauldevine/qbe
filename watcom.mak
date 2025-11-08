# OpenWatcom makefile for QBE - DOS 16-bit real mode
# Memory model: huge (required for large data structures)

CC = wcc
CFLAGS = -zq -w4 -mh -bt=dos -DDOS -I. -fo=.obj

# Common objects
COMMOBJ = main.obj util.obj parse.obj abi.obj cfg.obj mem.obj ssa.obj &
          alias.obj load.obj copy.obj fold.obj gvn.obj gcm.obj simpl.obj &
          live.obj spill.obj rega.obj emit.obj dosgetopt.obj

# AMD64 objects
AMD64OBJ = amd64\targ.obj amd64\sysv.obj amd64\isel.obj amd64\emit.obj

# ARM64 objects
ARM64OBJ = arm64\targ.obj arm64\abi.obj arm64\isel.obj arm64\emit.obj

# RISC-V 64 objects
RV64OBJ = rv64\targ.obj rv64\abi.obj rv64\isel.obj rv64\emit.obj

# All objects
OBJ = $(COMMOBJ) $(AMD64OBJ) $(ARM64OBJ) $(RV64OBJ)

# Linker
LINK = wlink
LFLAGS = system dos option stack=32k

qbe.exe: $(OBJ)
	$(LINK) $(LFLAGS) name qbe.exe file {$(OBJ)}

.c.obj:
	$(CC) $(CFLAGS) $<

# Dependencies
main.obj: main.c all.h config.h dosgetopt.h
util.obj: util.c all.h
parse.obj: parse.c all.h ops.h
abi.obj: abi.c all.h
cfg.obj: cfg.c all.h
mem.obj: mem.c all.h
ssa.obj: ssa.c all.h
alias.obj: alias.c all.h
load.obj: load.c all.h
copy.obj: copy.c all.h
fold.obj: fold.c all.h
gvn.obj: gvn.c all.h
gcm.obj: gcm.c all.h
simpl.obj: simpl.c all.h
live.obj: live.c all.h
spill.obj: spill.c all.h
rega.obj: rega.c all.h
emit.obj: emit.c all.h
dosgetopt.obj: dosgetopt.c dosgetopt.h

clean: .SYMBOLIC
	del *.obj
	del amd64\*.obj
	del arm64\*.obj
	del rv64\*.obj
	del qbe.exe
