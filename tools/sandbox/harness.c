#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define GL_GLEXT_PROTOTYPES
#include "libretro.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* A bounded development frontend, not a general RetroArch replacement. */
static char system_dir[PATH_MAX], save_dir[PATH_MAX], output_dir[PATH_MAX];
static unsigned frame_number, frame_limit = 180, capture_every = 30, video_count;
static _Atomic uint64_t audio_frames;
static bool use_gl, shutdown_requested, hw_requested;
static volatile sig_atomic_t interrupted, interrupt_signal;
static enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
static struct retro_hw_render_callback hw;
static EGLDisplay display = EGL_NO_DISPLAY;
static EGLContext context = EGL_NO_CONTEXT;
static EGLSurface surface = EGL_NO_SURFACE;
static GLuint framebuffer, color_texture, depth_buffer;
static unsigned last_width, last_height;
static uint8_t *last_rgb;
static FILE *frame_log, *record_file;
static unsigned record_every = 2, record_count, record_initial_black;
static bool record_seen_video;
static const unsigned render_size = 1024;
struct option { const char *key, *value; };
static struct option defaults[256];
static size_t default_count;
struct input_event { unsigned start, count, mask; int lx, ly, rx, ry; };
static struct input_event events[256];
static size_t event_count;
#define LIVE_PACKET_BYTES 256u
struct live_packet { uint64_t sequence; struct input_event input; };
static struct live_packet live_packet;
static const char *live_path;
static FILE *live_log;
static bool live_seen, live_active;
static unsigned live_accepted, live_expired, live_rejected;

/* Audio callbacks may arrive from Azahar's audio sink thread. Serialize only
 * recording state/file writes; the no-recording path keeps counting callbacks.
 * No resampling, mixing, silence insertion, or replacement cues are performed. */
#define AUDIO_WAV_MAX_BYTES (256u * 1024u * 1024u)
#define AUDIO_WAV_MAX_SECONDS 300u
static FILE *audio_wav;
static atomic_flag audio_wav_lock = ATOMIC_FLAG_INIT;
static _Atomic bool audio_wav_enabled;
static _Atomic int audio_wav_failed;
static uint64_t audio_wav_frames;
static uint32_t audio_wav_rate;
static double audio_wav_source_rate;
static void audio_lock(void) {
    while (atomic_flag_test_and_set_explicit(&audio_wav_lock, memory_order_acquire)) sched_yield();
}
static void audio_unlock(void) { atomic_flag_clear_explicit(&audio_wav_lock, memory_order_release); }
static void audio_u16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static void audio_u32(uint8_t *p,uint32_t n) {
    p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);
}
static void audio_header(uint8_t h[44],uint32_t rate,uint32_t bytes) {
    memset(h,0,44);memcpy(h,"RIFF",4);audio_u32(h+4,36u+bytes);memcpy(h+8,"WAVEfmt ",8);
    audio_u32(h+16,16);audio_u16(h+20,1);audio_u16(h+22,2);audio_u32(h+24,rate);
    audio_u32(h+28,rate*4u);audio_u16(h+32,4);audio_u16(h+34,16);memcpy(h+36,"data",4);audio_u32(h+40,bytes);
}
static bool audio_capture_close(void) {
    atomic_store_explicit(&audio_wav_enabled,false,memory_order_release);
    audio_lock();
    if(audio_wav){
        uint8_t header[44];audio_header(header,audio_wav_rate,(uint32_t)(audio_wav_frames*4u));
        if(!audio_wav_rate||fflush(audio_wav)||fseek(audio_wav,0,SEEK_SET)||
           fwrite(header,1,sizeof header,audio_wav)!=sizeof header)
            atomic_store(&audio_wav_failed,1);
        if(fclose(audio_wav))atomic_store(&audio_wav_failed,1);
        audio_wav=NULL;
        fprintf(stderr,"AUDIO WAV END frames=%" PRIu64 " bytes=%" PRIu64 " rate=%u source_rate=%.9f failed=%d\n",
            audio_wav_frames,audio_wav_frames*4u,audio_wav_rate,audio_wav_source_rate,atomic_load(&audio_wav_failed));
    }
    audio_unlock();return !atomic_load(&audio_wav_failed);
}
static void audio_capture_at_exit(void) { if(!audio_capture_close())fprintf(stderr,"HARNESS ERROR: audio WAV capture incomplete\n"); }
static bool audio_capture_open(const char *path) {
    int fd=open(path,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return false;
    audio_lock();audio_wav=fdopen(fd,"w+b");
    if(!audio_wav){close(fd);audio_unlock();return false;}
    uint8_t header[44];audio_header(header,0,0);
    bool ok=fwrite(header,1,sizeof header,audio_wav)==sizeof header;
    if(!ok){fclose(audio_wav);audio_wav=NULL;atomic_store(&audio_wav_failed,1);}
    else atomic_store_explicit(&audio_wav_enabled,true,memory_order_release);
    audio_unlock();return ok;
}
static void audio_capture_rate(double source_rate) {
    if(!atomic_load_explicit(&audio_wav_enabled,memory_order_acquire))return;
    audio_lock();
    if(!(source_rate>=8000&&source_rate<=192000)||
       (audio_wav_rate&&source_rate!=audio_wav_source_rate))atomic_store(&audio_wav_failed,1);
    else {
        audio_wav_source_rate=source_rate;
        /* RIFF PCM uses an integer Hz field; retain the exact AV rate in log. */
        audio_wav_rate=(uint32_t)(source_rate+.5);
        if(audio_wav_frames>(uint64_t)audio_wav_rate*AUDIO_WAV_MAX_SECONDS)atomic_store(&audio_wav_failed,1);
    }
    audio_unlock();
}
static void audio_capture_write(const int16_t *data,size_t count) {
    if(!atomic_load_explicit(&audio_wav_enabled,memory_order_acquire)||!count)return;
    audio_lock();
    if(!audio_wav||atomic_load(&audio_wav_failed)){audio_unlock();return;}
    uint64_t frame_bound=(AUDIO_WAV_MAX_BYTES-44u)/4u;
    uint64_t time_bound=(uint64_t)(audio_wav_rate?audio_wav_rate:8000u)*AUDIO_WAV_MAX_SECONDS;
    if(time_bound<frame_bound)frame_bound=time_bound;
    if(!data||audio_wav_frames>frame_bound||count>frame_bound-audio_wav_frames){
        atomic_store(&audio_wav_failed,1);audio_unlock();return;
    }
    uint8_t encoded[4096];size_t done=0;
    while(done<count){
        size_t batch=count-done;if(batch>sizeof encoded/4u)batch=sizeof encoded/4u;
        for(size_t i=0;i<batch*2u;++i)audio_u16(encoded+i*2u,(uint16_t)data[done*2u+i]);
        if(fwrite(encoded,4,batch,audio_wav)!=batch){atomic_store(&audio_wav_failed,1);break;}
        audio_wav_frames+=batch;done+=batch;
    }
    audio_unlock();
}

static void on_signal(int sig) { interrupt_signal = sig; interrupted = 1; }
static void die(const char *message) { fprintf(stderr, "HARNESS ERROR: %s\n", message); exit(2); }
static double monotonic_seconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static void make_dir(const char *path) {
    if (mkdir(path, 0700) != 0 && errno != EEXIST) die("cannot create isolated directory");
}
static void core_log(enum retro_log_level level, const char *format, ...) {
    if (level < RETRO_LOG_WARN) return;
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
}
static const char *option_value(const char *key) {
    static const struct option fixed[] = {
        {"citra_is_new_3ds", "New 3DS"}, {"citra_resolution_factor", "1"},
        {"citra_layout_option", "default"}, {"citra_swap_screen", "Top"},
        {"citra_use_virtual_sd", "enabled"},
        {"citra_use_libretro_save_path", "LibRetro Default"},
        {"citra_use_cpu_jit", "enabled"}, {"citra_cpu_clock_percentage", "100"},
        {"citra_use_disk_shader_cache", "disabled"},
        {"citra_enable_mouse_touchscreen", "disabled"},
        {"citra_enable_touch_touchscreen", "disabled"},
        {"citra_analog_function", "c_stick"},
        {"citra_enable_motion", "disabled"}
    };
    if (!strcmp(key, "citra_graphics_api")) return use_gl ? "OpenGL" : "Software";
    for (size_t i = 0; i < sizeof(fixed)/sizeof(*fixed); ++i)
        if (!strcmp(key, fixed[i].key)) return fixed[i].value;
    for (size_t i = 0; i < default_count; ++i)
        if (!strcmp(key, defaults[i].key)) return defaults[i].value;
    return NULL;
}
static uintptr_t current_framebuffer(void) { return framebuffer; }
static retro_proc_address_t get_proc(const char *name) {
    return (retro_proc_address_t)eglGetProcAddress(name);
}
static void read_defaults(const struct retro_core_option_v2_definition *defs) {
    default_count = 0;
    for (const struct retro_core_option_v2_definition *d = defs; d && d->key; ++d) {
        if (default_count == 256) die("too many core options");
        defaults[default_count++] = (struct option){d->key, d->default_value ? d->default_value : d->values[0].value};
    }
}
static bool environment(unsigned command, void *data) {
    switch (command) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = system_dir; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = save_dir; return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char **)data = system_dir; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 2; return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        read_defaults(((struct retro_core_options_v2 *)data)->definitions); return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        read_defaults(((struct retro_core_options_v2_intl *)data)->us->definitions); return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data; v->value = option_value(v->key); return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixel_format = *(enum retro_pixel_format *)data; return pixel_format == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        *(int *)data = RETRO_AV_ENABLE_VIDEO | RETRO_AV_ENABLE_AUDIO; return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *(enum retro_hw_context_type *)data = RETRO_HW_CONTEXT_OPENGL_CORE; return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        struct retro_hw_render_callback *request = data;
        if (!use_gl || (request->context_type != RETRO_HW_CONTEXT_OPENGL_CORE && request->context_type != RETRO_HW_CONTEXT_OPENGL)) return false;
        request->get_current_framebuffer = current_framebuffer;
        request->get_proc_address = get_proc;
        hw = *request; hw_requested = true; return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT: return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_GEOMETRY: return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        audio_capture_rate(((const struct retro_system_av_info *)data)->timing.sample_rate);return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        fprintf(stderr, "CORE MESSAGE: %s\n", ((struct retro_message *)data)->msg); return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION: *(unsigned *)data = 1; return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        fprintf(stderr, "CORE MESSAGE: %s\n", ((struct retro_message_ext *)data)->msg); return true;
    case RETRO_ENVIRONMENT_SHUTDOWN: shutdown_requested = true; return true;
    default: return false;
    }
}
static void setup_gl(void) {
    setenv("LIBGL_ALWAYS_SOFTWARE", "true", 1);
    setenv("EGL_PLATFORM", "surfaceless", 1);
    setenv("LP_NUM_THREADS", "2", 0);
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) die("EGL surfaceless initialization failed");
    if (!eglBindAPI(EGL_OPENGL_API)) die("EGL desktop OpenGL unavailable");
    const EGLint config_attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config; EGLint count;
    if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || count < 1) die("no EGL configuration");
    const EGLint surface_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    surface = eglCreatePbufferSurface(display, config, surface_attributes);
    const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, (EGLint)hw.version_major,
        EGL_CONTEXT_MINOR_VERSION_KHR, (EGLint)hw.version_minor,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, context)) die("OpenGL core context unavailable");
    fprintf(stderr, "EGL %d.%d; GL %s; renderer %s\n", major, minor, glGetString(GL_VERSION), glGetString(GL_RENDERER));
    glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(1, &color_texture); glBindTexture(GL_TEXTURE_2D, color_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_size, render_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture, 0);
    glGenRenderbuffers(1, &depth_buffer); glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, render_size, render_size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_buffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) die("frontend framebuffer incomplete");
    if (hw.context_reset) hw.context_reset();
}
static unsigned input_mask(void) {
    if (live_seen) return live_active ? live_packet.input.mask : 0;
    unsigned mask = 0;
    for (size_t i = 0; i < event_count; ++i)
        if (frame_number >= events[i].start && frame_number - events[i].start < events[i].count) mask |= events[i].mask;
    return mask;
}
static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0) return 0;
    if (device == RETRO_DEVICE_JOYPAD) {
        unsigned mask = input_mask();
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)mask;
        return id < 16 && (mask & (1u << id)) ? 1 : 0;
    }
    if (device == RETRO_DEVICE_ANALOG && index < 2 && id < 2) {
        if (live_seen) {
            if (!live_active) return 0;
            const struct input_event *e = &live_packet.input;
            return (int16_t)(index == RETRO_DEVICE_INDEX_ANALOG_LEFT ? (id ? e->ly : e->lx) : (id ? e->ry : e->rx));
        }
        int value = 0;
        for (size_t i = 0; i < event_count; ++i) if (frame_number >= events[i].start && frame_number - events[i].start < events[i].count)
            value = index == RETRO_DEVICE_INDEX_ANALOG_LEFT ? (id ? events[i].ly : events[i].lx) : (id ? events[i].ry : events[i].rx);
        return (int16_t)value;
    }
    return 0;
}
static void audio_sample(int16_t left, int16_t right) {
    const int16_t frame[2]={left,right};atomic_fetch_add_explicit(&audio_frames,1,memory_order_relaxed);audio_capture_write(frame,1);
}
static size_t audio_batch(const int16_t *data, size_t count) {
    atomic_fetch_add_explicit(&audio_frames,count,memory_order_relaxed);audio_capture_write(data,count);return count;
}
static void save_ppm(const char *basename) {
    if (!last_rgb) return;
    char path[PATH_MAX]; snprintf(path, sizeof(path), "%s/%s", output_dir, basename);
    FILE *f = fopen(path, "wb"); if (!f) die("cannot write frame capture");
    fprintf(f, "P6\n%u %u\n255\n", last_width, last_height);
    if (fwrite(last_rgb, 3, (size_t)last_width * last_height, f) != (size_t)last_width * last_height || fclose(f)) die("capture write failed");
}
static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (!data || !width || !height) return;
    if (width > render_size || height > render_size) die("core frame exceeds bounded render target");
    video_count++;
    bool capture = frame_number % capture_every == 0 || frame_number + 1 == frame_limit;
    if (!capture && (!record_file || frame_number % record_every != 0)) return;
    size_t pixels = (size_t)width * height;
    uint8_t *rgb = realloc(last_rgb, pixels * 3); if (!rgb) die("frame allocation failed"); last_rgb = rgb;
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        uint8_t *rgba = malloc(pixels * 4); if (!rgba) die("GL readback allocation failed");
        GLint prior_read; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prior_read);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer); glReadBuffer(GL_COLOR_ATTACHMENT0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prior_read);
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
            size_t src = ((size_t)(hw.bottom_left_origin ? height - 1 - y : y) * width + x) * 4;
            memcpy(rgb + ((size_t)y * width + x) * 3, rgba + src, 3);
        }
        free(rgba);
    } else {
        if (pixel_format != RETRO_PIXEL_FORMAT_XRGB8888 || pitch < width * 4) die("unsupported software frame layout");
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
            const uint8_t *src = (const uint8_t *)data + y * pitch + x * 4;
            uint8_t *dst = rgb + ((size_t)y * width + x) * 3;
            dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
        }
    }
    last_width = width; last_height = height; record_seen_video = true;
    if (!capture) return;
    char name[64]; snprintf(name, sizeof(name), "frame-%06u.ppm", frame_number); save_ppm(name);
    fprintf(frame_log, "%u,%u,%u,%u,%04x,%d,%d,%d,%d\n", frame_number, video_count, width, height, input_mask(),
        input_state(0, RETRO_DEVICE_ANALOG, 0, 0), input_state(0, RETRO_DEVICE_ANALOG, 0, 1),
        input_state(0, RETRO_DEVICE_ANALOG, 1, 0), input_state(0, RETRO_DEVICE_ANALOG, 1, 1)); fflush(frame_log);
    fprintf(stderr, "Captured frame %u (%ux%u), input=%04x\n", frame_number, width, height, input_mask());
}
/* Record at the frontend clock, independently of sparse PNG evidence. A
 * duplicate/null video callback holds the last received screen. Only frames
 * before the core's first video output are black. No fabricated scene or
 * interpolation is inserted. The wrapper streams this into FFmpeg. */
static void record_frame(void) {
    if (!record_file || frame_number % record_every != 0) return;
    if (!last_rgb) {
        last_rgb = calloc((size_t)400 * 480, 3);
        if (!last_rgb) die("recording allocation failed");
        last_width = 400; last_height = 480;
    }
    if (!record_seen_video) ++record_initial_black;
    if (last_width != 400 || last_height != 480) die("recording requires the original 400x480 screen layout");
    if (fwrite(last_rgb, 3, (size_t)400 * 480, record_file) != (size_t)400 * 480)
        die("RGB recording write failed");
    ++record_count;
}
static void load_events(const char *path) {
    FILE *f = fopen(path, "r"); if (!f) die("cannot open event script");
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        struct input_event e; int mask;
        if (sscanf(line, "%u,%u,%i,%d,%d,%d,%d", &e.start, &e.count, &mask, &e.lx, &e.ly, &e.rx, &e.ry) != 7 ||
            event_count == 256 || !e.count || e.count > 36000 || mask < 0 || mask > 65535 ||
            e.lx < -32768 || e.lx > 32767 || e.ly < -32768 || e.ly > 32767 || e.rx < -32768 || e.rx > 32767 || e.ry < -32768 || e.ry > 32767) die("invalid input event");
        e.mask = (unsigned)mask; events[event_count++] = e;
    }
    fclose(f);
}
/* Read one atomic replacement packet per frontend frame. Once the first
 * packet is accepted, live control owns the complete pad and expiry is
 * neutral; a later offline CSV event cannot unexpectedly resume a held key. */
static const char *skip_horizontal_space(const char *p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}
static bool live_unsigned(const char **cursor, uint64_t limit, int base, uint64_t *value) {
    const char *p = skip_horizontal_space(*cursor); char *end;
    if (*p < '0' || *p > '9') return false;
    errno = 0; unsigned long long parsed = strtoull(p, &end, base);
    if (errno || end == p || parsed > limit) return false;
    *cursor = skip_horizontal_space(end); *value = (uint64_t)parsed; return true;
}
static bool live_axis(const char **cursor, int *value) {
    const char *p = skip_horizontal_space(*cursor); char *end;
    if ((*p < '0' || *p > '9') && *p != '+' && *p != '-') return false;
    errno = 0; long parsed = strtol(p, &end, 10);
    if (errno || end == p || parsed < -32768 || parsed > 32767) return false;
    *cursor = skip_horizontal_space(end); *value = (int)parsed; return true;
}
static bool live_comma(const char **cursor) {
    if (**cursor != ',') return false;
    ++*cursor; return true;
}
static bool parse_live_packet(const char *bytes, size_t length, struct live_packet *packet) {
    if (!bytes || !packet || !length || length > LIVE_PACKET_BYTES || memchr(bytes, 0, length)) return false;
    char line[LIVE_PACKET_BYTES + 1]; memcpy(line, bytes, length); line[length] = 0;
    const char *p = line; struct live_packet next = {0}; uint64_t count, mask;
    if (!live_unsigned(&p, UINT64_MAX, 10, &next.sequence) || !next.sequence || !live_comma(&p) ||
        !live_unsigned(&p, 36000, 10, &count) || !count || !live_comma(&p) ||
        !live_unsigned(&p, 65535, 0, &mask) || !live_comma(&p) ||
        !live_axis(&p, &next.input.lx) || !live_comma(&p) || !live_axis(&p, &next.input.ly) || !live_comma(&p) ||
        !live_axis(&p, &next.input.rx) || !live_comma(&p) || !live_axis(&p, &next.input.ry)) return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p) return false;
    next.input.count = (unsigned)count; next.input.mask = (unsigned)mask; *packet = next; return true;
}
static void reject_live_packet(const char *reason) {
    ++live_rejected;
    if (live_rejected <= 8) fprintf(stderr, "LIVE INPUT rejected frame=%u reason=%s\n", frame_number, reason);
}
static void poll_live_input(void) {
    if (!live_path) return;
    if (live_active && frame_number - live_packet.input.start >= live_packet.input.count) {
        live_active = false; ++live_expired;
        fprintf(stderr, "LIVE INPUT expired sequence=%" PRIu64 " frame=%u\n", live_packet.sequence, frame_number);
    }
    int fd = open(live_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { if (errno != ENOENT) reject_live_packet("open"); return; }
    struct stat entry;
    if (fstat(fd, &entry) || !S_ISREG(entry.st_mode) || entry.st_size < 1 || entry.st_size > LIVE_PACKET_BYTES) {
        close(fd); reject_live_packet("file_type_or_size"); return;
    }
    char bytes[LIVE_PACKET_BYTES + 1]; ssize_t length = read(fd, bytes, sizeof bytes); close(fd);
    struct live_packet next;
    if (length != entry.st_size || length < 1 || !parse_live_packet(bytes, (size_t)length, &next)) {
        reject_live_packet("format_or_bounds"); return;
    }
    if (next.sequence <= live_packet.sequence) return; /* No renewal by stale/repeated packets. */
    next.input.start = frame_number;
    if (fprintf(live_log, "%" PRIu64 ",%u,%u,0x%04x,%d,%d,%d,%d\n", next.sequence, frame_number,
                next.input.count, next.input.mask, next.input.lx, next.input.ly, next.input.rx, next.input.ry) < 0 || fflush(live_log))
        die("live input evidence write failed");
    live_packet = next; live_seen = live_active = true; ++live_accepted;
    fprintf(stderr, "LIVE INPUT accepted sequence=%" PRIu64 " start=%u count=%u mask=0x%04x lx=%d ly=%d rx=%d ry=%d\n",
            next.sequence, frame_number, next.input.count, next.input.mask, next.input.lx, next.input.ly, next.input.rx, next.input.ry);
}
#define LOAD(name) __typeof__(&name) p_##name = (__typeof__(&name))dlsym(core, #name); if (!p_##name) die("missing " #name)
int main(int argc, char **argv) {
    const char *content = NULL, *events_path = NULL, *state = "state", *out = "captures", *core_path = "core/azahar_libretro.so";
    unsigned deadline = 45;
    const char *record_path = NULL,*audio_path=NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--gl")) use_gl = true;
        else if (i + 1 < argc && !strcmp(argv[i], "--frames")) frame_limit = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (i + 1 < argc && !strcmp(argv[i], "--capture-every")) capture_every = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (i + 1 < argc && !strcmp(argv[i], "--audio-wav")) audio_path=argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--record-rgb")) record_path = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--record-every")) record_every = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (i + 1 < argc && !strcmp(argv[i], "--seconds")) deadline = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (i + 1 < argc && !strcmp(argv[i], "--events")) events_path = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--live-input")) live_path = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--state")) state = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--out")) out = argv[++i];
        else if (i + 1 < argc && !strcmp(argv[i], "--core")) core_path = argv[++i];
        else if (argv[i][0] != '-' && !content) content = argv[i];
        else die("usage: harness content.3dsx [--gl] [--frames N] [--seconds N] [--capture-every N] [--record-rgb FILE] [--audio-wav NEW.wav] [--record-every N] [--events input.csv] [--live-input FILE] [--state DIR] [--out DIR]");
    }
    if (!content || !frame_limit || frame_limit > 36000 || !capture_every || !record_every || record_every > 60 || !deadline || deadline > 300) die("missing content or invalid run bounds");
    if(audio_path){if(atexit(audio_capture_at_exit)||!audio_capture_open(audio_path))die("cannot exclusively create audio WAV");}
    char state_abs[PATH_MAX], out_abs[PATH_MAX], content_abs[PATH_MAX], xdg[PATH_MAX];
    make_dir(state); make_dir(out);
    if (!realpath(state, state_abs) || !realpath(out, out_abs) || !realpath(content, content_abs)) die("path resolution failed");
    snprintf(output_dir, sizeof(output_dir), "%s", out_abs);
    snprintf(system_dir, sizeof(system_dir), "%s/system", state_abs); make_dir(system_dir);
    snprintf(save_dir, sizeof(save_dir), "%s/save", state_abs); make_dir(save_dir);
    snprintf(xdg, sizeof(xdg), "%s/xdg", state_abs); make_dir(xdg);
    setenv("XDG_CONFIG_HOME", xdg, 1); setenv("XDG_DATA_HOME", xdg, 1); setenv("XDG_CACHE_HOME", xdg, 1);
    setenv("LP_NUM_THREADS", "2", 0); setenv("OMP_NUM_THREADS", "2", 0);
    if (events_path) load_events(events_path);
    char log_path[PATH_MAX]; snprintf(log_path, sizeof(log_path), "%s/frames.csv", output_dir);
    frame_log = fopen(log_path, "wx"); if (!frame_log) die("output directory already used or unwritable");
    fprintf(frame_log, "frame,video_callbacks,width,height,input_mask,left_x,left_y,right_x,right_y\n");
    if (live_path) {
        snprintf(log_path, sizeof(log_path), "%s/live-input.csv", output_dir);
        live_log = fopen(log_path, "wx"); if (!live_log) die("cannot create live input evidence");
        if (fprintf(live_log, "sequence,start,count,mask,left_x,left_y,right_x,right_y\n") < 0 || fflush(live_log))
            die("cannot initialize live input evidence");
    }
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal); signal(SIGALRM, on_signal); alarm(deadline);
    signal(SIGPIPE, SIG_IGN);
    if (record_path) {
        struct stat entry;
        if (lstat(record_path, &entry) != 0 || !S_ISFIFO(entry.st_mode))
            die("record-rgb must be an existing private FIFO");
        record_file = fopen(record_path, "wb");
        if (!record_file) die("cannot open RGB recording FIFO");
    }
    void *core = dlopen(core_path, RTLD_NOW | RTLD_LOCAL); if (!core) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    LOAD(retro_set_environment); LOAD(retro_set_video_refresh); LOAD(retro_set_audio_sample);
    LOAD(retro_set_audio_sample_batch); LOAD(retro_set_input_poll); LOAD(retro_set_input_state);
    LOAD(retro_init); LOAD(retro_deinit); LOAD(retro_get_system_info); LOAD(retro_get_system_av_info);
    LOAD(retro_load_game); LOAD(retro_unload_game); LOAD(retro_run); LOAD(retro_set_controller_port_device);
    struct retro_system_info info; p_retro_get_system_info(&info);
    fprintf(stderr, "Core %s %s; content=%s; renderer=%s; save=%s\n", info.library_name, info.library_version, content_abs, use_gl ? "OpenGL/llvmpipe" : "Azahar software", save_dir);
    p_retro_set_environment(environment); p_retro_set_video_refresh(video_refresh);
    p_retro_set_audio_sample(audio_sample); p_retro_set_audio_sample_batch(audio_batch);
    p_retro_set_input_poll(input_poll); p_retro_set_input_state(input_state);
    p_retro_init(); p_retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    struct retro_game_info game = {.path = content_abs};
    if (!p_retro_load_game(&game)) die("core rejected content");
    if (hw_requested) setup_gl();
    struct retro_system_av_info av; p_retro_get_system_av_info(&av);
    fprintf(stderr, "AV %ux%u max=%ux%u rate=%.6f audio=%.0f\n", av.geometry.base_width, av.geometry.base_height, av.geometry.max_width, av.geometry.max_height, av.timing.fps, av.timing.sample_rate);
    audio_capture_rate(av.timing.sample_rate);
    if(atomic_load(&audio_wav_failed))die("invalid or changed audio capture sample rate");
    double started = monotonic_seconds();
    if (record_file && av.timing.fps != 60.0) die("recording requires the verified 60Hz frontend clock");
    for (frame_number = 0; frame_number < frame_limit && !interrupted && !shutdown_requested && !atomic_load(&audio_wav_failed); ++frame_number) {
        poll_live_input();
        p_retro_run();
        record_frame();
    }
    double elapsed = monotonic_seconds() - started; save_ppm("last.ppm");
    fprintf(stderr, "RUN END frames=%u video=%u audio=%llu elapsed=%.3f interrupted=%d shutdown=%d\n", frame_number, video_count, (unsigned long long)atomic_load(&audio_frames), elapsed, interrupted, shutdown_requested);
    if (live_log) {
        if (fclose(live_log)) die("live input evidence close failed");
        fprintf(stderr, "LIVE INPUT END accepted=%u expired=%u rejected_polls=%u last_sequence=%" PRIu64 "\n",
                live_accepted, live_expired, live_rejected, live_packet.sequence);
    }
    if (record_file) {
        if (fclose(record_file)) die("RGB recording flush failed");
        fprintf(stderr, "RECORD END frames=%u interval=%u width=400 height=480 initial_black=%u\n",
                record_count, record_every, record_initial_black);
    }
    fclose(frame_log);
    if (hw_requested && hw.context_destroy) hw.context_destroy();
    p_retro_unload_game(); p_retro_deinit();
    bool audio_ok=audio_capture_close();
    if (display != EGL_NO_DISPLAY) {
        glDeleteFramebuffers(1, &framebuffer); glDeleteTextures(1, &color_texture); glDeleteRenderbuffers(1, &depth_buffer);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context); eglDestroySurface(display, surface); eglTerminate(display);
    }
    free(last_rgb); alarm(0);
    if(!audio_ok){fprintf(stderr,"HARNESS ERROR: audio WAV write/rate/bound failure\n");return 2;}
    if (!shutdown_requested && frame_number < frame_limit && interrupted)
        return interrupt_signal == SIGALRM ? 124 : 128 + interrupt_signal;
    return video_count ? 0 : 1;
}
