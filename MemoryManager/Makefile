all: libMemoryManager.a

MemoryManager.o: MemoryManager.cpp MemoryManager.h
	g++ -std=c++17 -Wall -Wextra -g -c MemoryManager.cpp

libMemoryManager.a: MemoryManager.o
	ar rcs libMemoryManager.a MemoryManager.o

clean: 
	rm -f *.o *.a