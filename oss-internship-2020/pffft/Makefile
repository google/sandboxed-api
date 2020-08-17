CXXFLAGS ?= -std=c++17

pffft_main: test_pffft.o libpffft.a
	$(CXX) -o $@ $(CXXFLAGS) $(LDFLAGS) $^

libpffft.a: pffft.o fftpack.o
	ar rcs $@ $^

pffft.c: pffft.h

fftpack.c: fftpack.h

.PHONY: clean
clean: 
	rm -f *.o *.a pffft_main