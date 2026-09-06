# test/Makefile — host-side tests for the motion state machine. No board required.
#
#   make test     compile and run
#   make clean

CXXFLAGS = -std=c++17 -Wall -Wextra -Istub -I..

motion_test: motion_test.cpp ../motion.cpp ../motion.h ../config.h
	$(CXX) $(CXXFLAGS) -o $@ motion_test.cpp

test: motion_test
	./motion_test

clean:
	rm -f motion_test

.PHONY: test clean
