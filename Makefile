CFLAGS += -Wall -Wextra -Wpedantic -Waggregate-return -Wwrite-strings -Wfloat-equal -Wstack-usage=1024 -Winline

CFLAGS += -std=c11

CFLAGS +=-D_XOPEN_SOURCE=700 -pthread

comp = $(CC) $(CFLAGS) $^ -o ./bin/$@

ashti: ashti.c threads.o
	mkdir -p ./bin
	$(comp)


debug: CFLAGS += -g
debug: ashti

profile: CFLAGS += -pg
profile: ashti

.PHONY: clean

clean:
	rm -f *.o *.out ./src/*.o ./bin/dispatcher ./bin/listener

