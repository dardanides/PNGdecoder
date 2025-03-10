CC=gcc

OBJ_DIR=build
BIN_DIR=bin
INC_DIR=include

TARGET_LIBS=-lm -lz 
TARGET_CCFLAGS=-fPIC
TARGET_LDFLAGS=-shared
TARGET_SRCS=src/PNGdecoder.c src/libpng_utils.c
TARGET_OBJS=$(TARGET_SRCS:.c=.o)

DEMO_LIBS=-lSDL2 -lPNGdecoder
DEMO_CCFLAGS=
DEMO_LDFLAGS=-Wl,-rpath='$$ORIGIN'
DEMO_SRCS=src/demo.c
DEMO_OBJS=$(DEMO_SRCS:.c=.o)



TARGET_OBJ_DIR_D=$(OBJ_DIR)/debug
TARGET_OBJS_D=$(addprefix $(TARGET_OBJ_DIR_D)/, $(TARGET_OBJS))
TARGET_CCFLAGS_D=$(TARGET_CCFLAGS) -g -Wall -DDEBUG_MODE -I$(INC_DIR)

TARGET_D=$(BIN_DIR)/debug/libPNGdecoder.so

TARGET_OBJ_DIR_R=$(OBJ_DIR)/release
TARGET_OBJS_R=$(addprefix $(TARGET_OBJ_DIR_R)/, $(TARGET_OBJS))
TARGET_CCFLAGS_R=$(TARGET_CCFLAGS) -O2 -DNDEBUG -I$(INC_DIR)

TARGET_R=$(BIN_DIR)/release/libPNGdecoder.so



DEMO_OBJ_DIR_D=$(OBJ_DIR)/debug
DEMO_OBJS_D=$(addprefix $(DEMO_OBJ_DIR_D)/, $(DEMO_OBJS))
DEMO_CCFLAGS_D=$(DEMO_CCFLAGS) -g -Wall -DDEBUG_MODE -I$(INC_DIR)

DEMO_D=$(BIN_DIR)/debug/demo

DEMO_OBJ_DIR_R=$(OBJ_DIR)/release
DEMO_OBJS_R=$(addprefix $(DEMO_OBJ_DIR_R)/, $(DEMO_OBJS))
DEMO_CCFLAGS_R=$(DEMO_CCFLAGS) -O2 -DNDEBUG -I$(INC_DIR)

DEMO_R=$(BIN_DIR)/release/demo




all: debug release

debug: $(TARGET_D) $(DEMO_D)

release: $(TARGET_R) $(DEMO_R)

$(TARGET_OBJS_D): $(TARGET_OBJ_DIR_D)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(TARGET_CCFLAGS_D) -c $< -o $@ $(TARGET_LIBS) 

$(TARGET_D): $(TARGET_OBJS_D)
	@mkdir -p $(@D)
	$(CC) $^ $(TARGET_LDFLAGS) -o $@ $(TARGET_LIBS) 

$(TARGET_OBJS_R): $(TARGET_OBJ_DIR_R)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(TARGET_CCFLAGS_R) -c $< -o $@ $(TARGET_LIBS) 

$(TARGET_R): $(TARGET_OBJS_R)
	@mkdir -p $(@D)
	$(CC) $^ $(TARGET_LDFLAGS) -o $@ $(TARGET_LIBS) 


$(DEMO_OBJS_D): $(DEMO_OBJ_DIR_D)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(DEMO_CCFLAGS_D) -c $< -o $@ $(DEMO_LIBS) 

$(DEMO_D): $(DEMO_OBJS_D) $(TARGET_D)
	@mkdir -p $(@D)
	$(CC) $(DEMO_LDFLAGS) -L$(dir $(TARGET_D)) $< -o $@ $(DEMO_LIBS)

$(DEMO_OBJS_R): $(DEMO_OBJ_DIR_R)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(DEMO_CCFLAGS_R) -c $< -o $@ $(DEMO_LIBS) 

$(DEMO_R): $(DEMO_OBJS_R) $(TARGET_R)
	@mkdir -p $(@D)
	$(CC) $(DEMO_LDFLAGS) -L$(dir $(TARGET_R)) $< -o $@ $(DEMO_LIBS)


clean:
	rm -f -r $(OBJ_DIR)/* $(BIN_DIR)/*
