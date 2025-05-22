CC = gcc
CFLAGS = -O3 -Wall -Wextra -flto -g -march=native
LDFLAGS = -lgmp -lcurl -pthread
SOURCES = main.c memory_forth.c executeinstruction.c compiletoken.c images.c dictionnary.c env.c utils.c irc.c interpret.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = forth_bot.h memory_forth.h
TARGET = forth

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) *.s *.o

profile:
	$(CC) $(CFLAGS) -fprofile-generate $(SOURCES) -o $(TARGET) $(LDFLAGS)
	./$(TARGET)
	$(CC) $(CFLAGS) -fprofile-use $(SOURCES) -o $(TARGET) $(LDFLAGS)

.PHONY: all clean profile
