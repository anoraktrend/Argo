CC = cc
CFLAGS = -Wall -Wextra -pedantic -std=c23 -O3 -ffast-math -march=native -D_POSIX_C_SOURCE=200809L
LDFLAGS =

TARGET = ccompress

.PHONY: all clean test

all: $(TARGET)

$(TARGET): compress.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) test_extract test.*.c

test: $(TARGET)
	@echo "Testing ccompress..."
	@echo "int main(void) { printf(\"hello\\n\"); return 0; }" > test_input.c
	./$(TARGET) test_input.c -o test_output.c
	$(CC) -o test_extract test_output.c
	@echo "Extracted output:"
	./test_extract
	@rm -f test_input.c test_output.c test_extract
	@echo "Test passed!"
