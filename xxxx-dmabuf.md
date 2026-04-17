# DMA-BUF

在Wayland环境下，OpenGL程序通过DMA-BUF机制将绘制完成的画面传递给合成器。我们也可以不依赖EGL库提供的功能自己来实现。

首先，zwp_linux_dmabuf_v1全局对象会通过feedback事件告知客户端当前有哪些可用的DRI设备。从中选择一个支持所需像素格式的设备。

```c
struct zwp_linux_dmabuf_feedback_v1 *feedback = zwp_linux_dmabuf_v1_get_surface_feedback(globals.zwp_linux_dmabuf_v1, wl_surface);

struct feedback_context feedback_context;
feedback_context_init(&feedback_context);

assert(zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &feedback_listener, &feedback_context) == 0);
assert(wl_display_roundtrip(wl_display) > 0);
assert(feedback_context.done);

dev_t dev = find_dmabuf_device(&feedback_context);
assert(dev != -1);
```

接着，借助udev库获取该设备的文件路径，例如/dev/dri/renderD128，打开设备文件获得文件描述符fd，并将fd传递给GBM库。

```c
struct udev *udev = udev_new();
struct udev_device *udev_device = udev_device_new_from_devnum(udev, 'c', dev);
const char *node = udev_device_get_devnode(udev_device);

int gbm_fd = open(node, O_RDWR);
udev_device_unref(udev_device);
udev_unref(udev);
assert(gbm_fd>0);

struct gbm_device *gbm = gbm_create_device(gbm_fd);
assert(gbm != NULL);
```

利用GBM库创建缓冲区对象bo，然后通过zwp_linux_buffer_params_v1_create_immed请求将bo作为参数提交给服务端，从而得到对应的wl_buffer对象。

```c
struct gbm_bo *bo = gbm_bo_create(gbm, 400, 400, GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING);
assert(bo != NULL);
struct zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(globals.zwp_linux_dmabuf_v1);
assert(params != NULL);
int fd = gbm_bo_get_fd(bo);
assert(fd >= 0);

uint32_t stride = gbm_bo_get_stride(bo);
uint64_t modifier = gbm_bo_get_modifier(bo);
uint32_t modifier_lo = modifier & 0xFFFFFFFF;
uint32_t modifier_hi = modifier >> 32;
int plane_count = gbm_bo_get_plane_count(bo);
for (int i=0; i<plane_count; ++i) {
  uint32_t offset = gbm_bo_get_offset(bo, i);
  zwp_linux_buffer_params_v1_add(params, fd, i, offset, stride, modifier_hi, modifier_lo);
}

uint32_t width = gbm_bo_get_width(bo);
uint32_t height = gbm_bo_get_height(bo);
uint32_t format = gbm_bo_get_format(bo);

struct wl_buffer *wl_buffer = zwp_linux_buffer_params_v1_create_immed(params, width, height, format, 0);
assert(wl_buffer != NULL);
```

至此，便可通过Wayland标准的commit机制提交画面更新。

在渲染层面，还需要将bo与OpenGL纹理关联起来。

调用eglCreateImage，以之前创建的bo为源生成一个EGLImage。

```c
EGLAttrib attributes[] = {
  EGL_WIDTH,                          width,
  EGL_HEIGHT,                         height,
  EGL_LINUX_DRM_FOURCC_EXT,           format,
  EGL_DMA_BUF_PLANE0_FD_EXT,          fd,
  EGL_DMA_BUF_PLANE0_OFFSET_EXT,      0,
  EGL_DMA_BUF_PLANE0_PITCH_EXT,       stride,
  EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, modifier_hi,
  EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, modifier_lo,
  EGL_DMA_BUF_PLANE1_FD_EXT,          fd,
  EGL_DMA_BUF_PLANE1_OFFSET_EXT,      0,
  EGL_DMA_BUF_PLANE1_PITCH_EXT,       stride,
  EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, modifier_hi,
  EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, modifier_lo,
  EGL_DMA_BUF_PLANE2_FD_EXT,          fd,
  EGL_DMA_BUF_PLANE2_OFFSET_EXT,      0,
  EGL_DMA_BUF_PLANE2_PITCH_EXT,       stride,
  EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, modifier_hi,
  EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, modifier_lo,
  EGL_DMA_BUF_PLANE3_FD_EXT,          fd,
  EGL_DMA_BUF_PLANE3_OFFSET_EXT,      0,
  EGL_DMA_BUF_PLANE3_PITCH_EXT,       stride,
  EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT, modifier_hi,
  EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT, modifier_lo,
  EGL_NONE,
};

for (int i=0; i<plane_count; ++i) {
  uint32_t offset = gbm_bo_get_offset(bo, i);
  attributes[9 + 10 * i] = offset;
}
attributes[6 + 10 * plane_count] = EGL_NONE;
EGLImage image = eglCreateImage(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, &attributes[0]);
assert(image != EGL_NO_IMAGE);
```
在调用eglMakeCurrent设置当前渲染上下文时，将surface参数指定为 EGL_NO_SURFACE。

```c
EGLint context_attributes[] = {
  EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
  EGL_CONTEXT_CLIENT_VERSION,      4,
  EGL_CONTEXT_MINOR_VERSION_KHR,   3,
  EGL_NONE,
};

EGLContext egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, &context_attributes[0]);
assert(egl_context != EGL_NO_CONTEXT);
assert(eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context) == EGL_TRUE);
```

基于EGLImage创建一个OpenGL纹理对象。

```c
GLuint texture;
glGenTextures(1, &texture);
glBindTexture(GL_TEXTURE_2D, texture);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
```

最后，创建一个帧缓冲对象，把渲染结果直接画到纹理上。

```c
GLuint fbo;
glGenFramebuffers(1, &fbo);
glBindFramebuffer(GL_FRAMEBUFFER, fbo);
glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_FLIP_Y_MESA, GL_TRUE);
glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
```
