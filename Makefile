CFLAGS=-c -Wall -O2 -Wno-unused-result

nanocc: nanocc.o elf32.o
	gcc -o $@ $^

nanocc.o: nanocc.c nanocc-itf.h

elf32.o: elf32.c nanocc-itf.h

# naming convention:
# nanocc-rr-gg denotes a compiler that runs on rr producing code for gg

nanocc-elfx86-elfx86-2: nanocc-elfx86-elfx86
	cat nanocc.c elf32.c | ./nanocc-elfx86-elfx86 > $@
	chmod +x $@
	diff $@ nanocc-elfx86-elfx86

nanocc-elfx86-elfx86: nanocc
	cat nanocc.c elf32.c | ./nanocc > $@
	chmod +x $@

%.o: %.c
	gcc $(CFLAGS) $<

clean:
	rm -f nanocc.o elf32.o nanocc nanocc-elfx86-elfx86 nanocc-elfx86-elfx86-2

all: clean nanocc-elfx86-elfx86-2
