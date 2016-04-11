#include <iostream>
#include <exception>
#include <unistd.h>
#ifndef _WIN32
#include <SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <bcm_host.h>
#else
#include <process.h>
#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif
#include <cmath>

#ifndef _WIN32
using std::cin;
using std::cout;
using std::endl;
#endif
using std::string;
using std::exception;

class Exception : public exception
{
    public:
        explicit Exception(string message);
        virtual ~Exception() throw();

        virtual const char* what() const throw();
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

const char* Exception::what() const throw()
{
    return exceptionMessage.c_str();
}

class Renderer
{
    public:
        typedef void(*OnCloseCallback)();

        virtual ~Renderer();

        static Renderer* Initialize();
        void Terminate();
        void GetScreenSize(uint32_t &width, uint32_t &height);
        void SetOnCloseCallback(OnCloseCallback onCloseCallback);
        bool SwapBuffers();
    private:
#ifndef _WIN32
        EGLDisplay eglDisplay;
        EGLContext eglContext;
        EGLSurface eglSurface;
        static SDL_Surface* sdlScreen;
        pthread_t eventLoopThread;
#else
        HANDLE eventLoopThread;
        static GLFWwindow* glfwWindow;
#endif
        static bool eventLoopInitError, eventLoop;
        static uint32_t screenWidth, screenHeight;
        static OnCloseCallback onCloseCallback;
        bool isTerminated;

        Renderer();
        void EndEventLoop();
#ifndef _WIN32
        static void* WindowEventLoop(void*);
#else
        static void __cdecl WindowEventLoop(void*);
#endif
};

#ifndef _WIN32
SDL_Surface* Renderer::sdlScreen = NULL;
#else
GLFWwindow* Renderer::glfwWindow = NULL;
#endif
bool Renderer::eventLoop = true;
bool Renderer::eventLoopInitError = false;
uint32_t Renderer::screenWidth = 0;
uint32_t Renderer::screenHeight = 0;
Renderer::OnCloseCallback Renderer::onCloseCallback = NULL;

Renderer::Renderer()
{
    isTerminated = false;

#ifndef _WIN32
    bcm_host_init();

    EGLConfig config;
    EGLint numConfig;

    static EGL_DISPMANX_WINDOW_T nativeWindow;

    DISPMANX_ELEMENT_HANDLE_T dispmanElement;
    DISPMANX_DISPLAY_HANDLE_T dispmanDisplay;
    DISPMANX_UPDATE_HANDLE_T dispmanUpdate;

    VC_RECT_T dstRect, srcRect;

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

    if (graphics_get_display_size(0, &screenWidth, &screenHeight) < 0) {
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        throw Exception("Cannot obtain screen resolution");
    }

    eventLoopInitError = false;
    int returnCode = pthread_create(&eventLoopThread, NULL, &Renderer::WindowEventLoop, NULL);
    if (returnCode) {
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        throw Exception("Cannot create event loop thread");
    }
    while ((sdlScreen == NULL) && !eventLoopInitError) {
        usleep(1);
    }
    if (eventLoopInitError) {
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        throw Exception("Cannot create SDL window");
    }

    dstRect.x = 0;
    dstRect.y = 0;
    dstRect.width = screenWidth;
    dstRect.height = screenHeight;

    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.width = screenWidth << 16;
    srcRect.height = screenHeight << 16;

    dispmanDisplay = vc_dispmanx_display_open(0);
    dispmanUpdate = vc_dispmanx_update_start(0);

    dispmanElement = vc_dispmanx_element_add(dispmanUpdate, dispmanDisplay, 0, &dstRect, 0, &srcRect,
        DISPMANX_PROTECTION_NONE, 0, 0, (DISPMANX_TRANSFORM_T)0);

    nativeWindow.element = dispmanElement;
    nativeWindow.width = screenWidth;
    nativeWindow.height = screenHeight;
    vc_dispmanx_update_submit_sync(dispmanUpdate);

    eglSurface = eglCreateWindowSurface(eglDisplay, config, &nativeWindow, NULL);
    if (eglSurface == EGL_NO_SURFACE) {
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        EndEventLoop();
        throw Exception("Cannot create new EGL window surface");
    }
#else
#ifndef FORCE_FULLSCREEN
    screenWidth = 400;
    screenHeight = 300;
#else
    screenWidth = GetSystemMetrics(SM_CXSCREEN);
    screenHeight = GetSystemMetrics(SM_CYSCREEN);
#endif

    eventLoopInitError = false;
    eventLoopThread = (HANDLE)_beginthread(WindowEventLoop, 0, NULL);
    while ((glfwWindow == NULL) && !eventLoopInitError) {
        usleep(1);
    }
    if (eventLoopInitError) {
        throw Exception("Cannot create OpenGL window");
    }
#endif

#ifndef _WIN32
    if (eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) != EGL_TRUE) {
        eglDestroySurface(eglDisplay, eglSurface);
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        EndEventLoop();
        throw Exception("Cannot attach EGL rendering context to EGL surface");
    }
#else
    glfwMakeContextCurrent(glfwWindow);

    if (glewInit() != GLEW_OK) {
        glfwMakeContextCurrent(NULL);
        EndEventLoop();
        throw Exception("Cannot initialize GLEW");
    }
#endif
}

Renderer::~Renderer()
{
    Terminate();
}

void Renderer::EndEventLoop()
{
    eventLoop = false;

#ifndef _WIN32
    pthread_join(eventLoopThread, NULL);
#else
    WaitForSingleObject(eventLoopThread, INFINITE);
#endif
}

#ifndef _WIN32
void* Renderer::WindowEventLoop(void*)
#else
void __cdecl Renderer::WindowEventLoop(void*)
#endif
{
    eventLoopInitError = false;

#ifndef _WIN32
    SDL_Event event;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        eventLoopInitError = true;
        return NULL;
    }

    SDL_WM_SetCaption("SDL Window", "SDL Window");

    sdlScreen = SDL_SetVideoMode(screenWidth, screenHeight, 0, 0);
    if (sdlScreen == NULL) {
        eventLoopInitError = true;
        SDL_Quit();
        return NULL;
    }
#else
	if (glfwInit() != GL_TRUE) {
	    eventLoopInitError = true;
        _endthread();
	}

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#ifndef FORCE_FULLSCREEN
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

	glfwWindow = glfwCreateWindow(screenWidth, screenHeight, "Open GL Window", NULL, NULL);
#else

	glfwWindow = glfwCreateWindow(screenWidth, screenHeight, "Open GL Window", glfwGetPrimaryMonitor(), NULL);
#endif
	if (glfwWindow == NULL) {
	    eventLoopInitError = true;
        glfwTerminate();
        _endthread();
	}

	glfwSetInputMode(glfwWindow, GLFW_STICKY_KEYS, GL_TRUE);
    glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
#endif

    while (eventLoop) {
#ifndef _WIN32
        if (SDL_PollEvent(&event) && (event.type == SDL_KEYDOWN) && (event.key.keysym.sym == SDLK_ESCAPE)) {
#else
        glfwPollEvents();
        if ((glfwGetKey(glfwWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS) || (glfwWindowShouldClose(glfwWindow) != 0)) {
#endif
            if (onCloseCallback != NULL) {
                onCloseCallback();
            }
        }
        usleep(1);
    }

#ifndef _WIN32
    SDL_Quit();
    sdlScreen = NULL;
    return NULL;
#else
    glfwDestroyWindow(glfwWindow);
    glfwTerminate();
    _endthread();
#endif
}

Renderer* Renderer::Initialize()
{
    static Renderer instance;
    return &instance;
}

void Renderer::Terminate() {
    if (!isTerminated) {
#ifndef _WIN32
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(eglDisplay, eglSurface);
        eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
        EndEventLoop();
#else
        glfwMakeContextCurrent(NULL);
        EndEventLoop();
#endif
        isTerminated = true;
    }
}

void Renderer::GetScreenSize(uint32_t &width, uint32_t &height)
{
    width = screenWidth;
    height = screenHeight;
}

bool Renderer::SwapBuffers()
{
    if (isTerminated) {
        return false;
    }
#ifndef _WIN32
    return eglSwapBuffers(eglDisplay, eglSurface) == EGL_TRUE;
#else
    glfwSwapBuffers(glfwWindow);
    return true;
#endif
}

void Renderer::SetOnCloseCallback(OnCloseCallback callback)
{
    onCloseCallback = callback;
}

class ShaderProgram
{
    public:
        ShaderProgram(const char* vertexShaderCode, const char* fragmentShaderCode);
        ~ShaderProgram();

        GLuint GetProgram();
    private:
        GLuint vertexShader;
        GLuint fragmentShader;
        GLuint program;

        GLuint LoadShader(const char* shaderCode, GLenum shaderType);
};

ShaderProgram::ShaderProgram(const char* vertexShaderCode, const char* fragmentShaderCode)
{
    GLint isLinked;

    vertexShader = LoadShader(vertexShaderCode, GL_VERTEX_SHADER);
    if (vertexShader == 0) {
        throw Exception("Cannot load vertex shader");
    }
    fragmentShader = LoadShader(fragmentShaderCode, GL_FRAGMENT_SHADER);
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

GLuint ShaderProgram::LoadShader(const char* shaderCode, GLenum shaderType)
{
    GLuint shader;
    GLint isCompiled;

    shader = glCreateShader(shaderType);
    if (shader == 0)
        return 0;
    glShaderSource(shader, 1, &shaderCode, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (!isCompiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = new char[infoLen];
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

bool quit;

void CloseRequestHandler() {
    quit = true;
}

#ifndef _WIN32
int main(int argc, const char **argv)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
    quit = false;
    Renderer* renderer = NULL;

    try {
        renderer = Renderer::Initialize();
        renderer->SetOnCloseCallback(CloseRequestHandler);

        char vertexShaderCode[] =
            "attribute vec4 position;                   \n"
            "uniform mat4 matrix;                       \n"
            "void main()                                \n"
            "{                                          \n"
            "   gl_Position = matrix * position;        \n"
            "}                                          \n";

        char fragmentShaderCode[] =
#ifndef _WIN32
            "precision mediump float;                   \n"
#endif
            "void main()                                \n"
            "{                                          \n"
            "   gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
            "}                                          \n";

        ShaderProgram program(vertexShaderCode, fragmentShaderCode);

        GLuint positionAttribute = glGetAttribLocation(program.GetProgram(), "position");
        GLuint matrixUniform = glGetUniformLocation(program.GetProgram(), "matrix");

        GLfloat angle = 0.0f;

        GLfloat vertexData[] = {
           -0.67f, -0.67f, 0.0f,
           0.67f, -0.67f, 0.0f,
           0.0f,  0.67f, 0.0f,
        };

        uint32_t width, height;

        renderer->GetScreenSize(width, height);
        glViewport(0, 0, width, height);

        while (!quit) {
            GLfloat rotationMatrix[] = {
                cos(angle * M_PI / 180.0f), -sin(angle * M_PI / 180.0f), 0.0f, 0.0f,
                sin(angle * M_PI / 180.0f), cos(angle * M_PI / 180.0f), 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };

            glClearColor(0.15f, 0.25f, 0.35f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glUseProgram(program.GetProgram());
            glUniformMatrix4fv(matrixUniform, 1, GL_FALSE, &rotationMatrix[0]);

            glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, 0, vertexData);
            glEnableVertexAttribArray(positionAttribute);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glDisableVertexAttribArray(positionAttribute);

            renderer->SwapBuffers();

            angle += 0.1f;
            usleep(1);
        }

        renderer->Terminate();
    } catch (exception &e) {
        if (renderer != NULL) {
            renderer->Terminate();
        }
        #ifndef _WIN32
            cout << e.what() << endl;
        #else
            MessageBox(NULL, e.what(), "Exception", MB_OK | MB_ICONERROR);
        #endif
        return 1;
    }

	return 0;
}
