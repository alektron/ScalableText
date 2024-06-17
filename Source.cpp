#include "Windows.h"
#include "Windowsx.h"

#include <stdio.h>
#include <vector>
#include <string_view>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "utf8.h"

#include "glew.h"
#include "gl/GL.h"

// Size of a static C-style array. Don't use on pointers!
#define ARRAY_SIZE(_ARR) ((int)(sizeof(_ARR) / sizeof(*(_ARR)))) 

typedef void (__stdcall *glClearColorFn)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
typedef void (__stdcall *glClearFn)(GLbitfield mask);


HWND window = NULL;
bool keepAlive = true;
bool useAntiAliasing = true;
float zoom = 2.0f;

/************************************************************/
//
/************************************************************/

typedef BOOL (WINAPI* WGLCHOOSEPIXELFORMAT      )(HDC hdc, const int* piAttribIList, const FLOAT* pfAttribFList, UINT nMaxFormats, int* piFormats, UINT* nNumFormats);
typedef HGLRC(WINAPI* WGLCREATECONTEXTATTRIBSARB)(HDC hDC, HGLRC hShareContext, const int* attribList);
typedef BOOL (WINAPI* WGLSWAPINTERVALEXT        )(int interval);

static WGLCHOOSEPIXELFORMAT       wglChoosePixelFormatARB    = NULL;
static WGLCREATECONTEXTATTRIBSARB wglCreateContextAttribsARB = NULL;

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
  case WM_MOUSEWHEEL:
  {
    zoom += (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
    break;
  }
  case WM_KEYDOWN:
  {
    if (wParam == VK_SPACE) {
      useAntiAliasing = !useAntiAliasing;
    }
    break;
  }
  case WM_CLOSE:
  {
    keepAlive = false;
    break;
  }
  default:
    break;
  }

  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void DebugCallback(GLenum source​, GLenum type​, GLuint id​, GLenum severity​, GLsizei length​, const GLchar* message​, const void* userParam​)
{
  printf(message​);
}

bool RegisterWglFunctions()
{
  auto hInstance = GetModuleHandle(NULL);

  HWND window = CreateWindow(
    L"MyWindowClass",
    L"Temp",
    NULL,
    0, 0,
    0,
    0,
    NULL,
    NULL,
    GetModuleHandle(NULL),
    NULL
  );
  if (window == NULL) {
    printf("CreateWindow for temp window failed: %d", GetLastError());
    exit(-1);
  }

  HDC dc = GetDC(window);

  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;

  int pixelformat = ChoosePixelFormat(dc, &pfd);
  if (SetPixelFormat(dc, pixelformat, nullptr) == FALSE) {
    printf("SetPixelFormat for temp device context failed");
    exit(-1);
  }

  HGLRC rc = wglCreateContext(dc);
  if (rc == NULL) {
    printf("CreateContext for temp device context failed");
    exit(-1);
  }

  if (!wglMakeCurrent(dc, rc)) {
    printf("Could not make temp context current");
    exit(-1);
  }

  wglChoosePixelFormatARB    = (WGLCHOOSEPIXELFORMAT      )wglGetProcAddress("wglChoosePixelFormatARB");
  wglCreateContextAttribsARB = (WGLCREATECONTEXTATTRIBSARB)wglGetProcAddress("wglCreateContextAttribsARB");

  if (!wglChoosePixelFormatARB   ) { printf("Could not get function ptr for wglChoosePixelFormatARB"   ); exit(-1); }
  if (!wglCreateContextAttribsARB) { printf("Could not get function ptr for wglCreateContextAttribsARB"); exit(-1); }

  DestroyWindow(window);

  return true;
}

bool ReadWholeFile(std::wstring path, std::string& o_str)
{
  HANDLE file = CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (file == INVALID_HANDLE_VALUE)
    return false;

  LARGE_INTEGER size;
  GetFileSizeEx(file, &size);
  o_str.resize(size.QuadPart);
  DWORD readCnt;
  if (!ReadFile(file, o_str.data(), (DWORD)o_str.size(), &readCnt, NULL)) {
    CloseHandle(file);
    return false;
  }

  assert(readCnt == size.QuadPart);
  CloseHandle(file);
  return true;
}

struct Vec2
{
  float x = 0;
  float y = 0;

  Vec2() = default;
  Vec2(float x, float y) : x(x), y(y) {}
};

Vec2 operator+(Vec2 a, Vec2 b)
{
  return { a.x + b.x, a.y + b.y };
}

Vec2 operator*(Vec2 a, Vec2 b)
{
  return { a.x * b.x, a.y * b.y };
}

Vec2 operator/(Vec2 a, Vec2 b)
{
  return { a.x / b.x, a.y / b.y };
}

Vec2 operator*(Vec2 a, float b)
{
  return { a.x * b, a.y * b };
}

struct Bounds
{
  Vec2 Min;
  Vec2 Max;
};

struct Glyph
{
  Vec2 Position   ;
  Vec2 Barycentric;
};

Bounds CreateTextMesh(const stbtt_fontinfo* font, std::string_view text, std::vector<Glyph>& io_vertices, float fontSize)
{
  int ascent, descent, lineGap;
  stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);

  const float scale = stbtt_ScaleForPixelHeight(font, fontSize);

  auto appendTriangle = [&](Vec2 a, Vec2 b, Vec2 c, bool curve)
    {
      if (curve) {
        io_vertices.push_back({ a, Vec2( 0   , 0) });
        io_vertices.push_back({ b, Vec2( 0.5f, 0) });
        io_vertices.push_back({ c, Vec2( 1   , 1) });
      }
      else {
        io_vertices.push_back({ a, Vec2( 0, 1) });
        io_vertices.push_back({ b, Vec2( 0, 1) });
        io_vertices.push_back({ c, Vec2( 0, 1) });
      }
    };

  Vec2 offset;

  auto transformVertex = [&offset, scale](stbtt_vertex* vertex)
  {
    return (Vec2(vertex->x, vertex->y) + offset) * scale;
  };

  auto transformVertexControlPoint = [&offset, scale](stbtt_vertex* vertex)
  {
    return (Vec2(vertex->cx, vertex->cy) + offset) * scale;
  };

  int maxX = 0;
  for (size_t index_c = 0; index_c < text.size();) {
    uint32_t cp = 0;
    size_t charLength = Utf8ToUnicodeCodepoint(&cp, text.data() + index_c, text.data() + text.size());

    if (cp == '\n') {
      offset.y -= ascent - descent + lineGap ;
      offset.x = 0;
      index_c++;

      continue;
    }

    stbtt_vertex* v;
    int glyphIndex = stbtt_FindGlyphIndex(font, cp);
    int numV = stbtt_GetGlyphShape(font, glyphIndex, &v);

    int advanceWidth = 0, leftSideBearing = 0;
    stbtt_GetGlyphHMetrics(font, glyphIndex, &advanceWidth, &leftSideBearing);

    int glyphMinX = 0;
    int glyphMinY = 0;
    int glyphMaxX = 0;
    int glyphMaxY = 0;
    stbtt_GetGlyphBox(font, glyphIndex, &glyphMinX, &glyphMinY, &glyphMaxX, &glyphMaxY);

    maxX = max(maxX, advanceWidth + (int)offset.x);

    Vec2 first;
    Vec2 prev ;
    int contourCount = 0;
    for (int i = 0; i < numV; i++) {
      stbtt_vertex* vertex = v + i;

      switch (vertex->type)
      {
      case STBTT_vmove:
      {
        first = prev = transformVertex(vertex);
        contourCount = 0;
        break;
      }
      case STBTT_vline:
      {
        if (++contourCount >= 2)
          appendTriangle(first, prev, transformVertex(vertex), false);

        prev = transformVertex(vertex);
        break;
      }
      case STBTT_vcurve:
      {
        if (++contourCount >= 2)
          appendTriangle(first, prev, transformVertex(vertex), false);

        appendTriangle(prev, transformVertexControlPoint(vertex), transformVertex(vertex), true);
        prev = transformVertex(vertex);
        break;
      }
      default:
        break;
      }
    }

    offset.x += advanceWidth;
    if (index_c < text.size() - 1) {
      uint32_t nextCp = 0;
      Utf8ToUnicodeCodepoint(&nextCp, text.data() + index_c + charLength, text.data() + text.size());
      offset.x += stbtt_GetGlyphKernAdvance(font, glyphIndex, stbtt_FindGlyphIndex(font, nextCp));
    }

    index_c += charLength;
  }

  Bounds bounds;
  bounds.Min = Vec2(0, descent + offset.y) * scale;
  bounds.Max = Vec2((float)maxX, (float)ascent) * scale;
  return bounds;
}

int main()
{
  //Create window and initialize OpenGL.
  //Not relevant for the actual text rendering
  HDC dc;
  {
    auto hInstance = GetModuleHandle(NULL);

    WNDCLASSEX wndClass;
    memset(&wndClass, 0, sizeof(wndClass));
    wndClass.cbSize        = sizeof(WNDCLASSEX);
    wndClass.style         = CS_OWNDC | CS_BYTEALIGNCLIENT | CS_DBLCLKS;
    wndClass.lpfnWndProc   = WndProc;
    wndClass.hInstance     = hInstance;
    wndClass.hIcon         = NULL;
    wndClass.hbrBackground = NULL;
    wndClass.lpszClassName = L"MyWindowClass";

    ATOM reg = RegisterClassEx(&wndClass);
    if (reg == 0) {
      printf("RegisterClass failed: %d", GetLastError());
      exit(-1);
    }

    window = CreateWindow(
      L"MyWindowClass",
      L"Text Demo",
      WS_CAPTION| WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SIZEBOX,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      NULL,
      NULL,
      hInstance,
      NULL
    );
    if (window == NULL) {
      printf("CreateWindow failed: %d", GetLastError());
      exit(-1);
    }

    dc = GetDC(window);
    {
      RegisterWglFunctions();

#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_PIXEL_TYPE_ARB 0x2013
#define WGL_COLOR_BITS_ARB 0x2014
#define WGL_ALPHA_BITS_ARB 0x201B
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_TYPE_RGBA_ARB 0x202B
#define WGL_SAMPLE_BUFFERS_ARB 0x2041
#define WGL_SAMPLES_ARB 0x2042

      int  pixelFormats[1];
      UINT numFormats[1];
      int attribList[] =
      {
        WGL_DRAW_TO_WINDOW_ARB, (int)GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, (int)GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB,  (int)GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, 8,
        WGL_ALPHA_BITS_ARB, 8,
        WGL_DEPTH_BITS_ARB, 0,
        WGL_STENCIL_BITS_ARB, 0,
        WGL_SAMPLE_BUFFERS_ARB, (int)GL_FALSE,
        WGL_SAMPLES_ARB, 0,
        0, // End
      };


      wglChoosePixelFormatARB(dc, attribList, NULL, 1, pixelFormats, numFormats);
      if (SetPixelFormat(dc, pixelFormats[0], nullptr) == FALSE) {
        printf("SetPixelFormat failed");
        exit(-1);
      }

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB 0x2093
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

      int attributes[] =
      {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
      };

      HGLRC ctx = wglCreateContextAttribsARB(dc, NULL, attributes);

      if (wglMakeCurrent(dc, ctx) == FALSE)
        printf("MakeCurrent failed: %d", GetLastError());

      //VSync
      auto swapInterval = (WGLSWAPINTERVALEXT)wglGetProcAddress("wglSwapIntervalEXT");
      if (swapInterval){
        swapInterval(1);
      }

      glewExperimental = GL_TRUE;
      GLenum init = glewInit();
      GLenum err = glGetError();
      if (err != GL_NO_ERROR) {
        printf("glewInit failed: %d", err);
        exit(-1);
      }
      glEnable(GL_DEBUG_OUTPUT);
      glDebugMessageCallback(DebugCallback, nullptr);

    }

    ShowWindow(window, SW_MAXIMIZE);
  }
  

  //Read in the font file. String gets referenced by stbtt_fontinfo must be kept in memory
  //as long as 'font' is used.
  //The default font is Arial but for licensing reasons it is not included in the repository.
  //If you use a different font, make sure to adjust the name here.
  std::string fontFileContent;
  if (!ReadWholeFile(L"./Arial.ttf", fontFileContent)) {
    printf("Could not open font file");
    exit(-1);
  }

  stbtt_fontinfo font;
  stbtt_InitFont(&font, (const unsigned char*)fontFileContent.c_str(), 0); //See docs for stbtt_fontinfo for proper usage of third parameter

  //The text we want to render.
  std::string_view text = 
    "SPACEBAR to toggle antialiasing\n"
    "Mouse wheel to zoom\n"
    "Complex glyph (tested with Arial font): \xef\xb7\xba\n"
    "0123456789\n"
    "abcdefghijklmnopqrstuvwxyz\n"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
    "?()/&%^";

  //Create the vertex buffer for our text mesh.
  //We naively create a mesh for the whole text. A more complete implementation
  //might want to create something like a glyph atlas and only store every glyph once.
  std::vector<Glyph> textVertices;
  Bounds textBounds = CreateTextMesh(&font, text, textVertices, 32);
  //Note that the text meshes origin is the lower left corner of the first line.
  //'textBounds' can be used to offset the text if necessary

  GLuint textVertexBuf = 0;
  glGenBuffers(1, &textVertexBuf);
  glBindBuffer(GL_ARRAY_BUFFER, textVertexBuf);
  glBufferData(GL_ARRAY_BUFFER, textVertices.size() * sizeof(Glyph), textVertices.data(), GL_STATIC_DRAW);

  GLuint textVertexArr = 0;
  glGenVertexArrays(1, &textVertexArr);
  glBindVertexArray(textVertexArr);
  glBindBuffer(GL_ARRAY_BUFFER, textVertexBuf);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Glyph), 0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Glyph), (const void*)sizeof(Vec2));
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  //Screenspace quad that we can render our texture to later
  Vec2 quadVertices[] = {
    { -1,  -1 },
    {  1,  -1 },
    {  1,   1 },
    {  1,   1 },
    { -1,   1 },
    { -1,  -1 },
  };

  GLuint quadVertexBuf = 0;
  glGenBuffers(1, &quadVertexBuf );
  glBindBuffer(GL_ARRAY_BUFFER, quadVertexBuf );
  glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(Vec2), quadVertices, GL_STATIC_DRAW);

  GLuint quadVertexArr = 0;
  glGenVertexArrays(1, &quadVertexArr);
  glBindVertexArray(quadVertexArr);
  glBindBuffer(GL_ARRAY_BUFFER, quadVertexBuf);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vec2), 0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);


  GLuint glyphShader = 0;
  {
    std::string vShaderContent;
    ReadWholeFile(L"./Glyph.vs", vShaderContent);
    GLuint vShader = glCreateShader(GL_VERTEX_SHADER);

    const char* vSource = vShaderContent.c_str();
    glShaderSource(vShader, 1, &vSource, NULL);
    glCompileShader(vShader);

    std::string fShaderContent;
    ReadWholeFile(L"./Glyph.fs", fShaderContent);
    GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);

    const char* fSource = fShaderContent.c_str();
    glShaderSource(fShader, 1, &fSource, NULL);
    glCompileShader(fShader);

    glyphShader = glCreateProgram();
    glAttachShader(glyphShader, vShader);
    glAttachShader(glyphShader, fShader);
    glLinkProgram(glyphShader);

    glDeleteShader(fShader);
    glDeleteShader(vShader);
  }

  GLuint overlayShader = 0;
  {
    std::string vShaderContent;
    ReadWholeFile(L"./GlyphOverlay.vs", vShaderContent);
    GLuint vShader = glCreateShader(GL_VERTEX_SHADER);

    const char* vSource = vShaderContent.c_str();
    glShaderSource(vShader, 1, &vSource, NULL);
    glCompileShader(vShader);

    std::string fShaderContent;
    ReadWholeFile(L"./GlyphOverlay.fs", fShaderContent);
    GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);

    const char* fSource = fShaderContent.c_str();
    glShaderSource(fShader, 1, &fSource, NULL);
    glCompileShader(fShader);

    overlayShader = glCreateProgram();
    glAttachShader(overlayShader, vShader);
    glAttachShader(overlayShader, fShader);
    glLinkProgram(overlayShader);

    glDeleteShader(fShader);
    glDeleteShader(vShader);
  }

  //Get the window size at the start of the application.
  //Note that we do not really react to any window size changes later on.
  //Behavior when moving or resizing windows might not be as expected
  RECT rect;
  GetClientRect(window, &rect);
  int windowWidth   = rect.right  - rect.left;
  int windowHeight =  rect.bottom - rect.top ;

  GLuint glyphTex = 0;
  glGenTextures(1, &glyphTex);
  glBindTexture(GL_TEXTURE_2D, glyphTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, windowWidth, windowHeight, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);

  GLuint glyphFrameBuf = 0;
  glGenFramebuffers(1, &glyphFrameBuf);
  glBindFramebuffer(GL_FRAMEBUFFER, glyphFrameBuf);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glyphTex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  GLuint glyphShader_ViewMatrix = glGetUniformLocation(glyphShader, "u_ViewMatrix");
  GLuint glyphShader_Offset     = glGetUniformLocation(glyphShader, "u_Offset");
  GLuint glyphShader_Sample     = glGetUniformLocation(glyphShader, "u_Sample");
  GLuint glyphOverlay_AaPasses  = glGetUniformLocation(overlayShader, "u_NumAntiAliasingPasses");

  float aspectRatio = (float)windowHeight / windowWidth;

  float viewMatrix[9] = {
    1, 0, 0,
    0, 1, 0,
    -1, 0, 1,
  };

  while (keepAlive) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) != 0) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    viewMatrix[0] = zoom * 1.f/windowWidth ;
    viewMatrix[4] = zoom * 1.f/windowHeight;

    static const Vec2 ANTI_ALIASING_PATTERN[] = {
      { -0.5f,  0.0f },
      {  0.5f,  0.0f },
      {  0.0f,  0.5f },
      {  0.0f, -0.5f },
    };

    //Render the text mesh into a texture.
    //If antialiasing is enabled, the mesh gets rendered multiple times, each time with some small offset
    {
      glBindFramebuffer(GL_FRAMEBUFFER, glyphFrameBuf);
      glViewport(0, 0, windowWidth, windowHeight);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE); //We want to add up all 'colors' to count triangle overlaps
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);

      glUseProgram(glyphShader);
      glUniformMatrix3fv(glyphShader_ViewMatrix, 1, GL_FALSE, viewMatrix);
      glBindVertexArray(textVertexArr);
      if (useAntiAliasing) {
        for (size_t i = 0; i < ARRAY_SIZE(ANTI_ALIASING_PATTERN); i++) {
          Vec2 offset = ANTI_ALIASING_PATTERN[i] / Vec2((float)windowWidth, (float)windowHeight);
          glUniform2f(glyphShader_Offset, offset.x, offset.y);

          const float SAMPLE0[] = { 1, 0, 0, 0 };
          const float SAMPLE1[] = { 0, 1, 0, 0 };
          const float SAMPLE2[] = { 0, 0, 1, 0 };
          const float SAMPLE3[] = { 0, 0, 0, 1 };

          switch (i)
          {
          case 0: glUniform4fv(glyphShader_Sample, 1, SAMPLE0); break;
          case 1: glUniform4fv(glyphShader_Sample, 1, SAMPLE1); break;
          case 2: glUniform4fv(glyphShader_Sample, 1, SAMPLE2); break;
          case 3: glUniform4fv(glyphShader_Sample, 1, SAMPLE3); break;
          default:
            break;
          }

          glDrawArrays(GL_TRIANGLES, 0, (GLsizei)textVertices.size());
        }
      }
      else {
        const float SAMPLE[] = { 1, 0, 0, 0 };
        glUniform2f(glyphShader_Offset, 0, 0);
        glUniform4fv(glyphShader_Sample, 1, SAMPLE);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)textVertices.size());
      }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    //Use the previously created texture to render the text into the final framebuffer.
    //Note that we use a quad mesh that fills the whole screen for that.
    //Performance could be improved by only drawing a quad as big as the text bounds.
    {
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_ZERO, GL_SRC_COLOR);
      glViewport(0, 0, windowWidth, windowHeight);
      glClearColor(1, 1, 1, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glUseProgram(overlayShader);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, glyphTex);
      glBindVertexArray(quadVertexArr);
      glUniform1i(glyphOverlay_AaPasses, useAntiAliasing ? ARRAY_SIZE(ANTI_ALIASING_PATTERN) : 1);
      glDrawArrays(GL_TRIANGLES, 0, sizeof(quadVertices) / sizeof(quadVertices[0]));
    }

    SwapBuffers(dc);
  }
}