/*
 * glcube: a turning cube through OpenGL ES 2 on the board's Mali-G52, on
 * the monitor - stage 11.6's test, in the manner of kmscube.
 *
 * EGL on Mesa's surfaceless platform finds the GPU through libdrm
 * (/dev/dri/renderD128, the mali service), Panfrost compiles the shaders
 * and the GPU draws the cube into a framebuffer object; the frame is read
 * back with glReadPixels, copied into a back buffer of /dev/fb0 and
 * flipped to.  kmscube scans a GBM surface out through KMS; MEECHO has no
 * KMS, and GBM loads its backend with dlopen(), which a static program
 * cannot - so the copy stands in for scanout.  All the drawing is the
 * GPU's.
 *
 *	glcube [-n frames] [-s size] [-o] [-q]
 *
 *	-n N	frames to draw (600: some ten seconds)
 *	-s N	the square drawn, N x N pixels, in the middle of the screen
 *		(512)
 *	-o	offscreen: no /dev/fb0, draw and check only
 *	-q	no per-frame lines
 *
 * Every check prints a line (ok/FAIL); the last line counts them:
 * the context is Panfrost's, shaders compile and link, the middle of a
 * frame is the cube and its corner the clear colour, frames apart differ
 * (it turns), glGetError() stays clean.  And frames per second.
 */

#include <sys/ioctl.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include "../../../minix/drivers/video/fb/arch/aarch64/fb_ioc.h"

static int checks, failures, quiet;

static void
check(int ok, const char *what, const char *detail)
{
	checks++;
	if (!ok)
		failures++;
	printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", what,
	    detail ? ": " : "", detail ? detail : "");
	fflush(stdout);
}

static double
now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- the cube ---- */

static const char vs_src[] =
	"attribute vec3 pos;\n"
	"attribute vec3 col;\n"
	"attribute vec3 nrm;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 rot;\n"
	"varying vec3 v;\n"
	"void main() {\n"
	"	vec3 n = normalize((rot * vec4(nrm, 0.0)).xyz);\n"
	"	float l = 0.35 + 0.65 * max(dot(n, normalize(vec3(0.4, 0.6, 1.0))), 0.0);\n"
	"	v = col * l;\n"
	"	gl_Position = mvp * vec4(pos, 1.0);\n"
	"}\n";

static const char fs_src[] =
	"precision mediump float;\n"
	"varying vec3 v;\n"
	"void main() {\n"
	"	gl_FragColor = vec4(v, 1.0);\n"
	"}\n";

/* Six faces, four corners each: position, colour, normal. */
static GLfloat cube[24][9];
static GLushort cube_idx[36];

static void
make_cube(void)
{
	static const float face_n[6][3] = {
		{ 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 },
		{ -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
	};
	static const float face_c[6][3] = {
		{ 0.95f, 0.30f, 0.20f }, { 0.20f, 0.75f, 0.35f },
		{ 0.25f, 0.45f, 0.95f }, { 0.95f, 0.80f, 0.20f },
		{ 0.80f, 0.30f, 0.85f }, { 0.20f, 0.85f, 0.85f },
	};
	int f, k, a, b, c;

	for (f = 0; f < 6; f++) {
		const float *n = face_n[f];
		/* Two axes spanning the face: u across it, w = n x u. */
		float u[3] = { 1, 0, 0 }, w[3];

		if (n[0] != 0) {
			u[0] = 0;
			u[1] = 1;
		}
		w[0] = n[1] * u[2] - n[2] * u[1];
		w[1] = n[2] * u[0] - n[0] * u[2];
		w[2] = n[0] * u[1] - n[1] * u[0];
		for (k = 0; k < 4; k++) {
			float su = (k == 0 || k == 3) ? -1 : 1;
			float sw = (k < 2) ? -1 : 1;
			GLfloat *v = cube[f * 4 + k];

			for (a = 0; a < 3; a++) {
				v[a] = n[a] + su * u[a] + sw * w[a];
				v[3 + a] = face_c[f][a];
				v[6 + a] = n[a];
			}
		}
		a = f * 4;
		b = f * 6;
		c = 0;
		cube_idx[b + c++] = a;
		cube_idx[b + c++] = a + 1;
		cube_idx[b + c++] = a + 2;
		cube_idx[b + c++] = a;
		cube_idx[b + c++] = a + 2;
		cube_idx[b + c++] = a + 3;
	}
}

/* Column-major 4x4. */
static void
mat_mul(float *r, const float *a, const float *b)
{
	float t[16];
	int i, j, k;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++) {
			t[j * 4 + i] = 0;
			for (k = 0; k < 4; k++)
				t[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
		}
	memcpy(r, t, sizeof(t));
}

static void
mat_rot(float *m, float ang, float x, float y, float z)
{
	float l = sqrtf(x * x + y * y + z * z), c = cosf(ang), s = sinf(ang);

	x /= l; y /= l; z /= l;
	memset(m, 0, 16 * sizeof(float));
	m[0] = x * x * (1 - c) + c;
	m[1] = y * x * (1 - c) + z * s;
	m[2] = x * z * (1 - c) - y * s;
	m[4] = x * y * (1 - c) - z * s;
	m[5] = y * y * (1 - c) + c;
	m[6] = y * z * (1 - c) + x * s;
	m[8] = x * z * (1 - c) + y * s;
	m[9] = y * z * (1 - c) - x * s;
	m[10] = z * z * (1 - c) + c;
	m[15] = 1;
}

static void
mat_proj(float *m, float fovy, float aspect, float zn, float zf)
{
	float f = 1.0f / tanf(fovy / 2);

	memset(m, 0, 16 * sizeof(float));
	m[0] = f / aspect;
	m[5] = f;
	m[10] = (zf + zn) / (zn - zf);
	m[11] = -1;
	m[14] = 2 * zf * zn / (zn - zf);
}

static GLuint
shader(GLenum type, const char *src, const char *what)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	char log[512] = "";

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok)
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
	check(ok, what, ok ? NULL : log);
	return s;
}

/* ---- the screen ---- */

struct screen {
	int fd;
	struct fb_map m;
	uint32_t back;
};

static int
screen_open(struct screen *sc)
{
	if ((sc->fd = open("/dev/fb0", O_RDWR)) < 0)
		return -1;
	if (ioctl(sc->fd, FBIOC_MAP, &sc->m) < 0 ||
	    sc->m.format != FB_FORMAT_XRGB8888 || sc->m.nbuf < 2) {
		close(sc->fd);
		return -1;
	}
	sc->back = (sc->m.shown + 1) % sc->m.nbuf;
	return 0;
}

static void
screen_fill(struct screen *sc, uint32_t px)
{
	uint32_t b, y, x;

	for (b = 0; b < sc->m.nbuf; b++) {
		uint32_t *p = (uint32_t *)(uintptr_t)sc->m.addr[b];

		for (y = 0; y < sc->m.height; y++)
			for (x = 0; x < sc->m.width; x++)
				p[y * (sc->m.stride / 4) + x] = px;
	}
}

/* A frame read back from GL (RGBA, bottom row first) into the back buffer,
 * in the middle, and shown. */
static void
screen_show(struct screen *sc, const uint8_t *rgba, int n)
{
	uint32_t *p = (uint32_t *)(uintptr_t)sc->m.addr[sc->back];
	uint32_t x0 = (sc->m.width - n) / 2, y0 = (sc->m.height - n) / 2;
	struct fb_flip fl;
	int x, y;

	for (y = 0; y < n; y++) {
		const uint8_t *s = rgba + (size_t)(n - 1 - y) * n * 4;
		uint32_t *d = p + (y0 + y) * (sc->m.stride / 4) + x0;

		for (x = 0; x < n; x++, s += 4)
			d[x] = (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
	}
	memset(&fl, 0, sizeof(fl));
	fl.buf = sc->back;
	fl.y0 = y0;
	fl.y1 = y0 + n;
	if (ioctl(sc->fd, FBIOC_FLIP, &fl) == 0)
		sc->back = (sc->back + 1) % sc->m.nbuf;
}

/* ---- main ---- */

static uint32_t
pixel(const uint8_t *rgba, int n, int x, int y)
{
	const uint8_t *s = rgba + ((size_t)y * n + x) * 4;

	return (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
}

static uint32_t
sum(const uint8_t *rgba, int n)
{
	uint32_t h = 2166136261u;
	size_t i;

	for (i = 0; i < (size_t)n * n * 4; i++)
		h = (h ^ rgba[i]) * 16777619u;
	return h;
}

int
main(int argc, char **argv)
{
	static const EGLint ctx_attr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
	};
	/*
	 * EGL_SURFACE_TYPE said outright: eglChooseConfig() takes it as
	 * EGL_WINDOW_BIT when it is left out, and the surfaceless platform
	 * has pbuffer configs only - none would match (on the board: EGL up
	 * on Panfrost, and no config).  The cube draws into an FBO anyway.
	 */
	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
	};
	int frames = 600, n = 512, offscreen = 0, opt, i;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLContext ctx;
	EGLint maj, min, ncfg;
	GLuint prog, tex, depth, fbo, vbo, ibo;
	GLint ok = 0, l_mvp, l_rot, e;
	struct screen sc;
	uint8_t *rgba;
	uint32_t sum0 = 0, sum1 = 0, clear_px = 0x101828;
	float proj[16], view[16], rot[16], mvp[16];
	double t0, t1;
	char buf[256];

	while ((opt = getopt(argc, argv, "n:s:oq")) != -1) {
		switch (opt) {
		case 'n': frames = atoi(optarg); break;
		case 's': n = atoi(optarg); break;
		case 'o': offscreen = 1; break;
		case 'q': quiet = 1; break;
		default:
			fprintf(stderr, "usage: glcube [-n frames] [-s size] "
			    "[-o] [-q]\n");
			return 2;
		}
	}
	if (frames < 2)
		frames = 2;

	dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
	    EGL_DEFAULT_DISPLAY, NULL);
	snprintf(buf, sizeof(buf), "0x%x", eglGetError());
	check(dpy != EGL_NO_DISPLAY, "eglGetPlatformDisplay(surfaceless)",
	    dpy == EGL_NO_DISPLAY ? buf : NULL);
	if (dpy == EGL_NO_DISPLAY)
		goto out;
	ok = eglInitialize(dpy, &maj, &min);
	snprintf(buf, sizeof(buf), ok ? "EGL %d.%d, %s" : "0x%x", ok ? maj :
	    eglGetError(), min, ok ? eglQueryString(dpy, EGL_VENDOR) : "");
	check(ok, "eglInitialize", buf);
	if (!ok)
		goto out;
	eglBindAPI(EGL_OPENGL_ES_API);
	ok = eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) && ncfg > 0;
	check(ok, "an ES2 config", NULL);
	if (!ok)
		goto out;
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	snprintf(buf, sizeof(buf), "0x%x", eglGetError());
	check(ctx != EGL_NO_CONTEXT, "eglCreateContext (ES 2)",
	    ctx == EGL_NO_CONTEXT ? buf : NULL);
	if (ctx == EGL_NO_CONTEXT)
		goto out;
	ok = eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	check(ok, "made current, no surface", NULL);
	if (!ok)
		goto out;

	printf("GL_VENDOR   %s\nGL_RENDERER %s\nGL_VERSION  %s\nGLSL        "
	    "%s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
	    glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
	check(strstr((const char *)glGetString(GL_RENDERER), "Mali") != NULL,
	    "the renderer is the Mali (not software)",
	    (const char *)glGetString(GL_RENDERER));

	/* Program. */
	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs_src,
	    "vertex shader compiled"));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs_src,
	    "fragment shader compiled"));
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "col");
	glBindAttribLocation(prog, 2, "nrm");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	check(ok, "program linked", NULL);
	glUseProgram(prog);
	l_mvp = glGetUniformLocation(prog, "mvp");
	l_rot = glGetUniformLocation(prog, "rot");

	/* Target: a texture and a depth buffer, n x n. */
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, n, n, 0, GL_RGBA,
	    GL_UNSIGNED_BYTE, NULL);
	glGenRenderbuffers(1, &depth);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, n, n);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	    GL_TEXTURE_2D, tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
	    GL_RENDERBUFFER, depth);
	check(glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
	    GL_FRAMEBUFFER_COMPLETE, "framebuffer complete", NULL);

	/* The cube. */
	make_cube();
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cube_idx), cube_idx,
	    GL_STATIC_DRAW);
	for (i = 0; i < 3; i++) {
		glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE,
		    9 * sizeof(GLfloat), (void *)(uintptr_t)(i * 3 *
		    sizeof(GLfloat)));
		glEnableVertexAttribArray(i);
	}
	glViewport(0, 0, n, n);
	glEnable(GL_DEPTH_TEST);
	glClearColor(((clear_px >> 16) & 0xff) / 255.0f,
	    ((clear_px >> 8) & 0xff) / 255.0f, (clear_px & 0xff) / 255.0f,
	    1.0f);
	mat_proj(proj, 0.8f, 1.0f, 1.0f, 20.0f);
	memset(view, 0, sizeof(view));
	view[0] = view[5] = view[10] = view[15] = 1;
	view[14] = -6.0f;

	rgba = malloc((size_t)n * n * 4);
	if (rgba == NULL)
		goto out;
	if (!offscreen) {
		check(screen_open(&sc) == 0, "/dev/fb0 mapped", NULL);
		if (failures)
			goto out;
		screen_fill(&sc, 0x000000);
		printf("screen      %ux%u, %u buffers\n", sc.m.width,
		    sc.m.height, sc.m.nbuf);
	}

	t0 = now();
	for (i = 0; i < frames; i++) {
		float m[16];

		mat_rot(rot, i * 0.03f, 0.6f, 1.0f, 0.2f);
		mat_mul(m, view, rot);
		mat_mul(mvp, proj, m);
		glUniformMatrix4fv(l_mvp, 1, GL_FALSE, mvp);
		glUniformMatrix4fv(l_rot, 1, GL_FALSE, rot);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, 0);
		glReadPixels(0, 0, n, n, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		if (i == 0) {
			uint32_t mid = pixel(rgba, n, n / 2, n / 2);
			uint32_t corner = pixel(rgba, n, 1, 1);

			snprintf(buf, sizeof(buf), "0x%06x", mid);
			check(mid != clear_px && mid != 0,
			    "the middle of the first frame is the cube", buf);
			snprintf(buf, sizeof(buf), "0x%06x", corner);
			check(corner == clear_px,
			    "its corner is the clear colour", buf);
			sum0 = sum(rgba, n);
		} else if (i == 20)
			sum1 = sum(rgba, n);
		if (!offscreen)
			screen_show(&sc, rgba, n);
		if (!quiet && i % 100 == 0)
			printf("frame %d\n", i);
	}
	t1 = now();
	check(sum1 != 0 && sum0 != sum1, "frames 0 and 20 differ: it turns",
	    NULL);
	e = glGetError();
	snprintf(buf, sizeof(buf), "0x%x", e);
	check(e == GL_NO_ERROR, "glGetError() clean", buf);
	printf("GLCUBE %d frames %dx%d in %.2f s: %.1f frames/s\n", frames, n,
	    n, t1 - t0, frames / (t1 - t0));
	free(rgba);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
out:
	printf("GLCUBE %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
