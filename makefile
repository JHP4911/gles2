FLAGS = -Wall -O3 -fexceptions -fpermissive -fno-strict-aliasing
INCLUDES = -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux -I/usr/include/SDL
LD_BCM_LIBS_PATH = -L/opt/vc/lib
LIBS = -lbcm_host -lGLESv2 -lEGL -lSDL -lpthread

all:
	g++ $(FLAGS) $(INCLUDES) $(LD_BCM_LIBS_PATH) $(LIBS) -o gles2 gles2.cpp

clean:
	rm ./gles2
