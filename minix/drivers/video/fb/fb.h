#ifndef __FB_H__
#define __FB_H__

#include <minix/fb.h>

int arch_fb_init(int minor, struct edid_info *info);
int arch_get_device(int minor, struct device *dev);
int arch_get_varscreeninfo(int minor, struct fb_var_screeninfo *fbvsp);
int arch_put_varscreeninfo(int minor, struct fb_var_screeninfo *fbvs_copy);
int arch_get_fixscreeninfo(int minor, struct fb_fix_screeninfo *fbfsp);
int arch_pan_display(int minor, struct fb_var_screeninfo *fbvs_copy);

/*
 * After the CPU has written len bytes at off: make them visible to the
 * display controller, which on AArch64 reads behind the data cache.
 */
void arch_fb_flush(int minor, size_t off, size_t len);

/*
 * Requests the generic part does not know; ENOTTY if the arch does not.
 * "ep" is for the grant, "user_ep" the process that asked.
 */
int arch_fb_ioctl(int minor, unsigned long request, endpoint_t ep,
	cp_grant_id_t gid, endpoint_t user_ep);

/* The device's last close: whatever a program took over is given back. */
void arch_fb_release(int minor);

/*
 * The driver's tick, ten times a second whether or not there is a console:
 * for what the arch part watches (on AArch64, a monitor plugged in or taken
 * out after bring-up).
 */
void arch_fb_tick(int minor);

#define FB_MESSAGE "Hello, world! From framebuffer!\n"
#define FB_DEV_NR 1

#endif /* __FB_H__ */
