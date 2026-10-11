# test/Makefile — host-side tests for the motion state machine. No board required.
#
#   make test     compile and run
#   make clean

# Register every state: the tests cover the full machine, not the MVP subset in config.h.
CXXFLAGS = -std=c++17 -Wall -Wextra -Istub -I.. -DSTATE_FAST=1 -DSTATE_DIZZY=1 -DSTATE_HAPPY=1

motion_test: motion_test.cpp ../motion.cpp ../motion.h ../cfg.h ../config.h
	$(CXX) $(CXXFLAGS) -o $@ motion_test.cpp

test: motion_test
	./motion_test

clean:
	rm -f motion_test

.PHONY: test clean
