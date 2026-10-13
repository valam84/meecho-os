/*
 * Стенд mali: drm.h libdrm в ветке BSD - той, что видит машина (на хосте
 * определён __linux__, и заголовок взял бы кодировку ioctl Linux).  Сам
 * заголовок настоящий, путь даёт run.sh.
 */
#ifndef _SHIM_DRM_H
#define _SHIM_DRM_H
#pragma push_macro("__linux__")
#undef __linux__
#include REAL_DRM_H
#pragma pop_macro("__linux__")
#endif
