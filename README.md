# Cross-platform OpenGL 2 demo

This is simple cross-platform OpenGL 2/OpenGL ES 2 demo. It works both on Windows (Visual Studio 2017, Code::Blocks 17.12), and Linux (Raspbian distro) operating systems.
Displays simple screen with animated background and text information.

### Raspberry Pi instructions

To run this project on Raspberry Pi You will have to build it. SDL library is required, install it by using "apt-get" command:
```
sudo apt-get install libsdl1.2-dev
```
To build executable, clone this repository and use "make" command as show below:
```
git clone https://github.com/markondej/gles2
cd gles2
make
./gles2
```
