CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O0 -g
TARGET   = page_cache_scroll

.PHONY: all run run-fix clean

all: $(TARGET)

$(TARGET): page_cache_scroll.c
	$(CC) $(CFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

run-fix: $(TARGET)
	./$(TARGET) --fix

clean:
	rm -f $(TARGET) $(TARGET).exe
