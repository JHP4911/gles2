FLAGS = -Wall -O3 -fexceptions -fpermissive -fno-strict-aliasing
INCLUDES = -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux -I/usr/include/SDL
BRCM_LIBS_PATH = -L/opt/vc/lib
LIBS = -lbcm_host -lSDL -lpthread

ifeq ($(TFT_OUTPUT), 1)
	FLAGS += -DTFT_OUTPUT
endif

ifneq ($(OLD_BRCM_LIB), 1)
	LIBS += -lbrcmEGL -lbrcmGLESv2
else
	LIBS += -lEGL -lGLESv2
endif

all:
	g++ $(FLAGS) $(INCLUDES) $(BRCM_LIBS_PATH) $(LIBS) -o gles2 gles2.cpp

clean:
	rm ./gles2
