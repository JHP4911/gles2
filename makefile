FLAGS = -Wall -O3
INCLUDES = -I/opt/vc/include -I/usr/include/SDL
LIBS = -L/opt/vc/lib -lSDL -lpthread -lbcm_host -lbrcmEGL -lbrcmGLESv2

ifeq ($(TFT_OUTPUT), 1)
	FLAGS += -DTFT_OUTPUT
endif

all:
	g++ $(FLAGS) $(INCLUDES) $(LIBS) -o gles2 gles2.cpp

clean:
	rm ./gles2
