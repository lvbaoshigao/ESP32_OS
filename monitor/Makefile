# ESPOS Serial Monitor (GTK3, C++)
# Some environments ship a g++ without cc1plus; auto-fall back to clang++.
# Override with `make CXX=<compiler>`.
CXX ?= g++
CXX := $(shell printf 'int main(){}' | $(CXX) -x c++ - -o /dev/null >/dev/null 2>&1 && echo $(CXX) || echo clang++)

CXXFLAGS := -std=c++17 -O2 -Wall
GTKFLAGS := $(shell pkg-config --cflags --libs gtk+-3.0)

monitor: monitor.cpp
	$(CXX) $(CXXFLAGS) monitor.cpp -o monitor $(GTKFLAGS) -lpthread

clean:
	rm -f monitor

.PHONY: clean
