#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <cairo.h>

#define EGL_EGL_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

static
GLuint
add_shader(GLenum type, char const *source) {
  GLuint shader = glCreateShader(type);
  assert(shader != 0);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);

  GLint status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    GLint length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    char log[length];
    glGetShaderInfoLog(shader, length, &length, &log[0]);
    fprintf(stderr, "compile shader failed: %s\n", log);
    assert(0);
  }

  return shader;
}

static
GLuint
add_program(char const *vs, char const *fs) {
  GLuint program = glCreateProgram();
  glAttachShader(program, add_shader(GL_VERTEX_SHADER, vs));
  glAttachShader(program, add_shader(GL_FRAGMENT_SHADER, fs));
  glLinkProgram(program);

  GLint status;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) {
    GLint length;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    char log[length];
    glGetProgramInfoLog(program, length, &length, &log[0]);
    fprintf(stderr, "link program failed: %s\n", log);
    assert(0);
  }
  return program;
}

void
gl_draw_clock_init(void) {
  fprintf(stderr, "OpenGL Version: %s\n", glGetString(GL_VERSION));
  fprintf(stderr, "OpenGL Shading Language Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
  fprintf(stderr, "OpenGL Vendor: %s\n", glGetString(GL_VENDOR));
  fprintf(stderr, "OpenGL Renderer: %s\n", glGetString(GL_RENDERER));

  static const char *vs =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "out vec2 v_pos;\n"
    "void main() {\n"
    "  v_pos = a_pos;\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

  static const char *fs =
    "#version 330 core\n"
    "in vec2 v_pos;\n"
    "out vec4 FragColor;\n"
    "uniform int u_time;\n"
    "float\n"
    "tick(float step) {\n"
    "  float rad = radians(step);"
    "  return round(atan(v_pos.x, v_pos.y)/rad)*rad;\n"
    "}\n"
    "float\n"
    "line(float width, float r1, float r2, float rot) {\n"
    "  float c = cos(rot);\n"
    "  float s = sin(rot);\n"
    "  vec2 p = vec2(v_pos.x * c - v_pos.y * s, v_pos.x * s + v_pos.y * c);\n"
    "  float w = width/2.0;"
    "  float tx = smoothstep(w-0.005, w+0.005, abs(p.x));\n"
    "  float d = (r2 - r1) / 2.0;\n"
    "  float ty = smoothstep(d-0.005, d+0.005, abs((r1+r2)/2.0-p.y));\n"
    "  return 1.0 - (1.0-tx)*(1.0-ty);\n"
    "}\n"
    "void\n"
    "main() {\n"
    "  float d = length(v_pos);\n"
    "  float ring = smoothstep(0.005, 0.015, abs(d - 0.9));\n"
    "  float dot = smoothstep(0.035, 0.045, d);\n"
    "  float htick = line(0.02, 0.8, 0.9, tick(30.0));\n"
    "  float mtick = line(0.01, 0.85, 0.9, tick(6.0));\n"
    "  float back = min(min(ring, dot), min(htick, mtick));\n"
    "  float hhand = line(0.03, -0.09, 0.45, radians(float(u_time)/120.0));\n;"
    "  float mhand = line(0.02, -0.12, 0.60, radians(float(u_time%3600)/10.0));\n;"
    "  float shand = line(0.01, -0.15, 0.75, radians(float(u_time%60)*6.0));\n;"
    "  float gray = min(min(back, hhand), min(mhand, shand));\n"
    "  FragColor = vec4(gray, gray, gray, 1.0);\n"
    "}\n";

  GLuint program = add_program(vs, fs);
  glUseProgram(program);

  float verts[] = {
    -1.0, -1.0,
    1.0, -1.0,
    1.0,  1.0,
    -1.0,  1.0
  };
  GLuint vao, vbo;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

void
gl_draw_clock(void) {
  GLint program;
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  assert(program >= 0);

  GLint loc_u_time = glGetUniformLocation(program, "u_time");
  assert(loc_u_time >= 0);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  glUniform1i(loc_u_time, (t->tm_hour % 12) * 3600 + t->tm_min * 60 + t->tm_sec);
  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  glFinish();
}

int
__attribute__((weak))
main() {
  EGLDisplay egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(egl_display != EGL_NO_DISPLAY);

  EGLint major;
  EGLint minor;
  assert(eglInitialize(egl_display, &major, &minor) == EGL_TRUE);
  fprintf(stderr, "EGL Version %d.%d\n", major, minor);

  assert(eglBindAPI(EGL_OPENGL_API) == EGL_TRUE);

  EGLint config_attributes[] = {
    EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
    EGL_RED_SIZE,        8,
    EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,       8,
    EGL_ALPHA_SIZE,      8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_NONE,
  };

  EGLint count;
  EGLConfig egl_config;

  assert(eglChooseConfig(egl_display, &config_attributes[0], &egl_config, 1, &count) == EGL_TRUE);
  assert(count > 0);

  EGLint pbuffer_attributes[] = {
    EGL_WIDTH, 400,
    EGL_HEIGHT, 400,
    EGL_NONE,
  };

  EGLSurface egl_surface = eglCreatePbufferSurface(egl_display, egl_config, &pbuffer_attributes[0]);
  assert(egl_surface != EGL_NO_SURFACE);

  EGLint context_attributes[] = {
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    EGL_CONTEXT_CLIENT_VERSION,      4,
    EGL_CONTEXT_MINOR_VERSION_KHR,   3,
    EGL_NONE,
  };

  EGLContext egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, &context_attributes[0]);
  assert(egl_context != EGL_NO_CONTEXT);
  assert(eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context) == EGL_TRUE);

  gl_draw_clock_init();
  gl_draw_clock();

  glPixelStorei(GL_PACK_INVERT_MESA, 1);
  char pixels[400*400*4];
  glReadPixels(0, 0, 400, 400, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

  cairo_surface_t *surface = cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_ARGB32, 400, 400, 400*4);
  cairo_surface_write_to_png(surface, "clock.png");
  cairo_surface_destroy(surface);

  return 0;
}
