CC=g++
CFLAGS=-std=c++11
LDFLAGS=-levent
SOURCES=htable.cpp parser.cpp worker.cpp cleaner.cpp server.cpp main.cpp
TESTSOURCES=test.cpp
EXE=mycache
TESTEXE=testapp

all:
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOURCES) -o $(EXE)

test:
	$(CC) $(CFLAGS) $(TESTSOURCES) -o $(TESTEXE)

clean:
	rm -f $(EXE)
	rm -f $(TESTEXE)