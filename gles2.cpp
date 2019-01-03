#include <fstream>
#include <cmath>
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
#include <sstream>
#ifdef _MSC_VER
#include <process.h>
#endif
#include <windows.h>
#include "GL/gl.h"
#include "GL/wglext.h"
#include "GL3/gl3.h"
#endif

#ifndef _WIN32
using std::cin;
using std::cout;
using std::endl;
#else
using std::ostringstream;
#endif
using std::ios;
using std::ifstream;
using std::string;
using std::exception;
#ifndef _MSC_VER
using std::min;
#endif

#define ROTATION_AXIS_X 0
#define ROTATION_AXIS_Y 1
#define ROTATION_AXIS_Z 2

#define GL_SHADER_CODE_FROM_FILE 1
#define GL_SHADER_CODE_FROM_STRING 0

#define EVENT_LOOP_INIT_RESULT_SUCCESS 1
#define EVENT_LOOP_INIT_RESULT_FAILURE 2

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
#endif

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

class Window
{
    public:
        typedef void(*OnCloseCallback)();
#ifdef _WIN32
        static int32_t exitCode;
#endif

        virtual ~Window();
        static Window &Initialize();
        bool SwapBuffers();
        void Terminate();
        void GetClientSize(uint32_t &width, uint32_t &height);
        void SetOnCloseCallback(OnCloseCallback onCloseCallback);
    private:
#ifndef _WIN32
        DISPMANX_DISPLAY_HANDLE_T dispmanDisplay;
        DISPMANX_UPDATE_HANDLE_T dispmanUpdate;
        DISPMANX_ELEMENT_HANDLE_T dispmanElement;
        EGLDisplay eglDisplay;
        EGLContext eglContext;
        EGLSurface eglSurface;
#ifdef TFT_OUTPUT
        DISPMANX_RESOURCE_HANDLE_T dispmanResource;
        uint32_t fbMemSize, fbLineSize;
        VC_RECT_T dispmanRect;
        uint8_t *framebuffer;
        int fbFd;
#endif
        pthread_t eventLoopThread;
#else
        static HWND hWnd;
        HANDLE eventLoopThread;
        HGLRC hRC;
        HDC hDC;
#endif
        static OnCloseCallback onCloseCallback;
        static uint32_t clientWidth, clientHeight;
        static bool eventLoop;
        bool isTerminated;

        Window();
        Window(const Window &source);
        Window &operator=(const Window &source);
        void EndEventLoop();
#ifndef _WIN32
        static void *EventLoop(void *eventLoopInitResult);
#else
        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
        static void __cdecl EventLoop(void *eventLoopInitResult);
#endif
};

#ifdef _WIN32
int32_t Window::exitCode = 0;
HWND Window::hWnd = NULL;
#endif
bool Window::eventLoop = true;
uint32_t Window::clientWidth = 0;
uint32_t Window::clientHeight = 0;
Window::OnCloseCallback Window::onCloseCallback = NULL;

Window::Window() : isTerminated(false)
{
#ifndef _WIN32
    bcm_host_init();

    EGLConfig config;
    EGLint numConfig;

    static EGL_DISPMANX_WINDOW_T nativeWindow;

    VC_RECT_T dstRect, srcRect;
#else
    PIXELFORMATDESCRIPTOR pfd;
    GLuint pixelFormat;
    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;
#endif

    uint32_t eventLoopInitResult = 0;

#ifndef _WIN32
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        throw Exception("Cannot obtain EGL display connection");
    }

    if (eglInitialize(eglDisplay, NULL, NULL) != EGL_TRUE) {
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

    if (eglChooseConfig(eglDisplay, attribList, &config, 1, &numConfig) != EGL_TRUE) {
        eglTerminate(eglDisplay);
        throw Exception("Cannot obtain EGL frame buffer configuration");
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        eglTerminate(eglDisplay);
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
        throw Exception("Cannot create EGL rendering context");
    }

    if (graphics_get_display_size(0, &clientWidth, &clientHeight) < 0) {
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        throw Exception("Cannot obtain screen resolution");
    }

    int returnCode = pthread_create(&eventLoopThread, NULL, &Window::EventLoop, (void *)&eventLoopInitResult);
    if (returnCode) {
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        throw Exception("Cannot create event loop thread");
    }
    while (!eventLoopInitResult) {
        usleep(1);
    }
    if (eventLoopInitResult != EVENT_LOOP_INIT_RESULT_SUCCESS) {
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        throw Exception("Cannot create SDL window");
    }

    dstRect.x = 0;
    dstRect.y = 0;
    dstRect.width = clientWidth;
    dstRect.height = clientHeight;

    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.width = clientWidth << 16;
    srcRect.height = clientHeight << 16;

    dispmanDisplay = vc_dispmanx_display_open(0);
    dispmanUpdate = vc_dispmanx_update_start(0);

#ifdef TFT_OUTPUT
    uint32_t image;

    struct fb_var_screeninfo vInfo;
    struct fb_fix_screeninfo fInfo;

    fbFd = open("/dev/fb1", O_RDWR);
    if (fbFd < 0) {
        eglDestroyContext(eglDisplay, eglContext);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        throw Exception("Cannot open secondary framebuffer");
    }
    if (ioctl(fbFd, FBIOGET_FSCREENINFO, &fInfo) ||
        ioctl(fbFd, FBIOGET_VSCREENINFO, &vInfo)) {
        close(fbFd);
        eglDestroyContext(eglDisplay, eglContext);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        throw Exception("Cannot access secondary framebuffer information");
    }

    dispmanResource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, vInfo.xres, vInfo.yres, &image);
    if (!dispmanResource) {
        close(fbFd);
        eglDestroyContext(eglDisplay, eglContext);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
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
        throw Exception("Cannot initialize secondary framebuffer memory mapping");
    }

    vc_dispmanx_rect_set(&dispmanRect, 0, 0, vInfo.xres, vInfo.yres);
#endif

    dispmanElement = vc_dispmanx_element_add(dispmanUpdate, dispmanDisplay, 0, &dstRect, 0, &srcRect,
        DISPMANX_PROTECTION_NONE, 0, 0, (DISPMANX_TRANSFORM_T)0);

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
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        EndEventLoop();
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

    eventLoopThread = (HANDLE)_beginthread(EventLoop, 0, (void *)&eventLoopInitResult);
    while (!eventLoopInitResult) {
        usleep(1);
    }
    if (eventLoopInitResult != EVENT_LOOP_INIT_RESULT_SUCCESS) {
        throw Exception("Cannot create OpenGL window");
    }

    hDC = GetDC(hWnd);
    if (hDC == NULL) {
        EndEventLoop();
        throw Exception("Cannot obtain device context handle");
    }

    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;

    pixelFormat = ChoosePixelFormat(hDC, &pfd);
    if (!pixelFormat) {
        ReleaseDC(hWnd, hDC);
        EndEventLoop();
        throw Exception("Cannot obtain correct pixel format configuration");
    }

    if (!SetPixelFormat(hDC, pixelFormat, &pfd)) {
        ReleaseDC(hWnd, hDC);
        EndEventLoop();
        throw Exception("Cannot set correct pixel format configuration");
    }

    hRC = wglCreateContext(hDC);
    if (hRC == NULL) {
        ReleaseDC(hWnd, hDC);
        EndEventLoop();
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
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
        EndEventLoop();
        throw Exception("Cannot attach EGL rendering context to EGL surface");
    }
#else
    if (!wglMakeCurrent(hDC, hRC)) {
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
        EndEventLoop();
        throw Exception("Cannot attach OpenGL rendering context to thread");
    }

    GLint attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 2,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    wglCreateContextAttribsARB = reinterpret_cast <PFNWGLCREATECONTEXTATTRIBSARBPROC> (wglGetProcAddress("wglCreateContextAttribsARB"));
    if (wglCreateContextAttribsARB == NULL) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
        EndEventLoop();
        throw Exception("Cannot initialize wglCreateContextAttribsARB function");
    }

    HGLRC hRC3 = wglCreateContextAttribsARB(hDC, hRC, attribs);
    if (hRC3 == NULL) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
        EndEventLoop();
        throw Exception("Cannot create OpenGL rendering context");
    }

    wglDeleteContext(hRC);

    if (!wglMakeCurrent(hDC, hRC3)) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hRC3);
        ReleaseDC(hWnd, hDC);
        EndEventLoop();
        throw Exception("Cannot attach OpenGL rendering context to thread");
    }

    hRC = hRC3;
#endif
}

Window::~Window()
{
    Terminate();
}

Window &Window::Initialize()
{
    static Window instance;
    return instance;
}

bool Window::SwapBuffers()
{
    if (isTerminated) {
        return false;
    }
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

void Window::Terminate() {
    if (!isTerminated) {
#ifndef _WIN32
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(eglDisplay, eglSurface);
#ifdef TFT_OUTPUT
        munmap(framebuffer, fbMemSize);
        vc_dispmanx_resource_delete(dispmanResource);
        close(fbFd);
#endif
        eglDestroyContext(eglDisplay, eglContext);
        vc_dispmanx_display_close(dispmanDisplay);
        eglTerminate(eglDisplay);
#else
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hRC);
        ReleaseDC(hWnd, hDC);
#endif
        EndEventLoop();
        isTerminated = true;
    }
}

void Window::EndEventLoop()
{
    eventLoop = false;

#ifndef _WIN32
    pthread_join(eventLoopThread, NULL);
#else
    WaitForSingleObject(eventLoopThread, INFINITE);
#endif
}

#ifdef _WIN32
LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if ((msg == WM_CLOSE) || ((msg == WM_KEYDOWN) && (wParam == VK_ESCAPE))) {
        if (onCloseCallback != NULL) {
            onCloseCallback();
        }
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

#ifndef _WIN32
void *Window::EventLoop(void *eventLoopInitResult)
#else
void __cdecl Window::EventLoop(void *eventLoopInitResult)
#endif
{
#ifndef _WIN32
    SDL_Event event;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        *(uint32_t *)eventLoopInitResult = EVENT_LOOP_INIT_RESULT_FAILURE;
        return NULL;
    }

    SDL_WM_SetCaption("SDL Window", "SDL Icon");

    SDL_Surface *sdlScreen = SDL_SetVideoMode(640, 480, 0, 0);
    if (sdlScreen == NULL) {
        *(uint32_t *)eventLoopInitResult = EVENT_LOOP_INIT_RESULT_FAILURE;
        SDL_Quit();
        return NULL;
    }
#else
    HINSTANCE hInstance;
    WNDCLASSEX wcex;
    MSG msg;

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
        *(uint32_t *)eventLoopInitResult = EVENT_LOOP_INIT_RESULT_FAILURE;
        _endthread();
    }

    DWORD exStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;

    RECT clientArea;
    memset(&clientArea, 0, sizeof(RECT));
    clientArea.right = (long)clientWidth;
    clientArea.bottom = (long)clientHeight;

    if(!AdjustWindowRectEx(&clientArea, style, false, exStyle)) {
        *(uint32_t *)eventLoopInitResult = EVENT_LOOP_INIT_RESULT_FAILURE;
        _endthread();
    }

    hWnd = CreateWindowEx(exStyle, "OpenGLWindow", "OpenGL Window", style, CW_USEDEFAULT, CW_USEDEFAULT,
        clientArea.right - clientArea.left, clientArea.bottom - clientArea.top, NULL, NULL, hInstance, NULL);

#ifdef FORCE_FULLSCREEN
    SetWindowLong(hWnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
    SetWindowPos(hWnd, HWND_TOP, 0, 0, clientWidth, clientHeight, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
#endif

	if (hWnd == NULL) {
        *(uint32_t *)eventLoopInitResult = EVENT_LOOP_INIT_RESULT_FAILURE;
        _endthread();
	}

    ShowWindow(hWnd, SW_SHOW);
#endif

    *(uint32_t *)eventLoopInitResult = EVENT_LOOP_INIT_RESULT_SUCCESS;

#ifndef _WIN32
    while (eventLoop) {
        if (SDL_PollEvent(&event) && (event.type == SDL_KEYDOWN) && (event.key.keysym.sym == SDLK_ESCAPE)) {
            if (onCloseCallback != NULL) {
                onCloseCallback();
            }
        }
#else
    while (true) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                exitCode = (int32_t)msg.wParam;
                break;
            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        if (!eventLoop) {
            PostQuitMessage(0);
        }
#endif
        usleep(1);
    }

#ifndef _WIN32
    SDL_Quit();
    sdlScreen = NULL;
    return NULL;
#else
    DestroyWindow(hWnd);
    UnregisterClass("OpenGLWindow", hInstance);
    _endthread();
#endif
}

void Window::GetClientSize(uint32_t &width, uint32_t &height)
{
    width = clientWidth;
    height = clientHeight;
}

void Window::SetOnCloseCallback(OnCloseCallback callback)
{
    onCloseCallback = callback;
}

#ifdef _WIN32
template <typename T>
void initGLFunction(T &func, string funcName)
{
    ostringstream oss;
    if (func == NULL) {
        if ((func = reinterpret_cast <T> (wglGetProcAddress(funcName.c_str()))) == NULL) {
            oss << "Cannot initialize " << funcName << " function";
            throw Exception(oss.str());
        }
    }
}
#endif

class ShaderProgram
{
    public:
        ShaderProgram(const char *vertexShaderSrc, const char *fragmentShaderSrc, GLenum srcType);
        ~ShaderProgram();

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
            file.open(shaderSrc);
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
            throw Exception("Cannot load shader code, unknown source type");
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

class Matrix
{
    public:
        Matrix();
        Matrix(const Matrix &source);
        Matrix(GLuint width, GLuint height);
        Matrix(GLuint width, GLuint height, GLfloat *matrixData);
        ~Matrix();

        GLfloat *GetData();

        void GetSize(GLuint &width, GLuint &height);
        void SetSize(GLuint width, GLuint height);

        Matrix operator+(const Matrix &matrix);
        Matrix operator-(const Matrix &matrix);
        Matrix operator*(const Matrix &matrix);
        Matrix &operator=(const Matrix &source);
        Matrix &operator=(const GLfloat *sourceData);

        static Matrix GeneratePerpective(GLfloat width, GLfloat height, GLfloat nearPane, GLfloat farPane);
        static Matrix GenerateTranslation(GLfloat x, GLfloat y, GLfloat z);
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

Matrix Matrix::GenerateTranslation(GLfloat x, GLfloat y, GLfloat z)
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

Matrix Matrix::GenerateRotation(GLfloat angle, GLuint axis)
{
    Matrix result(4, 4);
    GLfloat *data = result.GetData();

    data[15] = 1.0f;
    GLfloat sinAngle = (GLfloat)sin(angle * M_PI / 180.0f);
    GLfloat cosAngle = (GLfloat)cos(angle * M_PI / 180.0f);

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

bool quit = false;

#ifndef _WIN32
void signalHandler(int sigNum) {
    if (sigNum == SIGINT) {
        quit = true;
    }
}
#else
PFNGLBINDBUFFERPROC glBindBuffer = NULL;
PFNGLBUFFERDATAPROC glBufferData = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
PFNGLGENBUFFERSPROC glGenBuffers = NULL;
PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = NULL;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = NULL;
PFNGLUSEPROGRAMPROC glUseProgram = NULL;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;
#endif

void closeRequestHandler() {
    quit = true;
}

#ifndef _WIN32
int main(int argc, const char **argv)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
    Window *window = NULL;
#ifndef _WIN32
    signal(SIGINT, signalHandler);
#endif

    try {
        window = &Window::Initialize();
        window->SetOnCloseCallback(closeRequestHandler);

#ifdef _WIN32
        initGLFunction(glBindBuffer, "glBindBuffer");
        initGLFunction(glBufferData, "glBufferData");
        initGLFunction(glDisableVertexAttribArray, "glDisableVertexAttribArray");
        initGLFunction(glEnableVertexAttribArray, "glEnableVertexAttribArray");
        initGLFunction(glGenBuffers, "glGenBuffers");
        initGLFunction(glGetAttribLocation, "glGetAttribLocation");
        initGLFunction(glGetUniformLocation, "glGetUniformLocation");
        initGLFunction(glUniformMatrix4fv, "glUniformMatrix4fv");
        initGLFunction(glUseProgram, "glUseProgram");
        initGLFunction(glVertexAttribPointer, "glVertexAttribPointer");
#endif

        char vertexShaderCode[] =
            "attribute vec3 vertexPosition;                             \n"
            "attribute vec3 vertexColor;                                \n"
            "varying vec3 fragmentColor;                                \n"
            "uniform mat4 rotationMatrix;                               \n"
            "                                                           \n"
            "void main()                                                \n"
            "{                                                          \n"
            "   gl_Position = rotationMatrix * vec4(vertexPosition, 1); \n"
            "   fragmentColor = vertexColor;                            \n"
            "}                                                          \n";

        char fragmentShaderCode[] =
            "varying vec3 fragmentColor;                                \n"
            "                                                           \n"
            "void main()                                                \n"
            "{                                                          \n"
            "   gl_FragColor = vec4(fragmentColor, 1);                  \n"
            "}                                                          \n";

        ShaderProgram program(vertexShaderCode, fragmentShaderCode, GL_SHADER_CODE_FROM_STRING);
        GLuint vertPositionAttribute = glGetAttribLocation(program.GetProgram(), "vertexPosition");
        GLuint vertColorAttribute = glGetAttribLocation(program.GetProgram(), "vertexColor");
        GLuint rotMatrixUniform = glGetUniformLocation(program.GetProgram(), "rotationMatrix");

        GLfloat angle = 0.0f;

        GLfloat vertexData[] = {
           -0.65f, -0.375f, 0.0f,
           0.65f, -0.375f, 0.0f,
           0.0f, 0.75f, 0.0f
        };

        GLfloat colorData[] = {
           1.0f, 0.0f, 0.0f,
           0.0f, 1.0f, 0.0f,
           0.0f, 0.0f, 1.0f
        };

        uint32_t width, height;
        window->GetClientSize(width, height);
        glViewport(0, 0, width, height);

        GLuint vertexBuffer;
        glGenBuffers(1, &vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);

        GLuint colorBuffer;
        glGenBuffers(1, &colorBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, colorBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), colorData, GL_STATIC_DRAW);

        while (!quit) {
            Matrix rotation = Matrix::GenerateRotation(angle, ROTATION_AXIS_Z);

            glClearColor(0.15f, 0.25f, 0.35f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glUseProgram(program.GetProgram());
            glUniformMatrix4fv(rotMatrixUniform, 1, GL_FALSE, rotation.GetData());

            glBindBuffer(GL_ARRAY_BUFFER, colorBuffer);
            glVertexAttribPointer(vertColorAttribute, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);

            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            glVertexAttribPointer(vertPositionAttribute, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);

            glEnableVertexAttribArray(vertColorAttribute);
            glEnableVertexAttribArray(vertPositionAttribute);

            glDrawArrays(GL_TRIANGLES, 0, 3);

            glDisableVertexAttribArray(vertPositionAttribute);
            glDisableVertexAttribArray(vertColorAttribute);

            window->SwapBuffers();

            angle += 0.1f;
            usleep(1000);
        }

        window->Terminate();
    } catch (exception &e) {
        if (window != NULL) {
            window->Terminate();
        }
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
