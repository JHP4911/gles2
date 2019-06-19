#include <fstream>
#include <cmath>
#include "lodepng/lodepng.h"
#ifndef _WIN32
#include <iostream>
#ifdef TFT_OUTPUT
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif
#include <SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <bcm_host.h>
#else
#include <queue>
#include <sstream>
#include <windows.h>
#include <GL/gl.h>
#include "GL/glext.h"
#include "GL/wglext.h"
#endif

#ifndef _WIN32
using std::cin;
using std::cout;
using std::endl;
#else
using std::queue;
using std::ostringstream;
#endif
using std::ios;
using std::ifstream;
using std::string;
using std::exception;
using std::vector;
#ifndef _MSC_VER
using std::min;
#endif

#define ROTATION_AXIS_X 0
#define ROTATION_AXIS_Y 1
#define ROTATION_AXIS_Z 2

#define GL_SHADER_CODE_FROM_STRING 0x0
#define GL_SHADER_CODE_FROM_FILE 0x1

#define GL_FONT_TEXT_VERTICAL_CENTER 0x1
#define GL_FONT_TEXT_HORIZONTAL_CENTER 0x2

#define WINDOW_EVENT_NO_EVENT 0
#define WINDOW_EVENT_ESC_KEY_PRESSED 1
#define WINDOW_EVENT_WINDOW_CLOSED 2
#define WINDOW_EVENT_APPLICATION_TERMINATED 3

#define NUMBER_OF_PARTICLES 16

class Exception : public exception
{
    public:
        explicit Exception(string message);
        virtual ~Exception() throw();

        virtual const char *what() const throw();
    private:
        string exceptionMessage;
};

Exception::Exception(string message) :
    exceptionMessage(message)
{
}

Exception::~Exception() throw()
{
}

const char *Exception::what() const throw()
{
    return exceptionMessage.c_str();
}

#ifdef _WIN32
void usleep(uint32_t uSec)
{
    LARGE_INTEGER ft;
    ft.QuadPart = -(10 * (int64_t)uSec);
    HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}

template <typename FunctionType>
void initGLFunction(FunctionType &func, string funcName)
{
    if (func == NULL) {
        if ((func = reinterpret_cast <FunctionType> (wglGetProcAddress(funcName.c_str()))) == NULL) {
            ostringstream oss;
            oss << "Cannot initialize " << funcName << " function";
            throw Exception(oss.str());
        }
    }
}
#endif

class Window
{
    public:
#ifdef _WIN32
        static int32_t exitCode;
#endif

        virtual ~Window();
        static Window &Initialize();
        void Close();
        bool SwapBuffers();
        void GetClientSize(uint32_t &width, uint32_t &height);
        int32_t PollEvent();
    private:
#ifndef _WIN32
        DISPMANX_DISPLAY_HANDLE_T dispmanDisplay;
        DISPMANX_ELEMENT_HANDLE_T dispmanElement;
        EGLDisplay eglDisplay;
        EGLContext eglContext;
        EGLSurface eglSurface;
        bool quit;
#ifdef TFT_OUTPUT
        DISPMANX_RESOURCE_HANDLE_T dispmanResource;
        uint32_t fbMemSize, fbLineSize;
        VC_RECT_T dispmanRect;
        uint8_t *framebuffer;
        int fbFd;
#endif
#else
        HWND hWnd;
        HINSTANCE hInstance;
        HANDLE eventLoopThread;
        HGLRC hRC;
        HDC hDC;
        static queue<int32_t> *events;
#endif
        uint32_t clientWidth, clientHeight;

        Window();
        Window(const Window &source);
        Window &operator=(const Window &source);
#ifdef _WIN32
        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#endif
};

#ifdef _WIN32
int32_t Window::exitCode = 0;
queue<int32_t> *Window::events = NULL;
#endif

Window::Window()
{
#ifndef _WIN32
    bcm_host_init();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw Exception("Cannot create SDL window");
    }

    SDL_WM_SetCaption("SDL Window", "SDL Icon");

    SDL_Surface *sdlScreen = SDL_SetVideoMode(640, 480, 0, 0);
    if (sdlScreen == NULL) {
        SDL_Quit();
        throw Exception("Cannot create SDL window");
    }

    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        SDL_Quit();
        throw Exception("Cannot obtain EGL display connection");
    }

    if (eglInitialize(eglDisplay, NULL, NULL) != EGL_TRUE) {
        SDL_Quit();
        throw Exception("Cannot initialize EGL display connection");
    }

    static const EGLint attribList[] =
    {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfig;
    if (eglChooseConfig(eglDisplay, attribList, &config, 1, &numConfig) != EGL_TRUE) {
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot obtain EGL frame buffer configuration");
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot set rendering API");
	}

    static const EGLint contextAttrib[] =
    {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, contextAttrib);
    if (eglContext == EGL_NO_CONTEXT) {
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot create EGL rendering context");
    }

    if (graphics_get_display_size(0, &clientWidth, &clientHeight) < 0) {
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot obtain screen resolution");
    }

    VC_RECT_T dstRect, srcRect;

    dstRect.x = 0;
    dstRect.y = 0;
    dstRect.width = clientWidth;
    dstRect.height = clientHeight;

    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.width = clientWidth << 16;
    srcRect.height = clientHeight << 16;

    dispmanDisplay = vc_dispmanx_display_open(0);
    DISPMANX_UPDATE_HANDLE_T dispmanUpdate = vc_dispmanx_update_start(0);

#ifdef TFT_OUTPUT
    uint32_t image;

    struct fb_var_screeninfo vInfo;
    struct fb_fix_screeninfo fInfo;

    fbFd = open("/dev/fb1", O_RDWR);
    if (fbFd < 0) {
        eglDestroyContext(eglDisplay, eglContext);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot open secondary framebuffer");
    }
    if (ioctl(fbFd, FBIOGET_FSCREENINFO, &fInfo) ||
        ioctl(fbFd, FBIOGET_VSCREENINFO, &vInfo)) {
        close(fbFd);
        eglDestroyContext(eglDisplay, eglContext);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot access secondary framebuffer information");
    }

    dispmanResource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, vInfo.xres, vInfo.yres, &image);
    if (!dispmanResource) {
        close(fbFd);
        eglDestroyContext(eglDisplay, eglContext);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot initialize secondary display");
    }

    fbMemSize = fInfo.smem_len;
    fbLineSize = vInfo.xres * vInfo.bits_per_pixel >> 3;

    framebuffer = (uint8_t *)mmap(0, fbMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd, 0);
    if (framebuffer == MAP_FAILED) {
        vc_dispmanx_resource_delete(dispmanResource);
        close(fbFd);
        eglDestroyContext(eglDisplay, eglContext);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot initialize secondary framebuffer memory mapping");
    }

    vc_dispmanx_rect_set(&dispmanRect, 0, 0, vInfo.xres, vInfo.yres);
#endif

    dispmanElement = vc_dispmanx_element_add(dispmanUpdate, dispmanDisplay, 0, &dstRect, 0, &srcRect,
        DISPMANX_PROTECTION_NONE, 0, 0, (DISPMANX_TRANSFORM_T)0);

    static EGL_DISPMANX_WINDOW_T nativeWindow;
    nativeWindow.element = dispmanElement;
    nativeWindow.width = clientWidth;
    nativeWindow.height = clientHeight;
    vc_dispmanx_update_submit_sync(dispmanUpdate);

    eglSurface = eglCreateWindowSurface(eglDisplay, config, &nativeWindow, NULL);
    if (eglSurface == EGL_NO_SURFACE) {
#ifdef TFT_OUTPUT
        munmap(framebuffer, fbMemSize);
        vc_dispmanx_resource_delete(dispmanResource);
        close(fbFd);
#endif
        eglDestroyContext(eglDisplay, eglContext);
        dispmanUpdate = vc_dispmanx_update_start(0);
        vc_dispmanx_element_remove(dispmanUpdate, dispmanElement);
        vc_dispmanx_update_submit_sync(dispmanUpdate);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot create new EGL window surface");
    }
#else
#ifndef FORCE_FULLSCREEN
    clientWidth = 640;
    clientHeight = 480;
#else
    clientWidth = GetSystemMetrics(SM_CXSCREEN);
    clientHeight = GetSystemMetrics(SM_CYSCREEN);
#endif

    WNDCLASSEX wcex;
    hInstance = GetModuleHandle(NULL);
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_OWNDC;
    wcex.lpfnWndProc = Window::WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = "OpenGLWindow";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);;

    if (!RegisterClassEx(&wcex)) {
        throw Exception("Cannot create OpenGL window");
    }

    DWORD exStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;

    RECT clientArea;
    memset(&clientArea, 0, sizeof(RECT));
    clientArea.right = (long)clientWidth;
    clientArea.bottom = (long)clientHeight;

    if(!AdjustWindowRectEx(&clientArea, style, false, exStyle)) {
        UnregisterClass("OpenGLWindow", hInstance);
        throw Exception("Cannot create OpenGL window");
    }

    hWnd = CreateWindowEx(exStyle, "OpenGLWindow", "OpenGL Window", style, CW_USEDEFAULT, CW_USEDEFAULT,
        clientArea.right - clientArea.left, clientArea.bottom - clientArea.top, NULL, NULL, hInstance, NULL);

#ifdef FORCE_FULLSCREEN
    SetWindowLong(hWnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
    SetWindowPos(hWnd, HWND_TOP, 0, 0, clientWidth, clientHeight, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
#endif

	if (hWnd == NULL) {
        UnregisterClass("OpenGLWindow", hInstance);
        throw Exception("Cannot create OpenGL window");
	}

    ShowWindow(hWnd, SW_SHOW);

    hDC = GetDC(hWnd);
    if (hDC == NULL) {
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw Exception("Cannot obtain device context handle");
    }

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;

    GLuint pixelFormat = ChoosePixelFormat(hDC, &pfd);
    if (!pixelFormat) {
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw Exception("Cannot obtain correct pixel format configuration");
    }

    if (!SetPixelFormat(hDC, pixelFormat, &pfd)) {
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw Exception("Cannot set correct pixel format configuration");
    }

    hRC = wglCreateContext(hDC);
    if (hRC == NULL) {
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw Exception("Cannot create OpenGL rendering context");
    }
#endif

#ifndef _WIN32
    if (eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) != EGL_TRUE) {
        eglDestroySurface(eglDisplay, eglSurface);
#ifdef TFT_OUTPUT
        munmap(framebuffer, fbMemSize);
        vc_dispmanx_resource_delete(dispmanResource);
        close(fbFd);
#endif
        eglDestroyContext(eglDisplay, eglContext);
        dispmanUpdate = vc_dispmanx_update_start(0);
        vc_dispmanx_element_remove(dispmanUpdate, dispmanElement);
        vc_dispmanx_update_submit_sync(dispmanUpdate);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        SDL_Quit();
        throw Exception("Cannot attach EGL rendering context to EGL surface");
    }

    quit = false;
#else
    if (!wglMakeCurrent(hDC, hRC)) {
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw Exception("Cannot attach OpenGL rendering context to thread");
    }

    GLint attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 2,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;
    try {
        initGLFunction(wglCreateContextAttribsARB, "wglCreateContextAttribsARB");
    } catch (Exception e) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw e;
    }

    HGLRC hRC2 = wglCreateContextAttribsARB(hDC, hRC, attribs);
    if (hRC2 == NULL) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw Exception("Cannot create OpenGL rendering context");
    }

    wglDeleteContext(hRC);

    if (!wglMakeCurrent(hDC, hRC2)) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hRC2);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        UnregisterClass("OpenGLWindow", hInstance);
        throw Exception("Cannot attach OpenGL rendering context to thread");
    }

    hRC = hRC2;
    events = new queue<int32_t>();
#endif
}

Window::~Window()
{
#ifndef _WIN32
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(eglDisplay, eglSurface);
#ifdef TFT_OUTPUT
    munmap(framebuffer, fbMemSize);
    vc_dispmanx_resource_delete(dispmanResource);
    close(fbFd);
#endif
    eglDestroyContext(eglDisplay, eglContext);
    DISPMANX_UPDATE_HANDLE_T dispmanUpdate = vc_dispmanx_update_start(0);
    vc_dispmanx_element_remove(dispmanUpdate, dispmanElement);
    vc_dispmanx_update_submit_sync(dispmanUpdate);
    vc_dispmanx_display_close(dispmanDisplay);
    eglTerminate(eglDisplay);
    SDL_Quit();
#else
    delete events;
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hRC);
    ReleaseDC(hWnd, hDC);
    DestroyWindow(hWnd);
    UnregisterClass("OpenGLWindow", hInstance);
#endif
}

Window &Window::Initialize()
{
    static Window instance;
    return instance;
}

void Window::Close() {
#ifndef _WIN32
    quit = true;
#else
    PostQuitMessage(0);
#endif
}

bool Window::SwapBuffers()
{
#ifndef _WIN32
    bool swapResult = eglSwapBuffers(eglDisplay, eglSurface) == EGL_TRUE;
#ifdef TFT_OUTPUT
    vc_dispmanx_snapshot(dispmanDisplay, dispmanResource, (DISPMANX_TRANSFORM_T)0);
    vc_dispmanx_resource_read_data(dispmanResource, &dispmanRect, framebuffer, fbLineSize);
#endif
    return swapResult;
#else
    return ::SwapBuffers(hDC);
#endif
}

int32_t Window::PollEvent()
{
#ifndef _WIN32
    SDL_Event event;
    if (SDL_PollEvent(&event)) {
        if ((event.type == SDL_KEYDOWN) && (event.key.keysym.sym == SDLK_ESCAPE)) {
            return WINDOW_EVENT_ESC_KEY_PRESSED;
        }
    } else if (quit) {
        return WINDOW_EVENT_APPLICATION_TERMINATED;
#else
    MSG msg;
    if (!events->empty()) {
        int32_t event = events->back();
        events->pop();
        return event;
    } else if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            exitCode = (int32_t)msg.wParam;
            return WINDOW_EVENT_APPLICATION_TERMINATED;
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
#endif
    }
    return WINDOW_EVENT_NO_EVENT;
}


#ifdef _WIN32
LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CLOSE) {
        events->push(WINDOW_EVENT_WINDOW_CLOSED);
        return 0;
    } else if ((msg == WM_KEYDOWN) && (wParam == VK_ESCAPE)) {
        events->push(WINDOW_EVENT_ESC_KEY_PRESSED);
        return 0;
    } else if ((msg == WM_SETCURSOR) && (LOWORD(lParam) == HTCLIENT)) {
        SetCursor(NULL);
        return 0;
    } else if (msg == WM_DESTROY) {
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
#endif

void Window::GetClientSize(uint32_t &width, uint32_t &height)
{
    width = clientWidth;
    height = clientHeight;
}

class ShaderProgram
{
    public:
        ShaderProgram(const char *vertexShaderSrc, const char *fragmentShaderSrc, GLenum srcType);
        virtual ~ShaderProgram();

        GLuint GetProgram();
    private:
        GLuint vertexShader;
        GLuint fragmentShader;
        GLuint program;
#ifdef _WIN32
        static PFNGLATTACHSHADERPROC glAttachShader;
        static PFNGLCOMPILESHADERPROC glCompileShader;
        static PFNGLCREATEPROGRAMPROC glCreateProgram;
        static PFNGLCREATESHADERPROC glCreateShader;
        static PFNGLDELETEPROGRAMPROC glDeleteProgram;
        static PFNGLDELETESHADERPROC glDeleteShader;
        static PFNGLGETPROGRAMIVPROC glGetProgramiv;
        static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
        static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
        static PFNGLGETSHADERIVPROC glGetShaderiv;
        static PFNGLLINKPROGRAMPROC glLinkProgram;
        static PFNGLSHADERSOURCEPROC glShaderSource;
#endif

        ShaderProgram(const ShaderProgram &source);
        ShaderProgram &operator=(const ShaderProgram &source);
        GLuint LoadShader(const char *shaderSrc, GLenum srcType, GLenum shaderType);
};

#ifdef _WIN32
PFNGLATTACHSHADERPROC ShaderProgram::glAttachShader = NULL;
PFNGLCOMPILESHADERPROC ShaderProgram::glCompileShader = NULL;
PFNGLCREATEPROGRAMPROC ShaderProgram::glCreateProgram = NULL;
PFNGLCREATESHADERPROC ShaderProgram::glCreateShader = NULL;
PFNGLDELETEPROGRAMPROC ShaderProgram::glDeleteProgram = NULL;
PFNGLDELETESHADERPROC ShaderProgram::glDeleteShader = NULL;
PFNGLGETPROGRAMIVPROC ShaderProgram::glGetProgramiv = NULL;
PFNGLGETSHADERINFOLOGPROC ShaderProgram::glGetShaderInfoLog = NULL;
PFNGLGETPROGRAMINFOLOGPROC ShaderProgram::glGetProgramInfoLog = NULL;
PFNGLGETSHADERIVPROC ShaderProgram::glGetShaderiv = NULL;
PFNGLLINKPROGRAMPROC ShaderProgram::glLinkProgram = NULL;
PFNGLSHADERSOURCEPROC ShaderProgram::glShaderSource = NULL;
#endif

ShaderProgram::ShaderProgram(const char *vertexShaderSrc, const char *fragmentShaderSrc, GLenum srcType)
{
    GLint isLinked;

#ifdef _WIN32
    initGLFunction(glAttachShader, "glAttachShader");
    initGLFunction(glCreateProgram, "glCreateProgram");
    initGLFunction(glDeleteProgram, "glDeleteProgram");
    initGLFunction(glDeleteShader, "glDeleteShader");
    initGLFunction(glGetProgramiv, "glGetProgramiv");
    initGLFunction(glGetProgramInfoLog, "glGetProgramInfoLog");
    initGLFunction(glLinkProgram, "glLinkProgram");
#endif

    vertexShader = LoadShader(vertexShaderSrc, srcType, GL_VERTEX_SHADER);
    if (vertexShader == 0) {
        throw Exception("Cannot load vertex shader");
    }
    fragmentShader = LoadShader(fragmentShaderSrc, srcType, GL_FRAGMENT_SHADER);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        throw Exception("Cannot load fragment shader");
    }
    program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(fragmentShader);
        glDeleteShader(vertexShader);
        throw Exception("Cannot create shader program");
    }
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
    if (!isLinked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char *infoLog = new char[infoLen];
            glGetProgramInfoLog(program, infoLen, NULL, infoLog);
#ifndef _WIN32
            cout << "Shader linker error:" << endl << infoLog << endl;
#else
#ifndef FORCE_FULLSCREEN
            MessageBox(NULL, infoLog, "Shader linker error", MB_ICONEXCLAMATION);
#endif
#endif
            delete[] infoLog;
        }
        glDeleteProgram(program);
        glDeleteShader(fragmentShader);
        glDeleteShader(vertexShader);
        throw Exception("Error while linking shader");
    }
}

ShaderProgram::~ShaderProgram()
{
    glDeleteProgram(program);
    glDeleteShader(fragmentShader);
    glDeleteShader(vertexShader);
}

GLuint ShaderProgram::GetProgram()
{
    return program;
}

GLuint ShaderProgram::LoadShader(const char *shaderSrc, GLenum srcType, GLenum shaderType)
{
    GLuint shader;
    GLint isCompiled, length;
    GLchar *code;

    ifstream file;
    switch (srcType) {
        case GL_SHADER_CODE_FROM_FILE:
            file.open(shaderSrc, ifstream::binary);
            if (!file.is_open()) {
                return 0;
            }
            file.seekg(0, ios::end);
            length = (GLint)file.tellg();
            file.seekg(0, ios::beg);
            code = new GLchar[length];
            file.read(code, length);
            file.close();
            break;
        case GL_SHADER_CODE_FROM_STRING:
            code = (GLchar *)shaderSrc;
            length = (GLint)strlen(code);
            break;
        default:
            return 0;
    }

#ifdef _WIN32
    initGLFunction(glCreateShader, "glCreateShader");
    initGLFunction(glCompileShader, "glCompileShader");
    initGLFunction(glGetShaderInfoLog, "glGetShaderInfoLog");
    initGLFunction(glGetShaderiv, "glGetShaderiv");
    initGLFunction(glShaderSource, "glShaderSource");
#endif

    shader = glCreateShader(shaderType);
    if (shader == 0)
        return 0;
    glShaderSource(shader, 1, (const GLchar **)&code, &length);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (!isCompiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char *infoLog = new char[infoLen];
            glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
#ifndef _WIN32
            cout << "Shader compilation error:" << endl << infoLog << endl;
#else
#ifndef FORCE_FULLSCREEN
            MessageBox(NULL, infoLog, "Shader compilation error", MB_ICONEXCLAMATION);
#endif
#endif
            delete [] infoLog;
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

class Texture
{
    public:
        Texture(const char *textureSrc);
        Texture(GLuint width, GLuint height, GLchar *data);
        virtual ~Texture();

        GLuint GetTexture();
        GLuint GetWidth();
        GLuint GetHeight();
    private:
        GLuint texture;
        GLuint width;
        GLuint height;

        Texture(const Texture &source);
        Texture &operator=(const Texture &source);
};

Texture::Texture(const char *textureSrc)
{
    vector<uint8_t> data;
    GLuint error = lodepng::decode(data, width, height, textureSrc);
    if (error) {
        throw Exception("Cannot load texture");
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &(data[0]));
}

Texture::Texture(GLuint width, GLuint height, GLchar *data) :
    width(width), height(height)
{
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

Texture::~Texture()
{
    glDeleteTextures(1, &texture);
}

GLuint Texture::GetTexture() {
    return texture;
}

GLuint Texture::GetWidth() {
    return width;
}

GLuint Texture::GetHeight() {
    return height;
}

class Matrix
{
    public:
        Matrix();
        Matrix(const Matrix &source);
        Matrix(GLuint width, GLuint height);
        Matrix(GLuint width, GLuint height, GLfloat *matrixData);
        virtual ~Matrix();

        GLfloat *GetData();

        void GetSize(GLuint &width, GLuint &height);
        void SetSize(GLuint width, GLuint height);

        Matrix operator+(const Matrix &matrix);
        Matrix operator-(const Matrix &matrix);
        Matrix operator*(const Matrix &matrix);
        Matrix &operator=(const Matrix &source);
        Matrix &operator=(const GLfloat *sourceData);

        static Matrix GeneratePerpective(GLfloat width, GLfloat height, GLfloat nearPane, GLfloat farPane);
        static Matrix GeneratePosition(GLfloat x, GLfloat y, GLfloat z);
        static Matrix GenerateScale(GLfloat x, GLfloat y, GLfloat z);
        static Matrix GenerateRotation(GLfloat angle, GLuint axis);
    private:
        GLfloat *data;
        GLuint width;
        GLuint height;
};

Matrix::Matrix() :
    width(4), height(4)
{
    data = new GLfloat[4 * 4];
    memset(data, 0, sizeof(GLfloat) * 4 * 4);
}

Matrix::Matrix(const Matrix &source) :
    width(source.width), height(source.height)
{
    data = new GLfloat[width * height];
    memcpy(data, source.data, sizeof(GLfloat) * width * height);
}

Matrix::Matrix(GLuint width, GLuint height) :
    width(width), height(height)
{
    if ((width < 1) || (height < 1)) {
        throw Exception("Cannot create matrix - dimensions must be greater than 0");
    }
    data = new GLfloat[width * height];
    memset(data, 0, sizeof(GLfloat) * width * height);
}

Matrix::Matrix(GLuint width, GLuint height, GLfloat *matrixData) :
    width(width), height(height)
{
    if ((width < 1) || (height < 1)) {
        throw Exception("Cannot create matrix - dimensions must be greater than 0");
    }
    data = new GLfloat[width * height];
    memcpy(data, matrixData, sizeof(GLfloat) * width * height);
}

Matrix::~Matrix()
{
    delete [] data;
}

Matrix Matrix::GeneratePerpective(GLfloat width, GLfloat height, GLfloat nearPane, GLfloat farPane)
{
    Matrix result(4, 4);
    GLfloat *data = result.GetData();

    data[0] = 2.0f * nearPane / width;
    data[5] = 2.0f * nearPane / height;
    data[10] = -(farPane + nearPane) / (farPane - nearPane);
    data[11] = -1.0f;
    data[14] = -2.0f * farPane * nearPane / (farPane - nearPane);

    return result;
}

Matrix Matrix::GeneratePosition(GLfloat x, GLfloat y, GLfloat z)
{
    Matrix result(4, 4);
    GLfloat *data = result.GetData();

    for (GLuint i = 0; i < 4; i++) {
        data[i + i * 4] = 1.0f;
    }
    data[12] = x;
    data[13] = y;
    data[14] = z;

    return result;
}

Matrix Matrix::GenerateScale(GLfloat x, GLfloat y, GLfloat z)
{
    Matrix result(4, 4);
    GLfloat *data = result.GetData();

    data[0] = x;
    data[5] = y;
    data[10] = z;
    data[15] = 1.0f;

    return result;
}

Matrix Matrix::GenerateRotation(GLfloat angle, GLuint axis)
{
    Matrix result(4, 4);
    GLfloat *data = result.GetData();

    data[15] = 1.0f;
    GLfloat sinAngle = (GLfloat)sin(angle);
    GLfloat cosAngle = (GLfloat)cos(angle);

    if (axis == ROTATION_AXIS_X) {
        data[0] = 1.0f;
        data[5] = cosAngle;
        data[6] = sinAngle;
        data[9] = -sinAngle;
        data[10] = cosAngle;
    }
    if (axis == ROTATION_AXIS_Y) {
        data[0] = cosAngle;
        data[2] = sinAngle;
        data[5] = 1.0f;
        data[8] = -sinAngle;
        data[10] = cosAngle;
    }
    if (axis == ROTATION_AXIS_Z) {
        data[0] = cosAngle;
        data[1] = sinAngle;
        data[4] = -sinAngle;
        data[5] = cosAngle;
        data[10] = 1.0f;
    }

    return result;
}

GLfloat *Matrix::GetData()
{
    return data;
}

Matrix Matrix::operator+(const Matrix &matrix)
{
    if ((width != matrix.width) || (height != matrix.height)) {
        throw Exception("Cannot add matrices - incompatible matrix dimensions");
    }
    Matrix result(width, height);
    for (GLuint i = 0; i < width * height; i++) {
        result.data[i] = data[i] + matrix.data[i];
    }
    return result;
}

Matrix Matrix::operator-(const Matrix &matrix)
{
    if ((width != matrix.width) || (height != matrix.height)) {
        throw Exception("Cannot subtract matrices - incompatible matrix dimensions");
    }
    Matrix result(width, height);
    for (GLuint i = 0; i < width * height; i++) {
        result.data[i] = data[i] - matrix.data[i];
    }
    return result;
}

Matrix Matrix::operator*(const Matrix &matrix)
{
    if (width != matrix.height) {
        throw Exception("Cannot multiply matrices - incompatible matrix dimensions");
    }
    Matrix result(matrix.width, height);
    for (GLuint j = 0; j < result.height; j++) {
        for (GLuint i = 0; i < result.width; i++) {
            GLfloat m = 0.0f;
            for (GLuint k = 0; k < width; k++) {
                m += data[j + k * height] * matrix.data[k + i * matrix.height];
            }
            result.data[j + i * result.height] = m;
        }
    }
    return result;
}

Matrix &Matrix::operator=(const Matrix &source)
{
    if ((width != source.width) || (height != source.height)) {
        delete [] data;
        width = source.width;
        height = source.height;
        data = new GLfloat[this->width * this->height];
    }
    memcpy(data, source.data, sizeof(GLfloat) * width * height);
    return *this;
}

Matrix &Matrix::operator=(const GLfloat *sourceData)
{
    memcpy(data, sourceData, sizeof(GLfloat) * width * height);
    return *this;
}

void Matrix::GetSize(GLuint &width, GLuint &height)
{
    width = this->width;
    height = this->height;
}

void Matrix::SetSize(GLuint width, GLuint height)
{
    if ((width < 1) || (height < 1)) {
        throw Exception("Cannot resize matrix - dimensions must be greater than 0");
    }
    if ((this->width == width) && (this->height == height)) {
        return;
    }
    GLfloat *oldData = data;
    data = new GLfloat[width * height];
    memset(data, 0, sizeof(GLfloat) * width * height);
    for (GLuint i = 0; i < min(this->width, width); i++) {
        memcpy(&data[i * height], &oldData[i * this->height], sizeof(GLfloat) * min(this->height, height));
    }
    this->width = width;
    this->height = height;
    delete [] oldData;
}

struct CharAdvance
{
    uint16_t character;
    GLfloat advance;
};

struct CharOffset
{
    GLfloat left, top;
};

struct CharSize
{
    GLfloat width, height;
};

struct TextureRect
{
    GLfloat left, top, width, height;
};

class FontChar
{
    public:
        FontChar(string code, GLfloat width, CharOffset offset, TextureRect rect, CharSize size);
        FontChar(const FontChar &source);
        FontChar &operator=(const FontChar &source);

        string GetCode();
        GLfloat GetWidth();
        CharOffset GetOffset();
        TextureRect GetRect();
        CharSize GetSize();
        void AddAdvance(CharAdvance advance);
        GLfloat GetAdvance(uint16_t character);
    private:
        string code;
        GLfloat width;
        CharOffset offset;
        TextureRect textureRect;
        CharSize size;
        vector<CharAdvance> advances;
};

FontChar::FontChar(string code, GLfloat width, CharOffset offset, TextureRect rect, CharSize size) :
    code(code), width(width), offset(offset), textureRect(rect), size(size)
{
}

FontChar::FontChar(const FontChar &source) :
    code(source.code), width(source.width), offset(source.offset), textureRect(source.textureRect), size(source.size), advances(source.advances)
{
}

FontChar &FontChar::operator=(const FontChar &source)
{
    code = source.code;
    width = source.width;
    offset = source.offset;
    textureRect = source.textureRect;
    size = source.size;
    advances = source.advances;
    return *this;
}

string FontChar::GetCode()
{
    return code;
}

GLfloat FontChar::GetWidth()
{
    return width;
}

CharOffset FontChar::GetOffset()
{
    return offset;
}

TextureRect FontChar::GetRect()
{
    return textureRect;
}

CharSize FontChar::GetSize()
{
    return size;
}

GLfloat FontChar::GetAdvance(uint16_t character)
{
    for (uint32_t i = 0; i < advances.size(); i++) {
        if (advances[i].character == character) {
            return advances[i].advance;
        }
    }
    return 0;
}

void FontChar::AddAdvance(CharAdvance advance) {
    advances.push_back(advance);
}

class Font
{
    public:
        Font(const char *fontSrc, Texture &texture, ShaderProgram &shader);
        virtual ~Font();

        void RenderText(string text, GLfloat left, GLfloat top, GLfloat height, GLfloat screenRatio, GLuint hookType);
    private:
        string name;
        Texture *texture;
        ShaderProgram *shader;
        GLuint vertexBuffer, textureBuffer, positionAttribute, textureAttribute, positionUniform, textureUniform, opacityUniform;
        vector<FontChar> font;
#ifdef _WIN32
        static PFNGLGENBUFFERSPROC glGenBuffers;
        static PFNGLDELETEBUFFERSPROC glDeleteBuffers;
        static PFNGLBINDBUFFERPROC glBindBuffer;
        static PFNGLBUFFERDATAPROC glBufferData;
        static PFNGLUSEPROGRAMPROC glUseProgram;
        static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
        static PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
        static PFNGLUNIFORM1IPROC glUniform1i;
        static PFNGLUNIFORM1FPROC glUniform1f;
        static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
        static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
        static PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
        static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
        static PFNGLACTIVETEXTUREPROC glActiveTexture;
#endif

        Font(const Font &source);
        Font &operator=(const Font &source);
        void AddCharacter(FontChar fontChar);
        FontChar GetCharacter(string text, uint32_t offset, uint16_t &index);
};

#ifdef _WIN32
PFNGLGENBUFFERSPROC Font::glGenBuffers = NULL;
PFNGLDELETEBUFFERSPROC Font::glDeleteBuffers = NULL;
PFNGLBINDBUFFERPROC Font::glBindBuffer = NULL;
PFNGLBUFFERDATAPROC Font::glBufferData = NULL;
PFNGLUSEPROGRAMPROC Font::glUseProgram = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC Font::glEnableVertexAttribArray = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC Font::glDisableVertexAttribArray = NULL;
PFNGLUNIFORM1IPROC Font::glUniform1i = NULL;
PFNGLUNIFORM1FPROC Font::glUniform1f = NULL;
PFNGLUNIFORMMATRIX4FVPROC Font::glUniformMatrix4fv = NULL;
PFNGLVERTEXATTRIBPOINTERPROC Font::glVertexAttribPointer = NULL;
PFNGLGETATTRIBLOCATIONPROC Font::glGetAttribLocation = NULL;
PFNGLGETUNIFORMLOCATIONPROC Font::glGetUniformLocation = NULL;
PFNGLACTIVETEXTUREPROC Font::glActiveTexture = NULL;
#endif

Font::Font(const char *fontSrc, Texture &texture, ShaderProgram &shader) :
    texture(&texture), shader(&shader)
{
#ifdef _WIN32
    initGLFunction(glGenBuffers, "glGenBuffers");
    initGLFunction(glDeleteBuffers, "glDeleteBuffers");
    initGLFunction(glBindBuffer, "glBindBuffer");
    initGLFunction(glBufferData, "glBufferData");
    initGLFunction(glUseProgram, "glUseProgram");
    initGLFunction(glEnableVertexAttribArray, "glEnableVertexAttribArray");
    initGLFunction(glDisableVertexAttribArray, "glDisableVertexAttribArray");
    initGLFunction(glUniform1i, "glUniform1i");
    initGLFunction(glUniform1f, "glUniform1f");
    initGLFunction(glUniformMatrix4fv, "glUniformMatrix4fv");
    initGLFunction(glVertexAttribPointer, "glVertexAttribPointer");
    initGLFunction(glGetAttribLocation, "glGetAttribLocation");
    initGLFunction(glGetUniformLocation, "glGetUniformLocation");
    initGLFunction(glActiveTexture, "glActiveTexture");
#endif

    ifstream file;
    uint16_t buffer[256];
    file.open(fontSrc, ifstream::binary);
    if (!file.is_open()) {
        throw Exception("Cannot open font file");
    }
    file.read((char *)buffer, 4);
    if ((file.rdstate() & ifstream::eofbit) || string((char *)buffer, 4) != "FONT") {
        file.close();
        throw Exception("Cannot load font file, wrong file format");
    }
    file.read((char *)buffer, sizeof(uint8_t));
    if (file.rdstate() & ifstream::eofbit) {
        file.close();
        throw Exception("Cannot load font file, wrong file format");
    }
    uint8_t length = *((uint8_t *)buffer);
    file.read((char *)buffer, length * sizeof(uint8_t));
    if (file.rdstate() & ifstream::eofbit) {
        file.close();
        throw Exception("Cannot load font file, wrong file format");
    }
    name = string((char *)buffer, length * sizeof(uint8_t));
    file.read((char *)buffer, sizeof(uint8_t));
    if (file.rdstate() & ifstream::eofbit) {
        file.close();
        throw Exception("Cannot load font file, wrong file format");
    }
    uint8_t height = *((uint8_t *)buffer);
    file.read((char *)buffer, sizeof(uint16_t));
    if (file.rdstate() & ifstream::eofbit) {
        file.close();
        throw Exception("Cannot load font file, wrong file format");
    }
    uint16_t chars = *buffer;
    for (uint16_t i = 0; i < chars; i++) {
        file.read((char *)buffer, sizeof(uint8_t));
        if (file.rdstate() & ifstream::eofbit) {
            file.close();
            throw Exception("Cannot load font file, wrong file format");
        }
        uint8_t size = *((uint8_t *)buffer);
        file.read((char *)buffer, size * sizeof(uint8_t));
        if (file.rdstate() & ifstream::eofbit) {
            file.close();
            throw Exception("Cannot load font file, wrong file format");
        }
        string code = string((char *)buffer, size * sizeof(uint8_t));
        file.read((char *)buffer, sizeof(uint8_t));
        if (file.rdstate() & ifstream::eofbit) {
            file.close();
            throw Exception("Cannot load font file, wrong file format");
        }
        GLfloat width = *((uint8_t *)buffer) / (GLfloat)height;
        file.read((char *)buffer, 2 * sizeof(uint8_t));
        if (file.rdstate() & ifstream::eofbit) {
            file.close();
            throw Exception("Cannot load font file, wrong file format");
        }
        CharOffset offset = {
            ((int8_t *)buffer)[0] / (GLfloat)height,
            ((int8_t *)buffer)[1] / (GLfloat)height
        };
        file.read((char *)buffer, 4 * sizeof(uint16_t));
        if (file.rdstate() & ifstream::eofbit) {
            file.close();
            throw Exception("Cannot load font file, wrong file format");
        }
        TextureRect textureRect = {
            buffer[0] / (GLfloat)texture.GetWidth(),
            buffer[1] / (GLfloat)texture.GetHeight(),
            buffer[2] / (GLfloat)texture.GetWidth(),
            buffer[3] / (GLfloat)texture.GetHeight()
        };
        CharSize dimensions = {
            buffer[2] / (GLfloat)height,
            buffer[3] / (GLfloat)height
        };
        FontChar fontChar(code, width, offset, textureRect, dimensions);
        file.read((char *)buffer, sizeof(uint16_t));
        if (file.rdstate() & ifstream::eofbit) {
            file.close();
            throw Exception("Cannot load font file, wrong file format");
        }
        uint16_t advances = *buffer;
        for (uint16_t j = 0; j < advances; j++) {
            file.read((char *)buffer, sizeof(uint16_t));
            if (file.rdstate() & ifstream::eofbit) {
                file.close();
                throw Exception("Cannot load font file, wrong file format");
            }
            uint16_t character = *buffer;
            file.read((char *)buffer, sizeof(uint8_t));
            if (file.rdstate() & ifstream::eofbit) {
                file.close();
                throw Exception("Cannot load font file, wrong file format");
            }
            fontChar.AddAdvance({
                character,
                *((int8_t *)buffer) / (GLfloat)height
            });
        }
        AddCharacter(fontChar);
    }
    file.close();

    positionAttribute = glGetAttribLocation(shader.GetProgram(), "vertexPosition");
    textureAttribute = glGetAttribLocation(shader.GetProgram(), "vertexTexture");
    positionUniform = glGetUniformLocation(shader.GetProgram(), "positionMatrix");
    textureUniform = glGetUniformLocation(shader.GetProgram(), "texture");
    opacityUniform = glGetUniformLocation(shader.GetProgram(), "opacity");

    glGenBuffers(1, &vertexBuffer);

    try {
        glGenBuffers(1, &textureBuffer);
    } catch (Exception e) {
        glDeleteBuffers(1, &vertexBuffer);
        throw e;
    }
}

Font::~Font()
{
    glDeleteBuffers(1, &vertexBuffer);
    glDeleteBuffers(1, &textureBuffer);
}

void Font::AddCharacter(FontChar fontChar)
{
    uint16_t begin = 0, end = (uint16_t)font.size();
    while (begin != end) {
        uint16_t check = (begin + end) >> 1;
        if (font[check].GetCode() < fontChar.GetCode()) {
            begin = check + 1;
        } else {
            end = check;
        }
    }
    vector<FontChar>::iterator position = font.begin() + begin;
    font.insert(position, fontChar);
}

FontChar Font::GetCharacter(string text, uint32_t offset, uint16_t &index) {
    uint16_t begin = 0, end = (uint16_t)font.size();
    while (begin != end) {
        uint16_t check = (begin + end) >> 1;
        string code = font[check].GetCode();
        if (code < text.substr(offset, code.size())) {
            begin = check + 1;
        }
        else {
            end = check;
        }
    }
    if (begin >= font.size()) {
        begin = 0;
    }
    index = begin;
    return font[begin];
}

void Font::RenderText(string text, GLfloat left, GLfloat top, GLfloat height, GLfloat screenRatio, GLuint hookType)
{
    GLfloat offsetLeft = 0.0f, offsetTop = 0.0f, renderWidth = 0.0f, renderHeight = 0.0f;
    uint32_t primitives = 0;
    uint16_t lastCharIndex = 0xFFFF;
    vector<GLfloat> vertexData, textureData;
    for (uint32_t i = 0; i < text.length(); i++) {
        if (text[i] == '\n') {
            offsetLeft = 0.0f;
            offsetTop -= height;
            continue;
        }

        uint16_t charIndex;
        FontChar fontChar = GetCharacter(text, i, charIndex);
        if (lastCharIndex != 0xFFFF) {
            offsetLeft += fontChar.GetAdvance(lastCharIndex) * height;
        }

        TextureRect rect = fontChar.GetRect();
        textureData.push_back(rect.left);
        textureData.push_back(rect.top);
        textureData.push_back(rect.left + rect.width);
        textureData.push_back(rect.top);
        textureData.push_back(rect.left + rect.width);
        textureData.push_back(rect.top + rect.height);
        textureData.push_back(rect.left);
        textureData.push_back(rect.top);
        textureData.push_back(rect.left + rect.width);
        textureData.push_back(rect.top + rect.height);
        textureData.push_back(rect.left);
        textureData.push_back(rect.top + rect.height);

        CharSize size = fontChar.GetSize();
        CharOffset offset = fontChar.GetOffset();
        vertexData.push_back(offsetLeft + offset.left * height);
        vertexData.push_back(offsetTop - offset.top * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + (offset.left + size.width) * height);
        vertexData.push_back(offsetTop - offset.top * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + (offset.left + size.width) * height);
        vertexData.push_back(offsetTop - (offset.top + size.height) * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + offset.left * height);
        vertexData.push_back(offsetTop - offset.top * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + (offset.left + size.width) * height);
        vertexData.push_back(offsetTop - (offset.top + size.height) * height);
        vertexData.push_back(0.0f);
        vertexData.push_back(offsetLeft + offset.left * height);
        vertexData.push_back(offsetTop - (offset.top + size.height) * height);
        vertexData.push_back(0.0f);

        offsetLeft += fontChar.GetWidth() * height;

        if (offsetLeft > renderWidth) {
            renderWidth = offsetLeft;
        }
        if (-offsetTop > renderHeight) {
            renderHeight = -offsetTop;
        }

        i += fontChar.GetCode().length() - 1;
        lastCharIndex = charIndex;
        primitives += 2;
    }
    renderHeight += height;

    glUseProgram(shader->GetProgram());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture->GetTexture());
    glUniform1i(textureUniform, 0);
 
    Matrix position = Matrix::GeneratePosition(left - ((hookType & GL_FONT_TEXT_VERTICAL_CENTER) ? renderWidth / 2.0f : 0.0f), top + ((hookType & GL_FONT_TEXT_HORIZONTAL_CENTER) ? renderHeight / 2.0f : 0.0f), 0.0f);
    glUniformMatrix4fv(positionUniform, 1, GL_FALSE, (Matrix::GenerateScale(1.0f / screenRatio, 1.0f, 0.0f) * position).GetData());

    glUniform1f(opacityUniform, 1.0f);

    glEnableVertexAttribArray(positionAttribute);
    glEnableVertexAttribArray(textureAttribute);

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(GLfloat), &(vertexData[0]), GL_STATIC_DRAW);
    glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glBindBuffer(GL_ARRAY_BUFFER, textureBuffer);
    glBufferData(GL_ARRAY_BUFFER, textureData.size() * sizeof(GLfloat), &(textureData[0]), GL_STATIC_DRAW);
    glVertexAttribPointer(textureAttribute, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glDrawArrays(GL_TRIANGLES, 0, primitives * 3);

    glDisableVertexAttribArray(positionAttribute);
    glDisableVertexAttribArray(textureAttribute);

    glDisable(GL_BLEND);
}

struct Particle
{
    GLfloat opacity, life, lifeDelta;
    Matrix scale, position, delta;
};

class Background
{
    public:
        Background(Texture &backgroundTexture, ShaderProgram &backgroundShader, Texture &particleTexture, ShaderProgram &particleShader, GLfloat screenRatio);
        virtual ~Background();

        void Render();
        void Animate();
    private:
        Texture *backgroundTexture, *particleTexture;
        ShaderProgram *backgroundShader, *particleShader;
        GLuint vertexBuffer, textureBuffer, backgroundVertexAttribute, backgroundTextureAttribute, backgroundTextureUniform, particleVertexAttribute;
        GLuint particleTextureAttribute, particlePositionUniform, particleTextureUniform, particleOpacityUniform;
        vector<Particle> particles;
        GLfloat screenRatio;

#ifdef _WIN32
        static PFNGLGENBUFFERSPROC glGenBuffers;
        static PFNGLDELETEBUFFERSPROC glDeleteBuffers;
        static PFNGLBINDBUFFERPROC glBindBuffer;
        static PFNGLBUFFERDATAPROC glBufferData;
        static PFNGLUSEPROGRAMPROC glUseProgram;
        static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
        static PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
        static PFNGLUNIFORM1IPROC glUniform1i;
        static PFNGLUNIFORM1FPROC glUniform1f;
        static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
        static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
        static PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
        static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
        static PFNGLACTIVETEXTUREPROC glActiveTexture;
#endif

        void ResetParticle(Particle &particle, bool initial);

        Background(const Background &source);
        Background &operator=(const Background &source);
};

#ifdef _WIN32
PFNGLGENBUFFERSPROC Background::glGenBuffers = NULL;
PFNGLDELETEBUFFERSPROC Background::glDeleteBuffers = NULL;
PFNGLBINDBUFFERPROC Background::glBindBuffer = NULL;
PFNGLBUFFERDATAPROC Background::glBufferData = NULL;
PFNGLUSEPROGRAMPROC Background::glUseProgram = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC Background::glEnableVertexAttribArray = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC Background::glDisableVertexAttribArray = NULL;
PFNGLUNIFORM1IPROC Background::glUniform1i = NULL;
PFNGLUNIFORM1FPROC Background::glUniform1f = NULL;
PFNGLUNIFORMMATRIX4FVPROC Background::glUniformMatrix4fv = NULL;
PFNGLVERTEXATTRIBPOINTERPROC Background::glVertexAttribPointer = NULL;
PFNGLGETATTRIBLOCATIONPROC Background::glGetAttribLocation = NULL;
PFNGLGETUNIFORMLOCATIONPROC Background::glGetUniformLocation = NULL;
PFNGLACTIVETEXTUREPROC Background::glActiveTexture = NULL;
#endif

Background::Background(Texture &backgroundTexture, ShaderProgram &backgroundShader, Texture &particleTexture, ShaderProgram &particleShader, GLfloat screenRatio) :
    backgroundTexture(&backgroundTexture), particleTexture(&particleTexture), backgroundShader(&backgroundShader), particleShader(&particleShader), screenRatio(screenRatio)
{
#ifdef _WIN32
    initGLFunction(glGenBuffers, "glGenBuffers");
    initGLFunction(glDeleteBuffers, "glDeleteBuffers");
    initGLFunction(glBindBuffer, "glBindBuffer");
    initGLFunction(glBufferData, "glBufferData");
    initGLFunction(glUseProgram, "glUseProgram");
    initGLFunction(glEnableVertexAttribArray, "glEnableVertexAttribArray");
    initGLFunction(glDisableVertexAttribArray, "glDisableVertexAttribArray");
    initGLFunction(glUniform1i, "glUniform1i");
    initGLFunction(glUniform1f, "glUniform1f");
    initGLFunction(glUniformMatrix4fv, "glUniformMatrix4fv");
    initGLFunction(glVertexAttribPointer, "glVertexAttribPointer");
    initGLFunction(glGetAttribLocation, "glGetAttribLocation");
    initGLFunction(glGetUniformLocation, "glGetUniformLocation");
    initGLFunction(glActiveTexture, "glActiveTexture");
#endif

    backgroundVertexAttribute = glGetAttribLocation(backgroundShader.GetProgram(), "vertexPosition");
    backgroundTextureAttribute = glGetAttribLocation(backgroundShader.GetProgram(), "vertexTexture");
    backgroundTextureUniform = glGetUniformLocation(backgroundShader.GetProgram(), "texture");
    particleVertexAttribute = glGetAttribLocation(particleShader.GetProgram(), "vertexPosition");
    particleTextureAttribute = glGetAttribLocation(particleShader.GetProgram(), "vertexTexture");
    particlePositionUniform = glGetUniformLocation(particleShader.GetProgram(), "positionMatrix");
    particleTextureUniform = glGetUniformLocation(particleShader.GetProgram(), "texture");
    particleOpacityUniform = glGetUniformLocation(particleShader.GetProgram(), "opacity");

    glGenBuffers(1, &vertexBuffer);

    try {
        glGenBuffers(1, &textureBuffer);
    } catch (Exception e) {
        glDeleteBuffers(1, &vertexBuffer);
        throw e;
    }

    for (uint32_t i = 0; i < NUMBER_OF_PARTICLES; i++) {
        Particle particle;
        ResetParticle(particle, true);
        particles.push_back(particle);
    }
}

Background::~Background()
{
    glDeleteBuffers(1, &vertexBuffer);
    glDeleteBuffers(1, &textureBuffer);
}

void Background::Render()
{
    Matrix screen = Matrix::GenerateScale(1.0f / screenRatio, 1.0f, 1.0f);

    GLfloat vertexData[] = {
        -1.0f, -1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        1.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f
    };

    GLfloat textureData[] = {
        0.0f, 1.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f
    };

    glUseProgram(backgroundShader->GetProgram());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, backgroundTexture->GetTexture());
    glUniform1i(backgroundTextureUniform, 0);

    glEnableVertexAttribArray(backgroundVertexAttribute);
    glEnableVertexAttribArray(backgroundTextureAttribute);

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);
    glVertexAttribPointer(backgroundVertexAttribute, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glBindBuffer(GL_ARRAY_BUFFER, textureBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(textureData), textureData, GL_STATIC_DRAW);
    glVertexAttribPointer(backgroundTextureAttribute, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(backgroundVertexAttribute);
    glDisableVertexAttribArray(backgroundTextureAttribute);

    glUseProgram(particleShader->GetProgram());

    glBindTexture(GL_TEXTURE_2D, particleTexture->GetTexture());
    glUniform1i(particleTextureUniform, 0);

    glEnableVertexAttribArray(particleVertexAttribute);
    glEnableVertexAttribArray(particleTextureAttribute);

    for (uint32_t i = 0; i < particles.size(); i++) {
        glUniformMatrix4fv(particlePositionUniform, 1, GL_FALSE, (screen * particles[i].position * particles[i].scale).GetData());

        glUniform1f(particleOpacityUniform, particles[i].opacity * sin(particles[i].life * M_PI));

        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glVertexAttribPointer(particleVertexAttribute, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

        glBindBuffer(GL_ARRAY_BUFFER, textureBuffer);
        glVertexAttribPointer(particleTextureAttribute, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glDisableVertexAttribArray(particleVertexAttribute);
    glDisableVertexAttribArray(particleTextureAttribute);

    glDisable(GL_BLEND);
}

void Background::Animate()
{
    for (uint32_t i = 0; i < particles.size(); i++) {
        particles[i].position = particles[i].position * particles[i].delta;
        particles[i].life += particles[i].lifeDelta;
        GLfloat *position = particles[i].position.GetData();
        GLfloat *scale = particles[i].scale.GetData();
        if (position[12] < -screenRatio - scale[0]) {
            position[12] = screenRatio + scale[0];
        }
        if (position[12] > screenRatio + scale[0]) {
            position[12] = -screenRatio - scale[0];
        }
        if ((particles[i].life > 1.0f) || (position[13] < -1.0f - scale[5])) {
            ResetParticle(particles[i], false);
        }
    }
}

void Background::ResetParticle(Particle &particle, bool initial)
{
    GLfloat scale = (rand() % 40) / 100.0f + 0.4f;
    if (initial) {
        particle.scale.SetSize(4, 4);
        particle.life = (rand() % 100) / 100.0f;
    }
    particle.scale = Matrix::GenerateScale((1.0f + (rand() % 40) / 100.0f) * scale, scale, scale);
    particle.position = Matrix::GeneratePosition(((rand() % 200) / 100.0f - 1.0f) * screenRatio, initial ? (rand() % 200) / 100.0f - 1.0f : (rand() % 200) / 100.0f - 0.66f, 0.0f);
    particle.delta = Matrix::GeneratePosition((rand() % 20) / 10000.0f - 0.001f, (rand() % 10) / 10000.0f - 0.002f, 0.0f);
    particle.opacity = 0.05f + (rand() % 15) / 100.0f;
    particle.life = initial ? (rand() % 100) / 100.0f : 0.0f;
    particle.lifeDelta = (1 + rand() % 60) / 10000.0f;
}

bool quit = false;

#ifndef _WIN32
void signalHandler(int sigNum) {
    if (sigNum == SIGINT) {
        quit = true;
    }
}
#endif

#ifndef _WIN32
int main(int argc, const char **argv)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
#ifndef _WIN32
    signal(SIGINT, signalHandler);
#endif

    try {
        Window *window = &Window::Initialize();

        uint32_t width, height;
        window->GetClientSize(width, height);
        glViewport(0, 0, width, height);
        GLfloat screenRatio = width / (GLfloat)height;

        Texture fontTexture("images/euphemia.png");
        ShaderProgram fontShader("shaders/particle.vs", "shaders/particle.fs", GL_SHADER_CODE_FROM_FILE);
        Font font("fonts/euphemia.fnt", fontTexture, fontShader);

        Texture backgroundTexture("images/background.png");
        ShaderProgram backgroundShader("shaders/background.vs", "shaders/background.fs", GL_SHADER_CODE_FROM_FILE);
        Texture particleTexture("images/particle.png");
        ShaderProgram particleShader("shaders/particle.vs", "shaders/particle.fs", GL_SHADER_CODE_FROM_FILE);
        Background background(backgroundTexture, backgroundShader, particleTexture, particleShader, screenRatio);

        while (!quit) {
            switch (window->PollEvent()) {
                case WINDOW_EVENT_NO_EVENT:
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    background.Render();
                    font.RenderText(
                        "CROSS-PLATFORM OPENGL 2 DEMO",
                        0.0f,
                        0.25f,
                        0.14f,
                        screenRatio,
                        GL_FONT_TEXT_VERTICAL_CENTER | GL_FONT_TEXT_HORIZONTAL_CENTER
                    );
                    font.RenderText(
                        "This is simple cross-platform OpenGL 2 demo.\n"
                        "Graphics and texts are generated real time.\n"
                        "This works both on Windows platform and\n"
                        "Raspberry Pi (with use of native OpenGL ES 2).",
                        0.0f,
                        -0.10f,
                        0.11f,
                        screenRatio,
                        GL_FONT_TEXT_VERTICAL_CENTER | GL_FONT_TEXT_HORIZONTAL_CENTER
                    );
                    window->SwapBuffers();
                    background.Animate();
                    usleep(10000);
                    break;
                case WINDOW_EVENT_ESC_KEY_PRESSED:
                case WINDOW_EVENT_WINDOW_CLOSED:
                    window->Close();
                    break;
                case WINDOW_EVENT_APPLICATION_TERMINATED:
                    quit = true;
                    break;
            }
        }
    } catch (exception &e) {
        #ifndef _WIN32
            cout << e.what() << endl;
        #else
            MessageBox(NULL, e.what(), "Exception", MB_OK | MB_ICONERROR);
        #endif
        return 1;
    }

#ifndef _WIN32
    return 0;
#else
    return Window::exitCode;
#endif
}
