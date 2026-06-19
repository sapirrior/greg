CC = gcc
CFLAGS = -Wall -Wextra -O3 -Iinclude -D_GNU_SOURCE
LDFLAGS = -lpcre2-8 -lpthread

# OS detection for Windows (MinGW) compatibility
ifeq ($(OS),Windows_NT)
    TARGET = greg.exe
    LDFLAGS = -lpcre2-8
else
    TARGET = greg
endif

SRC_DIR = src
OBJ_DIR = obj
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
