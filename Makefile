all:
	make build && rm -rf test_000 && make run

clean:
	rm 4d

build:
	g++ main.cpp -o 4d -lcurl -g

run:
	# ./4d "https://boards.4chan.org/gif/thread/28218474" test_000 10
	./4d "https://boards.4chan.org/h/thread/8526949" test_001 10

install:
	sudo rm -f /usr/bin/4d && sudo cp ~/Projects/C++/4chandownloader/4d /usr/bin/4d
