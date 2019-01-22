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
#include <queue>
#include <sstream>
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
using std::queue;
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

#define GL_SHADER_CODE_FROM_STRING 0
#define GL_SHADER_CODE_FROM_FILE 1

#define WINDOW_EVENT_NO_EVENT 0
#define WINDOW_EVENT_ESC_KEY_PRESSED 1
#define WINDOW_EVENT_WINDOW_CLOSED 2
#define WINDOW_EVENT_APPLICATION_TERMINATED 3

#define TEXTURE_SIZE 256

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
    } catch (exception e) {
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
#endif

#ifndef _WIN32
int main(int argc, const char **argv)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
    Window *window = NULL;
    uint8_t *textureData = NULL;
#ifndef _WIN32
    signal(SIGINT, signalHandler);
#endif

    try {
        window = &Window::Initialize();

#ifdef _WIN32
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
        PFNGLACTIVETEXTUREPROC glActiveTexture = NULL;
        PFNGLUNIFORM1IPROC glUniform1i = NULL;

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
        initGLFunction(glActiveTexture, "glActiveTexture");
        initGLFunction(glUniform1i, "glUniform1i");
#endif

        char vertexShaderCode[] =
            "uniform mat4 rotationMatrix;                               \n"
            "attribute vec3 vertexPosition;                             \n"
            "attribute vec3 vertexColor;                                \n"
            "attribute vec2 vertexTexCoord;                             \n"
            "varying vec3 varyingColor;                                 \n"
            "varying vec2 varyingTexCoord;                              \n"
            "                                                           \n"
            "void main()                                                \n"
            "{                                                          \n"
            "   gl_Position = rotationMatrix * vec4(vertexPosition, 1); \n"
            "   varyingColor = vertexColor;                             \n"
            "   varyingTexCoord = vertexTexCoord;                       \n"
            "}                                                          \n";

        char fragmentShaderCode[] =
#ifndef _WIN32
            "precision mediump float;                                   \n"
#endif
            "uniform sampler2D texture;                                 \n"
            "varying vec3 varyingColor;                                 \n"
            "varying vec2 varyingTexCoord;                              \n"
            "                                                           \n"
            "void main()                                                \n"
            "{                                                          \n"
            "   gl_FragColor = vec4(varyingColor, 1) *                  \n"
            "                  texture2D(texture, varyingTexCoord);     \n"
            "}                                                          \n";

        ShaderProgram program(vertexShaderCode, fragmentShaderCode, GL_SHADER_CODE_FROM_STRING);
        GLuint vertPositionAttribute = glGetAttribLocation(program.GetProgram(), "vertexPosition");
        GLuint vertColorAttribute = glGetAttribLocation(program.GetProgram(), "vertexColor");
        GLuint vertTexCoordAttribute = glGetAttribLocation(program.GetProgram(), "vertexTexCoord");
        GLuint rotMatrixUniform = glGetUniformLocation(program.GetProgram(), "rotationMatrix");
        GLuint textureUniform = glGetUniformLocation(program.GetProgram(), "texture");

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

        textureData = new uint8_t[TEXTURE_SIZE * TEXTURE_SIZE * 3];
        for (uint32_t i = 0; i < TEXTURE_SIZE * TEXTURE_SIZE * 3;) {
            uint8_t pixel = ((i / 3) % TEXTURE_SIZE) | ((i / 3) / TEXTURE_SIZE);
            textureData[i++] = pixel;
            textureData[i++] = pixel;
            textureData[i++] = pixel;
        }

        GLfloat texCoordData[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.5f, 1.0f
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
        glBufferData(GL_ARRAY_BUFFER, sizeof(colorData), colorData, GL_STATIC_DRAW);

        GLuint texCoordBuffer;
        glGenBuffers(1, &texCoordBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, texCoordBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(texCoordData), texCoordData, GL_STATIC_DRAW);

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXTURE_SIZE, TEXTURE_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, textureData);

        while (!quit) {
            Matrix rotation = Matrix::GenerateRotation(angle, ROTATION_AXIS_Z);
            switch (window->PollEvent()) {
                case WINDOW_EVENT_NO_EVENT:
                    glClearColor(0.15f, 0.25f, 0.35f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);

                    glUseProgram(program.GetProgram());
                    glUniformMatrix4fv(rotMatrixUniform, 1, GL_FALSE, rotation.GetData());

                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glUniform1i(textureUniform, 0);

                    glBindBuffer(GL_ARRAY_BUFFER, colorBuffer);
                    glVertexAttribPointer(vertColorAttribute, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

                    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
                    glVertexAttribPointer(vertPositionAttribute, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

                    glBindBuffer(GL_ARRAY_BUFFER, texCoordBuffer);
                    glVertexAttribPointer(vertTexCoordAttribute, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

                    glEnableVertexAttribArray(vertColorAttribute);
                    glEnableVertexAttribArray(vertPositionAttribute);
                    glEnableVertexAttribArray(vertTexCoordAttribute);

                    glDrawArrays(GL_TRIANGLES, 0, 3);

                    glDisableVertexAttribArray(vertColorAttribute);
                    glDisableVertexAttribArray(vertPositionAttribute);
                    glDisableVertexAttribArray(vertTexCoordAttribute);

                    window->SwapBuffers();

                    angle += 0.1f;
                    usleep(1000);
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

        delete [] textureData;
    } catch (exception &e) {
        if (textureData != NULL) {
            delete [] textureData;
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
