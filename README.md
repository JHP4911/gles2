# Cross-platform OpenGL 2 demo

This is simple cross-platform OpenGL 2 demo. It successfully builds both on Windows platform (Visual Studio 2017, Code::Blocks 17.12), and on Raspberry Pi (Raspbian distro).
Built program will display rotating triangle on screen. It uses the fact OpenGL 2 ES is very simmilar to OpenGL 2 and both may use nearly the same shader programs.

### Raspberry Pi instructions

To run this project on Raspberry Pi You will have to build it. SDL library is required, please install it using "apt-get" command:
```
sudo apt-get install libsdl1.2-dev

```
To build executable clone this repository, and use "make" command as show below:
```
git clone https://github.com/markondej/gles2
cd gles2
make
./gles2
```
