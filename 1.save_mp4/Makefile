BIN=mp4_test
TOP_DIR=$(shell pwd)

INLCUDE_MP4V2=$(TOP_DIR)/include/mp4v2
INLCUDE_USR=$(TOP_DIR)/include


x86_LIBS= -L$(TOP_DIR)/lib/x86/lib -lmp4v2 -lfaac -lstdc++ -lm
arm_LIBS= -L$(TOP_DIR)/lib/arm/lib -lmp4v2 -lfaac -lstdc++ -lm
INCLUDE= -I$(INLCUDE_MP4V2) -I$(INLCUDE_USR)

all:x86

x86:
	gcc -g -o $(BIN) *.c $(INCLUDE) $(x86_LIBS) 
	
arm:
	arm-hisiv300-linux-gcc  -g  -static -o $(BIN) *.c $(INCLUDE) $(arm_LIBS) 
	
clean:

	rm -rf $(BIN) *.mp4

.PHONY:	all x86 arm clean
