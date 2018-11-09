CC  = gcc
CPP = g++

########################################
CFLAGS = -fPIC -I/usr/local/include
CPPFLAGS = -fPIC -g -W -Wall -std=c++11

LINK_FLAGS = -w -g -D__STDC_CONSTANT_MACROS
LIBS =  -L../baresip -lbaresip -L../re -lre -L../rem -lrem
ifeq ($(OS),Windows_NT)
	LIBS += -lws2_32 -lwinmm -liphlpapi
endif
#########################################################

PROGRAM = server

OBJS = 

# The directories in which source files reside.  
# At least one path should be specified.  
# SRCDIRS   := .        # current directory  
SRCDIRS   :=  .
# The source file types (headers excluded).  
# At least one type should be specified.  
# The valid suffixes are among of .c, .C, .cc, .cpp, .CPP, .c++, .cp, or .cxx.  
# SRCEXTS   := .c      # C program  
# SRCEXTS   := .cpp    # C++ program  
# SRCEXTS   := .c .cpp # C/C++ program  
SRCEXTS   :=  .c

ifneq ($(SRCDIRS),)
ifneq ($(SRCEXTS),)
	SOURCES = $(foreach d,$(SRCDIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))  
	OBJS += $(foreach x,$(SRCEXTS), $(patsubst %$(x),%.o,$(filter %$(x),$(SOURCES))))  
endif
endif

DEBUGOBJS = $(patsubst %.o,%.d,$(OBJS))  

$(PROGRAM): $(OBJS)
	$(CPP) $(LINK_FLAGS) -o $@  $(OBJS) $(LIBS) 

clean:
	clear
	rm -rf $(OBJS) *.o $(PROGRAM)

#$(OBJS):%.o :%.c  先用$(OBJS)中的一项，比如foo.o: %.o : %.c  含义为:试着用%.o匹配foo.o。如果成功%就等于foo。如果不成功，  
# Make就会警告，然后。给foo.o添加依赖文件foo.c(用foo替换了%.c里的%)  
# 也可以不要下面的这个生成规则，因为下面的 include $(DEF)  就隐含了。此处为了明了，易懂。故留着  
#compile c source file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.o: %.C
	$(CC) $(CFLAGS) -c $< -o $@

#compile c++ source file
%.o: %.cpp
	$(CPP) $(CPPFLAGS) -c $< -o $@
%.o: %.cp
	$(CPP) $(CPPFLAGS) -c $< -o $@
%.o: %.CPP
	$(CPP) $(CPPFLAGS) -c $< -o $@
%.o: %.cc
	$(CPP) $(CPPFLAGS) -c $< -o $@
%.o: %.cxx
	$(CPP) $(CPPFLAGS) -c $< -o $@
%.o: %.c++
	$(CPP) $(CPPFLAGS) -c $< -o $@
