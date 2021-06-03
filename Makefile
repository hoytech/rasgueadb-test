example_test: example_test.cpp external/rasgueadb/*
	external/rasgueadb/rasgueadb-generate schema.yaml build
	g++ -Wall -O2 -std=c++17 example_test.cpp -llmdb -I build -I external -o example_test

.PHONY: test clean

test: example_test
	./example_test

clean:
	rm -rf db/ build/ example_test
