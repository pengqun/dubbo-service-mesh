
BIN  := mesh-agent
SRC  := main.c etcd.c log.c

ODIR := obj
OBJ  := $(patsubst %.c,$(ODIR)/%.o,$(SRC))
LIBS := -ljansson -lcurl

CFLAGS  += -I/usr/local/include
LDFLAGS += -L/usr/local/lib

all: $(BIN)

clean:
	$(RM) -rf $(BIN) out/*

$(BIN): $(OBJ)
	@echo LINK $(BIN)
	@$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJ): Makefile | $(ODIR)

$(ODIR):
	@mkdir -p $@

$(ODIR)/%.o : %.c
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<