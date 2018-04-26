
#PROFILE := true
#RELEASE := true
#NO_LOG := true
#MICRO_HTTP := true

ODIR := ../out
DEPDIR := ../deps

BIN  := $(ODIR)/mesh-agent
SRC  := $(wildcard *.c)
OBJ  := $(patsubst %.c,$(ODIR)/%.o,$(SRC))

LIBS := -lcurl -lpthread -ltcmalloc
STATIC_LIBS := $(DEPDIR)/jansson-2.11/src/.libs/libjansson.a $(DEPDIR)/libmicrohttpd-0.9.59/src/microhttpd/.libs/libmicrohttpd.a

CFLAGS  += -I/usr/local/include -I$(DEPDIR)/jansson-2.11/src -I$(DEPDIR)/libmicrohttpd-0.9.59/src/include
LDFLAGS += -L/usr/local/lib

ifdef PROFILE
CFLAGS += -DPROFILER
LIBS += -lprofiler
endif

ifdef RELEASE
CFLAGS += -O2
else
LDFLAGS += -rdynamic
endif

ifdef NO_LOG
CFLAGS += -DNO_LOG
endif
ifdef MICRO_HTTP
CFLAGS += -DMICRO_HTTP
endif

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