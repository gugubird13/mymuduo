CXXFLAGS = -O2 -std=c++11

testserver:
	g++ $(CXXFLAGS) -o testserver testserver.cc -lmymuduo -lpthread -luring

clean:
	rm -rf testserver