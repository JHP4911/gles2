#include <exception>
#include <cmath>
#include <unistd.h>
#ifndef _WIN32
#include <iostream>
#include <SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <bcm_host.h>
#else
#include <string>
#include <process.h>
#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif

#ifndef _WIN32
using std::cin;
using std::cout;
using std::endl;
#endif
using std::string;
using std::exception;
using std::min;

#define ROTATION_AXIS_X 0
#define ROTATION_AXIS_Y 1
#define ROTATION_AXIS_Z 2

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

class Matrix
{
    public:
        Matrix();
        Matrix(GLuint width, GLuint height);
        Matrix(GLuint width, GLuint height, GLfloat* matrixData);
        ~Matrix();

        GLfloat* GetData();

        void GetSize(GLuint &width, GLuint &height);
        void SetSize(GLuint width, GLuint height);

        Matrix operator +(const Matrix &matrix);
        Matrix operator -(const Matrix &matrix);
        Matrix operator *(const Matrix &matrix);
        Matrix & operator =(const Matrix &matrix);
        Matrix & operator =(const GLfloat* matrixData);

        static Matrix & GeneratePerpective(GLfloat width, GLfloat height, GLfloat nearPane, GLfloat farPane);
        static Matrix & GenerateTranslation(GLfloat x, GLfloat y, GLfloat z);
        static Matrix & GenerateRotation(GLfloat angle, GLuint axis);
    private:
        GLfloat* data;
        GLuint width;
        GLuint height;

        bool RecreateData(GLuint width, GLuint height, bool deleteData);
};

Matrix::Matrix() :
    width(4), height(4)
{
    data = new GLfloat[4 * 4];
    memset(data, 0, sizeof(GLfloat) * 4 * 4);
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

bool Matrix::RecreateData(GLuint width, GLuint height, bool deleteData)
{
    if ((this->width != width) || (this->height != height)) {
        if (deleteData) {
            delete [] data;
        }
        this->width = width;
        this->height = height;
        data = new GLfloat[this->width * this->height];
        return true;
    }
    return false;
}

Matrix & Matrix::GeneratePerpective(GLfloat width, GLfloat height, GLfloat nearPane, GLfloat farPane)
{
    Matrix* result = new Matrix(4, 4);
    GLfloat* data = result->GetData();

    data[0] = 2.0f * nearPane / width;
    data[5] = 2.0f * nearPane / height;
    data[10] = -(farPane + nearPane) / (farPane - nearPane);
    data[11] = -1.0f;
    data[14] = -2.0f * farPane * nearPane / (farPane - nearPane);

    return *result;
}

Matrix & Matrix::GenerateTranslation(GLfloat x, GLfloat y, GLfloat z)
{
    Matrix* result = new Matrix(4, 4);
    GLfloat* data = result->GetData();

    for (GLuint i = 0; i < 4; i++) {
        data[i + i * 4] = 1.0f;
    }
    data[12] = x;
    data[13] = y;
    data[14] = z;

    return *result;
}

Matrix & Matrix::GenerateRotation(GLfloat angle, GLuint axis)
{
    Matrix* result = new Matrix(4, 4);
    GLfloat* data = result->GetData();

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

    return *result;
}

GLfloat* Matrix::GetData()
{
    return data;
}

Matrix Matrix::operator +(const Matrix &matrix)
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

Matrix Matrix::operator -(const Matrix &matrix)
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

Matrix Matrix::operator *(const Matrix &matrix)
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

Matrix & Matrix::operator =(const Matrix &matrix)
{
    if ((width != matrix.width) || (height != matrix.height)) {
        delete [] data;
        width = matrix.width;
        height = matrix.height;
        data = new GLfloat[this->width * this->height];
    }
    memcpy(data, matrix.data, sizeof(GLfloat) * width * height);
    return *this;
}

Matrix & Matrix::operator =(const GLfloat *matrixData)
{
    memcpy(data, matrixData, sizeof(GLfloat) * width * height);
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
    GLfloat* oldData = data;
    data = new GLfloat[width * height];
    memset(data, 0, sizeof(GLfloat) * width * height);
    for (GLuint i = 0; i < min(this->width, width); i++) {
        memcpy(&data[i * height], &oldData[i * this->height], sizeof(GLfloat) * min(this->height, height));
    }
    this->width = width;
    this->height = height;
    delete [] oldData;
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
#ifndef _WIN32
            "precision mediump float;                                   \n"
#endif
            "varying vec3 fragmentColor;                                \n"
            "                                                           \n"
            "void main()                                                \n"
            "{                                                          \n"
            "   gl_FragColor = vec4(fragmentColor, 1);                  \n"
            "}                                                          \n";

        ShaderProgram program(vertexShaderCode, fragmentShaderCode);

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
        renderer->GetScreenSize(width, height);
        glViewport(0, 0, width, height);

        while (!quit) {
            Matrix rotation = Matrix::GenerateRotation(angle, ROTATION_AXIS_Z);

            glClearColor(0.15f, 0.25f, 0.35f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glUseProgram(program.GetProgram());
            glUniformMatrix4fv(rotMatrixUniform, 1, GL_FALSE, rotation.GetData());

            glVertexAttribPointer(vertColorAttribute, 3, GL_FLOAT, GL_FALSE, 0, colorData);
            glVertexAttribPointer(vertPositionAttribute, 3, GL_FLOAT, GL_FALSE, 0, vertexData);

            glEnableVertexAttribArray(vertColorAttribute);
            glEnableVertexAttribArray(vertPositionAttribute);

            glDrawArrays(GL_TRIANGLES, 0, 3);

            glDisableVertexAttribArray(vertPositionAttribute);
            glDisableVertexAttribArray(vertColorAttribute);

            renderer->SwapBuffers();

            angle += 1.0f;
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
