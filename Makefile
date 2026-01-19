TARGET := stillroom.elf

SRC := \
	src/stillroom.c \
	src/ui/keyboard.c \
	src/update_zip.c \
	src/audio_engine.c \
	src/soundfx.c \
	src/utils/string_utils.c \
	src/utils/file_utils.c \
	src/features/routines/routines.c \
	src/features/tasks/tasks.c \
	src/features/booklets/booklets.c \
	src/features/timer/timer.c \
	src/features/meditation/meditation.c \
	src/features/focus_menu/focus_menu.c \
	src/features/quest/quest.c

CFLAGS  ?= -O2 -fno-omit-frame-pointer -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
LDFLAGS ?=
LDLIBS  ?= -lSDL2 -lSDL2_image -lSDL2_ttf -lzip -lm -ldl -lpthread

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -I./src -o $@ $^ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
