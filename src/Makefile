
#PROFILE := true
RELEASE := true
NO_LOG := true
#MICRO_HTTP := true

ODIR := ../out
#DEPDIR := ../deps

BIN  := $(ODIR)/mesh-agent
SRC  := $(wildcard *.c)
OBJ  := $(patsubst %.c,$(ODIR)/%.o,$(SRC))

LIBS := -lcurl -lpthread -ljansson
#LIBS := -lcurl -lpthread -ltcmalloc -ljansson
#STATIC_LIBS := $(DEPDIR)/jansson-2.11/src/.libs/libjansson.a $(DEPDIR)/libmicrohttpd-0.9.59/src/microhttpd/.libs/libmicrohttpd.a

#CFLAGS  += -I/usr/local/include -I$(DEPDIR)/jansson-2.11/src -I$(DEPDIR)/libmicrohttpd-0.9.59/src/include
#LDFLAGS += -L/usr/local/lib

ifdef PROFILE
CFLAGS += -DPROFILER
LIBS += -lprofiler
endif

ifdef NO_LOG
CFLAGS += -DNO_LOG
endif
ifdef MICRO_HTTP
CFLAGS += -DMICRO_HTTP
endif

ifdef RELEASE
#CFLAGS += -Ofast
CFLAGS += -O2
else
CFLAGS += -O0
#CFLAGS += -O0 -g
#LDFLAGS += -rdynamic
endif

all: $(BIN)

clean:
	$(RM) -rf $(BIN) $(ODIR)/*

$(BIN): $(OBJ)
	@echo LINK $(BIN)
#	@$(CC) $(LDFLAGS) -o $@ $^ $(STATIC_LIBS) $(LIBS)
	@$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJ): Makefile | $(ODIR)

$(ODIR):
	@mkdir -p $@

$(ODIR)/%.o : %.c
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<