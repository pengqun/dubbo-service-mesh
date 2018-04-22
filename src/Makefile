
ODIR := ../out
DEPDIR := ../deps

BIN  := $(ODIR)/mesh-agent
SRC  := main.c etcd.c log.c
OBJ  := $(patsubst %.c,$(ODIR)/%.o,$(SRC))

LIBS := -lcurl -lpthread
STATIC_LIBS := $(DEPDIR)/jansson-2.11/src/.libs/libjansson.a $(DEPDIR)/libmicrohttpd-0.9.59/src/microhttpd/.libs/libmicrohttpd.a

CFLAGS  += -I/usr/local/include -I$(DEPDIR)/jansson-2.11/src -I$(DEPDIR)/libmicrohttpd-0.9.59/src/include
LDFLAGS += -L/usr/local/lib

all: $(BIN)

clean:
	$(RM) -rf $(BIN) $(ODIR)/*

$(BIN): $(OBJ)
	@echo LINK $(BIN)
	@$(CC) $(LDFLAGS) -o $@ $^ $(STATIC_LIBS) $(LIBS)

$(OBJ): Makefile | $(ODIR)

$(ODIR):
	@mkdir -p $@

$(ODIR)/%.o : %.c
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<