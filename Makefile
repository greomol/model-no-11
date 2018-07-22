all:	model

model:	main.cpp DSSimul.cpp DSSimul.h contextes.h
	c++ -o model main.cpp -lpthread

