diffgcov: diffgcov.o
	gcc -o diffgcov diffgcov.o

diffgcov.o: diffgcov.c
	g++ -O2 -c diffgcov.c

clean:
	\rm diffgcov diffgcov.o ~*
