.SUFFIXES:
.SUFFIXES: .c .o

CC=gcc 
CPPFLAGS=-Wall
#LDLIBS=-lhpdf
LDLIBS=
SOURCE_DIR=src
TARGET_DIR=target
EXECUTABLE=a.out

vpath %.c $(SOURCE_DIR)
vpath %.h $(SOURCE_DIR)

objects = $(addprefix $(TARGET_DIR)/, fat.o main.o filetools.o )
executable := $(addprefix $(TARGET_DIR)/, $(EXECUTABLE))

a.out : $(objects)
	$(CC) $(CPPFLAGS) -o $(executable) $(objects) $(LDLIBS)

target/%.o : %.c fat.h
	$(CC) -g -c $(CPPFLAGS) $< -o $@

# use $(RM) defined by GNU make instead of rm directly, because $(RM) does not alert: No such file or directory
.PHONY : clean
clean :
	$(RM) $(objects) $(executable)