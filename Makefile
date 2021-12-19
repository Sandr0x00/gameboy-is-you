export GBDKDIR=../gbdk/
CC	= ../gbdk/bin/lcc -Wa-l -Wl-m

.PHONY: all replay
all: clean main.gb
	rm -f *.lnk

%.s: %.c
	$(CC) -S -o $@ $<

%.o: %.c
	$(CC) $(RELEASE) -c -o $@ $<

%.o: %.s
	$(CC) -c -o $@ $<

# %.gb:	${_OBJ}
# 	$(CC) -o $@ $<

%.gb: %.o
	$(CC) -o $@ $<

clean:
	rm -f *.o *.lst *.map *.gb *.ihx *.sym *.cdb *.adb *.asm

run:
	../tas-emulator record replay

replay:
	python solve.py
	../tas-emulator -s 20 playback replay
