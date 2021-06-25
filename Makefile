example_test: example_test.cpp schema.yaml external/rasgueadb/*
	external/rasgueadb/rasgueadb-generate schema.yaml build
	g++ -Wall -g -O2 -fsanitize=address -std=c++2a example_test.cpp -llmdb -I build -I external -o example_test

.PHONY: test clean

test: example_test
	./example_test

clean:
	rm -rf db/ build/ example_test
