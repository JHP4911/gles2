FLAGS = -Wall -O3 -std=c++11
INCLUDES = -I/opt/vc/include -I/usr/include/SDL
LIBS = -L/opt/vc/lib -lSDL -lbcm_host -lbrcmEGL -lbrcmGLESv2

ifeq ($(TFT_OUTPUT), 1)
	FLAGS += -DTFT_OUTPUT
endif

all:
	g++ $(FLAGS) $(INCLUDES) $(LIBS) -o gles2 gles2.cpp lodepng/lodepng.cpp

clean:
	rm ./gles2
