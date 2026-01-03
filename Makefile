TARGET := stillroom.elf

SRC := \
	src/stillroom.c \
	src/audio_engine.c \
	src/soundfx.c

CFLAGS  ?= -O2 -fno-omit-frame-pointer
LDFLAGS ?=
LDLIBS  ?= -lSDL2 -lSDL2_image -lSDL2_ttf -lm -ldl -lpthread

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -I./src -o $@ $^ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
