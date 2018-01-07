# Makefile for 7 Seg Clock
# Mark Street <marksmanuk@gmail.com>

CC		= g++
RM		= rm
FLAGS	= -O3 -Wall
EXE		= 7segclk

%.o: %.c
	$(CC) $(FLAGS) -c -o $@ $<

all: $(EXE)

clean:
	$(RM) *.o $(EXE)

$(EXE): $(EXE).o
	$(CC) -o $@ $<
