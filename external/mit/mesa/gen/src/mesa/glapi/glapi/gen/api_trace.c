/* DO NOT EDIT - This file generated automatically by api_trace_c.py script */

/*
 * Copyright (C) 2026 Christian Gmeiner
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * Christian Gmeiner,
 * AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <inttypes.h>
#include <stdio.h>

#include "glapi/glapi/glapi.h"
#include "main/api_trace_helpers.h"
#include "main/context.h"
#include "main/enums.h"
#include "main/errors.h"
#include "dispatch.h"

static void GLAPIENTRY
_mesa_trace_NewList(GLuint list, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNewList(%u, %s)\n", list, _mesa_enum_to_string(mode));
   CALL_NewList(ctx->Dispatch.RealPublished, (list, mode));
}

static void GLAPIENTRY
_mesa_trace_EndList(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEndList()\n");
   CALL_EndList(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_CallList(GLuint list)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCallList(%u)\n", list);
   CALL_CallList(ctx->Dispatch.RealPublished, (list));
}

static void GLAPIENTRY
_mesa_trace_CallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCallLists(%d, %s, %p)\n", n, _mesa_enum_to_string(type), (void *)lists);
   CALL_CallLists(ctx->Dispatch.RealPublished, (n, type, lists));
}

static void GLAPIENTRY
_mesa_trace_DeleteLists(GLuint list, GLsizei range)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDeleteLists(%u, %d)\n", list, range);
   CALL_DeleteLists(ctx->Dispatch.RealPublished, (list, range));
}

static GLuint GLAPIENTRY
_mesa_trace_GenLists(GLsizei range)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenLists(%d)\n", range);
   return CALL_GenLists(ctx->Dispatch.RealPublished, (range));
}

static void GLAPIENTRY
_mesa_trace_ListBase(GLuint base)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glListBase(%u)\n", base);
   CALL_ListBase(ctx->Dispatch.RealPublished, (base));
}

static void GLAPIENTRY
_mesa_trace_Begin(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBegin(%s)\n", _mesa_enum_to_string(mode));
   CALL_Begin(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_Bitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBitmap(%d, %d, %f, %f, %f, %f, %p)\n", width, height, xorig, yorig, xmove, ymove, (void *)bitmap);
   CALL_Bitmap(ctx->Dispatch.RealPublished, (width, height, xorig, yorig, xmove, ymove, bitmap));
}

static void GLAPIENTRY
_mesa_trace_Color3b(GLbyte red, GLbyte green, GLbyte blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor3b(%d, %d, %d)\n", red, green, blue);
   CALL_Color3b(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_Color3bv(const GLbyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_BYTE);
   _mesa_debug(ctx, "glColor3bv(%s)\n", v_buf);
   CALL_Color3bv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color3d(GLdouble red, GLdouble green, GLdouble blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor3d(%f, %f, %f)\n", red, green, blue);
   CALL_Color3d(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_Color3dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glColor3dv(%s)\n", v_buf);
   CALL_Color3dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color3f(GLfloat red, GLfloat green, GLfloat blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor3f(%f, %f, %f)\n", red, green, blue);
   CALL_Color3f(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_Color3fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glColor3fv(%s)\n", v_buf);
   CALL_Color3fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color3i(GLint red, GLint green, GLint blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor3i(%d, %d, %d)\n", red, green, blue);
   CALL_Color3i(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_Color3iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glColor3iv(%s)\n", v_buf);
   CALL_Color3iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color3s(GLshort red, GLshort green, GLshort blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor3s(%d, %d, %d)\n", red, green, blue);
   CALL_Color3s(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_Color3sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glColor3sv(%s)\n", v_buf);
   CALL_Color3sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color3ub(GLubyte red, GLubyte green, GLubyte blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor3ub(%u, %u, %u)\n", red, green, blue);
   CALL_Color3ub(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_Color3ubv(const GLubyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_UBYTE);
   _mesa_debug(ctx, "glColor3ubv(%s)\n", v_buf);
   CALL_Color3ubv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color3ui(GLuint red, GLuint green, GLuint blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor3ui(%u, %u, %u)\n", red, green, blue);
   CALL_Color3ui(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_Color3uiv(const GLuint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glColor3uiv(%s)\n", v_buf);
   CALL_Color3uiv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color3us(GLushort red, GLushort green, GLushort blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor3us(%u, %u, %u)\n", red, green, blue);
   CALL_Color3us(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_Color3usv(const GLushort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_USHORT);
   _mesa_debug(ctx, "glColor3usv(%s)\n", v_buf);
   CALL_Color3usv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4b(%d, %d, %d, %d)\n", red, green, blue, alpha);
   CALL_Color4b(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_Color4bv(const GLbyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_BYTE);
   _mesa_debug(ctx, "glColor4bv(%s)\n", v_buf);
   CALL_Color4bv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4d(%f, %f, %f, %f)\n", red, green, blue, alpha);
   CALL_Color4d(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_Color4dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glColor4dv(%s)\n", v_buf);
   CALL_Color4dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4f(%f, %f, %f, %f)\n", red, green, blue, alpha);
   CALL_Color4f(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_Color4fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glColor4fv(%s)\n", v_buf);
   CALL_Color4fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color4i(GLint red, GLint green, GLint blue, GLint alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4i(%d, %d, %d, %d)\n", red, green, blue, alpha);
   CALL_Color4i(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_Color4iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glColor4iv(%s)\n", v_buf);
   CALL_Color4iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color4s(GLshort red, GLshort green, GLshort blue, GLshort alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4s(%d, %d, %d, %d)\n", red, green, blue, alpha);
   CALL_Color4s(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_Color4sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glColor4sv(%s)\n", v_buf);
   CALL_Color4sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4ub(%u, %u, %u, %u)\n", red, green, blue, alpha);
   CALL_Color4ub(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_Color4ubv(const GLubyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_UBYTE);
   _mesa_debug(ctx, "glColor4ubv(%s)\n", v_buf);
   CALL_Color4ubv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4ui(%u, %u, %u, %u)\n", red, green, blue, alpha);
   CALL_Color4ui(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_Color4uiv(const GLuint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glColor4uiv(%s)\n", v_buf);
   CALL_Color4uiv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color4us(GLushort red, GLushort green, GLushort blue, GLushort alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4us(%u, %u, %u, %u)\n", red, green, blue, alpha);
   CALL_Color4us(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_Color4usv(const GLushort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_USHORT);
   _mesa_debug(ctx, "glColor4usv(%s)\n", v_buf);
   CALL_Color4usv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_EdgeFlag(GLboolean flag)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEdgeFlag(%s)\n", flag ? "GL_TRUE" : "GL_FALSE");
   CALL_EdgeFlag(ctx->Dispatch.RealPublished, (flag));
}

static void GLAPIENTRY
_mesa_trace_EdgeFlagv(const GLboolean *flag)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEdgeFlagv(%p)\n", (void *)flag);
   CALL_EdgeFlagv(ctx->Dispatch.RealPublished, (flag));
}

static void GLAPIENTRY
_mesa_trace_End(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEnd()\n");
   CALL_End(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_Indexd(GLdouble c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexd(%f)\n", c);
   CALL_Indexd(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_Indexdv(const GLdouble *c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexdv(%p)\n", (void *)c);
   CALL_Indexdv(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_Indexf(GLfloat c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexf(%f)\n", c);
   CALL_Indexf(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_Indexfv(const GLfloat *c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexfv(%p)\n", (void *)c);
   CALL_Indexfv(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_Indexi(GLint c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexi(%d)\n", c);
   CALL_Indexi(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_Indexiv(const GLint *c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexiv(%p)\n", (void *)c);
   CALL_Indexiv(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_Indexs(GLshort c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexs(%d)\n", c);
   CALL_Indexs(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_Indexsv(const GLshort *c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexsv(%p)\n", (void *)c);
   CALL_Indexsv(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_Normal3b(GLbyte nx, GLbyte ny, GLbyte nz)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormal3b(%d, %d, %d)\n", nx, ny, nz);
   CALL_Normal3b(ctx->Dispatch.RealPublished, (nx, ny, nz));
}

static void GLAPIENTRY
_mesa_trace_Normal3bv(const GLbyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_BYTE);
   _mesa_debug(ctx, "glNormal3bv(%s)\n", v_buf);
   CALL_Normal3bv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Normal3d(GLdouble nx, GLdouble ny, GLdouble nz)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormal3d(%f, %f, %f)\n", nx, ny, nz);
   CALL_Normal3d(ctx->Dispatch.RealPublished, (nx, ny, nz));
}

static void GLAPIENTRY
_mesa_trace_Normal3dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glNormal3dv(%s)\n", v_buf);
   CALL_Normal3dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Normal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormal3f(%f, %f, %f)\n", nx, ny, nz);
   CALL_Normal3f(ctx->Dispatch.RealPublished, (nx, ny, nz));
}

static void GLAPIENTRY
_mesa_trace_Normal3fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glNormal3fv(%s)\n", v_buf);
   CALL_Normal3fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Normal3i(GLint nx, GLint ny, GLint nz)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormal3i(%d, %d, %d)\n", nx, ny, nz);
   CALL_Normal3i(ctx->Dispatch.RealPublished, (nx, ny, nz));
}

static void GLAPIENTRY
_mesa_trace_Normal3iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glNormal3iv(%s)\n", v_buf);
   CALL_Normal3iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Normal3s(GLshort nx, GLshort ny, GLshort nz)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormal3s(%d, %d, %d)\n", nx, ny, nz);
   CALL_Normal3s(ctx->Dispatch.RealPublished, (nx, ny, nz));
}

static void GLAPIENTRY
_mesa_trace_Normal3sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glNormal3sv(%s)\n", v_buf);
   CALL_Normal3sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos2d(GLdouble x, GLdouble y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos2d(%f, %f)\n", x, y);
   CALL_RasterPos2d(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_RasterPos2dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glRasterPos2dv(%s)\n", v_buf);
   CALL_RasterPos2dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos2f(GLfloat x, GLfloat y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos2f(%f, %f)\n", x, y);
   CALL_RasterPos2f(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_RasterPos2fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glRasterPos2fv(%s)\n", v_buf);
   CALL_RasterPos2fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos2i(GLint x, GLint y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos2i(%d, %d)\n", x, y);
   CALL_RasterPos2i(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_RasterPos2iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glRasterPos2iv(%s)\n", v_buf);
   CALL_RasterPos2iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos2s(GLshort x, GLshort y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos2s(%d, %d)\n", x, y);
   CALL_RasterPos2s(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_RasterPos2sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glRasterPos2sv(%s)\n", v_buf);
   CALL_RasterPos2sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos3d(GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos3d(%f, %f, %f)\n", x, y, z);
   CALL_RasterPos3d(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_RasterPos3dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glRasterPos3dv(%s)\n", v_buf);
   CALL_RasterPos3dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos3f(GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos3f(%f, %f, %f)\n", x, y, z);
   CALL_RasterPos3f(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_RasterPos3fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glRasterPos3fv(%s)\n", v_buf);
   CALL_RasterPos3fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos3i(GLint x, GLint y, GLint z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos3i(%d, %d, %d)\n", x, y, z);
   CALL_RasterPos3i(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_RasterPos3iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glRasterPos3iv(%s)\n", v_buf);
   CALL_RasterPos3iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos3s(GLshort x, GLshort y, GLshort z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos3s(%d, %d, %d)\n", x, y, z);
   CALL_RasterPos3s(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_RasterPos3sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glRasterPos3sv(%s)\n", v_buf);
   CALL_RasterPos3sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos4d(%f, %f, %f, %f)\n", x, y, z, w);
   CALL_RasterPos4d(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_RasterPos4dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glRasterPos4dv(%s)\n", v_buf);
   CALL_RasterPos4dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos4f(%f, %f, %f, %f)\n", x, y, z, w);
   CALL_RasterPos4f(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_RasterPos4fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glRasterPos4fv(%s)\n", v_buf);
   CALL_RasterPos4fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos4i(GLint x, GLint y, GLint z, GLint w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos4i(%d, %d, %d, %d)\n", x, y, z, w);
   CALL_RasterPos4i(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_RasterPos4iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glRasterPos4iv(%s)\n", v_buf);
   CALL_RasterPos4iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_RasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRasterPos4s(%d, %d, %d, %d)\n", x, y, z, w);
   CALL_RasterPos4s(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_RasterPos4sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glRasterPos4sv(%s)\n", v_buf);
   CALL_RasterPos4sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Rectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRectd(%f, %f, %f, %f)\n", x1, y1, x2, y2);
   CALL_Rectd(ctx->Dispatch.RealPublished, (x1, y1, x2, y2));
}

static void GLAPIENTRY
_mesa_trace_Rectdv(const GLdouble *v1, const GLdouble *v2)
{
   GET_CURRENT_CONTEXT(ctx);
   char v1_buf[512];
   _mesa_trace_format_array(v1_buf, sizeof(v1_buf), v1, 2, MESA_TRACE_ELEM_DOUBLE);
   char v2_buf[512];
   _mesa_trace_format_array(v2_buf, sizeof(v2_buf), v2, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glRectdv(%s, %s)\n", v1_buf, v2_buf);
   CALL_Rectdv(ctx->Dispatch.RealPublished, (v1, v2));
}

static void GLAPIENTRY
_mesa_trace_Rectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRectf(%f, %f, %f, %f)\n", x1, y1, x2, y2);
   CALL_Rectf(ctx->Dispatch.RealPublished, (x1, y1, x2, y2));
}

static void GLAPIENTRY
_mesa_trace_Rectfv(const GLfloat *v1, const GLfloat *v2)
{
   GET_CURRENT_CONTEXT(ctx);
   char v1_buf[512];
   _mesa_trace_format_array(v1_buf, sizeof(v1_buf), v1, 2, MESA_TRACE_ELEM_FLOAT);
   char v2_buf[512];
   _mesa_trace_format_array(v2_buf, sizeof(v2_buf), v2, 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glRectfv(%s, %s)\n", v1_buf, v2_buf);
   CALL_Rectfv(ctx->Dispatch.RealPublished, (v1, v2));
}

static void GLAPIENTRY
_mesa_trace_Recti(GLint x1, GLint y1, GLint x2, GLint y2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRecti(%d, %d, %d, %d)\n", x1, y1, x2, y2);
   CALL_Recti(ctx->Dispatch.RealPublished, (x1, y1, x2, y2));
}

static void GLAPIENTRY
_mesa_trace_Rectiv(const GLint *v1, const GLint *v2)
{
   GET_CURRENT_CONTEXT(ctx);
   char v1_buf[512];
   _mesa_trace_format_array(v1_buf, sizeof(v1_buf), v1, 2, MESA_TRACE_ELEM_INT);
   char v2_buf[512];
   _mesa_trace_format_array(v2_buf, sizeof(v2_buf), v2, 2, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glRectiv(%s, %s)\n", v1_buf, v2_buf);
   CALL_Rectiv(ctx->Dispatch.RealPublished, (v1, v2));
}

static void GLAPIENTRY
_mesa_trace_Rects(GLshort x1, GLshort y1, GLshort x2, GLshort y2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRects(%d, %d, %d, %d)\n", x1, y1, x2, y2);
   CALL_Rects(ctx->Dispatch.RealPublished, (x1, y1, x2, y2));
}

static void GLAPIENTRY
_mesa_trace_Rectsv(const GLshort *v1, const GLshort *v2)
{
   GET_CURRENT_CONTEXT(ctx);
   char v1_buf[512];
   _mesa_trace_format_array(v1_buf, sizeof(v1_buf), v1, 2, MESA_TRACE_ELEM_SHORT);
   char v2_buf[512];
   _mesa_trace_format_array(v2_buf, sizeof(v2_buf), v2, 2, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glRectsv(%s, %s)\n", v1_buf, v2_buf);
   CALL_Rectsv(ctx->Dispatch.RealPublished, (v1, v2));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1d(GLdouble s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1d(%f)\n", s);
   CALL_TexCoord1d(ctx->Dispatch.RealPublished, (s));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1dv(%p)\n", (void *)v);
   CALL_TexCoord1dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1f(GLfloat s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1f(%f)\n", s);
   CALL_TexCoord1f(ctx->Dispatch.RealPublished, (s));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1fv(%p)\n", (void *)v);
   CALL_TexCoord1fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1i(GLint s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1i(%d)\n", s);
   CALL_TexCoord1i(ctx->Dispatch.RealPublished, (s));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1iv(%p)\n", (void *)v);
   CALL_TexCoord1iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1s(GLshort s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1s(%d)\n", s);
   CALL_TexCoord1s(ctx->Dispatch.RealPublished, (s));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1sv(%p)\n", (void *)v);
   CALL_TexCoord1sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2d(GLdouble s, GLdouble t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord2d(%f, %f)\n", s, t);
   CALL_TexCoord2d(ctx->Dispatch.RealPublished, (s, t));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glTexCoord2dv(%s)\n", v_buf);
   CALL_TexCoord2dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2f(GLfloat s, GLfloat t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord2f(%f, %f)\n", s, t);
   CALL_TexCoord2f(ctx->Dispatch.RealPublished, (s, t));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glTexCoord2fv(%s)\n", v_buf);
   CALL_TexCoord2fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2i(GLint s, GLint t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord2i(%d, %d)\n", s, t);
   CALL_TexCoord2i(ctx->Dispatch.RealPublished, (s, t));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glTexCoord2iv(%s)\n", v_buf);
   CALL_TexCoord2iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2s(GLshort s, GLshort t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord2s(%d, %d)\n", s, t);
   CALL_TexCoord2s(ctx->Dispatch.RealPublished, (s, t));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glTexCoord2sv(%s)\n", v_buf);
   CALL_TexCoord2sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3d(GLdouble s, GLdouble t, GLdouble r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord3d(%f, %f, %f)\n", s, t, r);
   CALL_TexCoord3d(ctx->Dispatch.RealPublished, (s, t, r));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glTexCoord3dv(%s)\n", v_buf);
   CALL_TexCoord3dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3f(GLfloat s, GLfloat t, GLfloat r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord3f(%f, %f, %f)\n", s, t, r);
   CALL_TexCoord3f(ctx->Dispatch.RealPublished, (s, t, r));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glTexCoord3fv(%s)\n", v_buf);
   CALL_TexCoord3fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3i(GLint s, GLint t, GLint r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord3i(%d, %d, %d)\n", s, t, r);
   CALL_TexCoord3i(ctx->Dispatch.RealPublished, (s, t, r));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glTexCoord3iv(%s)\n", v_buf);
   CALL_TexCoord3iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3s(GLshort s, GLshort t, GLshort r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord3s(%d, %d, %d)\n", s, t, r);
   CALL_TexCoord3s(ctx->Dispatch.RealPublished, (s, t, r));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glTexCoord3sv(%s)\n", v_buf);
   CALL_TexCoord3sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord4d(%f, %f, %f, %f)\n", s, t, r, q);
   CALL_TexCoord4d(ctx->Dispatch.RealPublished, (s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glTexCoord4dv(%s)\n", v_buf);
   CALL_TexCoord4dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord4f(%f, %f, %f, %f)\n", s, t, r, q);
   CALL_TexCoord4f(ctx->Dispatch.RealPublished, (s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glTexCoord4fv(%s)\n", v_buf);
   CALL_TexCoord4fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4i(GLint s, GLint t, GLint r, GLint q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord4i(%d, %d, %d, %d)\n", s, t, r, q);
   CALL_TexCoord4i(ctx->Dispatch.RealPublished, (s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glTexCoord4iv(%s)\n", v_buf);
   CALL_TexCoord4iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord4s(%d, %d, %d, %d)\n", s, t, r, q);
   CALL_TexCoord4s(ctx->Dispatch.RealPublished, (s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glTexCoord4sv(%s)\n", v_buf);
   CALL_TexCoord4sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex2d(GLdouble x, GLdouble y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex2d(%f, %f)\n", x, y);
   CALL_Vertex2d(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_Vertex2dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertex2dv(%s)\n", v_buf);
   CALL_Vertex2dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex2f(GLfloat x, GLfloat y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex2f(%f, %f)\n", x, y);
   CALL_Vertex2f(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_Vertex2fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertex2fv(%s)\n", v_buf);
   CALL_Vertex2fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex2i(GLint x, GLint y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex2i(%d, %d)\n", x, y);
   CALL_Vertex2i(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_Vertex2iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glVertex2iv(%s)\n", v_buf);
   CALL_Vertex2iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex2s(GLshort x, GLshort y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex2s(%d, %d)\n", x, y);
   CALL_Vertex2s(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_Vertex2sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertex2sv(%s)\n", v_buf);
   CALL_Vertex2sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex3d(GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex3d(%f, %f, %f)\n", x, y, z);
   CALL_Vertex3d(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Vertex3dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertex3dv(%s)\n", v_buf);
   CALL_Vertex3dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex3f(%f, %f, %f)\n", x, y, z);
   CALL_Vertex3f(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Vertex3fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertex3fv(%s)\n", v_buf);
   CALL_Vertex3fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex3i(GLint x, GLint y, GLint z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex3i(%d, %d, %d)\n", x, y, z);
   CALL_Vertex3i(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Vertex3iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glVertex3iv(%s)\n", v_buf);
   CALL_Vertex3iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex3s(GLshort x, GLshort y, GLshort z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex3s(%d, %d, %d)\n", x, y, z);
   CALL_Vertex3s(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Vertex3sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertex3sv(%s)\n", v_buf);
   CALL_Vertex3sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex4d(%f, %f, %f, %f)\n", x, y, z, w);
   CALL_Vertex4d(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_Vertex4dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertex4dv(%s)\n", v_buf);
   CALL_Vertex4dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex4f(%f, %f, %f, %f)\n", x, y, z, w);
   CALL_Vertex4f(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_Vertex4fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertex4fv(%s)\n", v_buf);
   CALL_Vertex4fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex4i(GLint x, GLint y, GLint z, GLint w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex4i(%d, %d, %d, %d)\n", x, y, z, w);
   CALL_Vertex4i(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_Vertex4iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glVertex4iv(%s)\n", v_buf);
   CALL_Vertex4iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex4s(GLshort x, GLshort y, GLshort z, GLshort w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex4s(%d, %d, %d, %d)\n", x, y, z, w);
   CALL_Vertex4s(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_Vertex4sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertex4sv(%s)\n", v_buf);
   CALL_Vertex4sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_ClipPlane(GLenum plane, const GLdouble *equation)
{
   GET_CURRENT_CONTEXT(ctx);
   char equation_buf[512];
   _mesa_trace_format_array(equation_buf, sizeof(equation_buf), equation, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glClipPlane(%s, %s)\n", _mesa_enum_to_string(plane), equation_buf);
   CALL_ClipPlane(ctx->Dispatch.RealPublished, (plane, equation));
}

static void GLAPIENTRY
_mesa_trace_ColorMaterial(GLenum face, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorMaterial(%s, %s)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(mode));
   CALL_ColorMaterial(ctx->Dispatch.RealPublished, (face, mode));
}

static void GLAPIENTRY
_mesa_trace_CullFace(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCullFace(%s)\n", _mesa_enum_to_string(mode));
   CALL_CullFace(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_Fogf(GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogf(%s, %f)\n", _mesa_enum_to_string(pname), param);
   CALL_Fogf(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_Fogfv(GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogfv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_Fogfv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_Fogi(GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogi(%s, %d)\n", _mesa_enum_to_string(pname), param);
   CALL_Fogi(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_Fogiv(GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogiv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_Fogiv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_FrontFace(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFrontFace(%s)\n", _mesa_enum_to_string(mode));
   CALL_FrontFace(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_Hint(GLenum target, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glHint(%s, %s)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(mode));
   CALL_Hint(ctx->Dispatch.RealPublished, (target, mode));
}

static void GLAPIENTRY
_mesa_trace_Lightf(GLenum light, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightf(%s, %s, %f)\n", _mesa_enum_to_string(light), _mesa_enum_to_string(pname), param);
   CALL_Lightf(ctx->Dispatch.RealPublished, (light, pname, param));
}

static void GLAPIENTRY
_mesa_trace_Lightfv(GLenum light, GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightfv(%s, %s, %p)\n", _mesa_enum_to_string(light), _mesa_enum_to_string(pname), (void *)params);
   CALL_Lightfv(ctx->Dispatch.RealPublished, (light, pname, params));
}

static void GLAPIENTRY
_mesa_trace_Lighti(GLenum light, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLighti(%s, %s, %d)\n", _mesa_enum_to_string(light), _mesa_enum_to_string(pname), param);
   CALL_Lighti(ctx->Dispatch.RealPublished, (light, pname, param));
}

static void GLAPIENTRY
_mesa_trace_Lightiv(GLenum light, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightiv(%s, %s, %p)\n", _mesa_enum_to_string(light), _mesa_enum_to_string(pname), (void *)params);
   CALL_Lightiv(ctx->Dispatch.RealPublished, (light, pname, params));
}

static void GLAPIENTRY
_mesa_trace_LightModelf(GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightModelf(%s, %f)\n", _mesa_enum_to_string(pname), param);
   CALL_LightModelf(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_LightModelfv(GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightModelfv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_LightModelfv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_LightModeli(GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightModeli(%s, %d)\n", _mesa_enum_to_string(pname), param);
   CALL_LightModeli(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_LightModeliv(GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightModeliv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_LightModeliv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_LineStipple(GLint factor, GLushort pattern)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLineStipple(%d, %u)\n", factor, pattern);
   CALL_LineStipple(ctx->Dispatch.RealPublished, (factor, pattern));
}

static void GLAPIENTRY
_mesa_trace_LineWidth(GLfloat width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLineWidth(%f)\n", width);
   CALL_LineWidth(ctx->Dispatch.RealPublished, (width));
}

static void GLAPIENTRY
_mesa_trace_Materialf(GLenum face, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMaterialf(%s, %s, %f)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(pname), param);
   CALL_Materialf(ctx->Dispatch.RealPublished, (face, pname, param));
}

static void GLAPIENTRY
_mesa_trace_Materialfv(GLenum face, GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMaterialfv(%s, %s, %p)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(pname), (void *)params);
   CALL_Materialfv(ctx->Dispatch.RealPublished, (face, pname, params));
}

static void GLAPIENTRY
_mesa_trace_Materiali(GLenum face, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMateriali(%s, %s, %d)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(pname), param);
   CALL_Materiali(ctx->Dispatch.RealPublished, (face, pname, param));
}

static void GLAPIENTRY
_mesa_trace_Materialiv(GLenum face, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMaterialiv(%s, %s, %p)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(pname), (void *)params);
   CALL_Materialiv(ctx->Dispatch.RealPublished, (face, pname, params));
}

static void GLAPIENTRY
_mesa_trace_PointSize(GLfloat size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPointSize(%f)\n", size);
   CALL_PointSize(ctx->Dispatch.RealPublished, (size));
}

static void GLAPIENTRY
_mesa_trace_PolygonMode(GLenum face, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPolygonMode(%s, %s)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(mode));
   CALL_PolygonMode(ctx->Dispatch.RealPublished, (face, mode));
}

static void GLAPIENTRY
_mesa_trace_PolygonStipple(const GLubyte *mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPolygonStipple(%p)\n", (void *)mask);
   CALL_PolygonStipple(ctx->Dispatch.RealPublished, (mask));
}

static void GLAPIENTRY
_mesa_trace_Scissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glScissor(%d, %d, %d, %d)\n", x, y, width, height);
   CALL_Scissor(ctx->Dispatch.RealPublished, (x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_ShadeModel(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glShadeModel(%s)\n", _mesa_enum_to_string(mode));
   CALL_ShadeModel(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_TexParameterf(GLenum target, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexParameterf(%s, %s, %f)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_TexParameterf(ctx->Dispatch.RealPublished, (target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexParameterfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexParameterfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexParameteri(GLenum target, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexParameteri(%s, %s, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_TexParameteri(ctx->Dispatch.RealPublished, (target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexImage1D(%s, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), level, internalformat, width, border, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TexImage1D(ctx->Dispatch.RealPublished, (target, level, internalformat, width, border, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexImage2D(%s, %d, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), level, internalformat, width, height, border, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TexImage2D(ctx->Dispatch.RealPublished, (target, level, internalformat, width, height, border, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TexEnvf(GLenum target, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexEnvf(%s, %s, %f)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_TexEnvf(ctx->Dispatch.RealPublished, (target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexEnvfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexEnvfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexEnvi(GLenum target, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexEnvi(%s, %s, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_TexEnvi(ctx->Dispatch.RealPublished, (target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TexEnviv(GLenum target, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexEnviv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexEnviv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexGend(GLenum coord, GLenum pname, GLdouble param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexGend(%s, %s, %f)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), param);
   CALL_TexGend(ctx->Dispatch.RealPublished, (coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TexGendv(GLenum coord, GLenum pname, const GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexGendv(%s, %s, %p)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexGendv(ctx->Dispatch.RealPublished, (coord, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexGenf(GLenum coord, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexGenf(%s, %s, %f)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), param);
   CALL_TexGenf(ctx->Dispatch.RealPublished, (coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexGenfv(%s, %s, %p)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexGenfv(ctx->Dispatch.RealPublished, (coord, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexGeni(GLenum coord, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexGeni(%s, %s, %d)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), param);
   CALL_TexGeni(ctx->Dispatch.RealPublished, (coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TexGeniv(GLenum coord, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexGeniv(%s, %s, %p)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexGeniv(ctx->Dispatch.RealPublished, (coord, pname, params));
}

static void GLAPIENTRY
_mesa_trace_FeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFeedbackBuffer(%d, %s, %p)\n", size, _mesa_enum_to_string(type), (void *)buffer);
   CALL_FeedbackBuffer(ctx->Dispatch.RealPublished, (size, type, buffer));
}

static void GLAPIENTRY
_mesa_trace_SelectBuffer(GLsizei size, GLuint *buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSelectBuffer(%d, %p)\n", size, (void *)buffer);
   CALL_SelectBuffer(ctx->Dispatch.RealPublished, (size, buffer));
}

static GLint GLAPIENTRY
_mesa_trace_RenderMode(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRenderMode(%s)\n", _mesa_enum_to_string(mode));
   return CALL_RenderMode(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_InitNames(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInitNames()\n");
   CALL_InitNames(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_LoadName(GLuint name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLoadName(%u)\n", name);
   CALL_LoadName(ctx->Dispatch.RealPublished, (name));
}

static void GLAPIENTRY
_mesa_trace_PassThrough(GLfloat token)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPassThrough(%f)\n", token);
   CALL_PassThrough(ctx->Dispatch.RealPublished, (token));
}

static void GLAPIENTRY
_mesa_trace_PopName(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPopName()\n");
   CALL_PopName(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_PushName(GLuint name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPushName(%u)\n", name);
   CALL_PushName(ctx->Dispatch.RealPublished, (name));
}

static void GLAPIENTRY
_mesa_trace_DrawBuffer(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawBuffer(%s)\n", _mesa_enum_to_string(mode));
   CALL_DrawBuffer(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_Clear(GLbitfield mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClear(0x%x)\n", mask);
   CALL_Clear(ctx->Dispatch.RealPublished, (mask));
}

static void GLAPIENTRY
_mesa_trace_ClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearAccum(%f, %f, %f, %f)\n", red, green, blue, alpha);
   CALL_ClearAccum(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_ClearIndex(GLfloat c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearIndex(%f)\n", c);
   CALL_ClearIndex(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_ClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearColor(%f, %f, %f, %f)\n", red, green, blue, alpha);
   CALL_ClearColor(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_ClearStencil(GLint s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearStencil(%d)\n", s);
   CALL_ClearStencil(ctx->Dispatch.RealPublished, (s));
}

static void GLAPIENTRY
_mesa_trace_ClearDepth(GLclampd depth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearDepth(%f)\n", depth);
   CALL_ClearDepth(ctx->Dispatch.RealPublished, (depth));
}

static void GLAPIENTRY
_mesa_trace_StencilMask(GLuint mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glStencilMask(%u)\n", mask);
   CALL_StencilMask(ctx->Dispatch.RealPublished, (mask));
}

static void GLAPIENTRY
_mesa_trace_ColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorMask(%s, %s, %s, %s)\n", red ? "GL_TRUE" : "GL_FALSE", green ? "GL_TRUE" : "GL_FALSE", blue ? "GL_TRUE" : "GL_FALSE", alpha ? "GL_TRUE" : "GL_FALSE");
   CALL_ColorMask(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_DepthMask(GLboolean flag)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDepthMask(%s)\n", flag ? "GL_TRUE" : "GL_FALSE");
   CALL_DepthMask(ctx->Dispatch.RealPublished, (flag));
}

static void GLAPIENTRY
_mesa_trace_IndexMask(GLuint mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexMask(%u)\n", mask);
   CALL_IndexMask(ctx->Dispatch.RealPublished, (mask));
}

static void GLAPIENTRY
_mesa_trace_Accum(GLenum op, GLfloat value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glAccum(%s, %f)\n", _mesa_enum_to_string(op), value);
   CALL_Accum(ctx->Dispatch.RealPublished, (op, value));
}

static void GLAPIENTRY
_mesa_trace_Disable(GLenum cap)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDisable(%s)\n", _mesa_enum_to_string(cap));
   CALL_Disable(ctx->Dispatch.RealPublished, (cap));
}

static void GLAPIENTRY
_mesa_trace_Enable(GLenum cap)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEnable(%s)\n", _mesa_enum_to_string(cap));
   CALL_Enable(ctx->Dispatch.RealPublished, (cap));
}

static void GLAPIENTRY
_mesa_trace_Finish(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFinish()\n");
   CALL_Finish(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_Flush(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFlush()\n");
   CALL_Flush(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_PopAttrib(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPopAttrib()\n");
   CALL_PopAttrib(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_PushAttrib(GLbitfield mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPushAttrib(0x%x)\n", mask);
   CALL_PushAttrib(ctx->Dispatch.RealPublished, (mask));
}

static void GLAPIENTRY
_mesa_trace_Map1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMap1d(%s, %f, %f, %d, %d, %p)\n", _mesa_enum_to_string(target), u1, u2, stride, order, (void *)points);
   CALL_Map1d(ctx->Dispatch.RealPublished, (target, u1, u2, stride, order, points));
}

static void GLAPIENTRY
_mesa_trace_Map1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMap1f(%s, %f, %f, %d, %d, %p)\n", _mesa_enum_to_string(target), u1, u2, stride, order, (void *)points);
   CALL_Map1f(ctx->Dispatch.RealPublished, (target, u1, u2, stride, order, points));
}

static void GLAPIENTRY
_mesa_trace_Map2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMap2d(%s, %f, %f, %d, %d, %f, %f, %d, %d, %p)\n", _mesa_enum_to_string(target), u1, u2, ustride, uorder, v1, v2, vstride, vorder, (void *)points);
   CALL_Map2d(ctx->Dispatch.RealPublished, (target, u1, u2, ustride, uorder, v1, v2, vstride, vorder, points));
}

static void GLAPIENTRY
_mesa_trace_Map2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMap2f(%s, %f, %f, %d, %d, %f, %f, %d, %d, %p)\n", _mesa_enum_to_string(target), u1, u2, ustride, uorder, v1, v2, vstride, vorder, (void *)points);
   CALL_Map2f(ctx->Dispatch.RealPublished, (target, u1, u2, ustride, uorder, v1, v2, vstride, vorder, points));
}

static void GLAPIENTRY
_mesa_trace_MapGrid1d(GLint un, GLdouble u1, GLdouble u2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapGrid1d(%d, %f, %f)\n", un, u1, u2);
   CALL_MapGrid1d(ctx->Dispatch.RealPublished, (un, u1, u2));
}

static void GLAPIENTRY
_mesa_trace_MapGrid1f(GLint un, GLfloat u1, GLfloat u2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapGrid1f(%d, %f, %f)\n", un, u1, u2);
   CALL_MapGrid1f(ctx->Dispatch.RealPublished, (un, u1, u2));
}

static void GLAPIENTRY
_mesa_trace_MapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapGrid2d(%d, %f, %f, %d, %f, %f)\n", un, u1, u2, vn, v1, v2);
   CALL_MapGrid2d(ctx->Dispatch.RealPublished, (un, u1, u2, vn, v1, v2));
}

static void GLAPIENTRY
_mesa_trace_MapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapGrid2f(%d, %f, %f, %d, %f, %f)\n", un, u1, u2, vn, v1, v2);
   CALL_MapGrid2f(ctx->Dispatch.RealPublished, (un, u1, u2, vn, v1, v2));
}

static void GLAPIENTRY
_mesa_trace_EvalCoord1d(GLdouble u)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalCoord1d(%f)\n", u);
   CALL_EvalCoord1d(ctx->Dispatch.RealPublished, (u));
}

static void GLAPIENTRY
_mesa_trace_EvalCoord1dv(const GLdouble *u)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalCoord1dv(%p)\n", (void *)u);
   CALL_EvalCoord1dv(ctx->Dispatch.RealPublished, (u));
}

static void GLAPIENTRY
_mesa_trace_EvalCoord1f(GLfloat u)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalCoord1f(%f)\n", u);
   CALL_EvalCoord1f(ctx->Dispatch.RealPublished, (u));
}

static void GLAPIENTRY
_mesa_trace_EvalCoord1fv(const GLfloat *u)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalCoord1fv(%p)\n", (void *)u);
   CALL_EvalCoord1fv(ctx->Dispatch.RealPublished, (u));
}

static void GLAPIENTRY
_mesa_trace_EvalCoord2d(GLdouble u, GLdouble v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalCoord2d(%f, %f)\n", u, v);
   CALL_EvalCoord2d(ctx->Dispatch.RealPublished, (u, v));
}

static void GLAPIENTRY
_mesa_trace_EvalCoord2dv(const GLdouble *u)
{
   GET_CURRENT_CONTEXT(ctx);
   char u_buf[512];
   _mesa_trace_format_array(u_buf, sizeof(u_buf), u, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glEvalCoord2dv(%s)\n", u_buf);
   CALL_EvalCoord2dv(ctx->Dispatch.RealPublished, (u));
}

static void GLAPIENTRY
_mesa_trace_EvalCoord2f(GLfloat u, GLfloat v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalCoord2f(%f, %f)\n", u, v);
   CALL_EvalCoord2f(ctx->Dispatch.RealPublished, (u, v));
}

static void GLAPIENTRY
_mesa_trace_EvalCoord2fv(const GLfloat *u)
{
   GET_CURRENT_CONTEXT(ctx);
   char u_buf[512];
   _mesa_trace_format_array(u_buf, sizeof(u_buf), u, 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glEvalCoord2fv(%s)\n", u_buf);
   CALL_EvalCoord2fv(ctx->Dispatch.RealPublished, (u));
}

static void GLAPIENTRY
_mesa_trace_EvalMesh1(GLenum mode, GLint i1, GLint i2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalMesh1(%s, %d, %d)\n", _mesa_enum_to_string(mode), i1, i2);
   CALL_EvalMesh1(ctx->Dispatch.RealPublished, (mode, i1, i2));
}

static void GLAPIENTRY
_mesa_trace_EvalPoint1(GLint i)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalPoint1(%d)\n", i);
   CALL_EvalPoint1(ctx->Dispatch.RealPublished, (i));
}

static void GLAPIENTRY
_mesa_trace_EvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalMesh2(%s, %d, %d, %d, %d)\n", _mesa_enum_to_string(mode), i1, i2, j1, j2);
   CALL_EvalMesh2(ctx->Dispatch.RealPublished, (mode, i1, i2, j1, j2));
}

static void GLAPIENTRY
_mesa_trace_EvalPoint2(GLint i, GLint j)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvalPoint2(%d, %d)\n", i, j);
   CALL_EvalPoint2(ctx->Dispatch.RealPublished, (i, j));
}

static void GLAPIENTRY
_mesa_trace_AlphaFunc(GLenum func, GLclampf ref)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glAlphaFunc(%s, %f)\n", _mesa_enum_to_string(func), ref);
   CALL_AlphaFunc(ctx->Dispatch.RealPublished, (func, ref));
}

static void GLAPIENTRY
_mesa_trace_BlendFunc(GLenum sfactor, GLenum dfactor)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendFunc(%s, %s)\n", _mesa_enum_to_string(sfactor), _mesa_enum_to_string(dfactor));
   CALL_BlendFunc(ctx->Dispatch.RealPublished, (sfactor, dfactor));
}

static void GLAPIENTRY
_mesa_trace_LogicOp(GLenum opcode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLogicOp(%s)\n", _mesa_enum_to_string(opcode));
   CALL_LogicOp(ctx->Dispatch.RealPublished, (opcode));
}

static void GLAPIENTRY
_mesa_trace_StencilFunc(GLenum func, GLint ref, GLuint mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glStencilFunc(%s, %d, %u)\n", _mesa_enum_to_string(func), ref, mask);
   CALL_StencilFunc(ctx->Dispatch.RealPublished, (func, ref, mask));
}

static void GLAPIENTRY
_mesa_trace_StencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glStencilOp(%s, %s, %s)\n", _mesa_enum_to_string(fail), _mesa_enum_to_string(zfail), _mesa_enum_to_string(zpass));
   CALL_StencilOp(ctx->Dispatch.RealPublished, (fail, zfail, zpass));
}

static void GLAPIENTRY
_mesa_trace_DepthFunc(GLenum func)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDepthFunc(%s)\n", _mesa_enum_to_string(func));
   CALL_DepthFunc(ctx->Dispatch.RealPublished, (func));
}

static void GLAPIENTRY
_mesa_trace_PixelZoom(GLfloat xfactor, GLfloat yfactor)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPixelZoom(%f, %f)\n", xfactor, yfactor);
   CALL_PixelZoom(ctx->Dispatch.RealPublished, (xfactor, yfactor));
}

static void GLAPIENTRY
_mesa_trace_PixelTransferf(GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPixelTransferf(%s, %f)\n", _mesa_enum_to_string(pname), param);
   CALL_PixelTransferf(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_PixelTransferi(GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPixelTransferi(%s, %d)\n", _mesa_enum_to_string(pname), param);
   CALL_PixelTransferi(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_PixelStoref(GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPixelStoref(%s, %f)\n", _mesa_enum_to_string(pname), param);
   CALL_PixelStoref(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_PixelStorei(GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPixelStorei(%s, %d)\n", _mesa_enum_to_string(pname), param);
   CALL_PixelStorei(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_PixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values)
{
   GET_CURRENT_CONTEXT(ctx);
   char values_buf[512];
   _mesa_trace_format_array(values_buf, sizeof(values_buf), values, (size_t)mapsize, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glPixelMapfv(%s, %d, %s)\n", _mesa_enum_to_string(map), mapsize, values_buf);
   CALL_PixelMapfv(ctx->Dispatch.RealPublished, (map, mapsize, values));
}

static void GLAPIENTRY
_mesa_trace_PixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values)
{
   GET_CURRENT_CONTEXT(ctx);
   char values_buf[512];
   _mesa_trace_format_array(values_buf, sizeof(values_buf), values, (size_t)mapsize, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glPixelMapuiv(%s, %d, %s)\n", _mesa_enum_to_string(map), mapsize, values_buf);
   CALL_PixelMapuiv(ctx->Dispatch.RealPublished, (map, mapsize, values));
}

static void GLAPIENTRY
_mesa_trace_PixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values)
{
   GET_CURRENT_CONTEXT(ctx);
   char values_buf[512];
   _mesa_trace_format_array(values_buf, sizeof(values_buf), values, (size_t)mapsize, MESA_TRACE_ELEM_USHORT);
   _mesa_debug(ctx, "glPixelMapusv(%s, %d, %s)\n", _mesa_enum_to_string(map), mapsize, values_buf);
   CALL_PixelMapusv(ctx->Dispatch.RealPublished, (map, mapsize, values));
}

static void GLAPIENTRY
_mesa_trace_ReadBuffer(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glReadBuffer(%s)\n", _mesa_enum_to_string(mode));
   CALL_ReadBuffer(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_CopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyPixels(%d, %d, %d, %d, %s)\n", x, y, width, height, _mesa_enum_to_string(type));
   CALL_CopyPixels(ctx->Dispatch.RealPublished, (x, y, width, height, type));
}

static void GLAPIENTRY
_mesa_trace_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glReadPixels(%d, %d, %d, %d, %s, %s, %p)\n", x, y, width, height, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_ReadPixels(ctx->Dispatch.RealPublished, (x, y, width, height, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_DrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawPixels(%d, %d, %s, %s, %p)\n", width, height, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_DrawPixels(ctx->Dispatch.RealPublished, (width, height, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_GetBooleanv(GLenum pname, GLboolean *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetBooleanv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_GetBooleanv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetClipPlane(GLenum plane, GLdouble *equation)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetClipPlane(%s, %p)\n", _mesa_enum_to_string(plane), (void *)equation);
   CALL_GetClipPlane(ctx->Dispatch.RealPublished, (plane, equation));
}

static void GLAPIENTRY
_mesa_trace_GetDoublev(GLenum pname, GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetDoublev(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_GetDoublev(ctx->Dispatch.RealPublished, (pname, params));
}

static GLenum GLAPIENTRY
_mesa_trace_GetError(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetError()\n");
   return CALL_GetError(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_GetFloatv(GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFloatv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_GetFloatv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetIntegerv(GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetIntegerv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_GetIntegerv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetLightfv(%s, %s, %p)\n", _mesa_enum_to_string(light), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetLightfv(ctx->Dispatch.RealPublished, (light, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetLightiv(GLenum light, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetLightiv(%s, %s, %p)\n", _mesa_enum_to_string(light), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetLightiv(ctx->Dispatch.RealPublished, (light, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetMapdv(GLenum target, GLenum query, GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMapdv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(query), (void *)v);
   CALL_GetMapdv(ctx->Dispatch.RealPublished, (target, query, v));
}

static void GLAPIENTRY
_mesa_trace_GetMapfv(GLenum target, GLenum query, GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMapfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(query), (void *)v);
   CALL_GetMapfv(ctx->Dispatch.RealPublished, (target, query, v));
}

static void GLAPIENTRY
_mesa_trace_GetMapiv(GLenum target, GLenum query, GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMapiv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(query), (void *)v);
   CALL_GetMapiv(ctx->Dispatch.RealPublished, (target, query, v));
}

static void GLAPIENTRY
_mesa_trace_GetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMaterialfv(%s, %s, %p)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMaterialfv(ctx->Dispatch.RealPublished, (face, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetMaterialiv(GLenum face, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMaterialiv(%s, %s, %p)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMaterialiv(ctx->Dispatch.RealPublished, (face, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetPixelMapfv(GLenum map, GLfloat *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPixelMapfv(%s, %p)\n", _mesa_enum_to_string(map), (void *)values);
   CALL_GetPixelMapfv(ctx->Dispatch.RealPublished, (map, values));
}

static void GLAPIENTRY
_mesa_trace_GetPixelMapuiv(GLenum map, GLuint *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPixelMapuiv(%s, %p)\n", _mesa_enum_to_string(map), (void *)values);
   CALL_GetPixelMapuiv(ctx->Dispatch.RealPublished, (map, values));
}

static void GLAPIENTRY
_mesa_trace_GetPixelMapusv(GLenum map, GLushort *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPixelMapusv(%s, %p)\n", _mesa_enum_to_string(map), (void *)values);
   CALL_GetPixelMapusv(ctx->Dispatch.RealPublished, (map, values));
}

static void GLAPIENTRY
_mesa_trace_GetPolygonStipple(GLubyte *mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPolygonStipple(%p)\n", (void *)mask);
   CALL_GetPolygonStipple(ctx->Dispatch.RealPublished, (mask));
}

static const GLubyte * GLAPIENTRY
_mesa_trace_GetString(GLenum name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetString(%s)\n", _mesa_enum_to_string(name));
   return CALL_GetString(ctx->Dispatch.RealPublished, (name));
}

static void GLAPIENTRY
_mesa_trace_GetTexEnvfv(GLenum target, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexEnvfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexEnvfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexEnviv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexEnviv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexEnviv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexGendv(GLenum coord, GLenum pname, GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexGendv(%s, %s, %p)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexGendv(ctx->Dispatch.RealPublished, (coord, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexGenfv(GLenum coord, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexGenfv(%s, %s, %p)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexGenfv(ctx->Dispatch.RealPublished, (coord, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexGeniv(GLenum coord, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexGeniv(%s, %s, %p)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexGeniv(ctx->Dispatch.RealPublished, (coord, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexImage(%s, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), level, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_GetTexImage(ctx->Dispatch.RealPublished, (target, level, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_GetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexParameterfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexParameterfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexLevelParameterfv(%s, %d, %s, %p)\n", _mesa_enum_to_string(target), level, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexLevelParameterfv(ctx->Dispatch.RealPublished, (target, level, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexLevelParameteriv(%s, %d, %s, %p)\n", _mesa_enum_to_string(target), level, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexLevelParameteriv(ctx->Dispatch.RealPublished, (target, level, pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsEnabled(GLenum cap)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsEnabled(%s)\n", _mesa_enum_to_string(cap));
   return CALL_IsEnabled(ctx->Dispatch.RealPublished, (cap));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsList(GLuint list)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsList(%u)\n", list);
   return CALL_IsList(ctx->Dispatch.RealPublished, (list));
}

static void GLAPIENTRY
_mesa_trace_DepthRange(GLclampd zNear, GLclampd zFar)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDepthRange(%f, %f)\n", zNear, zFar);
   CALL_DepthRange(ctx->Dispatch.RealPublished, (zNear, zFar));
}

static void GLAPIENTRY
_mesa_trace_Frustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFrustum(%f, %f, %f, %f, %f, %f)\n", left, right, bottom, top, zNear, zFar);
   CALL_Frustum(ctx->Dispatch.RealPublished, (left, right, bottom, top, zNear, zFar));
}

static void GLAPIENTRY
_mesa_trace_LoadIdentity(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLoadIdentity()\n");
   CALL_LoadIdentity(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_LoadMatrixf(const GLfloat *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glLoadMatrixf(%s)\n", m_buf);
   CALL_LoadMatrixf(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_LoadMatrixd(const GLdouble *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glLoadMatrixd(%s)\n", m_buf);
   CALL_LoadMatrixd(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_MatrixMode(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixMode(%s)\n", _mesa_enum_to_string(mode));
   CALL_MatrixMode(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_MultMatrixf(const GLfloat *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glMultMatrixf(%s)\n", m_buf);
   CALL_MultMatrixf(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_MultMatrixd(const GLdouble *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glMultMatrixd(%s)\n", m_buf);
   CALL_MultMatrixd(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_Ortho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glOrtho(%f, %f, %f, %f, %f, %f)\n", left, right, bottom, top, zNear, zFar);
   CALL_Ortho(ctx->Dispatch.RealPublished, (left, right, bottom, top, zNear, zFar));
}

static void GLAPIENTRY
_mesa_trace_PopMatrix(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPopMatrix()\n");
   CALL_PopMatrix(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_PushMatrix(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPushMatrix()\n");
   CALL_PushMatrix(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_Rotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRotated(%f, %f, %f, %f)\n", angle, x, y, z);
   CALL_Rotated(ctx->Dispatch.RealPublished, (angle, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRotatef(%f, %f, %f, %f)\n", angle, x, y, z);
   CALL_Rotatef(ctx->Dispatch.RealPublished, (angle, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Scaled(GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glScaled(%f, %f, %f)\n", x, y, z);
   CALL_Scaled(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glScalef(%f, %f, %f)\n", x, y, z);
   CALL_Scalef(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Translated(GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTranslated(%f, %f, %f)\n", x, y, z);
   CALL_Translated(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTranslatef(%f, %f, %f)\n", x, y, z);
   CALL_Translatef(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glViewport(%d, %d, %d, %d)\n", x, y, width, height);
   CALL_Viewport(ctx->Dispatch.RealPublished, (x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_ArrayElement(GLint i)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glArrayElement(%d)\n", i);
   CALL_ArrayElement(ctx->Dispatch.RealPublished, (i));
}

static void GLAPIENTRY
_mesa_trace_BindTexture(GLenum target, GLuint texture)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindTexture(%s, %u)\n", _mesa_enum_to_string(target), texture);
   CALL_BindTexture(ctx->Dispatch.RealPublished, (target, texture));
}

static void GLAPIENTRY
_mesa_trace_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorPointer(%d, %s, %d, %p)\n", size, _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_ColorPointer(ctx->Dispatch.RealPublished, (size, type, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_DisableClientState(GLenum array)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDisableClientState(%s)\n", _mesa_enum_to_string(array));
   CALL_DisableClientState(ctx->Dispatch.RealPublished, (array));
}

static void GLAPIENTRY
_mesa_trace_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawArrays(%s, %d, %d)\n", _mesa_enum_to_string(mode), first, count);
   CALL_DrawArrays(ctx->Dispatch.RealPublished, (mode, first, count));
}

static void GLAPIENTRY
_mesa_trace_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElements(%s, %d, %s, %p)\n", _mesa_enum_to_string(mode), count, _mesa_enum_to_string(type), (void *)indices);
   CALL_DrawElements(ctx->Dispatch.RealPublished, (mode, count, type, indices));
}

static void GLAPIENTRY
_mesa_trace_EdgeFlagPointer(GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEdgeFlagPointer(%d, %p)\n", stride, (void *)pointer);
   CALL_EdgeFlagPointer(ctx->Dispatch.RealPublished, (stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_EnableClientState(GLenum array)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEnableClientState(%s)\n", _mesa_enum_to_string(array));
   CALL_EnableClientState(ctx->Dispatch.RealPublished, (array));
}

static void GLAPIENTRY
_mesa_trace_IndexPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexPointer(%s, %d, %p)\n", _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_IndexPointer(ctx->Dispatch.RealPublished, (type, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_Indexub(GLubyte c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexub(%u)\n", c);
   CALL_Indexub(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_Indexubv(const GLubyte *c)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexubv(%p)\n", (void *)c);
   CALL_Indexubv(ctx->Dispatch.RealPublished, (c));
}

static void GLAPIENTRY
_mesa_trace_InterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInterleavedArrays(%s, %d, %p)\n", _mesa_enum_to_string(format), stride, (void *)pointer);
   CALL_InterleavedArrays(ctx->Dispatch.RealPublished, (format, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_NormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormalPointer(%s, %d, %p)\n", _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_NormalPointer(ctx->Dispatch.RealPublished, (type, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_PolygonOffset(GLfloat factor, GLfloat units)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPolygonOffset(%f, %f)\n", factor, units);
   CALL_PolygonOffset(ctx->Dispatch.RealPublished, (factor, units));
}

static void GLAPIENTRY
_mesa_trace_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordPointer(%d, %s, %d, %p)\n", size, _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_TexCoordPointer(ctx->Dispatch.RealPublished, (size, type, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexPointer(%d, %s, %d, %p)\n", size, _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_VertexPointer(ctx->Dispatch.RealPublished, (size, type, stride, pointer));
}

static GLboolean GLAPIENTRY
_mesa_trace_AreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
   GET_CURRENT_CONTEXT(ctx);
   char textures_buf[512];
   _mesa_trace_format_array(textures_buf, sizeof(textures_buf), textures, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glAreTexturesResident(%d, %s, %p)\n", n, textures_buf, (void *)residences);
   return CALL_AreTexturesResident(ctx->Dispatch.RealPublished, (n, textures, residences));
}

static void GLAPIENTRY
_mesa_trace_CopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTexImage1D(%s, %d, %s, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalformat), x, y, width, border);
   CALL_CopyTexImage1D(ctx->Dispatch.RealPublished, (target, level, internalformat, x, y, width, border));
}

static void GLAPIENTRY
_mesa_trace_CopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTexImage2D(%s, %d, %s, %d, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalformat), x, y, width, height, border);
   CALL_CopyTexImage2D(ctx->Dispatch.RealPublished, (target, level, internalformat, x, y, width, height, border));
}

static void GLAPIENTRY
_mesa_trace_CopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTexSubImage1D(%s, %d, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), level, xoffset, x, y, width);
   CALL_CopyTexSubImage1D(ctx->Dispatch.RealPublished, (target, level, xoffset, x, y, width));
}

static void GLAPIENTRY
_mesa_trace_CopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTexSubImage2D(%s, %d, %d, %d, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), level, xoffset, yoffset, x, y, width, height);
   CALL_CopyTexSubImage2D(ctx->Dispatch.RealPublished, (target, level, xoffset, yoffset, x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_DeleteTextures(GLsizei n, const GLuint *textures)
{
   GET_CURRENT_CONTEXT(ctx);
   char textures_buf[512];
   _mesa_trace_format_array(textures_buf, sizeof(textures_buf), textures, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteTextures(%d, %s)\n", n, textures_buf);
   CALL_DeleteTextures(ctx->Dispatch.RealPublished, (n, textures));
}

static void GLAPIENTRY
_mesa_trace_GenTextures(GLsizei n, GLuint *textures)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenTextures(%d, %p)\n", n, (void *)textures);
   CALL_GenTextures(ctx->Dispatch.RealPublished, (n, textures));
}

static void GLAPIENTRY
_mesa_trace_GetPointerv(GLenum pname, GLvoid **params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPointerv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_GetPointerv(ctx->Dispatch.RealPublished, (pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsTexture(GLuint texture)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsTexture(%u)\n", texture);
   return CALL_IsTexture(ctx->Dispatch.RealPublished, (texture));
}

static void GLAPIENTRY
_mesa_trace_PrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
   GET_CURRENT_CONTEXT(ctx);
   char textures_buf[512];
   _mesa_trace_format_array(textures_buf, sizeof(textures_buf), textures, (size_t)n, MESA_TRACE_ELEM_UINT);
   char priorities_buf[512];
   _mesa_trace_format_array(priorities_buf, sizeof(priorities_buf), priorities, (size_t)n, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glPrioritizeTextures(%d, %s, %s)\n", n, textures_buf, priorities_buf);
   CALL_PrioritizeTextures(ctx->Dispatch.RealPublished, (n, textures, priorities));
}

static void GLAPIENTRY
_mesa_trace_TexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexSubImage1D(%s, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), level, xoffset, width, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TexSubImage1D(ctx->Dispatch.RealPublished, (target, level, xoffset, width, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexSubImage2D(%s, %d, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), level, xoffset, yoffset, width, height, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TexSubImage2D(ctx->Dispatch.RealPublished, (target, level, xoffset, yoffset, width, height, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_PopClientAttrib(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPopClientAttrib()\n");
   CALL_PopClientAttrib(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_PushClientAttrib(GLbitfield mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPushClientAttrib(0x%x)\n", mask);
   CALL_PushClientAttrib(ctx->Dispatch.RealPublished, (mask));
}

static void GLAPIENTRY
_mesa_trace_BlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendColor(%f, %f, %f, %f)\n", red, green, blue, alpha);
   CALL_BlendColor(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_BlendEquation(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendEquation(%s)\n", _mesa_enum_to_string(mode));
   CALL_BlendEquation(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawRangeElements(%s, %u, %u, %d, %s, %p)\n", _mesa_enum_to_string(mode), start, end, count, _mesa_enum_to_string(type), (void *)indices);
   CALL_DrawRangeElements(ctx->Dispatch.RealPublished, (mode, start, end, count, type, indices));
}

static void GLAPIENTRY
_mesa_trace_TexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexImage3D(%s, %d, %d, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), level, internalformat, width, height, depth, border, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TexImage3D(ctx->Dispatch.RealPublished, (target, level, internalformat, width, height, depth, border, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexSubImage3D(%s, %d, %d, %d, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TexSubImage3D(ctx->Dispatch.RealPublished, (target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_CopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTexSubImage3D(%s, %d, %d, %d, %d, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, x, y, width, height);
   CALL_CopyTexSubImage3D(ctx->Dispatch.RealPublished, (target, level, xoffset, yoffset, zoffset, x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_ActiveTexture(GLenum texture)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glActiveTexture(%s)\n", _mesa_enum_to_string(texture));
   CALL_ActiveTexture(ctx->Dispatch.RealPublished, (texture));
}

static void GLAPIENTRY
_mesa_trace_ClientActiveTexture(GLenum texture)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClientActiveTexture(%s)\n", _mesa_enum_to_string(texture));
   CALL_ClientActiveTexture(ctx->Dispatch.RealPublished, (texture));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1d(GLenum target, GLdouble s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1d(%s, %f)\n", _mesa_enum_to_string(target), s);
   CALL_MultiTexCoord1d(ctx->Dispatch.RealPublished, (target, s));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1dv(GLenum target, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1dv(%s, %p)\n", _mesa_enum_to_string(target), (void *)v);
   CALL_MultiTexCoord1dv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1fARB(GLenum target, GLfloat s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1fARB(%s, %f)\n", _mesa_enum_to_string(target), s);
   CALL_MultiTexCoord1fARB(ctx->Dispatch.RealPublished, (target, s));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1fvARB(GLenum target, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1fvARB(%s, %p)\n", _mesa_enum_to_string(target), (void *)v);
   CALL_MultiTexCoord1fvARB(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1i(GLenum target, GLint s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1i(%s, %d)\n", _mesa_enum_to_string(target), s);
   CALL_MultiTexCoord1i(ctx->Dispatch.RealPublished, (target, s));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1iv(GLenum target, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1iv(%s, %p)\n", _mesa_enum_to_string(target), (void *)v);
   CALL_MultiTexCoord1iv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1s(GLenum target, GLshort s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1s(%s, %d)\n", _mesa_enum_to_string(target), s);
   CALL_MultiTexCoord1s(ctx->Dispatch.RealPublished, (target, s));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1sv(GLenum target, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1sv(%s, %p)\n", _mesa_enum_to_string(target), (void *)v);
   CALL_MultiTexCoord1sv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2d(GLenum target, GLdouble s, GLdouble t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord2d(%s, %f, %f)\n", _mesa_enum_to_string(target), s, t);
   CALL_MultiTexCoord2d(ctx->Dispatch.RealPublished, (target, s, t));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2dv(GLenum target, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glMultiTexCoord2dv(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord2dv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord2fARB(%s, %f, %f)\n", _mesa_enum_to_string(target), s, t);
   CALL_MultiTexCoord2fARB(ctx->Dispatch.RealPublished, (target, s, t));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2fvARB(GLenum target, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glMultiTexCoord2fvARB(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord2fvARB(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2i(GLenum target, GLint s, GLint t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord2i(%s, %d, %d)\n", _mesa_enum_to_string(target), s, t);
   CALL_MultiTexCoord2i(ctx->Dispatch.RealPublished, (target, s, t));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2iv(GLenum target, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultiTexCoord2iv(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord2iv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2s(GLenum target, GLshort s, GLshort t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord2s(%s, %d, %d)\n", _mesa_enum_to_string(target), s, t);
   CALL_MultiTexCoord2s(ctx->Dispatch.RealPublished, (target, s, t));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2sv(GLenum target, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glMultiTexCoord2sv(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord2sv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3d(GLenum target, GLdouble s, GLdouble t, GLdouble r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord3d(%s, %f, %f, %f)\n", _mesa_enum_to_string(target), s, t, r);
   CALL_MultiTexCoord3d(ctx->Dispatch.RealPublished, (target, s, t, r));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3dv(GLenum target, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glMultiTexCoord3dv(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord3dv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord3fARB(%s, %f, %f, %f)\n", _mesa_enum_to_string(target), s, t, r);
   CALL_MultiTexCoord3fARB(ctx->Dispatch.RealPublished, (target, s, t, r));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3fvARB(GLenum target, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glMultiTexCoord3fvARB(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord3fvARB(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3i(GLenum target, GLint s, GLint t, GLint r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord3i(%s, %d, %d, %d)\n", _mesa_enum_to_string(target), s, t, r);
   CALL_MultiTexCoord3i(ctx->Dispatch.RealPublished, (target, s, t, r));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3iv(GLenum target, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultiTexCoord3iv(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord3iv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3s(GLenum target, GLshort s, GLshort t, GLshort r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord3s(%s, %d, %d, %d)\n", _mesa_enum_to_string(target), s, t, r);
   CALL_MultiTexCoord3s(ctx->Dispatch.RealPublished, (target, s, t, r));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3sv(GLenum target, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glMultiTexCoord3sv(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord3sv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4d(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord4d(%s, %f, %f, %f, %f)\n", _mesa_enum_to_string(target), s, t, r, q);
   CALL_MultiTexCoord4d(ctx->Dispatch.RealPublished, (target, s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4dv(GLenum target, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glMultiTexCoord4dv(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord4dv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord4fARB(%s, %f, %f, %f, %f)\n", _mesa_enum_to_string(target), s, t, r, q);
   CALL_MultiTexCoord4fARB(ctx->Dispatch.RealPublished, (target, s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4fvARB(GLenum target, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glMultiTexCoord4fvARB(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord4fvARB(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4i(GLenum target, GLint s, GLint t, GLint r, GLint q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord4i(%s, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), s, t, r, q);
   CALL_MultiTexCoord4i(ctx->Dispatch.RealPublished, (target, s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4iv(GLenum target, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultiTexCoord4iv(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord4iv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4s(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord4s(%s, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), s, t, r, q);
   CALL_MultiTexCoord4s(ctx->Dispatch.RealPublished, (target, s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4sv(GLenum target, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glMultiTexCoord4sv(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord4sv(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_CompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTexImage1D(%s, %d, %s, %d, %d, %d, %p)\n", _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalformat), width, border, imageSize, (void *)data);
   CALL_CompressedTexImage1D(ctx->Dispatch.RealPublished, (target, level, internalformat, width, border, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTexImage2D(%s, %d, %s, %d, %d, %d, %d, %p)\n", _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalformat), width, height, border, imageSize, (void *)data);
   CALL_CompressedTexImage2D(ctx->Dispatch.RealPublished, (target, level, internalformat, width, height, border, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTexImage3D(%s, %d, %s, %d, %d, %d, %d, %d, %p)\n", _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalformat), width, height, depth, border, imageSize, (void *)data);
   CALL_CompressedTexImage3D(ctx->Dispatch.RealPublished, (target, level, internalformat, width, height, depth, border, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTexSubImage1D(%s, %d, %d, %d, %s, %d, %p)\n", _mesa_enum_to_string(target), level, xoffset, width, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedTexSubImage1D(ctx->Dispatch.RealPublished, (target, level, xoffset, width, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTexSubImage2D(%s, %d, %d, %d, %d, %d, %s, %d, %p)\n", _mesa_enum_to_string(target), level, xoffset, yoffset, width, height, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedTexSubImage2D(ctx->Dispatch.RealPublished, (target, level, xoffset, yoffset, width, height, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTexSubImage3D(%s, %d, %d, %d, %d, %d, %d, %d, %s, %d, %p)\n", _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedTexSubImage3D(ctx->Dispatch.RealPublished, (target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_GetCompressedTexImage(GLenum target, GLint level, GLvoid *img)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetCompressedTexImage(%s, %d, %p)\n", _mesa_enum_to_string(target), level, (void *)img);
   CALL_GetCompressedTexImage(ctx->Dispatch.RealPublished, (target, level, img));
}

static void GLAPIENTRY
_mesa_trace_LoadTransposeMatrixd(const GLdouble *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glLoadTransposeMatrixd(%s)\n", m_buf);
   CALL_LoadTransposeMatrixd(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_LoadTransposeMatrixf(const GLfloat *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glLoadTransposeMatrixf(%s)\n", m_buf);
   CALL_LoadTransposeMatrixf(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_MultTransposeMatrixd(const GLdouble *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glMultTransposeMatrixd(%s)\n", m_buf);
   CALL_MultTransposeMatrixd(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_MultTransposeMatrixf(const GLfloat *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glMultTransposeMatrixf(%s)\n", m_buf);
   CALL_MultTransposeMatrixf(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_SampleCoverage(GLclampf value, GLboolean invert)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSampleCoverage(%f, %s)\n", value, invert ? "GL_TRUE" : "GL_FALSE");
   CALL_SampleCoverage(ctx->Dispatch.RealPublished, (value, invert));
}

static void GLAPIENTRY
_mesa_trace_BlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendFuncSeparate(%s, %s, %s, %s)\n", _mesa_enum_to_string(sfactorRGB), _mesa_enum_to_string(dfactorRGB), _mesa_enum_to_string(sfactorAlpha), _mesa_enum_to_string(dfactorAlpha));
   CALL_BlendFuncSeparate(ctx->Dispatch.RealPublished, (sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha));
}

static void GLAPIENTRY
_mesa_trace_FogCoordPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogCoordPointer(%s, %d, %p)\n", _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_FogCoordPointer(ctx->Dispatch.RealPublished, (type, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_FogCoordd(GLdouble coord)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogCoordd(%f)\n", coord);
   CALL_FogCoordd(ctx->Dispatch.RealPublished, (coord));
}

static void GLAPIENTRY
_mesa_trace_FogCoorddv(const GLdouble *coord)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogCoorddv(%p)\n", (void *)coord);
   CALL_FogCoorddv(ctx->Dispatch.RealPublished, (coord));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
   GET_CURRENT_CONTEXT(ctx);
   char first_buf[512];
   _mesa_trace_format_array(first_buf, sizeof(first_buf), first, (size_t)primcount, MESA_TRACE_ELEM_INT);
   char count_buf[512];
   _mesa_trace_format_array(count_buf, sizeof(count_buf), count, (size_t)primcount, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultiDrawArrays(%s, %s, %s, %d)\n", _mesa_enum_to_string(mode), first_buf, count_buf, primcount);
   CALL_MultiDrawArrays(ctx->Dispatch.RealPublished, (mode, first, count, primcount));
}

static void GLAPIENTRY
_mesa_trace_PointParameterf(GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPointParameterf(%s, %f)\n", _mesa_enum_to_string(pname), param);
   CALL_PointParameterf(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_PointParameterfv(GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPointParameterfv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_PointParameterfv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_PointParameteri(GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPointParameteri(%s, %d)\n", _mesa_enum_to_string(pname), param);
   CALL_PointParameteri(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_PointParameteriv(GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPointParameteriv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_PointParameteriv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3b(GLbyte red, GLbyte green, GLbyte blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColor3b(%d, %d, %d)\n", red, green, blue);
   CALL_SecondaryColor3b(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3bv(const GLbyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_BYTE);
   _mesa_debug(ctx, "glSecondaryColor3bv(%s)\n", v_buf);
   CALL_SecondaryColor3bv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3d(GLdouble red, GLdouble green, GLdouble blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColor3d(%f, %f, %f)\n", red, green, blue);
   CALL_SecondaryColor3d(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glSecondaryColor3dv(%s)\n", v_buf);
   CALL_SecondaryColor3dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3i(GLint red, GLint green, GLint blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColor3i(%d, %d, %d)\n", red, green, blue);
   CALL_SecondaryColor3i(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glSecondaryColor3iv(%s)\n", v_buf);
   CALL_SecondaryColor3iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3s(GLshort red, GLshort green, GLshort blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColor3s(%d, %d, %d)\n", red, green, blue);
   CALL_SecondaryColor3s(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glSecondaryColor3sv(%s)\n", v_buf);
   CALL_SecondaryColor3sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColor3ub(%u, %u, %u)\n", red, green, blue);
   CALL_SecondaryColor3ub(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3ubv(const GLubyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_UBYTE);
   _mesa_debug(ctx, "glSecondaryColor3ubv(%s)\n", v_buf);
   CALL_SecondaryColor3ubv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3ui(GLuint red, GLuint green, GLuint blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColor3ui(%u, %u, %u)\n", red, green, blue);
   CALL_SecondaryColor3ui(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3uiv(const GLuint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glSecondaryColor3uiv(%s)\n", v_buf);
   CALL_SecondaryColor3uiv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3us(GLushort red, GLushort green, GLushort blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColor3us(%u, %u, %u)\n", red, green, blue);
   CALL_SecondaryColor3us(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3usv(const GLushort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_USHORT);
   _mesa_debug(ctx, "glSecondaryColor3usv(%s)\n", v_buf);
   CALL_SecondaryColor3usv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColorPointer(%d, %s, %d, %p)\n", size, _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_SecondaryColorPointer(ctx->Dispatch.RealPublished, (size, type, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_WindowPos2d(GLdouble x, GLdouble y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos2d(%f, %f)\n", x, y);
   CALL_WindowPos2d(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_WindowPos2dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glWindowPos2dv(%s)\n", v_buf);
   CALL_WindowPos2dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos2f(GLfloat x, GLfloat y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos2f(%f, %f)\n", x, y);
   CALL_WindowPos2f(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_WindowPos2fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glWindowPos2fv(%s)\n", v_buf);
   CALL_WindowPos2fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos2i(GLint x, GLint y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos2i(%d, %d)\n", x, y);
   CALL_WindowPos2i(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_WindowPos2iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glWindowPos2iv(%s)\n", v_buf);
   CALL_WindowPos2iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos2s(GLshort x, GLshort y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos2s(%d, %d)\n", x, y);
   CALL_WindowPos2s(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_WindowPos2sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glWindowPos2sv(%s)\n", v_buf);
   CALL_WindowPos2sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos3d(GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos3d(%f, %f, %f)\n", x, y, z);
   CALL_WindowPos3d(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_WindowPos3dv(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glWindowPos3dv(%s)\n", v_buf);
   CALL_WindowPos3dv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos3f(GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos3f(%f, %f, %f)\n", x, y, z);
   CALL_WindowPos3f(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_WindowPos3fv(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glWindowPos3fv(%s)\n", v_buf);
   CALL_WindowPos3fv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos3i(GLint x, GLint y, GLint z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos3i(%d, %d, %d)\n", x, y, z);
   CALL_WindowPos3i(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_WindowPos3iv(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glWindowPos3iv(%s)\n", v_buf);
   CALL_WindowPos3iv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos3s(GLshort x, GLshort y, GLshort z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos3s(%d, %d, %d)\n", x, y, z);
   CALL_WindowPos3s(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_WindowPos3sv(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glWindowPos3sv(%s)\n", v_buf);
   CALL_WindowPos3sv(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_BeginQuery(GLenum target, GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBeginQuery(%s, %u)\n", _mesa_enum_to_string(target), id);
   CALL_BeginQuery(ctx->Dispatch.RealPublished, (target, id));
}

static void GLAPIENTRY
_mesa_trace_BindBuffer(GLenum target, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindBuffer(%s, %u)\n", _mesa_enum_to_string(target), buffer);
   CALL_BindBuffer(ctx->Dispatch.RealPublished, (target, buffer));
}

static void GLAPIENTRY
_mesa_trace_BufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBufferData(%s, %" PRIdPTR ", %p, %s)\n", _mesa_enum_to_string(target), (intptr_t)size, (void *)data, _mesa_enum_to_string(usage));
   CALL_BufferData(ctx->Dispatch.RealPublished, (target, size, data, usage));
}

static void GLAPIENTRY
_mesa_trace_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBufferSubData(%s, %" PRIdPTR ", %" PRIdPTR ", %p)\n", _mesa_enum_to_string(target), (intptr_t)offset, (intptr_t)size, (void *)data);
   CALL_BufferSubData(ctx->Dispatch.RealPublished, (target, offset, size, data));
}

static void GLAPIENTRY
_mesa_trace_DeleteBuffers(GLsizei n, const GLuint *buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   char buffer_buf[512];
   _mesa_trace_format_array(buffer_buf, sizeof(buffer_buf), buffer, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteBuffers(%d, %s)\n", n, buffer_buf);
   CALL_DeleteBuffers(ctx->Dispatch.RealPublished, (n, buffer));
}

static void GLAPIENTRY
_mesa_trace_DeleteQueries(GLsizei n, const GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);
   char ids_buf[512];
   _mesa_trace_format_array(ids_buf, sizeof(ids_buf), ids, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteQueries(%d, %s)\n", n, ids_buf);
   CALL_DeleteQueries(ctx->Dispatch.RealPublished, (n, ids));
}

static void GLAPIENTRY
_mesa_trace_EndQuery(GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEndQuery(%s)\n", _mesa_enum_to_string(target));
   CALL_EndQuery(ctx->Dispatch.RealPublished, (target));
}

static void GLAPIENTRY
_mesa_trace_GenBuffers(GLsizei n, GLuint *buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenBuffers(%d, %p)\n", n, (void *)buffer);
   CALL_GenBuffers(ctx->Dispatch.RealPublished, (n, buffer));
}

static void GLAPIENTRY
_mesa_trace_GenQueries(GLsizei n, GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenQueries(%d, %p)\n", n, (void *)ids);
   CALL_GenQueries(ctx->Dispatch.RealPublished, (n, ids));
}

static void GLAPIENTRY
_mesa_trace_GetBufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetBufferParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetBufferParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetBufferPointerv(GLenum target, GLenum pname, GLvoid **params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetBufferPointerv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetBufferPointerv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetBufferSubData(%s, %" PRIdPTR ", %" PRIdPTR ", %p)\n", _mesa_enum_to_string(target), (intptr_t)offset, (intptr_t)size, (void *)data);
   CALL_GetBufferSubData(ctx->Dispatch.RealPublished, (target, offset, size, data));
}

static void GLAPIENTRY
_mesa_trace_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryObjectiv(%u, %s, %p)\n", id, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetQueryObjectiv(ctx->Dispatch.RealPublished, (id, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryObjectuiv(%u, %s, %p)\n", id, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetQueryObjectuiv(ctx->Dispatch.RealPublished, (id, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetQueryiv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryiv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetQueryiv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsBuffer(GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsBuffer(%u)\n", buffer);
   return CALL_IsBuffer(ctx->Dispatch.RealPublished, (buffer));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsQuery(GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsQuery(%u)\n", id);
   return CALL_IsQuery(ctx->Dispatch.RealPublished, (id));
}

static GLvoid * GLAPIENTRY
_mesa_trace_MapBuffer(GLenum target, GLenum access)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapBuffer(%s, %s)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(access));
   return CALL_MapBuffer(ctx->Dispatch.RealPublished, (target, access));
}

static GLboolean GLAPIENTRY
_mesa_trace_UnmapBuffer(GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUnmapBuffer(%s)\n", _mesa_enum_to_string(target));
   return CALL_UnmapBuffer(ctx->Dispatch.RealPublished, (target));
}

static void GLAPIENTRY
_mesa_trace_AttachShader(GLuint program, GLuint shader)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glAttachShader(%u, %u)\n", program, shader);
   CALL_AttachShader(ctx->Dispatch.RealPublished, (program, shader));
}

static void GLAPIENTRY
_mesa_trace_BindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindAttribLocation(%u, %u, %s)\n", program, index, name ? (const char *)name : "(null)");
   CALL_BindAttribLocation(ctx->Dispatch.RealPublished, (program, index, name));
}

static void GLAPIENTRY
_mesa_trace_BlendEquationSeparate(GLenum modeRGB, GLenum modeA)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendEquationSeparate(%s, %s)\n", _mesa_enum_to_string(modeRGB), _mesa_enum_to_string(modeA));
   CALL_BlendEquationSeparate(ctx->Dispatch.RealPublished, (modeRGB, modeA));
}

static void GLAPIENTRY
_mesa_trace_CompileShader(GLuint shader)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompileShader(%u)\n", shader);
   CALL_CompileShader(ctx->Dispatch.RealPublished, (shader));
}

static GLuint GLAPIENTRY
_mesa_trace_CreateProgram(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateProgram()\n");
   return CALL_CreateProgram(ctx->Dispatch.RealPublished, ());
}

static GLuint GLAPIENTRY
_mesa_trace_CreateShader(GLenum type)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateShader(%s)\n", _mesa_enum_to_string(type));
   return CALL_CreateShader(ctx->Dispatch.RealPublished, (type));
}

static void GLAPIENTRY
_mesa_trace_DeleteProgram(GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDeleteProgram(%u)\n", program);
   CALL_DeleteProgram(ctx->Dispatch.RealPublished, (program));
}

static void GLAPIENTRY
_mesa_trace_DeleteShader(GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDeleteShader(%u)\n", program);
   CALL_DeleteShader(ctx->Dispatch.RealPublished, (program));
}

static void GLAPIENTRY
_mesa_trace_DetachShader(GLuint program, GLuint shader)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDetachShader(%u, %u)\n", program, shader);
   CALL_DetachShader(ctx->Dispatch.RealPublished, (program, shader));
}

static void GLAPIENTRY
_mesa_trace_DisableVertexAttribArray(GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDisableVertexAttribArray(%u)\n", index);
   CALL_DisableVertexAttribArray(ctx->Dispatch.RealPublished, (index));
}

static void GLAPIENTRY
_mesa_trace_DrawBuffers(GLsizei n, const GLenum *bufs)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawBuffers(%d, %p)\n", n, (void *)bufs);
   CALL_DrawBuffers(ctx->Dispatch.RealPublished, (n, bufs));
}

static void GLAPIENTRY
_mesa_trace_EnableVertexAttribArray(GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEnableVertexAttribArray(%u)\n", index);
   CALL_EnableVertexAttribArray(ctx->Dispatch.RealPublished, (index));
}

static void GLAPIENTRY
_mesa_trace_GetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveAttrib(%u, %u, %d, %p, %p, %p, %p)\n", program, index, bufSize, (void *)length, (void *)size, (void *)type, (void *)name);
   CALL_GetActiveAttrib(ctx->Dispatch.RealPublished, (program, index, bufSize, length, size, type, name));
}

static void GLAPIENTRY
_mesa_trace_GetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveUniform(%u, %u, %d, %p, %p, %p, %p)\n", program, index, bufSize, (void *)length, (void *)size, (void *)type, (void *)name);
   CALL_GetActiveUniform(ctx->Dispatch.RealPublished, (program, index, bufSize, length, size, type, name));
}

static void GLAPIENTRY
_mesa_trace_GetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *obj)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetAttachedShaders(%u, %d, %p, %p)\n", program, maxCount, (void *)count, (void *)obj);
   CALL_GetAttachedShaders(ctx->Dispatch.RealPublished, (program, maxCount, count, obj));
}

static GLint GLAPIENTRY
_mesa_trace_GetAttribLocation(GLuint program, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetAttribLocation(%u, %s)\n", program, name ? (const char *)name : "(null)");
   return CALL_GetAttribLocation(ctx->Dispatch.RealPublished, (program, name));
}

static void GLAPIENTRY
_mesa_trace_GetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramInfoLog(%u, %d, %p, %p)\n", program, bufSize, (void *)length, (void *)infoLog);
   CALL_GetProgramInfoLog(ctx->Dispatch.RealPublished, (program, bufSize, length, infoLog));
}

static void GLAPIENTRY
_mesa_trace_GetProgramiv(GLuint program, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramiv(%u, %s, %p)\n", program, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetProgramiv(ctx->Dispatch.RealPublished, (program, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetShaderInfoLog(%u, %d, %p, %p)\n", shader, bufSize, (void *)length, (void *)infoLog);
   CALL_GetShaderInfoLog(ctx->Dispatch.RealPublished, (shader, bufSize, length, infoLog));
}

static void GLAPIENTRY
_mesa_trace_GetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetShaderSource(%u, %d, %p, %p)\n", shader, bufSize, (void *)length, (void *)source);
   CALL_GetShaderSource(ctx->Dispatch.RealPublished, (shader, bufSize, length, source));
}

static void GLAPIENTRY
_mesa_trace_GetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetShaderiv(%u, %s, %p)\n", shader, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetShaderiv(ctx->Dispatch.RealPublished, (shader, pname, params));
}

static GLint GLAPIENTRY
_mesa_trace_GetUniformLocation(GLuint program, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformLocation(%u, %s)\n", program, name ? (const char *)name : "(null)");
   return CALL_GetUniformLocation(ctx->Dispatch.RealPublished, (program, name));
}

static void GLAPIENTRY
_mesa_trace_GetUniformfv(GLuint program, GLint location, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformfv(%u, %d, %p)\n", program, location, (void *)params);
   CALL_GetUniformfv(ctx->Dispatch.RealPublished, (program, location, params));
}

static void GLAPIENTRY
_mesa_trace_GetUniformiv(GLuint program, GLint location, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformiv(%u, %d, %p)\n", program, location, (void *)params);
   CALL_GetUniformiv(ctx->Dispatch.RealPublished, (program, location, params));
}

static void GLAPIENTRY
_mesa_trace_GetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexAttribPointerv(%u, %s, %p)\n", index, _mesa_enum_to_string(pname), (void *)pointer);
   CALL_GetVertexAttribPointerv(ctx->Dispatch.RealPublished, (index, pname, pointer));
}

static void GLAPIENTRY
_mesa_trace_GetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexAttribdv(%u, %s, %p)\n", index, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetVertexAttribdv(ctx->Dispatch.RealPublished, (index, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexAttribfv(%u, %s, %p)\n", index, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetVertexAttribfv(ctx->Dispatch.RealPublished, (index, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetVertexAttribiv(GLuint index, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexAttribiv(%u, %s, %p)\n", index, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetVertexAttribiv(ctx->Dispatch.RealPublished, (index, pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsProgram(GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsProgram(%u)\n", program);
   return CALL_IsProgram(ctx->Dispatch.RealPublished, (program));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsShader(GLuint shader)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsShader(%u)\n", shader);
   return CALL_IsShader(ctx->Dispatch.RealPublished, (shader));
}

static void GLAPIENTRY
_mesa_trace_LinkProgram(GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLinkProgram(%u)\n", program);
   CALL_LinkProgram(ctx->Dispatch.RealPublished, (program));
}

static void GLAPIENTRY
_mesa_trace_ShaderSource(GLuint shader, GLsizei count, const GLchar * const *string, const GLint *length)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glShaderSource(%u, %d, %s, %p)\n", shader, count, string ? (const char *)string : "(null)", (void *)length);
   CALL_ShaderSource(ctx->Dispatch.RealPublished, (shader, count, string, length));
}

static void GLAPIENTRY
_mesa_trace_StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glStencilFuncSeparate(%s, %s, %d, %u)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(func), ref, mask);
   CALL_StencilFuncSeparate(ctx->Dispatch.RealPublished, (face, func, ref, mask));
}

static void GLAPIENTRY
_mesa_trace_StencilMaskSeparate(GLenum face, GLuint mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glStencilMaskSeparate(%s, %u)\n", _mesa_enum_to_string(face), mask);
   CALL_StencilMaskSeparate(ctx->Dispatch.RealPublished, (face, mask));
}

static void GLAPIENTRY
_mesa_trace_StencilOpSeparate(GLenum face, GLenum sfail, GLenum zfail, GLenum zpass)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glStencilOpSeparate(%s, %s, %s, %s)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(sfail), _mesa_enum_to_string(zfail), _mesa_enum_to_string(zpass));
   CALL_StencilOpSeparate(ctx->Dispatch.RealPublished, (face, sfail, zfail, zpass));
}

static void GLAPIENTRY
_mesa_trace_Uniform1f(GLint location, GLfloat v0)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform1f(%d, %f)\n", location, v0);
   CALL_Uniform1f(ctx->Dispatch.RealPublished, (location, v0));
}

static void GLAPIENTRY
_mesa_trace_Uniform1fv(GLint location, GLsizei count, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniform1fv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform1fv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform1i(GLint location, GLint v0)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform1i(%d, %d)\n", location, v0);
   CALL_Uniform1i(ctx->Dispatch.RealPublished, (location, v0));
}

static void GLAPIENTRY
_mesa_trace_Uniform1iv(GLint location, GLsizei count, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glUniform1iv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform1iv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform2f(GLint location, GLfloat v0, GLfloat v1)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform2f(%d, %f, %f)\n", location, v0, v1);
   CALL_Uniform2f(ctx->Dispatch.RealPublished, (location, v0, v1));
}

static void GLAPIENTRY
_mesa_trace_Uniform2fv(GLint location, GLsizei count, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniform2fv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform2fv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform2i(GLint location, GLint v0, GLint v1)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform2i(%d, %d, %d)\n", location, v0, v1);
   CALL_Uniform2i(ctx->Dispatch.RealPublished, (location, v0, v1));
}

static void GLAPIENTRY
_mesa_trace_Uniform2iv(GLint location, GLsizei count, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glUniform2iv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform2iv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform3f(%d, %f, %f, %f)\n", location, v0, v1, v2);
   CALL_Uniform3f(ctx->Dispatch.RealPublished, (location, v0, v1, v2));
}

static void GLAPIENTRY
_mesa_trace_Uniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniform3fv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform3fv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform3i(GLint location, GLint v0, GLint v1, GLint v2)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform3i(%d, %d, %d, %d)\n", location, v0, v1, v2);
   CALL_Uniform3i(ctx->Dispatch.RealPublished, (location, v0, v1, v2));
}

static void GLAPIENTRY
_mesa_trace_Uniform3iv(GLint location, GLsizei count, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glUniform3iv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform3iv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform4f(%d, %f, %f, %f, %f)\n", location, v0, v1, v2, v3);
   CALL_Uniform4f(ctx->Dispatch.RealPublished, (location, v0, v1, v2, v3));
}

static void GLAPIENTRY
_mesa_trace_Uniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniform4fv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform4fv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform4i(%d, %d, %d, %d, %d)\n", location, v0, v1, v2, v3);
   CALL_Uniform4i(ctx->Dispatch.RealPublished, (location, v0, v1, v2, v3));
}

static void GLAPIENTRY
_mesa_trace_Uniform4iv(GLint location, GLsizei count, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glUniform4iv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform4iv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniformMatrix2fv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix2fv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 9, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniformMatrix3fv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix3fv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniformMatrix4fv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix4fv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UseProgram(GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUseProgram(%u)\n", program);
   CALL_UseProgram(ctx->Dispatch.RealPublished, (program));
}

static void GLAPIENTRY
_mesa_trace_ValidateProgram(GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glValidateProgram(%u)\n", program);
   CALL_ValidateProgram(ctx->Dispatch.RealPublished, (program));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1d(GLuint index, GLdouble x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1d(%u, %f)\n", index, x);
   CALL_VertexAttrib1d(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1dv(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1dv(%u, %p)\n", index, (void *)v);
   CALL_VertexAttrib1dv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1s(GLuint index, GLshort x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1s(%u, %d)\n", index, x);
   CALL_VertexAttrib1s(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1sv(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1sv(%u, %p)\n", index, (void *)v);
   CALL_VertexAttrib1sv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2d(GLuint index, GLdouble x, GLdouble y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib2d(%u, %f, %f)\n", index, x, y);
   CALL_VertexAttrib2d(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2dv(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttrib2dv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib2dv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2s(GLuint index, GLshort x, GLshort y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib2s(%u, %d, %d)\n", index, x, y);
   CALL_VertexAttrib2s(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2sv(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttrib2sv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib2sv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib3d(%u, %f, %f, %f)\n", index, x, y, z);
   CALL_VertexAttrib3d(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3dv(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttrib3dv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib3dv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib3s(%u, %d, %d, %d)\n", index, x, y, z);
   CALL_VertexAttrib3s(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3sv(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttrib3sv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib3sv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4Nbv(GLuint index, const GLbyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_BYTE);
   _mesa_debug(ctx, "glVertexAttrib4Nbv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4Nbv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4Niv(GLuint index, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glVertexAttrib4Niv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4Niv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4Nsv(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttrib4Nsv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4Nsv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib4Nub(%u, %u, %u, %u, %u)\n", index, x, y, z, w);
   CALL_VertexAttrib4Nub(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4Nubv(GLuint index, const GLubyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_UBYTE);
   _mesa_debug(ctx, "glVertexAttrib4Nubv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4Nubv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4Nuiv(GLuint index, const GLuint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glVertexAttrib4Nuiv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4Nuiv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4Nusv(GLuint index, const GLushort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_USHORT);
   _mesa_debug(ctx, "glVertexAttrib4Nusv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4Nusv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4bv(GLuint index, const GLbyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_BYTE);
   _mesa_debug(ctx, "glVertexAttrib4bv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4bv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib4d(%u, %f, %f, %f, %f)\n", index, x, y, z, w);
   CALL_VertexAttrib4d(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4dv(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttrib4dv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4dv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4iv(GLuint index, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glVertexAttrib4iv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4iv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib4s(%u, %d, %d, %d, %d)\n", index, x, y, z, w);
   CALL_VertexAttrib4s(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4sv(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttrib4sv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4sv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4ubv(GLuint index, const GLubyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_UBYTE);
   _mesa_debug(ctx, "glVertexAttrib4ubv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4ubv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4uiv(GLuint index, const GLuint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glVertexAttrib4uiv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4uiv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4usv(GLuint index, const GLushort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_USHORT);
   _mesa_debug(ctx, "glVertexAttrib4usv(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4usv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribPointer(%u, %d, %s, %s, %d, %p)\n", index, size, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", stride, (void *)pointer);
   CALL_VertexAttribPointer(ctx->Dispatch.RealPublished, (index, size, type, normalized, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 6, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniformMatrix2x3fv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix2x3fv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 8, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniformMatrix2x4fv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix2x4fv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 6, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniformMatrix3x2fv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix3x2fv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 12, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniformMatrix3x4fv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix3x4fv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 8, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniformMatrix4x2fv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix4x2fv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 12, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glUniformMatrix4x3fv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix4x3fv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_BeginConditionalRender(GLuint query, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBeginConditionalRender(%u, %s)\n", query, _mesa_enum_to_string(mode));
   CALL_BeginConditionalRender(ctx->Dispatch.RealPublished, (query, mode));
}

static void GLAPIENTRY
_mesa_trace_BeginTransformFeedback(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBeginTransformFeedback(%s)\n", _mesa_enum_to_string(mode));
   CALL_BeginTransformFeedback(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_BindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindBufferBase(%s, %u, %u)\n", _mesa_enum_to_string(target), index, buffer);
   CALL_BindBufferBase(ctx->Dispatch.RealPublished, (target, index, buffer));
}

static void GLAPIENTRY
_mesa_trace_BindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindBufferRange(%s, %u, %u, %" PRIdPTR ", %" PRIdPTR ")\n", _mesa_enum_to_string(target), index, buffer, (intptr_t)offset, (intptr_t)size);
   CALL_BindBufferRange(ctx->Dispatch.RealPublished, (target, index, buffer, offset, size));
}

static void GLAPIENTRY
_mesa_trace_BindFragDataLocation(GLuint program, GLuint colorNumber, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindFragDataLocation(%u, %u, %s)\n", program, colorNumber, name ? (const char *)name : "(null)");
   CALL_BindFragDataLocation(ctx->Dispatch.RealPublished, (program, colorNumber, name));
}

static void GLAPIENTRY
_mesa_trace_ClampColor(GLenum target, GLenum clamp)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClampColor(%s, %s)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(clamp));
   CALL_ClampColor(ctx->Dispatch.RealPublished, (target, clamp));
}

static void GLAPIENTRY
_mesa_trace_ClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearBufferfi(%s, %d, %f, %d)\n", _mesa_enum_to_string(buffer), drawbuffer, depth, stencil);
   CALL_ClearBufferfi(ctx->Dispatch.RealPublished, (buffer, drawbuffer, depth, stencil));
}

static void GLAPIENTRY
_mesa_trace_ClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearBufferfv(%s, %d, %p)\n", _mesa_enum_to_string(buffer), drawbuffer, (void *)value);
   CALL_ClearBufferfv(ctx->Dispatch.RealPublished, (buffer, drawbuffer, value));
}

static void GLAPIENTRY
_mesa_trace_ClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearBufferiv(%s, %d, %p)\n", _mesa_enum_to_string(buffer), drawbuffer, (void *)value);
   CALL_ClearBufferiv(ctx->Dispatch.RealPublished, (buffer, drawbuffer, value));
}

static void GLAPIENTRY
_mesa_trace_ClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearBufferuiv(%s, %d, %p)\n", _mesa_enum_to_string(buffer), drawbuffer, (void *)value);
   CALL_ClearBufferuiv(ctx->Dispatch.RealPublished, (buffer, drawbuffer, value));
}

static void GLAPIENTRY
_mesa_trace_ColorMaski(GLuint buf, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorMaski(%u, %s, %s, %s, %s)\n", buf, r ? "GL_TRUE" : "GL_FALSE", g ? "GL_TRUE" : "GL_FALSE", b ? "GL_TRUE" : "GL_FALSE", a ? "GL_TRUE" : "GL_FALSE");
   CALL_ColorMaski(ctx->Dispatch.RealPublished, (buf, r, g, b, a));
}

static void GLAPIENTRY
_mesa_trace_Disablei(GLenum target, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDisablei(%s, %u)\n", _mesa_enum_to_string(target), index);
   CALL_Disablei(ctx->Dispatch.RealPublished, (target, index));
}

static void GLAPIENTRY
_mesa_trace_Enablei(GLenum target, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEnablei(%s, %u)\n", _mesa_enum_to_string(target), index);
   CALL_Enablei(ctx->Dispatch.RealPublished, (target, index));
}

static void GLAPIENTRY
_mesa_trace_EndConditionalRender(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEndConditionalRender()\n");
   CALL_EndConditionalRender(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_EndTransformFeedback(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEndTransformFeedback()\n");
   CALL_EndTransformFeedback(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_GetBooleani_v(GLenum value, GLuint index, GLboolean *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetBooleani_v(%s, %u, %p)\n", _mesa_enum_to_string(value), index, (void *)data);
   CALL_GetBooleani_v(ctx->Dispatch.RealPublished, (value, index, data));
}

static GLint GLAPIENTRY
_mesa_trace_GetFragDataLocation(GLuint program, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFragDataLocation(%u, %s)\n", program, name ? (const char *)name : "(null)");
   return CALL_GetFragDataLocation(ctx->Dispatch.RealPublished, (program, name));
}

static void GLAPIENTRY
_mesa_trace_GetIntegeri_v(GLenum value, GLuint index, GLint *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetIntegeri_v(%s, %u, %p)\n", _mesa_enum_to_string(value), index, (void *)data);
   CALL_GetIntegeri_v(ctx->Dispatch.RealPublished, (value, index, data));
}

static const GLubyte * GLAPIENTRY
_mesa_trace_GetStringi(GLenum name, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetStringi(%s, %u)\n", _mesa_enum_to_string(name), index);
   return CALL_GetStringi(ctx->Dispatch.RealPublished, (name, index));
}

static void GLAPIENTRY
_mesa_trace_GetTexParameterIiv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexParameterIiv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexParameterIiv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexParameterIuiv(GLenum target, GLenum pname, GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexParameterIuiv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexParameterIuiv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTransformFeedbackVarying(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTransformFeedbackVarying(%u, %u, %d, %p, %p, %p, %p)\n", program, index, bufSize, (void *)length, (void *)size, (void *)type, (void *)name);
   CALL_GetTransformFeedbackVarying(ctx->Dispatch.RealPublished, (program, index, bufSize, length, size, type, name));
}

static void GLAPIENTRY
_mesa_trace_GetUniformuiv(GLuint program, GLint location, GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformuiv(%u, %d, %p)\n", program, location, (void *)params);
   CALL_GetUniformuiv(ctx->Dispatch.RealPublished, (program, location, params));
}

static void GLAPIENTRY
_mesa_trace_GetVertexAttribIiv(GLuint index, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexAttribIiv(%u, %s, %p)\n", index, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetVertexAttribIiv(ctx->Dispatch.RealPublished, (index, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexAttribIuiv(%u, %s, %p)\n", index, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetVertexAttribIuiv(ctx->Dispatch.RealPublished, (index, pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsEnabledi(GLenum target, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsEnabledi(%s, %u)\n", _mesa_enum_to_string(target), index);
   return CALL_IsEnabledi(ctx->Dispatch.RealPublished, (target, index));
}

static void GLAPIENTRY
_mesa_trace_TexParameterIiv(GLenum target, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexParameterIiv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexParameterIiv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexParameterIuiv(GLenum target, GLenum pname, const GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexParameterIuiv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexParameterIuiv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TransformFeedbackVaryings(GLuint program, GLsizei count, const GLchar * const *varyings, GLenum bufferMode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTransformFeedbackVaryings(%u, %d, %s, %s)\n", program, count, varyings ? (const char *)varyings : "(null)", _mesa_enum_to_string(bufferMode));
   CALL_TransformFeedbackVaryings(ctx->Dispatch.RealPublished, (program, count, varyings, bufferMode));
}

static void GLAPIENTRY
_mesa_trace_Uniform1ui(GLint location, GLuint x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform1ui(%d, %u)\n", location, x);
   CALL_Uniform1ui(ctx->Dispatch.RealPublished, (location, x));
}

static void GLAPIENTRY
_mesa_trace_Uniform1uiv(GLint location, GLsizei count, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glUniform1uiv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform1uiv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform2ui(GLint location, GLuint x, GLuint y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform2ui(%d, %u, %u)\n", location, x, y);
   CALL_Uniform2ui(ctx->Dispatch.RealPublished, (location, x, y));
}

static void GLAPIENTRY
_mesa_trace_Uniform2uiv(GLint location, GLsizei count, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glUniform2uiv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform2uiv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform3ui(GLint location, GLuint x, GLuint y, GLuint z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform3ui(%d, %u, %u, %u)\n", location, x, y, z);
   CALL_Uniform3ui(ctx->Dispatch.RealPublished, (location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Uniform3uiv(GLint location, GLsizei count, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glUniform3uiv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform3uiv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform4ui(GLint location, GLuint x, GLuint y, GLuint z, GLuint w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform4ui(%d, %u, %u, %u, %u)\n", location, x, y, z, w);
   CALL_Uniform4ui(ctx->Dispatch.RealPublished, (location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_Uniform4uiv(GLint location, GLsizei count, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glUniform4uiv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform4uiv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI1iv(GLuint index, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI1iv(%u, %p)\n", index, (void *)v);
   CALL_VertexAttribI1iv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI1uiv(GLuint index, const GLuint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI1uiv(%u, %p)\n", index, (void *)v);
   CALL_VertexAttribI1uiv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI4bv(GLuint index, const GLbyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_BYTE);
   _mesa_debug(ctx, "glVertexAttribI4bv(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI4bv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI4sv(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttribI4sv(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI4sv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI4ubv(GLuint index, const GLubyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_UBYTE);
   _mesa_debug(ctx, "glVertexAttribI4ubv(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI4ubv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI4usv(GLuint index, const GLushort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_USHORT);
   _mesa_debug(ctx, "glVertexAttribI4usv(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI4usv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribIPointer(%u, %d, %s, %d, %p)\n", index, size, _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_VertexAttribIPointer(ctx->Dispatch.RealPublished, (index, size, type, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_PrimitiveRestartIndex(GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPrimitiveRestartIndex(%u)\n", index);
   CALL_PrimitiveRestartIndex(ctx->Dispatch.RealPublished, (index));
}

static void GLAPIENTRY
_mesa_trace_TexBuffer(GLenum target, GLenum internalFormat, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexBuffer(%s, %s, %u)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalFormat), buffer);
   CALL_TexBuffer(ctx->Dispatch.RealPublished, (target, internalFormat, buffer));
}

static void GLAPIENTRY
_mesa_trace_FramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferTexture(%s, %s, %u, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), texture, level);
   CALL_FramebufferTexture(ctx->Dispatch.RealPublished, (target, attachment, texture, level));
}

static void GLAPIENTRY
_mesa_trace_GetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetBufferParameteri64v(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetBufferParameteri64v(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetInteger64i_v(GLenum cap, GLuint index, GLint64 *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetInteger64i_v(%s, %u, %p)\n", _mesa_enum_to_string(cap), index, (void *)data);
   CALL_GetInteger64i_v(ctx->Dispatch.RealPublished, (cap, index, data));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribDivisor(GLuint index, GLuint divisor)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribDivisor(%u, %u)\n", index, divisor);
   CALL_VertexAttribDivisor(ctx->Dispatch.RealPublished, (index, divisor));
}

static void GLAPIENTRY
_mesa_trace_MinSampleShading(GLfloat value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMinSampleShading(%f)\n", value);
   CALL_MinSampleShading(ctx->Dispatch.RealPublished, (value));
}

static void GLAPIENTRY
_mesa_trace_MemoryBarrierByRegion(GLbitfield barriers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMemoryBarrierByRegion(0x%x)\n", barriers);
   CALL_MemoryBarrierByRegion(ctx->Dispatch.RealPublished, (barriers));
}

static void GLAPIENTRY
_mesa_trace_BindProgramARB(GLenum target, GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindProgramARB(%s, %u)\n", _mesa_enum_to_string(target), program);
   CALL_BindProgramARB(ctx->Dispatch.RealPublished, (target, program));
}

static void GLAPIENTRY
_mesa_trace_DeleteProgramsARB(GLsizei n, const GLuint *programs)
{
   GET_CURRENT_CONTEXT(ctx);
   char programs_buf[512];
   _mesa_trace_format_array(programs_buf, sizeof(programs_buf), programs, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteProgramsARB(%d, %s)\n", n, programs_buf);
   CALL_DeleteProgramsARB(ctx->Dispatch.RealPublished, (n, programs));
}

static void GLAPIENTRY
_mesa_trace_GenProgramsARB(GLsizei n, GLuint *programs)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenProgramsARB(%d, %p)\n", n, (void *)programs);
   CALL_GenProgramsARB(ctx->Dispatch.RealPublished, (n, programs));
}

static void GLAPIENTRY
_mesa_trace_GetProgramEnvParameterdvARB(GLenum target, GLuint index, GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramEnvParameterdvARB(%s, %u, %p)\n", _mesa_enum_to_string(target), index, (void *)params);
   CALL_GetProgramEnvParameterdvARB(ctx->Dispatch.RealPublished, (target, index, params));
}

static void GLAPIENTRY
_mesa_trace_GetProgramEnvParameterfvARB(GLenum target, GLuint index, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramEnvParameterfvARB(%s, %u, %p)\n", _mesa_enum_to_string(target), index, (void *)params);
   CALL_GetProgramEnvParameterfvARB(ctx->Dispatch.RealPublished, (target, index, params));
}

static void GLAPIENTRY
_mesa_trace_GetProgramLocalParameterdvARB(GLenum target, GLuint index, GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramLocalParameterdvARB(%s, %u, %p)\n", _mesa_enum_to_string(target), index, (void *)params);
   CALL_GetProgramLocalParameterdvARB(ctx->Dispatch.RealPublished, (target, index, params));
}

static void GLAPIENTRY
_mesa_trace_GetProgramLocalParameterfvARB(GLenum target, GLuint index, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramLocalParameterfvARB(%s, %u, %p)\n", _mesa_enum_to_string(target), index, (void *)params);
   CALL_GetProgramLocalParameterfvARB(ctx->Dispatch.RealPublished, (target, index, params));
}

static void GLAPIENTRY
_mesa_trace_GetProgramStringARB(GLenum target, GLenum pname, GLvoid *string)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramStringARB(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)string);
   CALL_GetProgramStringARB(ctx->Dispatch.RealPublished, (target, pname, string));
}

static void GLAPIENTRY
_mesa_trace_GetProgramivARB(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramivARB(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetProgramivARB(ctx->Dispatch.RealPublished, (target, pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsProgramARB(GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsProgramARB(%u)\n", program);
   return CALL_IsProgramARB(ctx->Dispatch.RealPublished, (program));
}

static void GLAPIENTRY
_mesa_trace_ProgramEnvParameter4dARB(GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramEnvParameter4dARB(%s, %u, %f, %f, %f, %f)\n", _mesa_enum_to_string(target), index, x, y, z, w);
   CALL_ProgramEnvParameter4dARB(ctx->Dispatch.RealPublished, (target, index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramEnvParameter4dvARB(GLenum target, GLuint index, const GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   char params_buf[512];
   _mesa_trace_format_array(params_buf, sizeof(params_buf), params, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramEnvParameter4dvARB(%s, %u, %s)\n", _mesa_enum_to_string(target), index, params_buf);
   CALL_ProgramEnvParameter4dvARB(ctx->Dispatch.RealPublished, (target, index, params));
}

static void GLAPIENTRY
_mesa_trace_ProgramEnvParameter4fARB(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramEnvParameter4fARB(%s, %u, %f, %f, %f, %f)\n", _mesa_enum_to_string(target), index, x, y, z, w);
   CALL_ProgramEnvParameter4fARB(ctx->Dispatch.RealPublished, (target, index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramEnvParameter4fvARB(GLenum target, GLuint index, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   char params_buf[512];
   _mesa_trace_format_array(params_buf, sizeof(params_buf), params, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramEnvParameter4fvARB(%s, %u, %s)\n", _mesa_enum_to_string(target), index, params_buf);
   CALL_ProgramEnvParameter4fvARB(ctx->Dispatch.RealPublished, (target, index, params));
}

static void GLAPIENTRY
_mesa_trace_ProgramLocalParameter4dARB(GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramLocalParameter4dARB(%s, %u, %f, %f, %f, %f)\n", _mesa_enum_to_string(target), index, x, y, z, w);
   CALL_ProgramLocalParameter4dARB(ctx->Dispatch.RealPublished, (target, index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramLocalParameter4dvARB(GLenum target, GLuint index, const GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   char params_buf[512];
   _mesa_trace_format_array(params_buf, sizeof(params_buf), params, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramLocalParameter4dvARB(%s, %u, %s)\n", _mesa_enum_to_string(target), index, params_buf);
   CALL_ProgramLocalParameter4dvARB(ctx->Dispatch.RealPublished, (target, index, params));
}

static void GLAPIENTRY
_mesa_trace_ProgramLocalParameter4fARB(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramLocalParameter4fARB(%s, %u, %f, %f, %f, %f)\n", _mesa_enum_to_string(target), index, x, y, z, w);
   CALL_ProgramLocalParameter4fARB(ctx->Dispatch.RealPublished, (target, index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramLocalParameter4fvARB(GLenum target, GLuint index, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   char params_buf[512];
   _mesa_trace_format_array(params_buf, sizeof(params_buf), params, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramLocalParameter4fvARB(%s, %u, %s)\n", _mesa_enum_to_string(target), index, params_buf);
   CALL_ProgramLocalParameter4fvARB(ctx->Dispatch.RealPublished, (target, index, params));
}

static void GLAPIENTRY
_mesa_trace_ProgramStringARB(GLenum target, GLenum format, GLsizei len, const GLvoid *string)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramStringARB(%s, %s, %d, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(format), len, (void *)string);
   CALL_ProgramStringARB(ctx->Dispatch.RealPublished, (target, format, len, string));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1fARB(GLuint index, GLfloat x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1fARB(%u, %f)\n", index, x);
   CALL_VertexAttrib1fARB(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1fvARB(GLuint index, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1fvARB(%u, %p)\n", index, (void *)v);
   CALL_VertexAttrib1fvARB(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2fARB(GLuint index, GLfloat x, GLfloat y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib2fARB(%u, %f, %f)\n", index, x, y);
   CALL_VertexAttrib2fARB(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2fvARB(GLuint index, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttrib2fvARB(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib2fvARB(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3fARB(GLuint index, GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib3fARB(%u, %f, %f, %f)\n", index, x, y, z);
   CALL_VertexAttrib3fARB(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3fvARB(GLuint index, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttrib3fvARB(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib3fvARB(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4fARB(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib4fARB(%u, %f, %f, %f, %f)\n", index, x, y, z, w);
   CALL_VertexAttrib4fARB(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4fvARB(GLuint index, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttrib4fvARB(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4fvARB(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_AttachObjectARB(GLhandleARB containerObj, GLhandleARB obj)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glAttachObjectARB(%u, %u)\n", (unsigned int)containerObj, (unsigned int)obj);
   CALL_AttachObjectARB(ctx->Dispatch.RealPublished, (containerObj, obj));
}

static GLhandleARB GLAPIENTRY
_mesa_trace_CreateProgramObjectARB(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateProgramObjectARB()\n");
   return CALL_CreateProgramObjectARB(ctx->Dispatch.RealPublished, ());
}

static GLhandleARB GLAPIENTRY
_mesa_trace_CreateShaderObjectARB(GLenum shaderType)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateShaderObjectARB(%s)\n", _mesa_enum_to_string(shaderType));
   return CALL_CreateShaderObjectARB(ctx->Dispatch.RealPublished, (shaderType));
}

static void GLAPIENTRY
_mesa_trace_DeleteObjectARB(GLhandleARB obj)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDeleteObjectARB(%u)\n", (unsigned int)obj);
   CALL_DeleteObjectARB(ctx->Dispatch.RealPublished, (obj));
}

static void GLAPIENTRY
_mesa_trace_DetachObjectARB(GLhandleARB containerObj, GLhandleARB attachedObj)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDetachObjectARB(%u, %u)\n", (unsigned int)containerObj, (unsigned int)attachedObj);
   CALL_DetachObjectARB(ctx->Dispatch.RealPublished, (containerObj, attachedObj));
}

static void GLAPIENTRY
_mesa_trace_GetAttachedObjectsARB(GLhandleARB containerObj, GLsizei maxLength, GLsizei *length, GLhandleARB *infoLog)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetAttachedObjectsARB(%u, %d, %p, %p)\n", (unsigned int)containerObj, maxLength, (void *)length, (void *)infoLog);
   CALL_GetAttachedObjectsARB(ctx->Dispatch.RealPublished, (containerObj, maxLength, length, infoLog));
}

static GLhandleARB GLAPIENTRY
_mesa_trace_GetHandleARB(GLenum pname)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetHandleARB(%s)\n", _mesa_enum_to_string(pname));
   return CALL_GetHandleARB(ctx->Dispatch.RealPublished, (pname));
}

static void GLAPIENTRY
_mesa_trace_GetInfoLogARB(GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *infoLog)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetInfoLogARB(%u, %d, %p, %p)\n", (unsigned int)obj, maxLength, (void *)length, (void *)infoLog);
   CALL_GetInfoLogARB(ctx->Dispatch.RealPublished, (obj, maxLength, length, infoLog));
}

static void GLAPIENTRY
_mesa_trace_GetObjectParameterfvARB(GLhandleARB obj, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetObjectParameterfvARB(%u, %s, %p)\n", (unsigned int)obj, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetObjectParameterfvARB(ctx->Dispatch.RealPublished, (obj, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetObjectParameterivARB(GLhandleARB obj, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetObjectParameterivARB(%u, %s, %p)\n", (unsigned int)obj, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetObjectParameterivARB(ctx->Dispatch.RealPublished, (obj, pname, params));
}

static void GLAPIENTRY
_mesa_trace_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawArraysInstanced(%s, %d, %d, %d)\n", _mesa_enum_to_string(mode), first, count, primcount);
   CALL_DrawArraysInstanced(ctx->Dispatch.RealPublished, (mode, first, count, primcount));
}

static void GLAPIENTRY
_mesa_trace_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instance_count)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsInstanced(%s, %d, %s, %p, %d)\n", _mesa_enum_to_string(mode), count, _mesa_enum_to_string(type), (void *)indices, instance_count);
   CALL_DrawElementsInstanced(ctx->Dispatch.RealPublished, (mode, count, type, indices, instance_count));
}

static void GLAPIENTRY
_mesa_trace_BindFramebuffer(GLenum target, GLuint framebuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindFramebuffer(%s, %u)\n", _mesa_enum_to_string(target), framebuffer);
   CALL_BindFramebuffer(ctx->Dispatch.RealPublished, (target, framebuffer));
}

static void GLAPIENTRY
_mesa_trace_BindRenderbuffer(GLenum target, GLuint renderbuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindRenderbuffer(%s, %u)\n", _mesa_enum_to_string(target), renderbuffer);
   CALL_BindRenderbuffer(ctx->Dispatch.RealPublished, (target, renderbuffer));
}

static void GLAPIENTRY
_mesa_trace_BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlitFramebuffer(%d, %d, %d, %d, %d, %d, %d, %d, 0x%x, %s)\n", srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, _mesa_enum_to_string(filter));
   CALL_BlitFramebuffer(ctx->Dispatch.RealPublished, (srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter));
}

static GLenum GLAPIENTRY
_mesa_trace_CheckFramebufferStatus(GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCheckFramebufferStatus(%s)\n", _mesa_enum_to_string(target));
   return CALL_CheckFramebufferStatus(ctx->Dispatch.RealPublished, (target));
}

static void GLAPIENTRY
_mesa_trace_DeleteFramebuffers(GLsizei n, const GLuint *framebuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   char framebuffers_buf[512];
   _mesa_trace_format_array(framebuffers_buf, sizeof(framebuffers_buf), framebuffers, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteFramebuffers(%d, %s)\n", n, framebuffers_buf);
   CALL_DeleteFramebuffers(ctx->Dispatch.RealPublished, (n, framebuffers));
}

static void GLAPIENTRY
_mesa_trace_DeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   char renderbuffers_buf[512];
   _mesa_trace_format_array(renderbuffers_buf, sizeof(renderbuffers_buf), renderbuffers, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteRenderbuffers(%d, %s)\n", n, renderbuffers_buf);
   CALL_DeleteRenderbuffers(ctx->Dispatch.RealPublished, (n, renderbuffers));
}

static void GLAPIENTRY
_mesa_trace_FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferRenderbuffer(%s, %s, %s, %u)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), _mesa_enum_to_string(renderbuffertarget), renderbuffer);
   CALL_FramebufferRenderbuffer(ctx->Dispatch.RealPublished, (target, attachment, renderbuffertarget, renderbuffer));
}

static void GLAPIENTRY
_mesa_trace_FramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferTexture1D(%s, %s, %s, %u, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), _mesa_enum_to_string(textarget), texture, level);
   CALL_FramebufferTexture1D(ctx->Dispatch.RealPublished, (target, attachment, textarget, texture, level));
}

static void GLAPIENTRY
_mesa_trace_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferTexture2D(%s, %s, %s, %u, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), _mesa_enum_to_string(textarget), texture, level);
   CALL_FramebufferTexture2D(ctx->Dispatch.RealPublished, (target, attachment, textarget, texture, level));
}

static void GLAPIENTRY
_mesa_trace_FramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint layer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferTexture3D(%s, %s, %s, %u, %d, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), _mesa_enum_to_string(textarget), texture, level, layer);
   CALL_FramebufferTexture3D(ctx->Dispatch.RealPublished, (target, attachment, textarget, texture, level, layer));
}

static void GLAPIENTRY
_mesa_trace_FramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferTextureLayer(%s, %s, %u, %d, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), texture, level, layer);
   CALL_FramebufferTextureLayer(ctx->Dispatch.RealPublished, (target, attachment, texture, level, layer));
}

static void GLAPIENTRY
_mesa_trace_GenFramebuffers(GLsizei n, GLuint *framebuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenFramebuffers(%d, %p)\n", n, (void *)framebuffers);
   CALL_GenFramebuffers(ctx->Dispatch.RealPublished, (n, framebuffers));
}

static void GLAPIENTRY
_mesa_trace_GenRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenRenderbuffers(%d, %p)\n", n, (void *)renderbuffers);
   CALL_GenRenderbuffers(ctx->Dispatch.RealPublished, (n, renderbuffers));
}

static void GLAPIENTRY
_mesa_trace_GenerateMipmap(GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenerateMipmap(%s)\n", _mesa_enum_to_string(target));
   CALL_GenerateMipmap(ctx->Dispatch.RealPublished, (target));
}

static void GLAPIENTRY
_mesa_trace_GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFramebufferAttachmentParameteriv(%s, %s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetFramebufferAttachmentParameteriv(ctx->Dispatch.RealPublished, (target, attachment, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetRenderbufferParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetRenderbufferParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsFramebuffer(GLuint framebuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsFramebuffer(%u)\n", framebuffer);
   return CALL_IsFramebuffer(ctx->Dispatch.RealPublished, (framebuffer));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsRenderbuffer(GLuint renderbuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsRenderbuffer(%u)\n", renderbuffer);
   return CALL_IsRenderbuffer(ctx->Dispatch.RealPublished, (renderbuffer));
}

static void GLAPIENTRY
_mesa_trace_RenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRenderbufferStorage(%s, %s, %d, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), width, height);
   CALL_RenderbufferStorage(ctx->Dispatch.RealPublished, (target, internalformat, width, height));
}

static void GLAPIENTRY
_mesa_trace_RenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRenderbufferStorageMultisample(%s, %d, %s, %d, %d)\n", _mesa_enum_to_string(target), samples, _mesa_enum_to_string(internalformat), width, height);
   CALL_RenderbufferStorageMultisample(ctx->Dispatch.RealPublished, (target, samples, internalformat, width, height));
}

static void GLAPIENTRY
_mesa_trace_FlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFlushMappedBufferRange(%s, %" PRIdPTR ", %" PRIdPTR ")\n", _mesa_enum_to_string(target), (intptr_t)offset, (intptr_t)length);
   CALL_FlushMappedBufferRange(ctx->Dispatch.RealPublished, (target, offset, length));
}

static GLvoid * GLAPIENTRY
_mesa_trace_MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapBufferRange(%s, %" PRIdPTR ", %" PRIdPTR ", 0x%x)\n", _mesa_enum_to_string(target), (intptr_t)offset, (intptr_t)length, access);
   return CALL_MapBufferRange(ctx->Dispatch.RealPublished, (target, offset, length, access));
}

static void GLAPIENTRY
_mesa_trace_BindVertexArray(GLuint array)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindVertexArray(%u)\n", array);
   CALL_BindVertexArray(ctx->Dispatch.RealPublished, (array));
}

static void GLAPIENTRY
_mesa_trace_DeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
   GET_CURRENT_CONTEXT(ctx);
   char arrays_buf[512];
   _mesa_trace_format_array(arrays_buf, sizeof(arrays_buf), arrays, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteVertexArrays(%d, %s)\n", n, arrays_buf);
   CALL_DeleteVertexArrays(ctx->Dispatch.RealPublished, (n, arrays));
}

static void GLAPIENTRY
_mesa_trace_GenVertexArrays(GLsizei n, GLuint *arrays)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenVertexArrays(%d, %p)\n", n, (void *)arrays);
   CALL_GenVertexArrays(ctx->Dispatch.RealPublished, (n, arrays));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsVertexArray(GLuint array)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsVertexArray(%u)\n", array);
   return CALL_IsVertexArray(ctx->Dispatch.RealPublished, (array));
}

static void GLAPIENTRY
_mesa_trace_GetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveUniformBlockName(%u, %u, %d, %p, %p)\n", program, uniformBlockIndex, bufSize, (void *)length, (void *)uniformBlockName);
   CALL_GetActiveUniformBlockName(ctx->Dispatch.RealPublished, (program, uniformBlockIndex, bufSize, length, uniformBlockName));
}

static void GLAPIENTRY
_mesa_trace_GetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveUniformBlockiv(%u, %u, %s, %p)\n", program, uniformBlockIndex, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetActiveUniformBlockiv(ctx->Dispatch.RealPublished, (program, uniformBlockIndex, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetActiveUniformName(GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformName)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveUniformName(%u, %u, %d, %p, %p)\n", program, uniformIndex, bufSize, (void *)length, (void *)uniformName);
   CALL_GetActiveUniformName(ctx->Dispatch.RealPublished, (program, uniformIndex, bufSize, length, uniformName));
}

static void GLAPIENTRY
_mesa_trace_GetActiveUniformsiv(GLuint program, GLsizei uniformCount, const GLuint *uniformIndices, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveUniformsiv(%u, %d, %p, %s, %p)\n", program, uniformCount, (void *)uniformIndices, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetActiveUniformsiv(ctx->Dispatch.RealPublished, (program, uniformCount, uniformIndices, pname, params));
}

static GLuint GLAPIENTRY
_mesa_trace_GetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformBlockIndex(%u, %s)\n", program, uniformBlockName ? (const char *)uniformBlockName : "(null)");
   return CALL_GetUniformBlockIndex(ctx->Dispatch.RealPublished, (program, uniformBlockName));
}

static void GLAPIENTRY
_mesa_trace_GetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar * const *uniformNames, GLuint *uniformIndices)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformIndices(%u, %d, %s, %p)\n", program, uniformCount, uniformNames ? (const char *)uniformNames : "(null)", (void *)uniformIndices);
   CALL_GetUniformIndices(ctx->Dispatch.RealPublished, (program, uniformCount, uniformNames, uniformIndices));
}

static void GLAPIENTRY
_mesa_trace_UniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniformBlockBinding(%u, %u, %u)\n", program, uniformBlockIndex, uniformBlockBinding);
   CALL_UniformBlockBinding(ctx->Dispatch.RealPublished, (program, uniformBlockIndex, uniformBlockBinding));
}

static void GLAPIENTRY
_mesa_trace_CopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyBufferSubData(%s, %s, %" PRIdPTR ", %" PRIdPTR ", %" PRIdPTR ")\n", _mesa_enum_to_string(readTarget), _mesa_enum_to_string(writeTarget), (intptr_t)readOffset, (intptr_t)writeOffset, (intptr_t)size);
   CALL_CopyBufferSubData(ctx->Dispatch.RealPublished, (readTarget, writeTarget, readOffset, writeOffset, size));
}

static GLenum GLAPIENTRY
_mesa_trace_ClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClientWaitSync(%p, 0x%x, %" PRIu64 ")\n", (void *)sync, flags, (uint64_t)timeout);
   return CALL_ClientWaitSync(ctx->Dispatch.RealPublished, (sync, flags, timeout));
}

static void GLAPIENTRY
_mesa_trace_DeleteSync(GLsync sync)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDeleteSync(%p)\n", (void *)sync);
   CALL_DeleteSync(ctx->Dispatch.RealPublished, (sync));
}

static GLsync GLAPIENTRY
_mesa_trace_FenceSync(GLenum condition, GLbitfield flags)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFenceSync(%s, 0x%x)\n", _mesa_enum_to_string(condition), flags);
   return CALL_FenceSync(ctx->Dispatch.RealPublished, (condition, flags));
}

static void GLAPIENTRY
_mesa_trace_GetInteger64v(GLenum pname, GLint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetInteger64v(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_GetInteger64v(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei *length, GLint *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSynciv(%p, %s, %d, %p, %p)\n", (void *)sync, _mesa_enum_to_string(pname), bufSize, (void *)length, (void *)values);
   CALL_GetSynciv(ctx->Dispatch.RealPublished, (sync, pname, bufSize, length, values));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsSync(GLsync sync)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsSync(%p)\n", (void *)sync);
   return CALL_IsSync(ctx->Dispatch.RealPublished, (sync));
}

static void GLAPIENTRY
_mesa_trace_WaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWaitSync(%p, 0x%x, %" PRIu64 ")\n", (void *)sync, flags, (uint64_t)timeout);
   CALL_WaitSync(ctx->Dispatch.RealPublished, (sync, flags, timeout));
}

static void GLAPIENTRY
_mesa_trace_DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsBaseVertex(%s, %d, %s, %p, %d)\n", _mesa_enum_to_string(mode), count, _mesa_enum_to_string(type), (void *)indices, basevertex);
   CALL_DrawElementsBaseVertex(ctx->Dispatch.RealPublished, (mode, count, type, indices, basevertex));
}

static void GLAPIENTRY
_mesa_trace_DrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei primcount, GLint basevertex)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsInstancedBaseVertex(%s, %d, %s, %p, %d, %d)\n", _mesa_enum_to_string(mode), count, _mesa_enum_to_string(type), (void *)indices, primcount, basevertex);
   CALL_DrawElementsInstancedBaseVertex(ctx->Dispatch.RealPublished, (mode, count, type, indices, primcount, basevertex));
}

static void GLAPIENTRY
_mesa_trace_DrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawRangeElementsBaseVertex(%s, %u, %u, %d, %s, %p, %d)\n", _mesa_enum_to_string(mode), start, end, count, _mesa_enum_to_string(type), (void *)indices, basevertex);
   CALL_DrawRangeElementsBaseVertex(ctx->Dispatch.RealPublished, (mode, start, end, count, type, indices, basevertex));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type, const GLvoid * const *indices, GLsizei primcount, const GLint *basevertex)
{
   GET_CURRENT_CONTEXT(ctx);
   char count_buf[512];
   _mesa_trace_format_array(count_buf, sizeof(count_buf), count, (size_t)primcount, MESA_TRACE_ELEM_INT);
   char basevertex_buf[512];
   _mesa_trace_format_array(basevertex_buf, sizeof(basevertex_buf), basevertex, (size_t)primcount, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultiDrawElementsBaseVertex(%s, %s, %s, %p, %d, %s)\n", _mesa_enum_to_string(mode), count_buf, _mesa_enum_to_string(type), (void *)indices, primcount, basevertex_buf);
   CALL_MultiDrawElementsBaseVertex(ctx->Dispatch.RealPublished, (mode, count, type, indices, primcount, basevertex));
}

static void GLAPIENTRY
_mesa_trace_ProvokingVertex(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProvokingVertex(%s)\n", _mesa_enum_to_string(mode));
   CALL_ProvokingVertex(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_GetMultisamplefv(GLenum pname, GLuint index, GLfloat *val)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultisamplefv(%s, %u, %p)\n", _mesa_enum_to_string(pname), index, (void *)val);
   CALL_GetMultisamplefv(ctx->Dispatch.RealPublished, (pname, index, val));
}

static void GLAPIENTRY
_mesa_trace_SampleMaski(GLuint index, GLbitfield mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSampleMaski(%u, 0x%x)\n", index, mask);
   CALL_SampleMaski(ctx->Dispatch.RealPublished, (index, mask));
}

static void GLAPIENTRY
_mesa_trace_TexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexImage2DMultisample(%s, %d, %s, %d, %d, %s)\n", _mesa_enum_to_string(target), samples, _mesa_enum_to_string(internalformat), width, height, fixedsamplelocations ? "GL_TRUE" : "GL_FALSE");
   CALL_TexImage2DMultisample(ctx->Dispatch.RealPublished, (target, samples, internalformat, width, height, fixedsamplelocations));
}

static void GLAPIENTRY
_mesa_trace_TexImage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexImage3DMultisample(%s, %d, %s, %d, %d, %d, %s)\n", _mesa_enum_to_string(target), samples, _mesa_enum_to_string(internalformat), width, height, depth, fixedsamplelocations ? "GL_TRUE" : "GL_FALSE");
   CALL_TexImage3DMultisample(ctx->Dispatch.RealPublished, (target, samples, internalformat, width, height, depth, fixedsamplelocations));
}

static void GLAPIENTRY
_mesa_trace_BlendEquationSeparateiARB(GLuint buf, GLenum modeRGB, GLenum modeA)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendEquationSeparateiARB(%u, %s, %s)\n", buf, _mesa_enum_to_string(modeRGB), _mesa_enum_to_string(modeA));
   CALL_BlendEquationSeparateiARB(ctx->Dispatch.RealPublished, (buf, modeRGB, modeA));
}

static void GLAPIENTRY
_mesa_trace_BlendEquationiARB(GLuint buf, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendEquationiARB(%u, %s)\n", buf, _mesa_enum_to_string(mode));
   CALL_BlendEquationiARB(ctx->Dispatch.RealPublished, (buf, mode));
}

static void GLAPIENTRY
_mesa_trace_BlendFuncSeparateiARB(GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendFuncSeparateiARB(%u, %s, %s, %s, %s)\n", buf, _mesa_enum_to_string(srcRGB), _mesa_enum_to_string(dstRGB), _mesa_enum_to_string(srcA), _mesa_enum_to_string(dstA));
   CALL_BlendFuncSeparateiARB(ctx->Dispatch.RealPublished, (buf, srcRGB, dstRGB, srcA, dstA));
}

static void GLAPIENTRY
_mesa_trace_BlendFunciARB(GLuint buf, GLenum src, GLenum dst)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendFunciARB(%u, %s, %s)\n", buf, _mesa_enum_to_string(src), _mesa_enum_to_string(dst));
   CALL_BlendFunciARB(ctx->Dispatch.RealPublished, (buf, src, dst));
}

static void GLAPIENTRY
_mesa_trace_BindFragDataLocationIndexed(GLuint program, GLuint colorNumber, GLuint index, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindFragDataLocationIndexed(%u, %u, %u, %s)\n", program, colorNumber, index, name ? (const char *)name : "(null)");
   CALL_BindFragDataLocationIndexed(ctx->Dispatch.RealPublished, (program, colorNumber, index, name));
}

static GLint GLAPIENTRY
_mesa_trace_GetFragDataIndex(GLuint program, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFragDataIndex(%u, %s)\n", program, name ? (const char *)name : "(null)");
   return CALL_GetFragDataIndex(ctx->Dispatch.RealPublished, (program, name));
}

static void GLAPIENTRY
_mesa_trace_BindSampler(GLuint unit, GLuint sampler)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindSampler(%u, %u)\n", unit, sampler);
   CALL_BindSampler(ctx->Dispatch.RealPublished, (unit, sampler));
}

static void GLAPIENTRY
_mesa_trace_DeleteSamplers(GLsizei count, const GLuint *samplers)
{
   GET_CURRENT_CONTEXT(ctx);
   char samplers_buf[512];
   _mesa_trace_format_array(samplers_buf, sizeof(samplers_buf), samplers, (size_t)count, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteSamplers(%d, %s)\n", count, samplers_buf);
   CALL_DeleteSamplers(ctx->Dispatch.RealPublished, (count, samplers));
}

static void GLAPIENTRY
_mesa_trace_GenSamplers(GLsizei count, GLuint *samplers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenSamplers(%d, %p)\n", count, (void *)samplers);
   CALL_GenSamplers(ctx->Dispatch.RealPublished, (count, samplers));
}

static void GLAPIENTRY
_mesa_trace_GetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSamplerParameterIiv(%u, %s, %p)\n", sampler, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetSamplerParameterIiv(ctx->Dispatch.RealPublished, (sampler, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSamplerParameterIuiv(%u, %s, %p)\n", sampler, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetSamplerParameterIuiv(ctx->Dispatch.RealPublished, (sampler, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSamplerParameterfv(%u, %s, %p)\n", sampler, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetSamplerParameterfv(ctx->Dispatch.RealPublished, (sampler, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSamplerParameteriv(%u, %s, %p)\n", sampler, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetSamplerParameteriv(ctx->Dispatch.RealPublished, (sampler, pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsSampler(GLuint sampler)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsSampler(%u)\n", sampler);
   return CALL_IsSampler(ctx->Dispatch.RealPublished, (sampler));
}

static void GLAPIENTRY
_mesa_trace_SamplerParameterIiv(GLuint sampler, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSamplerParameterIiv(%u, %s, %p)\n", sampler, _mesa_enum_to_string(pname), (void *)params);
   CALL_SamplerParameterIiv(ctx->Dispatch.RealPublished, (sampler, pname, params));
}

static void GLAPIENTRY
_mesa_trace_SamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSamplerParameterIuiv(%u, %s, %p)\n", sampler, _mesa_enum_to_string(pname), (void *)params);
   CALL_SamplerParameterIuiv(ctx->Dispatch.RealPublished, (sampler, pname, params));
}

static void GLAPIENTRY
_mesa_trace_SamplerParameterf(GLuint sampler, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSamplerParameterf(%u, %s, %f)\n", sampler, _mesa_enum_to_string(pname), param);
   CALL_SamplerParameterf(ctx->Dispatch.RealPublished, (sampler, pname, param));
}

static void GLAPIENTRY
_mesa_trace_SamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSamplerParameterfv(%u, %s, %p)\n", sampler, _mesa_enum_to_string(pname), (void *)params);
   CALL_SamplerParameterfv(ctx->Dispatch.RealPublished, (sampler, pname, params));
}

static void GLAPIENTRY
_mesa_trace_SamplerParameteri(GLuint sampler, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSamplerParameteri(%u, %s, %d)\n", sampler, _mesa_enum_to_string(pname), param);
   CALL_SamplerParameteri(ctx->Dispatch.RealPublished, (sampler, pname, param));
}

static void GLAPIENTRY
_mesa_trace_SamplerParameteriv(GLuint sampler, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSamplerParameteriv(%u, %s, %p)\n", sampler, _mesa_enum_to_string(pname), (void *)params);
   CALL_SamplerParameteriv(ctx->Dispatch.RealPublished, (sampler, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetQueryObjecti64v(GLuint id, GLenum pname, GLint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryObjecti64v(%u, %s, %p)\n", id, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetQueryObjecti64v(ctx->Dispatch.RealPublished, (id, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetQueryObjectui64v(GLuint id, GLenum pname, GLuint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryObjectui64v(%u, %s, %p)\n", id, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetQueryObjectui64v(ctx->Dispatch.RealPublished, (id, pname, params));
}

static void GLAPIENTRY
_mesa_trace_QueryCounter(GLuint id, GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glQueryCounter(%u, %s)\n", id, _mesa_enum_to_string(target));
   CALL_QueryCounter(ctx->Dispatch.RealPublished, (id, target));
}

static void GLAPIENTRY
_mesa_trace_ColorP3ui(GLenum type, GLuint color)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorP3ui(%s, %u)\n", _mesa_enum_to_string(type), color);
   CALL_ColorP3ui(ctx->Dispatch.RealPublished, (type, color));
}

static void GLAPIENTRY
_mesa_trace_ColorP3uiv(GLenum type, const GLuint *color)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorP3uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)color);
   CALL_ColorP3uiv(ctx->Dispatch.RealPublished, (type, color));
}

static void GLAPIENTRY
_mesa_trace_ColorP4ui(GLenum type, GLuint color)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorP4ui(%s, %u)\n", _mesa_enum_to_string(type), color);
   CALL_ColorP4ui(ctx->Dispatch.RealPublished, (type, color));
}

static void GLAPIENTRY
_mesa_trace_ColorP4uiv(GLenum type, const GLuint *color)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorP4uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)color);
   CALL_ColorP4uiv(ctx->Dispatch.RealPublished, (type, color));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoordP1ui(GLenum texture, GLenum type, GLuint coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoordP1ui(%s, %s, %u)\n", _mesa_enum_to_string(texture), _mesa_enum_to_string(type), coords);
   CALL_MultiTexCoordP1ui(ctx->Dispatch.RealPublished, (texture, type, coords));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoordP1uiv(GLenum texture, GLenum type, const GLuint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoordP1uiv(%s, %s, %p)\n", _mesa_enum_to_string(texture), _mesa_enum_to_string(type), (void *)coords);
   CALL_MultiTexCoordP1uiv(ctx->Dispatch.RealPublished, (texture, type, coords));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoordP2ui(GLenum texture, GLenum type, GLuint coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoordP2ui(%s, %s, %u)\n", _mesa_enum_to_string(texture), _mesa_enum_to_string(type), coords);
   CALL_MultiTexCoordP2ui(ctx->Dispatch.RealPublished, (texture, type, coords));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoordP2uiv(GLenum texture, GLenum type, const GLuint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoordP2uiv(%s, %s, %p)\n", _mesa_enum_to_string(texture), _mesa_enum_to_string(type), (void *)coords);
   CALL_MultiTexCoordP2uiv(ctx->Dispatch.RealPublished, (texture, type, coords));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoordP3ui(GLenum texture, GLenum type, GLuint coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoordP3ui(%s, %s, %u)\n", _mesa_enum_to_string(texture), _mesa_enum_to_string(type), coords);
   CALL_MultiTexCoordP3ui(ctx->Dispatch.RealPublished, (texture, type, coords));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoordP3uiv(GLenum texture, GLenum type, const GLuint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoordP3uiv(%s, %s, %p)\n", _mesa_enum_to_string(texture), _mesa_enum_to_string(type), (void *)coords);
   CALL_MultiTexCoordP3uiv(ctx->Dispatch.RealPublished, (texture, type, coords));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoordP4ui(GLenum texture, GLenum type, GLuint coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoordP4ui(%s, %s, %u)\n", _mesa_enum_to_string(texture), _mesa_enum_to_string(type), coords);
   CALL_MultiTexCoordP4ui(ctx->Dispatch.RealPublished, (texture, type, coords));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoordP4uiv(GLenum texture, GLenum type, const GLuint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoordP4uiv(%s, %s, %p)\n", _mesa_enum_to_string(texture), _mesa_enum_to_string(type), (void *)coords);
   CALL_MultiTexCoordP4uiv(ctx->Dispatch.RealPublished, (texture, type, coords));
}

static void GLAPIENTRY
_mesa_trace_NormalP3ui(GLenum type, GLuint coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormalP3ui(%s, %u)\n", _mesa_enum_to_string(type), coords);
   CALL_NormalP3ui(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_NormalP3uiv(GLenum type, const GLuint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormalP3uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)coords);
   CALL_NormalP3uiv(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColorP3ui(GLenum type, GLuint color)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColorP3ui(%s, %u)\n", _mesa_enum_to_string(type), color);
   CALL_SecondaryColorP3ui(ctx->Dispatch.RealPublished, (type, color));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColorP3uiv(GLenum type, const GLuint *color)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColorP3uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)color);
   CALL_SecondaryColorP3uiv(ctx->Dispatch.RealPublished, (type, color));
}

static void GLAPIENTRY
_mesa_trace_TexCoordP1ui(GLenum type, GLuint coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordP1ui(%s, %u)\n", _mesa_enum_to_string(type), coords);
   CALL_TexCoordP1ui(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_TexCoordP1uiv(GLenum type, const GLuint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordP1uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)coords);
   CALL_TexCoordP1uiv(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_TexCoordP2ui(GLenum type, GLuint coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordP2ui(%s, %u)\n", _mesa_enum_to_string(type), coords);
   CALL_TexCoordP2ui(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_TexCoordP2uiv(GLenum type, const GLuint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordP2uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)coords);
   CALL_TexCoordP2uiv(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_TexCoordP3ui(GLenum type, GLuint coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordP3ui(%s, %u)\n", _mesa_enum_to_string(type), coords);
   CALL_TexCoordP3ui(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_TexCoordP3uiv(GLenum type, const GLuint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordP3uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)coords);
   CALL_TexCoordP3uiv(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_TexCoordP4ui(GLenum type, GLuint coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordP4ui(%s, %u)\n", _mesa_enum_to_string(type), coords);
   CALL_TexCoordP4ui(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_TexCoordP4uiv(GLenum type, const GLuint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordP4uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)coords);
   CALL_TexCoordP4uiv(ctx->Dispatch.RealPublished, (type, coords));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribP1ui(GLuint index, GLenum type, GLboolean normalized, GLuint value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribP1ui(%u, %s, %s, %u)\n", index, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", value);
   CALL_VertexAttribP1ui(ctx->Dispatch.RealPublished, (index, type, normalized, value));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribP1uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribP1uiv(%u, %s, %s, %p)\n", index, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", (void *)value);
   CALL_VertexAttribP1uiv(ctx->Dispatch.RealPublished, (index, type, normalized, value));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribP2ui(GLuint index, GLenum type, GLboolean normalized, GLuint value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribP2ui(%u, %s, %s, %u)\n", index, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", value);
   CALL_VertexAttribP2ui(ctx->Dispatch.RealPublished, (index, type, normalized, value));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribP2uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribP2uiv(%u, %s, %s, %p)\n", index, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", (void *)value);
   CALL_VertexAttribP2uiv(ctx->Dispatch.RealPublished, (index, type, normalized, value));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribP3ui(GLuint index, GLenum type, GLboolean normalized, GLuint value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribP3ui(%u, %s, %s, %u)\n", index, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", value);
   CALL_VertexAttribP3ui(ctx->Dispatch.RealPublished, (index, type, normalized, value));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribP3uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribP3uiv(%u, %s, %s, %p)\n", index, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", (void *)value);
   CALL_VertexAttribP3uiv(ctx->Dispatch.RealPublished, (index, type, normalized, value));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribP4ui(GLuint index, GLenum type, GLboolean normalized, GLuint value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribP4ui(%u, %s, %s, %u)\n", index, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", value);
   CALL_VertexAttribP4ui(ctx->Dispatch.RealPublished, (index, type, normalized, value));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribP4uiv(GLuint index, GLenum type, GLboolean normalized, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribP4uiv(%u, %s, %s, %p)\n", index, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", (void *)value);
   CALL_VertexAttribP4uiv(ctx->Dispatch.RealPublished, (index, type, normalized, value));
}

static void GLAPIENTRY
_mesa_trace_VertexP2ui(GLenum type, GLuint value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexP2ui(%s, %u)\n", _mesa_enum_to_string(type), value);
   CALL_VertexP2ui(ctx->Dispatch.RealPublished, (type, value));
}

static void GLAPIENTRY
_mesa_trace_VertexP2uiv(GLenum type, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexP2uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)value);
   CALL_VertexP2uiv(ctx->Dispatch.RealPublished, (type, value));
}

static void GLAPIENTRY
_mesa_trace_VertexP3ui(GLenum type, GLuint value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexP3ui(%s, %u)\n", _mesa_enum_to_string(type), value);
   CALL_VertexP3ui(ctx->Dispatch.RealPublished, (type, value));
}

static void GLAPIENTRY
_mesa_trace_VertexP3uiv(GLenum type, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexP3uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)value);
   CALL_VertexP3uiv(ctx->Dispatch.RealPublished, (type, value));
}

static void GLAPIENTRY
_mesa_trace_VertexP4ui(GLenum type, GLuint value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexP4ui(%s, %u)\n", _mesa_enum_to_string(type), value);
   CALL_VertexP4ui(ctx->Dispatch.RealPublished, (type, value));
}

static void GLAPIENTRY
_mesa_trace_VertexP4uiv(GLenum type, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexP4uiv(%s, %p)\n", _mesa_enum_to_string(type), (void *)value);
   CALL_VertexP4uiv(ctx->Dispatch.RealPublished, (type, value));
}

static void GLAPIENTRY
_mesa_trace_DrawArraysIndirect(GLenum mode, const GLvoid *indirect)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawArraysIndirect(%s, %p)\n", _mesa_enum_to_string(mode), (void *)indirect);
   CALL_DrawArraysIndirect(ctx->Dispatch.RealPublished, (mode, indirect));
}

static void GLAPIENTRY
_mesa_trace_DrawElementsIndirect(GLenum mode, GLenum type, const GLvoid *indirect)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsIndirect(%s, %s, %p)\n", _mesa_enum_to_string(mode), _mesa_enum_to_string(type), (void *)indirect);
   CALL_DrawElementsIndirect(ctx->Dispatch.RealPublished, (mode, type, indirect));
}

static void GLAPIENTRY
_mesa_trace_GetUniformdv(GLuint program, GLint location, GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformdv(%u, %d, %p)\n", program, location, (void *)params);
   CALL_GetUniformdv(ctx->Dispatch.RealPublished, (program, location, params));
}

static void GLAPIENTRY
_mesa_trace_Uniform1d(GLint location, GLdouble x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform1d(%d, %f)\n", location, x);
   CALL_Uniform1d(ctx->Dispatch.RealPublished, (location, x));
}

static void GLAPIENTRY
_mesa_trace_Uniform1dv(GLint location, GLsizei count, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniform1dv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform1dv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform2d(GLint location, GLdouble x, GLdouble y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform2d(%d, %f, %f)\n", location, x, y);
   CALL_Uniform2d(ctx->Dispatch.RealPublished, (location, x, y));
}

static void GLAPIENTRY
_mesa_trace_Uniform2dv(GLint location, GLsizei count, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniform2dv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform2dv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform3d(GLint location, GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform3d(%d, %f, %f, %f)\n", location, x, y, z);
   CALL_Uniform3d(ctx->Dispatch.RealPublished, (location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Uniform3dv(GLint location, GLsizei count, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniform3dv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform3dv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform4d(GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform4d(%d, %f, %f, %f, %f)\n", location, x, y, z, w);
   CALL_Uniform4d(ctx->Dispatch.RealPublished, (location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_Uniform4dv(GLint location, GLsizei count, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniform4dv(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform4dv(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix2dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniformMatrix2dv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix2dv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix2x3dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 6, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniformMatrix2x3dv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix2x3dv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix2x4dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 8, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniformMatrix2x4dv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix2x4dv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix3dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 9, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniformMatrix3dv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix3dv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix3x2dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 6, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniformMatrix3x2dv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix3x2dv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix3x4dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 12, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniformMatrix3x4dv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix3x4dv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix4dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniformMatrix4dv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix4dv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix4x2dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 8, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniformMatrix4x2dv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix4x2dv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UniformMatrix4x3dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 12, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glUniformMatrix4x3dv(%d, %d, %s, %s)\n", location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_UniformMatrix4x3dv(ctx->Dispatch.RealPublished, (location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_GetActiveSubroutineName(GLuint program, GLenum shadertype, GLuint index, GLsizei bufsize, GLsizei *length, GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveSubroutineName(%u, %s, %u, %d, %p, %p)\n", program, _mesa_enum_to_string(shadertype), index, bufsize, (void *)length, (void *)name);
   CALL_GetActiveSubroutineName(ctx->Dispatch.RealPublished, (program, shadertype, index, bufsize, length, name));
}

static void GLAPIENTRY
_mesa_trace_GetActiveSubroutineUniformName(GLuint program, GLenum shadertype, GLuint index, GLsizei bufsize, GLsizei *length, GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveSubroutineUniformName(%u, %s, %u, %d, %p, %p)\n", program, _mesa_enum_to_string(shadertype), index, bufsize, (void *)length, (void *)name);
   CALL_GetActiveSubroutineUniformName(ctx->Dispatch.RealPublished, (program, shadertype, index, bufsize, length, name));
}

static void GLAPIENTRY
_mesa_trace_GetActiveSubroutineUniformiv(GLuint program, GLenum shadertype, GLuint index, GLenum pname, GLint *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveSubroutineUniformiv(%u, %s, %u, %s, %p)\n", program, _mesa_enum_to_string(shadertype), index, _mesa_enum_to_string(pname), (void *)values);
   CALL_GetActiveSubroutineUniformiv(ctx->Dispatch.RealPublished, (program, shadertype, index, pname, values));
}

static void GLAPIENTRY
_mesa_trace_GetProgramStageiv(GLuint program, GLenum shadertype, GLenum pname, GLint *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramStageiv(%u, %s, %s, %p)\n", program, _mesa_enum_to_string(shadertype), _mesa_enum_to_string(pname), (void *)values);
   CALL_GetProgramStageiv(ctx->Dispatch.RealPublished, (program, shadertype, pname, values));
}

static GLuint GLAPIENTRY
_mesa_trace_GetSubroutineIndex(GLuint program, GLenum shadertype, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSubroutineIndex(%u, %s, %s)\n", program, _mesa_enum_to_string(shadertype), name ? (const char *)name : "(null)");
   return CALL_GetSubroutineIndex(ctx->Dispatch.RealPublished, (program, shadertype, name));
}

static GLint GLAPIENTRY
_mesa_trace_GetSubroutineUniformLocation(GLuint program, GLenum shadertype, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSubroutineUniformLocation(%u, %s, %s)\n", program, _mesa_enum_to_string(shadertype), name ? (const char *)name : "(null)");
   return CALL_GetSubroutineUniformLocation(ctx->Dispatch.RealPublished, (program, shadertype, name));
}

static void GLAPIENTRY
_mesa_trace_GetUniformSubroutineuiv(GLenum shadertype, GLint location, GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformSubroutineuiv(%s, %d, %p)\n", _mesa_enum_to_string(shadertype), location, (void *)params);
   CALL_GetUniformSubroutineuiv(ctx->Dispatch.RealPublished, (shadertype, location, params));
}

static void GLAPIENTRY
_mesa_trace_UniformSubroutinesuiv(GLenum shadertype, GLsizei count, const GLuint *indices)
{
   GET_CURRENT_CONTEXT(ctx);
   char indices_buf[512];
   _mesa_trace_format_array(indices_buf, sizeof(indices_buf), indices, (size_t)count, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glUniformSubroutinesuiv(%s, %d, %s)\n", _mesa_enum_to_string(shadertype), count, indices_buf);
   CALL_UniformSubroutinesuiv(ctx->Dispatch.RealPublished, (shadertype, count, indices));
}

static void GLAPIENTRY
_mesa_trace_PatchParameterfv(GLenum pname, const GLfloat *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPatchParameterfv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)values);
   CALL_PatchParameterfv(ctx->Dispatch.RealPublished, (pname, values));
}

static void GLAPIENTRY
_mesa_trace_PatchParameteri(GLenum pname, GLint value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPatchParameteri(%s, %d)\n", _mesa_enum_to_string(pname), value);
   CALL_PatchParameteri(ctx->Dispatch.RealPublished, (pname, value));
}

static void GLAPIENTRY
_mesa_trace_BindTransformFeedback(GLenum target, GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindTransformFeedback(%s, %u)\n", _mesa_enum_to_string(target), id);
   CALL_BindTransformFeedback(ctx->Dispatch.RealPublished, (target, id));
}

static void GLAPIENTRY
_mesa_trace_DeleteTransformFeedbacks(GLsizei n, const GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);
   char ids_buf[512];
   _mesa_trace_format_array(ids_buf, sizeof(ids_buf), ids, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteTransformFeedbacks(%d, %s)\n", n, ids_buf);
   CALL_DeleteTransformFeedbacks(ctx->Dispatch.RealPublished, (n, ids));
}

static void GLAPIENTRY
_mesa_trace_DrawTransformFeedback(GLenum mode, GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawTransformFeedback(%s, %u)\n", _mesa_enum_to_string(mode), id);
   CALL_DrawTransformFeedback(ctx->Dispatch.RealPublished, (mode, id));
}

static void GLAPIENTRY
_mesa_trace_GenTransformFeedbacks(GLsizei n, GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenTransformFeedbacks(%d, %p)\n", n, (void *)ids);
   CALL_GenTransformFeedbacks(ctx->Dispatch.RealPublished, (n, ids));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsTransformFeedback(GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsTransformFeedback(%u)\n", id);
   return CALL_IsTransformFeedback(ctx->Dispatch.RealPublished, (id));
}

static void GLAPIENTRY
_mesa_trace_PauseTransformFeedback(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPauseTransformFeedback()\n");
   CALL_PauseTransformFeedback(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_ResumeTransformFeedback(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glResumeTransformFeedback()\n");
   CALL_ResumeTransformFeedback(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_BeginQueryIndexed(GLenum target, GLuint index, GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBeginQueryIndexed(%s, %u, %u)\n", _mesa_enum_to_string(target), index, id);
   CALL_BeginQueryIndexed(ctx->Dispatch.RealPublished, (target, index, id));
}

static void GLAPIENTRY
_mesa_trace_DrawTransformFeedbackStream(GLenum mode, GLuint id, GLuint stream)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawTransformFeedbackStream(%s, %u, %u)\n", _mesa_enum_to_string(mode), id, stream);
   CALL_DrawTransformFeedbackStream(ctx->Dispatch.RealPublished, (mode, id, stream));
}

static void GLAPIENTRY
_mesa_trace_EndQueryIndexed(GLenum target, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEndQueryIndexed(%s, %u)\n", _mesa_enum_to_string(target), index);
   CALL_EndQueryIndexed(ctx->Dispatch.RealPublished, (target, index));
}

static void GLAPIENTRY
_mesa_trace_GetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryIndexediv(%s, %u, %s, %p)\n", _mesa_enum_to_string(target), index, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetQueryIndexediv(ctx->Dispatch.RealPublished, (target, index, pname, params));
}

static void GLAPIENTRY
_mesa_trace_ClearDepthf(GLclampf depth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearDepthf(%f)\n", depth);
   CALL_ClearDepthf(ctx->Dispatch.RealPublished, (depth));
}

static void GLAPIENTRY
_mesa_trace_DepthRangef(GLclampf zNear, GLclampf zFar)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDepthRangef(%f, %f)\n", zNear, zFar);
   CALL_DepthRangef(ctx->Dispatch.RealPublished, (zNear, zFar));
}

static void GLAPIENTRY
_mesa_trace_GetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetShaderPrecisionFormat(%s, %s, %p, %p)\n", _mesa_enum_to_string(shadertype), _mesa_enum_to_string(precisiontype), (void *)range, (void *)precision);
   CALL_GetShaderPrecisionFormat(ctx->Dispatch.RealPublished, (shadertype, precisiontype, range, precision));
}

static void GLAPIENTRY
_mesa_trace_ReleaseShaderCompiler(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glReleaseShaderCompiler()\n");
   CALL_ReleaseShaderCompiler(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_ShaderBinary(GLsizei n, const GLuint *shaders, GLenum binaryformat, const GLvoid *binary, GLsizei length)
{
   GET_CURRENT_CONTEXT(ctx);
   char shaders_buf[512];
   _mesa_trace_format_array(shaders_buf, sizeof(shaders_buf), shaders, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glShaderBinary(%d, %s, %s, %p, %d)\n", n, shaders_buf, _mesa_enum_to_string(binaryformat), (void *)binary, length);
   CALL_ShaderBinary(ctx->Dispatch.RealPublished, (n, shaders, binaryformat, binary, length));
}

static void GLAPIENTRY
_mesa_trace_GetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat, GLvoid *binary)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramBinary(%u, %d, %p, %p, %p)\n", program, bufSize, (void *)length, (void *)binaryFormat, (void *)binary);
   CALL_GetProgramBinary(ctx->Dispatch.RealPublished, (program, bufSize, length, binaryFormat, binary));
}

static void GLAPIENTRY
_mesa_trace_ProgramBinary(GLuint program, GLenum binaryFormat, const GLvoid *binary, GLsizei length)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramBinary(%u, %s, %p, %d)\n", program, _mesa_enum_to_string(binaryFormat), (void *)binary, length);
   CALL_ProgramBinary(ctx->Dispatch.RealPublished, (program, binaryFormat, binary, length));
}

static void GLAPIENTRY
_mesa_trace_ProgramParameteri(GLuint program, GLenum pname, GLint value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramParameteri(%u, %s, %d)\n", program, _mesa_enum_to_string(pname), value);
   CALL_ProgramParameteri(ctx->Dispatch.RealPublished, (program, pname, value));
}

static void GLAPIENTRY
_mesa_trace_GetVertexAttribLdv(GLuint index, GLenum pname, GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexAttribLdv(%u, %s, %p)\n", index, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetVertexAttribLdv(ctx->Dispatch.RealPublished, (index, pname, params));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL1d(GLuint index, GLdouble x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribL1d(%u, %f)\n", index, x);
   CALL_VertexAttribL1d(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL1dv(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribL1dv(%u, %p)\n", index, (void *)v);
   CALL_VertexAttribL1dv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL2d(GLuint index, GLdouble x, GLdouble y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribL2d(%u, %f, %f)\n", index, x, y);
   CALL_VertexAttribL2d(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL2dv(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttribL2dv(%u, %s)\n", index, v_buf);
   CALL_VertexAttribL2dv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL3d(GLuint index, GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribL3d(%u, %f, %f, %f)\n", index, x, y, z);
   CALL_VertexAttribL3d(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL3dv(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttribL3dv(%u, %s)\n", index, v_buf);
   CALL_VertexAttribL3dv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribL4d(%u, %f, %f, %f, %f)\n", index, x, y, z, w);
   CALL_VertexAttribL4d(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL4dv(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttribL4dv(%u, %s)\n", index, v_buf);
   CALL_VertexAttribL4dv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribLPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribLPointer(%u, %d, %s, %d, %p)\n", index, size, _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_VertexAttribLPointer(ctx->Dispatch.RealPublished, (index, size, type, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_DepthRangeArrayv(GLuint first, GLsizei count, const GLclampd *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)count * 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glDepthRangeArrayv(%u, %d, %s)\n", first, count, v_buf);
   CALL_DepthRangeArrayv(ctx->Dispatch.RealPublished, (first, count, v));
}

static void GLAPIENTRY
_mesa_trace_DepthRangeIndexed(GLuint index, GLclampd n, GLclampd f)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDepthRangeIndexed(%u, %f, %f)\n", index, n, f);
   CALL_DepthRangeIndexed(ctx->Dispatch.RealPublished, (index, n, f));
}

static void GLAPIENTRY
_mesa_trace_GetDoublei_v(GLenum target, GLuint index, GLdouble *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetDoublei_v(%s, %u, %p)\n", _mesa_enum_to_string(target), index, (void *)data);
   CALL_GetDoublei_v(ctx->Dispatch.RealPublished, (target, index, data));
}

static void GLAPIENTRY
_mesa_trace_GetFloati_v(GLenum target, GLuint index, GLfloat *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFloati_v(%s, %u, %p)\n", _mesa_enum_to_string(target), index, (void *)data);
   CALL_GetFloati_v(ctx->Dispatch.RealPublished, (target, index, data));
}

static void GLAPIENTRY
_mesa_trace_ScissorArrayv(GLuint first, GLsizei count, const int *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)count * 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glScissorArrayv(%u, %d, %s)\n", first, count, v_buf);
   CALL_ScissorArrayv(ctx->Dispatch.RealPublished, (first, count, v));
}

static void GLAPIENTRY
_mesa_trace_ScissorIndexed(GLuint index, GLint left, GLint bottom, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glScissorIndexed(%u, %d, %d, %d, %d)\n", index, left, bottom, width, height);
   CALL_ScissorIndexed(ctx->Dispatch.RealPublished, (index, left, bottom, width, height));
}

static void GLAPIENTRY
_mesa_trace_ScissorIndexedv(GLuint index, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glScissorIndexedv(%u, %s)\n", index, v_buf);
   CALL_ScissorIndexedv(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_ViewportArrayv(GLuint first, GLsizei count, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)count * 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glViewportArrayv(%u, %d, %s)\n", first, count, v_buf);
   CALL_ViewportArrayv(ctx->Dispatch.RealPublished, (first, count, v));
}

static void GLAPIENTRY
_mesa_trace_ViewportIndexedf(GLuint index, GLfloat x, GLfloat y, GLfloat w, GLfloat h)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glViewportIndexedf(%u, %f, %f, %f, %f)\n", index, x, y, w, h);
   CALL_ViewportIndexedf(ctx->Dispatch.RealPublished, (index, x, y, w, h));
}

static void GLAPIENTRY
_mesa_trace_ViewportIndexedfv(GLuint index, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glViewportIndexedfv(%u, %s)\n", index, v_buf);
   CALL_ViewportIndexedfv(ctx->Dispatch.RealPublished, (index, v));
}

static GLenum GLAPIENTRY
_mesa_trace_GetGraphicsResetStatusARB(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetGraphicsResetStatusARB()\n");
   return CALL_GetGraphicsResetStatusARB(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_GetnCompressedTexImageARB(GLenum target, GLint lod, GLsizei bufSize, GLvoid *img)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnCompressedTexImageARB(%s, %d, %d, %p)\n", _mesa_enum_to_string(target), lod, bufSize, (void *)img);
   CALL_GetnCompressedTexImageARB(ctx->Dispatch.RealPublished, (target, lod, bufSize, img));
}

static void GLAPIENTRY
_mesa_trace_GetnMapdvARB(GLenum target, GLenum query, GLsizei bufSize, GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnMapdvARB(%s, %s, %d, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(query), bufSize, (void *)v);
   CALL_GetnMapdvARB(ctx->Dispatch.RealPublished, (target, query, bufSize, v));
}

static void GLAPIENTRY
_mesa_trace_GetnMapfvARB(GLenum target, GLenum query, GLsizei bufSize, GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnMapfvARB(%s, %s, %d, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(query), bufSize, (void *)v);
   CALL_GetnMapfvARB(ctx->Dispatch.RealPublished, (target, query, bufSize, v));
}

static void GLAPIENTRY
_mesa_trace_GetnMapivARB(GLenum target, GLenum query, GLsizei bufSize, GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnMapivARB(%s, %s, %d, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(query), bufSize, (void *)v);
   CALL_GetnMapivARB(ctx->Dispatch.RealPublished, (target, query, bufSize, v));
}

static void GLAPIENTRY
_mesa_trace_GetnPixelMapfvARB(GLenum map, GLsizei bufSize, GLfloat *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnPixelMapfvARB(%s, %d, %p)\n", _mesa_enum_to_string(map), bufSize, (void *)values);
   CALL_GetnPixelMapfvARB(ctx->Dispatch.RealPublished, (map, bufSize, values));
}

static void GLAPIENTRY
_mesa_trace_GetnPixelMapuivARB(GLenum map, GLsizei bufSize, GLuint *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnPixelMapuivARB(%s, %d, %p)\n", _mesa_enum_to_string(map), bufSize, (void *)values);
   CALL_GetnPixelMapuivARB(ctx->Dispatch.RealPublished, (map, bufSize, values));
}

static void GLAPIENTRY
_mesa_trace_GetnPixelMapusvARB(GLenum map, GLsizei bufSize, GLushort *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnPixelMapusvARB(%s, %d, %p)\n", _mesa_enum_to_string(map), bufSize, (void *)values);
   CALL_GetnPixelMapusvARB(ctx->Dispatch.RealPublished, (map, bufSize, values));
}

static void GLAPIENTRY
_mesa_trace_GetnPolygonStippleARB(GLsizei bufSize, GLubyte *pattern)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnPolygonStippleARB(%d, %p)\n", bufSize, (void *)pattern);
   CALL_GetnPolygonStippleARB(ctx->Dispatch.RealPublished, (bufSize, pattern));
}

static void GLAPIENTRY
_mesa_trace_GetnTexImageARB(GLenum target, GLint level, GLenum format, GLenum type, GLsizei bufSize, GLvoid *img)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnTexImageARB(%s, %d, %s, %s, %d, %p)\n", _mesa_enum_to_string(target), level, _mesa_enum_to_string(format), _mesa_enum_to_string(type), bufSize, (void *)img);
   CALL_GetnTexImageARB(ctx->Dispatch.RealPublished, (target, level, format, type, bufSize, img));
}

static void GLAPIENTRY
_mesa_trace_GetnUniformdvARB(GLuint program, GLint location, GLsizei bufSize, GLdouble *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnUniformdvARB(%u, %d, %d, %p)\n", program, location, bufSize, (void *)params);
   CALL_GetnUniformdvARB(ctx->Dispatch.RealPublished, (program, location, bufSize, params));
}

static void GLAPIENTRY
_mesa_trace_GetnUniformfvARB(GLuint program, GLint location, GLsizei bufSize, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnUniformfvARB(%u, %d, %d, %p)\n", program, location, bufSize, (void *)params);
   CALL_GetnUniformfvARB(ctx->Dispatch.RealPublished, (program, location, bufSize, params));
}

static void GLAPIENTRY
_mesa_trace_GetnUniformivARB(GLuint program, GLint location, GLsizei bufSize, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnUniformivARB(%u, %d, %d, %p)\n", program, location, bufSize, (void *)params);
   CALL_GetnUniformivARB(ctx->Dispatch.RealPublished, (program, location, bufSize, params));
}

static void GLAPIENTRY
_mesa_trace_GetnUniformuivARB(GLuint program, GLint location, GLsizei bufSize, GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnUniformuivARB(%u, %d, %d, %p)\n", program, location, bufSize, (void *)params);
   CALL_GetnUniformuivARB(ctx->Dispatch.RealPublished, (program, location, bufSize, params));
}

static void GLAPIENTRY
_mesa_trace_ReadnPixelsARB(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLsizei bufSize, GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glReadnPixelsARB(%d, %d, %d, %d, %s, %s, %d, %p)\n", x, y, width, height, _mesa_enum_to_string(format), _mesa_enum_to_string(type), bufSize, (void *)data);
   CALL_ReadnPixelsARB(ctx->Dispatch.RealPublished, (x, y, width, height, format, type, bufSize, data));
}

static void GLAPIENTRY
_mesa_trace_DrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei instance_count, GLuint baseinstance)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawArraysInstancedBaseInstance(%s, %d, %d, %d, %u)\n", _mesa_enum_to_string(mode), first, count, instance_count, baseinstance);
   CALL_DrawArraysInstancedBaseInstance(ctx->Dispatch.RealPublished, (mode, first, count, instance_count, baseinstance));
}

static void GLAPIENTRY
_mesa_trace_DrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei primcount, GLuint baseinstance)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsInstancedBaseInstance(%s, %d, %s, %p, %d, %u)\n", _mesa_enum_to_string(mode), count, _mesa_enum_to_string(type), (void *)indices, primcount, baseinstance);
   CALL_DrawElementsInstancedBaseInstance(ctx->Dispatch.RealPublished, (mode, count, type, indices, primcount, baseinstance));
}

static void GLAPIENTRY
_mesa_trace_DrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instance_count, GLint basevertex, GLuint baseinstance)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsInstancedBaseVertexBaseInstance(%s, %d, %s, %p, %d, %d, %u)\n", _mesa_enum_to_string(mode), count, _mesa_enum_to_string(type), (void *)indices, instance_count, basevertex, baseinstance);
   CALL_DrawElementsInstancedBaseVertexBaseInstance(ctx->Dispatch.RealPublished, (mode, count, type, indices, instance_count, basevertex, baseinstance));
}

static void GLAPIENTRY
_mesa_trace_DrawTransformFeedbackInstanced(GLenum mode, GLuint id, GLsizei primcount)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawTransformFeedbackInstanced(%s, %u, %d)\n", _mesa_enum_to_string(mode), id, primcount);
   CALL_DrawTransformFeedbackInstanced(ctx->Dispatch.RealPublished, (mode, id, primcount));
}

static void GLAPIENTRY
_mesa_trace_DrawTransformFeedbackStreamInstanced(GLenum mode, GLuint id, GLuint stream, GLsizei primcount)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawTransformFeedbackStreamInstanced(%s, %u, %u, %d)\n", _mesa_enum_to_string(mode), id, stream, primcount);
   CALL_DrawTransformFeedbackStreamInstanced(ctx->Dispatch.RealPublished, (mode, id, stream, primcount));
}

static void GLAPIENTRY
_mesa_trace_GetInternalformativ(GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetInternalformativ(%s, %s, %s, %d, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), _mesa_enum_to_string(pname), bufSize, (void *)params);
   CALL_GetInternalformativ(ctx->Dispatch.RealPublished, (target, internalformat, pname, bufSize, params));
}

static void GLAPIENTRY
_mesa_trace_GetActiveAtomicCounterBufferiv(GLuint program, GLuint bufferIndex, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetActiveAtomicCounterBufferiv(%u, %u, %s, %p)\n", program, bufferIndex, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetActiveAtomicCounterBufferiv(ctx->Dispatch.RealPublished, (program, bufferIndex, pname, params));
}

static void GLAPIENTRY
_mesa_trace_BindImageTexture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindImageTexture(%u, %u, %d, %s, %d, %s, %s)\n", unit, texture, level, layered ? "GL_TRUE" : "GL_FALSE", layer, _mesa_enum_to_string(access), _mesa_enum_to_string(format));
   CALL_BindImageTexture(ctx->Dispatch.RealPublished, (unit, texture, level, layered, layer, access, format));
}

static void GLAPIENTRY
_mesa_trace_MemoryBarrier(GLbitfield barriers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMemoryBarrier(0x%x)\n", barriers);
   CALL_MemoryBarrier(ctx->Dispatch.RealPublished, (barriers));
}

static void GLAPIENTRY
_mesa_trace_TexStorage1D(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorage1D(%s, %d, %s, %d)\n", _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width);
   CALL_TexStorage1D(ctx->Dispatch.RealPublished, (target, levels, internalFormat, width));
}

static void GLAPIENTRY
_mesa_trace_TexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorage2D(%s, %d, %s, %d, %d)\n", _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width, height);
   CALL_TexStorage2D(ctx->Dispatch.RealPublished, (target, levels, internalFormat, width, height));
}

static void GLAPIENTRY
_mesa_trace_TexStorage3D(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorage3D(%s, %d, %s, %d, %d, %d)\n", _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width, height, depth);
   CALL_TexStorage3D(ctx->Dispatch.RealPublished, (target, levels, internalFormat, width, height, depth));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage1DEXT(GLuint texture, GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage1DEXT(%u, %s, %d, %s, %d)\n", texture, _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width);
   CALL_TextureStorage1DEXT(ctx->Dispatch.RealPublished, (texture, target, levels, internalFormat, width));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage2DEXT(GLuint texture, GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage2DEXT(%u, %s, %d, %s, %d, %d)\n", texture, _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width, height);
   CALL_TextureStorage2DEXT(ctx->Dispatch.RealPublished, (texture, target, levels, internalFormat, width, height));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage3DEXT(GLuint texture, GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage3DEXT(%u, %s, %d, %s, %d, %d, %d)\n", texture, _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width, height, depth);
   CALL_TextureStorage3DEXT(ctx->Dispatch.RealPublished, (texture, target, levels, internalFormat, width, height, depth));
}

static void GLAPIENTRY
_mesa_trace_ClearBufferData(GLenum target, GLenum internalformat, GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearBufferData(%s, %s, %s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)data);
   CALL_ClearBufferData(ctx->Dispatch.RealPublished, (target, internalformat, format, type, data));
}

static void GLAPIENTRY
_mesa_trace_ClearBufferSubData(GLenum target, GLenum internalformat, GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearBufferSubData(%s, %s, %" PRIdPTR ", %" PRIdPTR ", %s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), (intptr_t)offset, (intptr_t)size, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)data);
   CALL_ClearBufferSubData(ctx->Dispatch.RealPublished, (target, internalformat, offset, size, format, type, data));
}

static void GLAPIENTRY
_mesa_trace_DispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDispatchCompute(%u, %u, %u)\n", num_groups_x, num_groups_y, num_groups_z);
   CALL_DispatchCompute(ctx->Dispatch.RealPublished, (num_groups_x, num_groups_y, num_groups_z));
}

static void GLAPIENTRY
_mesa_trace_DispatchComputeIndirect(GLintptr indirect)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDispatchComputeIndirect(%" PRIdPTR ")\n", (intptr_t)indirect);
   CALL_DispatchComputeIndirect(ctx->Dispatch.RealPublished, (indirect));
}

static void GLAPIENTRY
_mesa_trace_CopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyImageSubData(%u, %s, %d, %d, %d, %d, %u, %s, %d, %d, %d, %d, %d, %d, %d)\n", srcName, _mesa_enum_to_string(srcTarget), srcLevel, srcX, srcY, srcZ, dstName, _mesa_enum_to_string(dstTarget), dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight, srcDepth);
   CALL_CopyImageSubData(ctx->Dispatch.RealPublished, (srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight, srcDepth));
}

static void GLAPIENTRY
_mesa_trace_TextureView(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels, GLuint minlayer, GLuint numlayers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureView(%u, %s, %u, %s, %u, %u, %u, %u)\n", texture, _mesa_enum_to_string(target), origtexture, _mesa_enum_to_string(internalformat), minlevel, numlevels, minlayer, numlayers);
   CALL_TextureView(ctx->Dispatch.RealPublished, (texture, target, origtexture, internalformat, minlevel, numlevels, minlayer, numlayers));
}

static void GLAPIENTRY
_mesa_trace_BindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindVertexBuffer(%u, %u, %" PRIdPTR ", %d)\n", bindingindex, buffer, (intptr_t)offset, stride);
   CALL_BindVertexBuffer(ctx->Dispatch.RealPublished, (bindingindex, buffer, offset, stride));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribBinding(GLuint attribindex, GLuint bindingindex)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribBinding(%u, %u)\n", attribindex, bindingindex);
   CALL_VertexAttribBinding(ctx->Dispatch.RealPublished, (attribindex, bindingindex));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribFormat(GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribFormat(%u, %d, %s, %s, %u)\n", attribindex, size, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", relativeoffset);
   CALL_VertexAttribFormat(ctx->Dispatch.RealPublished, (attribindex, size, type, normalized, relativeoffset));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribIFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribIFormat(%u, %d, %s, %u)\n", attribindex, size, _mesa_enum_to_string(type), relativeoffset);
   CALL_VertexAttribIFormat(ctx->Dispatch.RealPublished, (attribindex, size, type, relativeoffset));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribLFormat(GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribLFormat(%u, %d, %s, %u)\n", attribindex, size, _mesa_enum_to_string(type), relativeoffset);
   CALL_VertexAttribLFormat(ctx->Dispatch.RealPublished, (attribindex, size, type, relativeoffset));
}

static void GLAPIENTRY
_mesa_trace_VertexBindingDivisor(GLuint bindingindex, GLuint divisor)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexBindingDivisor(%u, %u)\n", bindingindex, divisor);
   CALL_VertexBindingDivisor(ctx->Dispatch.RealPublished, (bindingindex, divisor));
}

static void GLAPIENTRY
_mesa_trace_FramebufferParameteri(GLenum target, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferParameteri(%s, %s, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_FramebufferParameteri(ctx->Dispatch.RealPublished, (target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetFramebufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFramebufferParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetFramebufferParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetInternalformati64v(GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetInternalformati64v(%s, %s, %s, %d, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), _mesa_enum_to_string(pname), bufSize, (void *)params);
   CALL_GetInternalformati64v(ctx->Dispatch.RealPublished, (target, internalformat, pname, bufSize, params));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawArraysIndirect(GLenum mode, const GLvoid *indirect, GLsizei primcount, GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiDrawArraysIndirect(%s, %p, %d, %d)\n", _mesa_enum_to_string(mode), (void *)indirect, primcount, stride);
   CALL_MultiDrawArraysIndirect(ctx->Dispatch.RealPublished, (mode, indirect, primcount, stride));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawElementsIndirect(GLenum mode, GLenum type, const GLvoid *indirect, GLsizei primcount, GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiDrawElementsIndirect(%s, %s, %p, %d, %d)\n", _mesa_enum_to_string(mode), _mesa_enum_to_string(type), (void *)indirect, primcount, stride);
   CALL_MultiDrawElementsIndirect(ctx->Dispatch.RealPublished, (mode, type, indirect, primcount, stride));
}

static void GLAPIENTRY
_mesa_trace_GetProgramInterfaceiv(GLuint program, GLenum programInterface, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramInterfaceiv(%u, %s, %s, %p)\n", program, _mesa_enum_to_string(programInterface), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetProgramInterfaceiv(ctx->Dispatch.RealPublished, (program, programInterface, pname, params));
}

static GLuint GLAPIENTRY
_mesa_trace_GetProgramResourceIndex(GLuint program, GLenum programInterface, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramResourceIndex(%u, %s, %s)\n", program, _mesa_enum_to_string(programInterface), name ? (const char *)name : "(null)");
   return CALL_GetProgramResourceIndex(ctx->Dispatch.RealPublished, (program, programInterface, name));
}

static GLint GLAPIENTRY
_mesa_trace_GetProgramResourceLocation(GLuint program, GLenum programInterface, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramResourceLocation(%u, %s, %s)\n", program, _mesa_enum_to_string(programInterface), name ? (const char *)name : "(null)");
   return CALL_GetProgramResourceLocation(ctx->Dispatch.RealPublished, (program, programInterface, name));
}

static GLint GLAPIENTRY
_mesa_trace_GetProgramResourceLocationIndex(GLuint program, GLenum programInterface, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramResourceLocationIndex(%u, %s, %s)\n", program, _mesa_enum_to_string(programInterface), name ? (const char *)name : "(null)");
   return CALL_GetProgramResourceLocationIndex(ctx->Dispatch.RealPublished, (program, programInterface, name));
}

static void GLAPIENTRY
_mesa_trace_GetProgramResourceName(GLuint program, GLenum programInterface, GLuint index, GLsizei bufSize, GLsizei *length, GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramResourceName(%u, %s, %u, %d, %p, %p)\n", program, _mesa_enum_to_string(programInterface), index, bufSize, (void *)length, (void *)name);
   CALL_GetProgramResourceName(ctx->Dispatch.RealPublished, (program, programInterface, index, bufSize, length, name));
}

static void GLAPIENTRY
_mesa_trace_GetProgramResourceiv(GLuint program, GLenum programInterface, GLuint index, GLsizei propCount, const GLenum *props, GLsizei bufSize, GLsizei *length, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramResourceiv(%u, %s, %u, %d, %p, %d, %p, %p)\n", program, _mesa_enum_to_string(programInterface), index, propCount, (void *)props, bufSize, (void *)length, (void *)params);
   CALL_GetProgramResourceiv(ctx->Dispatch.RealPublished, (program, programInterface, index, propCount, props, bufSize, length, params));
}

static void GLAPIENTRY
_mesa_trace_ShaderStorageBlockBinding(GLuint program, GLuint shaderStorageBlockIndex, GLuint shaderStorageBlockBinding)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glShaderStorageBlockBinding(%u, %u, %u)\n", program, shaderStorageBlockIndex, shaderStorageBlockBinding);
   CALL_ShaderStorageBlockBinding(ctx->Dispatch.RealPublished, (program, shaderStorageBlockIndex, shaderStorageBlockBinding));
}

static void GLAPIENTRY
_mesa_trace_TexBufferRange(GLenum target, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexBufferRange(%s, %s, %u, %" PRIdPTR ", %" PRIdPTR ")\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), buffer, (intptr_t)offset, (intptr_t)size);
   CALL_TexBufferRange(ctx->Dispatch.RealPublished, (target, internalformat, buffer, offset, size));
}

static void GLAPIENTRY
_mesa_trace_TexStorage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorage2DMultisample(%s, %d, %s, %d, %d, %s)\n", _mesa_enum_to_string(target), samples, _mesa_enum_to_string(internalformat), width, height, fixedsamplelocations ? "GL_TRUE" : "GL_FALSE");
   CALL_TexStorage2DMultisample(ctx->Dispatch.RealPublished, (target, samples, internalformat, width, height, fixedsamplelocations));
}

static void GLAPIENTRY
_mesa_trace_TexStorage3DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorage3DMultisample(%s, %d, %s, %d, %d, %d, %s)\n", _mesa_enum_to_string(target), samples, _mesa_enum_to_string(internalformat), width, height, depth, fixedsamplelocations ? "GL_TRUE" : "GL_FALSE");
   CALL_TexStorage3DMultisample(ctx->Dispatch.RealPublished, (target, samples, internalformat, width, height, depth, fixedsamplelocations));
}

static void GLAPIENTRY
_mesa_trace_BufferStorage(GLenum target, GLsizeiptr size, const GLvoid *data, GLbitfield flags)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBufferStorage(%s, %" PRIdPTR ", %p, 0x%x)\n", _mesa_enum_to_string(target), (intptr_t)size, (void *)data, flags);
   CALL_BufferStorage(ctx->Dispatch.RealPublished, (target, size, data, flags));
}

static void GLAPIENTRY
_mesa_trace_ClearTexImage(GLuint texture, GLint level, GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearTexImage(%u, %d, %s, %s, %p)\n", texture, level, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)data);
   CALL_ClearTexImage(ctx->Dispatch.RealPublished, (texture, level, format, type, data));
}

static void GLAPIENTRY
_mesa_trace_ClearTexSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearTexSubImage(%u, %d, %d, %d, %d, %d, %d, %d, %s, %s, %p)\n", texture, level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)data);
   CALL_ClearTexSubImage(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, data));
}

static void GLAPIENTRY
_mesa_trace_BindBuffersBase(GLenum target, GLuint first, GLsizei count, const GLuint *buffers)
{
   GET_CURRENT_CONTEXT(ctx);
   char buffers_buf[512];
   _mesa_trace_format_array(buffers_buf, sizeof(buffers_buf), buffers, (size_t)count, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glBindBuffersBase(%s, %u, %d, %s)\n", _mesa_enum_to_string(target), first, count, buffers_buf);
   CALL_BindBuffersBase(ctx->Dispatch.RealPublished, (target, first, count, buffers));
}

static void GLAPIENTRY
_mesa_trace_BindBuffersRange(GLenum target, GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLsizeiptr *sizes)
{
   GET_CURRENT_CONTEXT(ctx);
   char buffers_buf[512];
   _mesa_trace_format_array(buffers_buf, sizeof(buffers_buf), buffers, (size_t)count, MESA_TRACE_ELEM_UINT);
   char offsets_buf[512];
   _mesa_trace_format_array(offsets_buf, sizeof(offsets_buf), offsets, (size_t)count, MESA_TRACE_ELEM_INTPTR);
   char sizes_buf[512];
   _mesa_trace_format_array(sizes_buf, sizeof(sizes_buf), sizes, (size_t)count, MESA_TRACE_ELEM_INTPTR);
   _mesa_debug(ctx, "glBindBuffersRange(%s, %u, %d, %s, %s, %s)\n", _mesa_enum_to_string(target), first, count, buffers_buf, offsets_buf, sizes_buf);
   CALL_BindBuffersRange(ctx->Dispatch.RealPublished, (target, first, count, buffers, offsets, sizes));
}

static void GLAPIENTRY
_mesa_trace_BindImageTextures(GLuint first, GLsizei count, const GLuint *textures)
{
   GET_CURRENT_CONTEXT(ctx);
   char textures_buf[512];
   _mesa_trace_format_array(textures_buf, sizeof(textures_buf), textures, (size_t)count, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glBindImageTextures(%u, %d, %s)\n", first, count, textures_buf);
   CALL_BindImageTextures(ctx->Dispatch.RealPublished, (first, count, textures));
}

static void GLAPIENTRY
_mesa_trace_BindSamplers(GLuint first, GLsizei count, const GLuint *samplers)
{
   GET_CURRENT_CONTEXT(ctx);
   char samplers_buf[512];
   _mesa_trace_format_array(samplers_buf, sizeof(samplers_buf), samplers, (size_t)count, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glBindSamplers(%u, %d, %s)\n", first, count, samplers_buf);
   CALL_BindSamplers(ctx->Dispatch.RealPublished, (first, count, samplers));
}

static void GLAPIENTRY
_mesa_trace_BindTextures(GLuint first, GLsizei count, const GLuint *textures)
{
   GET_CURRENT_CONTEXT(ctx);
   char textures_buf[512];
   _mesa_trace_format_array(textures_buf, sizeof(textures_buf), textures, (size_t)count, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glBindTextures(%u, %d, %s)\n", first, count, textures_buf);
   CALL_BindTextures(ctx->Dispatch.RealPublished, (first, count, textures));
}

static void GLAPIENTRY
_mesa_trace_BindVertexBuffers(GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLsizei *strides)
{
   GET_CURRENT_CONTEXT(ctx);
   char buffers_buf[512];
   _mesa_trace_format_array(buffers_buf, sizeof(buffers_buf), buffers, (size_t)count, MESA_TRACE_ELEM_UINT);
   char offsets_buf[512];
   _mesa_trace_format_array(offsets_buf, sizeof(offsets_buf), offsets, (size_t)count, MESA_TRACE_ELEM_INTPTR);
   char strides_buf[512];
   _mesa_trace_format_array(strides_buf, sizeof(strides_buf), strides, (size_t)count, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glBindVertexBuffers(%u, %d, %s, %s, %s)\n", first, count, buffers_buf, offsets_buf, strides_buf);
   CALL_BindVertexBuffers(ctx->Dispatch.RealPublished, (first, count, buffers, offsets, strides));
}

static GLuint64 GLAPIENTRY
_mesa_trace_GetImageHandleARB(GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum format)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetImageHandleARB(%u, %d, %s, %d, %s)\n", texture, level, layered ? "GL_TRUE" : "GL_FALSE", layer, _mesa_enum_to_string(format));
   return CALL_GetImageHandleARB(ctx->Dispatch.RealPublished, (texture, level, layered, layer, format));
}

static GLuint64 GLAPIENTRY
_mesa_trace_GetTextureHandleARB(GLuint texture)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureHandleARB(%u)\n", texture);
   return CALL_GetTextureHandleARB(ctx->Dispatch.RealPublished, (texture));
}

static GLuint64 GLAPIENTRY
_mesa_trace_GetTextureSamplerHandleARB(GLuint texture, GLuint sampler)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureSamplerHandleARB(%u, %u)\n", texture, sampler);
   return CALL_GetTextureSamplerHandleARB(ctx->Dispatch.RealPublished, (texture, sampler));
}

static void GLAPIENTRY
_mesa_trace_GetVertexAttribLui64vARB(GLuint index, GLenum pname, GLuint64EXT *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexAttribLui64vARB(%u, %s, %p)\n", index, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetVertexAttribLui64vARB(ctx->Dispatch.RealPublished, (index, pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsImageHandleResidentARB(GLuint64 handle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsImageHandleResidentARB(%" PRIu64 ")\n", (uint64_t)handle);
   return CALL_IsImageHandleResidentARB(ctx->Dispatch.RealPublished, (handle));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsTextureHandleResidentARB(GLuint64 handle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsTextureHandleResidentARB(%" PRIu64 ")\n", (uint64_t)handle);
   return CALL_IsTextureHandleResidentARB(ctx->Dispatch.RealPublished, (handle));
}

static void GLAPIENTRY
_mesa_trace_MakeImageHandleNonResidentARB(GLuint64 handle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMakeImageHandleNonResidentARB(%" PRIu64 ")\n", (uint64_t)handle);
   CALL_MakeImageHandleNonResidentARB(ctx->Dispatch.RealPublished, (handle));
}

static void GLAPIENTRY
_mesa_trace_MakeImageHandleResidentARB(GLuint64 handle, GLenum access)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMakeImageHandleResidentARB(%" PRIu64 ", %s)\n", (uint64_t)handle, _mesa_enum_to_string(access));
   CALL_MakeImageHandleResidentARB(ctx->Dispatch.RealPublished, (handle, access));
}

static void GLAPIENTRY
_mesa_trace_MakeTextureHandleNonResidentARB(GLuint64 handle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMakeTextureHandleNonResidentARB(%" PRIu64 ")\n", (uint64_t)handle);
   CALL_MakeTextureHandleNonResidentARB(ctx->Dispatch.RealPublished, (handle));
}

static void GLAPIENTRY
_mesa_trace_MakeTextureHandleResidentARB(GLuint64 handle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMakeTextureHandleResidentARB(%" PRIu64 ")\n", (uint64_t)handle);
   CALL_MakeTextureHandleResidentARB(ctx->Dispatch.RealPublished, (handle));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformHandleui64ARB(GLuint program, GLint location, GLuint64 value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniformHandleui64ARB(%u, %d, %" PRIu64 ")\n", program, location, (uint64_t)value);
   CALL_ProgramUniformHandleui64ARB(ctx->Dispatch.RealPublished, (program, location, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformHandleui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glProgramUniformHandleui64vARB(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniformHandleui64vARB(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_UniformHandleui64ARB(GLint location, GLuint64 value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniformHandleui64ARB(%d, %" PRIu64 ")\n", location, (uint64_t)value);
   CALL_UniformHandleui64ARB(ctx->Dispatch.RealPublished, (location, value));
}

static void GLAPIENTRY
_mesa_trace_UniformHandleui64vARB(GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glUniformHandleui64vARB(%d, %d, %s)\n", location, count, value_buf);
   CALL_UniformHandleui64vARB(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL1ui64ARB(GLuint index, GLuint64EXT x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribL1ui64ARB(%u, %" PRIu64 ")\n", index, (uint64_t)x);
   CALL_VertexAttribL1ui64ARB(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribL1ui64vARB(GLuint index, const GLuint64EXT *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribL1ui64vARB(%u, %p)\n", index, (void *)v);
   CALL_VertexAttribL1ui64vARB(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_DispatchComputeGroupSizeARB(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z, GLuint group_size_x, GLuint group_size_y, GLuint group_size_z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDispatchComputeGroupSizeARB(%u, %u, %u, %u, %u, %u)\n", num_groups_x, num_groups_y, num_groups_z, group_size_x, group_size_y, group_size_z);
   CALL_DispatchComputeGroupSizeARB(ctx->Dispatch.RealPublished, (num_groups_x, num_groups_y, num_groups_z, group_size_x, group_size_y, group_size_z));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawArraysIndirectCountARB(GLenum mode, GLintptr indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiDrawArraysIndirectCountARB(%s, %" PRIdPTR ", %" PRIdPTR ", %d, %d)\n", _mesa_enum_to_string(mode), (intptr_t)indirect, (intptr_t)drawcount, maxdrawcount, stride);
   CALL_MultiDrawArraysIndirectCountARB(ctx->Dispatch.RealPublished, (mode, indirect, drawcount, maxdrawcount, stride));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawElementsIndirectCountARB(GLenum mode, GLenum type, GLintptr indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiDrawElementsIndirectCountARB(%s, %s, %" PRIdPTR ", %" PRIdPTR ", %d, %d)\n", _mesa_enum_to_string(mode), _mesa_enum_to_string(type), (intptr_t)indirect, (intptr_t)drawcount, maxdrawcount, stride);
   CALL_MultiDrawElementsIndirectCountARB(ctx->Dispatch.RealPublished, (mode, type, indirect, drawcount, maxdrawcount, stride));
}

static void GLAPIENTRY
_mesa_trace_ClipControl(GLenum origin, GLenum depth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClipControl(%s, %s)\n", _mesa_enum_to_string(origin), _mesa_enum_to_string(depth));
   CALL_ClipControl(ctx->Dispatch.RealPublished, (origin, depth));
}

static void GLAPIENTRY
_mesa_trace_BindTextureUnit(GLuint unit, GLuint texture)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindTextureUnit(%u, %u)\n", unit, texture);
   CALL_BindTextureUnit(ctx->Dispatch.RealPublished, (unit, texture));
}

static void GLAPIENTRY
_mesa_trace_BlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlitNamedFramebuffer(%u, %u, %d, %d, %d, %d, %d, %d, %d, %d, 0x%x, %s)\n", readFramebuffer, drawFramebuffer, srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, _mesa_enum_to_string(filter));
   CALL_BlitNamedFramebuffer(ctx->Dispatch.RealPublished, (readFramebuffer, drawFramebuffer, srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter));
}

static GLenum GLAPIENTRY
_mesa_trace_CheckNamedFramebufferStatus(GLuint framebuffer, GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCheckNamedFramebufferStatus(%u, %s)\n", framebuffer, _mesa_enum_to_string(target));
   return CALL_CheckNamedFramebufferStatus(ctx->Dispatch.RealPublished, (framebuffer, target));
}

static void GLAPIENTRY
_mesa_trace_ClearNamedBufferData(GLuint buffer, GLenum internalformat, GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearNamedBufferData(%u, %s, %s, %s, %p)\n", buffer, _mesa_enum_to_string(internalformat), _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)data);
   CALL_ClearNamedBufferData(ctx->Dispatch.RealPublished, (buffer, internalformat, format, type, data));
}

static void GLAPIENTRY
_mesa_trace_ClearNamedBufferSubData(GLuint buffer, GLenum internalformat, GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearNamedBufferSubData(%u, %s, %" PRIdPTR ", %" PRIdPTR ", %s, %s, %p)\n", buffer, _mesa_enum_to_string(internalformat), (intptr_t)offset, (intptr_t)size, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)data);
   CALL_ClearNamedBufferSubData(ctx->Dispatch.RealPublished, (buffer, internalformat, offset, size, format, type, data));
}

static void GLAPIENTRY
_mesa_trace_ClearNamedFramebufferfi(GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearNamedFramebufferfi(%u, %s, %d, %f, %d)\n", framebuffer, _mesa_enum_to_string(buffer), drawbuffer, depth, stencil);
   CALL_ClearNamedFramebufferfi(ctx->Dispatch.RealPublished, (framebuffer, buffer, drawbuffer, depth, stencil));
}

static void GLAPIENTRY
_mesa_trace_ClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearNamedFramebufferfv(%u, %s, %d, %p)\n", framebuffer, _mesa_enum_to_string(buffer), drawbuffer, (void *)value);
   CALL_ClearNamedFramebufferfv(ctx->Dispatch.RealPublished, (framebuffer, buffer, drawbuffer, value));
}

static void GLAPIENTRY
_mesa_trace_ClearNamedFramebufferiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearNamedFramebufferiv(%u, %s, %d, %p)\n", framebuffer, _mesa_enum_to_string(buffer), drawbuffer, (void *)value);
   CALL_ClearNamedFramebufferiv(ctx->Dispatch.RealPublished, (framebuffer, buffer, drawbuffer, value));
}

static void GLAPIENTRY
_mesa_trace_ClearNamedFramebufferuiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearNamedFramebufferuiv(%u, %s, %d, %p)\n", framebuffer, _mesa_enum_to_string(buffer), drawbuffer, (void *)value);
   CALL_ClearNamedFramebufferuiv(ctx->Dispatch.RealPublished, (framebuffer, buffer, drawbuffer, value));
}

static void GLAPIENTRY
_mesa_trace_CompressedTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTextureSubImage1D(%u, %d, %d, %d, %s, %d, %p)\n", texture, level, xoffset, width, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedTextureSubImage1D(ctx->Dispatch.RealPublished, (texture, level, xoffset, width, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTextureSubImage2D(%u, %d, %d, %d, %d, %d, %s, %d, %p)\n", texture, level, xoffset, yoffset, width, height, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedTextureSubImage2D(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, width, height, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTextureSubImage3D(%u, %d, %d, %d, %d, %d, %d, %d, %s, %d, %p)\n", texture, level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedTextureSubImage3D(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CopyNamedBufferSubData(GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyNamedBufferSubData(%u, %u, %" PRIdPTR ", %" PRIdPTR ", %" PRIdPTR ")\n", readBuffer, writeBuffer, (intptr_t)readOffset, (intptr_t)writeOffset, (intptr_t)size);
   CALL_CopyNamedBufferSubData(ctx->Dispatch.RealPublished, (readBuffer, writeBuffer, readOffset, writeOffset, size));
}

static void GLAPIENTRY
_mesa_trace_CopyTextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTextureSubImage1D(%u, %d, %d, %d, %d, %d)\n", texture, level, xoffset, x, y, width);
   CALL_CopyTextureSubImage1D(ctx->Dispatch.RealPublished, (texture, level, xoffset, x, y, width));
}

static void GLAPIENTRY
_mesa_trace_CopyTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTextureSubImage2D(%u, %d, %d, %d, %d, %d, %d, %d)\n", texture, level, xoffset, yoffset, x, y, width, height);
   CALL_CopyTextureSubImage2D(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_CopyTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTextureSubImage3D(%u, %d, %d, %d, %d, %d, %d, %d, %d)\n", texture, level, xoffset, yoffset, zoffset, x, y, width, height);
   CALL_CopyTextureSubImage3D(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, zoffset, x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_CreateBuffers(GLsizei n, GLuint *buffers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateBuffers(%d, %p)\n", n, (void *)buffers);
   CALL_CreateBuffers(ctx->Dispatch.RealPublished, (n, buffers));
}

static void GLAPIENTRY
_mesa_trace_CreateFramebuffers(GLsizei n, GLuint *framebuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateFramebuffers(%d, %p)\n", n, (void *)framebuffers);
   CALL_CreateFramebuffers(ctx->Dispatch.RealPublished, (n, framebuffers));
}

static void GLAPIENTRY
_mesa_trace_CreateProgramPipelines(GLsizei n, GLuint *pipelines)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateProgramPipelines(%d, %p)\n", n, (void *)pipelines);
   CALL_CreateProgramPipelines(ctx->Dispatch.RealPublished, (n, pipelines));
}

static void GLAPIENTRY
_mesa_trace_CreateQueries(GLenum target, GLsizei n, GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateQueries(%s, %d, %p)\n", _mesa_enum_to_string(target), n, (void *)ids);
   CALL_CreateQueries(ctx->Dispatch.RealPublished, (target, n, ids));
}

static void GLAPIENTRY
_mesa_trace_CreateRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateRenderbuffers(%d, %p)\n", n, (void *)renderbuffers);
   CALL_CreateRenderbuffers(ctx->Dispatch.RealPublished, (n, renderbuffers));
}

static void GLAPIENTRY
_mesa_trace_CreateSamplers(GLsizei n, GLuint *samplers)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateSamplers(%d, %p)\n", n, (void *)samplers);
   CALL_CreateSamplers(ctx->Dispatch.RealPublished, (n, samplers));
}

static void GLAPIENTRY
_mesa_trace_CreateTextures(GLenum target, GLsizei n, GLuint *textures)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateTextures(%s, %d, %p)\n", _mesa_enum_to_string(target), n, (void *)textures);
   CALL_CreateTextures(ctx->Dispatch.RealPublished, (target, n, textures));
}

static void GLAPIENTRY
_mesa_trace_CreateTransformFeedbacks(GLsizei n, GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateTransformFeedbacks(%d, %p)\n", n, (void *)ids);
   CALL_CreateTransformFeedbacks(ctx->Dispatch.RealPublished, (n, ids));
}

static void GLAPIENTRY
_mesa_trace_CreateVertexArrays(GLsizei n, GLuint *arrays)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateVertexArrays(%d, %p)\n", n, (void *)arrays);
   CALL_CreateVertexArrays(ctx->Dispatch.RealPublished, (n, arrays));
}

static void GLAPIENTRY
_mesa_trace_DisableVertexArrayAttrib(GLuint vaobj, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDisableVertexArrayAttrib(%u, %u)\n", vaobj, index);
   CALL_DisableVertexArrayAttrib(ctx->Dispatch.RealPublished, (vaobj, index));
}

static void GLAPIENTRY
_mesa_trace_EnableVertexArrayAttrib(GLuint vaobj, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEnableVertexArrayAttrib(%u, %u)\n", vaobj, index);
   CALL_EnableVertexArrayAttrib(ctx->Dispatch.RealPublished, (vaobj, index));
}

static void GLAPIENTRY
_mesa_trace_FlushMappedNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFlushMappedNamedBufferRange(%u, %" PRIdPTR ", %" PRIdPTR ")\n", buffer, (intptr_t)offset, (intptr_t)length);
   CALL_FlushMappedNamedBufferRange(ctx->Dispatch.RealPublished, (buffer, offset, length));
}

static void GLAPIENTRY
_mesa_trace_GenerateTextureMipmap(GLuint texture)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenerateTextureMipmap(%u)\n", texture);
   CALL_GenerateTextureMipmap(ctx->Dispatch.RealPublished, (texture));
}

static void GLAPIENTRY
_mesa_trace_GetCompressedTextureImage(GLuint texture, GLint level, GLsizei bufSize, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetCompressedTextureImage(%u, %d, %d, %p)\n", texture, level, bufSize, (void *)pixels);
   CALL_GetCompressedTextureImage(ctx->Dispatch.RealPublished, (texture, level, bufSize, pixels));
}

static void GLAPIENTRY
_mesa_trace_GetNamedBufferParameteri64v(GLuint buffer, GLenum pname, GLint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedBufferParameteri64v(%u, %s, %p)\n", buffer, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedBufferParameteri64v(ctx->Dispatch.RealPublished, (buffer, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetNamedBufferParameteriv(GLuint buffer, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedBufferParameteriv(%u, %s, %p)\n", buffer, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedBufferParameteriv(ctx->Dispatch.RealPublished, (buffer, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetNamedBufferPointerv(GLuint buffer, GLenum pname, GLvoid **params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedBufferPointerv(%u, %s, %p)\n", buffer, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedBufferPointerv(ctx->Dispatch.RealPublished, (buffer, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedBufferSubData(%u, %" PRIdPTR ", %" PRIdPTR ", %p)\n", buffer, (intptr_t)offset, (intptr_t)size, (void *)data);
   CALL_GetNamedBufferSubData(ctx->Dispatch.RealPublished, (buffer, offset, size, data));
}

static void GLAPIENTRY
_mesa_trace_GetNamedFramebufferAttachmentParameteriv(GLuint framebuffer, GLenum attachment, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedFramebufferAttachmentParameteriv(%u, %s, %s, %p)\n", framebuffer, _mesa_enum_to_string(attachment), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedFramebufferAttachmentParameteriv(ctx->Dispatch.RealPublished, (framebuffer, attachment, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetNamedFramebufferParameteriv(GLuint framebuffer, GLenum pname, GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedFramebufferParameteriv(%u, %s, %p)\n", framebuffer, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetNamedFramebufferParameteriv(ctx->Dispatch.RealPublished, (framebuffer, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetNamedRenderbufferParameteriv(GLuint renderbuffer, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedRenderbufferParameteriv(%u, %s, %p)\n", renderbuffer, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedRenderbufferParameteriv(ctx->Dispatch.RealPublished, (renderbuffer, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetQueryBufferObjecti64v(GLuint id, GLuint buffer, GLenum pname, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryBufferObjecti64v(%u, %u, %s, %" PRIdPTR ")\n", id, buffer, _mesa_enum_to_string(pname), (intptr_t)offset);
   CALL_GetQueryBufferObjecti64v(ctx->Dispatch.RealPublished, (id, buffer, pname, offset));
}

static void GLAPIENTRY
_mesa_trace_GetQueryBufferObjectiv(GLuint id, GLuint buffer, GLenum pname, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryBufferObjectiv(%u, %u, %s, %" PRIdPTR ")\n", id, buffer, _mesa_enum_to_string(pname), (intptr_t)offset);
   CALL_GetQueryBufferObjectiv(ctx->Dispatch.RealPublished, (id, buffer, pname, offset));
}

static void GLAPIENTRY
_mesa_trace_GetQueryBufferObjectui64v(GLuint id, GLuint buffer, GLenum pname, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryBufferObjectui64v(%u, %u, %s, %" PRIdPTR ")\n", id, buffer, _mesa_enum_to_string(pname), (intptr_t)offset);
   CALL_GetQueryBufferObjectui64v(ctx->Dispatch.RealPublished, (id, buffer, pname, offset));
}

static void GLAPIENTRY
_mesa_trace_GetQueryBufferObjectuiv(GLuint id, GLuint buffer, GLenum pname, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetQueryBufferObjectuiv(%u, %u, %s, %" PRIdPTR ")\n", id, buffer, _mesa_enum_to_string(pname), (intptr_t)offset);
   CALL_GetQueryBufferObjectuiv(ctx->Dispatch.RealPublished, (id, buffer, pname, offset));
}

static void GLAPIENTRY
_mesa_trace_GetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureImage(%u, %d, %s, %s, %d, %p)\n", texture, level, _mesa_enum_to_string(format), _mesa_enum_to_string(type), bufSize, (void *)pixels);
   CALL_GetTextureImage(ctx->Dispatch.RealPublished, (texture, level, format, type, bufSize, pixels));
}

static void GLAPIENTRY
_mesa_trace_GetTextureLevelParameterfv(GLuint texture, GLint level, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureLevelParameterfv(%u, %d, %s, %p)\n", texture, level, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureLevelParameterfv(ctx->Dispatch.RealPublished, (texture, level, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureLevelParameteriv(%u, %d, %s, %p)\n", texture, level, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureLevelParameteriv(ctx->Dispatch.RealPublished, (texture, level, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureParameterIiv(GLuint texture, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureParameterIiv(%u, %s, %p)\n", texture, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureParameterIiv(ctx->Dispatch.RealPublished, (texture, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureParameterIuiv(%u, %s, %p)\n", texture, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureParameterIuiv(ctx->Dispatch.RealPublished, (texture, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureParameterfv(GLuint texture, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureParameterfv(%u, %s, %p)\n", texture, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureParameterfv(ctx->Dispatch.RealPublished, (texture, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureParameteriv(GLuint texture, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureParameteriv(%u, %s, %p)\n", texture, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureParameteriv(ctx->Dispatch.RealPublished, (texture, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTransformFeedbacki64_v(GLuint xfb, GLenum pname, GLuint index, GLint64 *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTransformFeedbacki64_v(%u, %s, %u, %p)\n", xfb, _mesa_enum_to_string(pname), index, (void *)param);
   CALL_GetTransformFeedbacki64_v(ctx->Dispatch.RealPublished, (xfb, pname, index, param));
}

static void GLAPIENTRY
_mesa_trace_GetTransformFeedbacki_v(GLuint xfb, GLenum pname, GLuint index, GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTransformFeedbacki_v(%u, %s, %u, %p)\n", xfb, _mesa_enum_to_string(pname), index, (void *)param);
   CALL_GetTransformFeedbacki_v(ctx->Dispatch.RealPublished, (xfb, pname, index, param));
}

static void GLAPIENTRY
_mesa_trace_GetTransformFeedbackiv(GLuint xfb, GLenum pname, GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTransformFeedbackiv(%u, %s, %p)\n", xfb, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetTransformFeedbackiv(ctx->Dispatch.RealPublished, (xfb, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetVertexArrayIndexed64iv(GLuint vaobj, GLuint index, GLenum pname, GLint64 *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexArrayIndexed64iv(%u, %u, %s, %p)\n", vaobj, index, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetVertexArrayIndexed64iv(ctx->Dispatch.RealPublished, (vaobj, index, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetVertexArrayIndexediv(GLuint vaobj, GLuint index, GLenum pname, GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexArrayIndexediv(%u, %u, %s, %p)\n", vaobj, index, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetVertexArrayIndexediv(ctx->Dispatch.RealPublished, (vaobj, index, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetVertexArrayiv(GLuint vaobj, GLenum pname, GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexArrayiv(%u, %s, %p)\n", vaobj, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetVertexArrayiv(ctx->Dispatch.RealPublished, (vaobj, pname, param));
}

static void GLAPIENTRY
_mesa_trace_InvalidateNamedFramebufferData(GLuint framebuffer, GLsizei numAttachments, const GLenum *attachments)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInvalidateNamedFramebufferData(%u, %d, %p)\n", framebuffer, numAttachments, (void *)attachments);
   CALL_InvalidateNamedFramebufferData(ctx->Dispatch.RealPublished, (framebuffer, numAttachments, attachments));
}

static void GLAPIENTRY
_mesa_trace_InvalidateNamedFramebufferSubData(GLuint framebuffer, GLsizei numAttachments, const GLenum *attachments, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInvalidateNamedFramebufferSubData(%u, %d, %p, %d, %d, %d, %d)\n", framebuffer, numAttachments, (void *)attachments, x, y, width, height);
   CALL_InvalidateNamedFramebufferSubData(ctx->Dispatch.RealPublished, (framebuffer, numAttachments, attachments, x, y, width, height));
}

static GLvoid * GLAPIENTRY
_mesa_trace_MapNamedBuffer(GLuint buffer, GLenum access)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapNamedBuffer(%u, %s)\n", buffer, _mesa_enum_to_string(access));
   return CALL_MapNamedBuffer(ctx->Dispatch.RealPublished, (buffer, access));
}

static GLvoid * GLAPIENTRY
_mesa_trace_MapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapNamedBufferRange(%u, %" PRIdPTR ", %" PRIdPTR ", 0x%x)\n", buffer, (intptr_t)offset, (intptr_t)length, access);
   return CALL_MapNamedBufferRange(ctx->Dispatch.RealPublished, (buffer, offset, length, access));
}

static void GLAPIENTRY
_mesa_trace_NamedBufferData(GLuint buffer, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedBufferData(%u, %" PRIdPTR ", %p, %s)\n", buffer, (intptr_t)size, (void *)data, _mesa_enum_to_string(usage));
   CALL_NamedBufferData(ctx->Dispatch.RealPublished, (buffer, size, data, usage));
}

static void GLAPIENTRY
_mesa_trace_NamedBufferStorage(GLuint buffer, GLsizeiptr size, const GLvoid *data, GLbitfield flags)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedBufferStorage(%u, %" PRIdPTR ", %p, 0x%x)\n", buffer, (intptr_t)size, (void *)data, flags);
   CALL_NamedBufferStorage(ctx->Dispatch.RealPublished, (buffer, size, data, flags));
}

static void GLAPIENTRY
_mesa_trace_NamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedBufferSubData(%u, %" PRIdPTR ", %" PRIdPTR ", %p)\n", buffer, (intptr_t)offset, (intptr_t)size, (void *)data);
   CALL_NamedBufferSubData(ctx->Dispatch.RealPublished, (buffer, offset, size, data));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferDrawBuffer(GLuint framebuffer, GLenum buf)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferDrawBuffer(%u, %s)\n", framebuffer, _mesa_enum_to_string(buf));
   CALL_NamedFramebufferDrawBuffer(ctx->Dispatch.RealPublished, (framebuffer, buf));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum *bufs)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferDrawBuffers(%u, %d, %p)\n", framebuffer, n, (void *)bufs);
   CALL_NamedFramebufferDrawBuffers(ctx->Dispatch.RealPublished, (framebuffer, n, bufs));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferParameteri(GLuint framebuffer, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferParameteri(%u, %s, %d)\n", framebuffer, _mesa_enum_to_string(pname), param);
   CALL_NamedFramebufferParameteri(ctx->Dispatch.RealPublished, (framebuffer, pname, param));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferReadBuffer(GLuint framebuffer, GLenum buf)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferReadBuffer(%u, %s)\n", framebuffer, _mesa_enum_to_string(buf));
   CALL_NamedFramebufferReadBuffer(ctx->Dispatch.RealPublished, (framebuffer, buf));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferRenderbuffer(GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferRenderbuffer(%u, %s, %s, %u)\n", framebuffer, _mesa_enum_to_string(attachment), _mesa_enum_to_string(renderbuffertarget), renderbuffer);
   CALL_NamedFramebufferRenderbuffer(ctx->Dispatch.RealPublished, (framebuffer, attachment, renderbuffertarget, renderbuffer));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferTexture(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferTexture(%u, %s, %u, %d)\n", framebuffer, _mesa_enum_to_string(attachment), texture, level);
   CALL_NamedFramebufferTexture(ctx->Dispatch.RealPublished, (framebuffer, attachment, texture, level));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint layer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferTextureLayer(%u, %s, %u, %d, %d)\n", framebuffer, _mesa_enum_to_string(attachment), texture, level, layer);
   CALL_NamedFramebufferTextureLayer(ctx->Dispatch.RealPublished, (framebuffer, attachment, texture, level, layer));
}

static void GLAPIENTRY
_mesa_trace_NamedRenderbufferStorage(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedRenderbufferStorage(%u, %s, %d, %d)\n", renderbuffer, _mesa_enum_to_string(internalformat), width, height);
   CALL_NamedRenderbufferStorage(ctx->Dispatch.RealPublished, (renderbuffer, internalformat, width, height));
}

static void GLAPIENTRY
_mesa_trace_NamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedRenderbufferStorageMultisample(%u, %d, %s, %d, %d)\n", renderbuffer, samples, _mesa_enum_to_string(internalformat), width, height);
   CALL_NamedRenderbufferStorageMultisample(ctx->Dispatch.RealPublished, (renderbuffer, samples, internalformat, width, height));
}

static void GLAPIENTRY
_mesa_trace_TextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureBuffer(%u, %s, %u)\n", texture, _mesa_enum_to_string(internalformat), buffer);
   CALL_TextureBuffer(ctx->Dispatch.RealPublished, (texture, internalformat, buffer));
}

static void GLAPIENTRY
_mesa_trace_TextureBufferRange(GLuint texture, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureBufferRange(%u, %s, %u, %" PRIdPTR ", %" PRIdPTR ")\n", texture, _mesa_enum_to_string(internalformat), buffer, (intptr_t)offset, (intptr_t)size);
   CALL_TextureBufferRange(ctx->Dispatch.RealPublished, (texture, internalformat, buffer, offset, size));
}

static void GLAPIENTRY
_mesa_trace_TextureParameterIiv(GLuint texture, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameterIiv(%u, %s, %p)\n", texture, _mesa_enum_to_string(pname), (void *)params);
   CALL_TextureParameterIiv(ctx->Dispatch.RealPublished, (texture, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TextureParameterIuiv(GLuint texture, GLenum pname, const GLuint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameterIuiv(%u, %s, %p)\n", texture, _mesa_enum_to_string(pname), (void *)params);
   CALL_TextureParameterIuiv(ctx->Dispatch.RealPublished, (texture, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TextureParameterf(GLuint texture, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameterf(%u, %s, %f)\n", texture, _mesa_enum_to_string(pname), param);
   CALL_TextureParameterf(ctx->Dispatch.RealPublished, (texture, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TextureParameterfv(GLuint texture, GLenum pname, const GLfloat *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameterfv(%u, %s, %p)\n", texture, _mesa_enum_to_string(pname), (void *)param);
   CALL_TextureParameterfv(ctx->Dispatch.RealPublished, (texture, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TextureParameteri(GLuint texture, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameteri(%u, %s, %d)\n", texture, _mesa_enum_to_string(pname), param);
   CALL_TextureParameteri(ctx->Dispatch.RealPublished, (texture, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TextureParameteriv(GLuint texture, GLenum pname, const GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameteriv(%u, %s, %p)\n", texture, _mesa_enum_to_string(pname), (void *)param);
   CALL_TextureParameteriv(ctx->Dispatch.RealPublished, (texture, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage1D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage1D(%u, %d, %s, %d)\n", texture, levels, _mesa_enum_to_string(internalformat), width);
   CALL_TextureStorage1D(ctx->Dispatch.RealPublished, (texture, levels, internalformat, width));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage2D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage2D(%u, %d, %s, %d, %d)\n", texture, levels, _mesa_enum_to_string(internalformat), width, height);
   CALL_TextureStorage2D(ctx->Dispatch.RealPublished, (texture, levels, internalformat, width, height));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage2DMultisample(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage2DMultisample(%u, %d, %s, %d, %d, %s)\n", texture, samples, _mesa_enum_to_string(internalformat), width, height, fixedsamplelocations ? "GL_TRUE" : "GL_FALSE");
   CALL_TextureStorage2DMultisample(ctx->Dispatch.RealPublished, (texture, samples, internalformat, width, height, fixedsamplelocations));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage3D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage3D(%u, %d, %s, %d, %d, %d)\n", texture, levels, _mesa_enum_to_string(internalformat), width, height, depth);
   CALL_TextureStorage3D(ctx->Dispatch.RealPublished, (texture, levels, internalformat, width, height, depth));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage3DMultisample(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage3DMultisample(%u, %d, %s, %d, %d, %d, %s)\n", texture, samples, _mesa_enum_to_string(internalformat), width, height, depth, fixedsamplelocations ? "GL_TRUE" : "GL_FALSE");
   CALL_TextureStorage3DMultisample(ctx->Dispatch.RealPublished, (texture, samples, internalformat, width, height, depth, fixedsamplelocations));
}

static void GLAPIENTRY
_mesa_trace_TextureSubImage1D(GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureSubImage1D(%u, %d, %d, %d, %s, %s, %p)\n", texture, level, xoffset, width, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TextureSubImage1D(ctx->Dispatch.RealPublished, (texture, level, xoffset, width, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureSubImage2D(%u, %d, %d, %d, %d, %d, %s, %s, %p)\n", texture, level, xoffset, yoffset, width, height, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TextureSubImage2D(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, width, height, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureSubImage3D(%u, %d, %d, %d, %d, %d, %d, %d, %s, %s, %p)\n", texture, level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TextureSubImage3D(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TransformFeedbackBufferBase(GLuint xfb, GLuint index, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTransformFeedbackBufferBase(%u, %u, %u)\n", xfb, index, buffer);
   CALL_TransformFeedbackBufferBase(ctx->Dispatch.RealPublished, (xfb, index, buffer));
}

static void GLAPIENTRY
_mesa_trace_TransformFeedbackBufferRange(GLuint xfb, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTransformFeedbackBufferRange(%u, %u, %u, %" PRIdPTR ", %" PRIdPTR ")\n", xfb, index, buffer, (intptr_t)offset, (intptr_t)size);
   CALL_TransformFeedbackBufferRange(ctx->Dispatch.RealPublished, (xfb, index, buffer, offset, size));
}

static GLboolean GLAPIENTRY
_mesa_trace_UnmapNamedBufferEXT(GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUnmapNamedBufferEXT(%u)\n", buffer);
   return CALL_UnmapNamedBufferEXT(ctx->Dispatch.RealPublished, (buffer));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayAttribBinding(GLuint vaobj, GLuint attribindex, GLuint bindingindex)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayAttribBinding(%u, %u, %u)\n", vaobj, attribindex, bindingindex);
   CALL_VertexArrayAttribBinding(ctx->Dispatch.RealPublished, (vaobj, attribindex, bindingindex));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayAttribFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayAttribFormat(%u, %u, %d, %s, %s, %u)\n", vaobj, attribindex, size, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", relativeoffset);
   CALL_VertexArrayAttribFormat(ctx->Dispatch.RealPublished, (vaobj, attribindex, size, type, normalized, relativeoffset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayAttribIFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayAttribIFormat(%u, %u, %d, %s, %u)\n", vaobj, attribindex, size, _mesa_enum_to_string(type), relativeoffset);
   CALL_VertexArrayAttribIFormat(ctx->Dispatch.RealPublished, (vaobj, attribindex, size, type, relativeoffset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayAttribLFormat(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayAttribLFormat(%u, %u, %d, %s, %u)\n", vaobj, attribindex, size, _mesa_enum_to_string(type), relativeoffset);
   CALL_VertexArrayAttribLFormat(ctx->Dispatch.RealPublished, (vaobj, attribindex, size, type, relativeoffset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayBindingDivisor(GLuint vaobj, GLuint bindingindex, GLuint divisor)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayBindingDivisor(%u, %u, %u)\n", vaobj, bindingindex, divisor);
   CALL_VertexArrayBindingDivisor(ctx->Dispatch.RealPublished, (vaobj, bindingindex, divisor));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayElementBuffer(GLuint vaobj, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayElementBuffer(%u, %u)\n", vaobj, buffer);
   CALL_VertexArrayElementBuffer(ctx->Dispatch.RealPublished, (vaobj, buffer));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexBuffer(GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexBuffer(%u, %u, %u, %" PRIdPTR ", %d)\n", vaobj, bindingindex, buffer, (intptr_t)offset, stride);
   CALL_VertexArrayVertexBuffer(ctx->Dispatch.RealPublished, (vaobj, bindingindex, buffer, offset, stride));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexBuffers(GLuint vaobj, GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLsizei *strides)
{
   GET_CURRENT_CONTEXT(ctx);
   char buffers_buf[512];
   _mesa_trace_format_array(buffers_buf, sizeof(buffers_buf), buffers, (size_t)count, MESA_TRACE_ELEM_UINT);
   char offsets_buf[512];
   _mesa_trace_format_array(offsets_buf, sizeof(offsets_buf), offsets, (size_t)count, MESA_TRACE_ELEM_INTPTR);
   char strides_buf[512];
   _mesa_trace_format_array(strides_buf, sizeof(strides_buf), strides, (size_t)count, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glVertexArrayVertexBuffers(%u, %u, %d, %s, %s, %s)\n", vaobj, first, count, buffers_buf, offsets_buf, strides_buf);
   CALL_VertexArrayVertexBuffers(ctx->Dispatch.RealPublished, (vaobj, first, count, buffers, offsets, strides));
}

static void GLAPIENTRY
_mesa_trace_GetCompressedTextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLsizei bufSize, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetCompressedTextureSubImage(%u, %d, %d, %d, %d, %d, %d, %d, %d, %p)\n", texture, level, xoffset, yoffset, zoffset, width, height, depth, bufSize, (void *)pixels);
   CALL_GetCompressedTextureSubImage(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, zoffset, width, height, depth, bufSize, pixels));
}

static void GLAPIENTRY
_mesa_trace_GetTextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, GLsizei bufSize, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureSubImage(%u, %d, %d, %d, %d, %d, %d, %d, %s, %s, %d, %p)\n", texture, level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), _mesa_enum_to_string(type), bufSize, (void *)pixels);
   CALL_GetTextureSubImage(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, bufSize, pixels));
}

static void GLAPIENTRY
_mesa_trace_BufferPageCommitmentARB(GLenum target, GLintptr offset, GLsizeiptr size, GLboolean commit)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBufferPageCommitmentARB(%s, %" PRIdPTR ", %" PRIdPTR ", %s)\n", _mesa_enum_to_string(target), (intptr_t)offset, (intptr_t)size, commit ? "GL_TRUE" : "GL_FALSE");
   CALL_BufferPageCommitmentARB(ctx->Dispatch.RealPublished, (target, offset, size, commit));
}

static void GLAPIENTRY
_mesa_trace_NamedBufferPageCommitmentARB(GLuint buffer, GLintptr offset, GLsizeiptr size, GLboolean commit)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedBufferPageCommitmentARB(%u, %" PRIdPTR ", %" PRIdPTR ", %s)\n", buffer, (intptr_t)offset, (intptr_t)size, commit ? "GL_TRUE" : "GL_FALSE");
   CALL_NamedBufferPageCommitmentARB(ctx->Dispatch.RealPublished, (buffer, offset, size, commit));
}

static void GLAPIENTRY
_mesa_trace_GetUniformi64vARB(GLuint program, GLint location, GLint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformi64vARB(%u, %d, %p)\n", program, location, (void *)params);
   CALL_GetUniformi64vARB(ctx->Dispatch.RealPublished, (program, location, params));
}

static void GLAPIENTRY
_mesa_trace_GetUniformui64vARB(GLuint program, GLint location, GLuint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUniformui64vARB(%u, %d, %p)\n", program, location, (void *)params);
   CALL_GetUniformui64vARB(ctx->Dispatch.RealPublished, (program, location, params));
}

static void GLAPIENTRY
_mesa_trace_GetnUniformi64vARB(GLuint program, GLint location, GLsizei bufSize, GLint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnUniformi64vARB(%u, %d, %d, %p)\n", program, location, bufSize, (void *)params);
   CALL_GetnUniformi64vARB(ctx->Dispatch.RealPublished, (program, location, bufSize, params));
}

static void GLAPIENTRY
_mesa_trace_GetnUniformui64vARB(GLuint program, GLint location, GLsizei bufSize, GLuint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnUniformui64vARB(%u, %d, %d, %p)\n", program, location, bufSize, (void *)params);
   CALL_GetnUniformui64vARB(ctx->Dispatch.RealPublished, (program, location, bufSize, params));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1i64ARB(GLuint program, GLint location, GLint64 x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform1i64ARB(%u, %d, %" PRId64 ")\n", program, location, (int64_t)x);
   CALL_ProgramUniform1i64ARB(ctx->Dispatch.RealPublished, (program, location, x));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1i64vARB(GLuint program, GLint location, GLsizei count, const GLint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_INT64);
   _mesa_debug(ctx, "glProgramUniform1i64vARB(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform1i64vARB(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1ui64ARB(GLuint program, GLint location, GLuint64 x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform1ui64ARB(%u, %d, %" PRIu64 ")\n", program, location, (uint64_t)x);
   CALL_ProgramUniform1ui64ARB(ctx->Dispatch.RealPublished, (program, location, x));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1ui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glProgramUniform1ui64vARB(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform1ui64vARB(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2i64ARB(GLuint program, GLint location, GLint64 x, GLint64 y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform2i64ARB(%u, %d, %" PRId64 ", %" PRId64 ")\n", program, location, (int64_t)x, (int64_t)y);
   CALL_ProgramUniform2i64ARB(ctx->Dispatch.RealPublished, (program, location, x, y));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2i64vARB(GLuint program, GLint location, GLsizei count, const GLint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_INT64);
   _mesa_debug(ctx, "glProgramUniform2i64vARB(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform2i64vARB(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2ui64ARB(GLuint program, GLint location, GLuint64 x, GLuint64 y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform2ui64ARB(%u, %d, %" PRIu64 ", %" PRIu64 ")\n", program, location, (uint64_t)x, (uint64_t)y);
   CALL_ProgramUniform2ui64ARB(ctx->Dispatch.RealPublished, (program, location, x, y));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2ui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glProgramUniform2ui64vARB(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform2ui64vARB(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3i64ARB(GLuint program, GLint location, GLint64 x, GLint64 y, GLint64 z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform3i64ARB(%u, %d, %" PRId64 ", %" PRId64 ", %" PRId64 ")\n", program, location, (int64_t)x, (int64_t)y, (int64_t)z);
   CALL_ProgramUniform3i64ARB(ctx->Dispatch.RealPublished, (program, location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3i64vARB(GLuint program, GLint location, GLsizei count, const GLint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_INT64);
   _mesa_debug(ctx, "glProgramUniform3i64vARB(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform3i64vARB(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3ui64ARB(GLuint program, GLint location, GLuint64 x, GLuint64 y, GLuint64 z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform3ui64ARB(%u, %d, %" PRIu64 ", %" PRIu64 ", %" PRIu64 ")\n", program, location, (uint64_t)x, (uint64_t)y, (uint64_t)z);
   CALL_ProgramUniform3ui64ARB(ctx->Dispatch.RealPublished, (program, location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3ui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glProgramUniform3ui64vARB(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform3ui64vARB(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4i64ARB(GLuint program, GLint location, GLint64 x, GLint64 y, GLint64 z, GLint64 w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform4i64ARB(%u, %d, %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ")\n", program, location, (int64_t)x, (int64_t)y, (int64_t)z, (int64_t)w);
   CALL_ProgramUniform4i64ARB(ctx->Dispatch.RealPublished, (program, location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4i64vARB(GLuint program, GLint location, GLsizei count, const GLint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_INT64);
   _mesa_debug(ctx, "glProgramUniform4i64vARB(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform4i64vARB(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4ui64ARB(GLuint program, GLint location, GLuint64 x, GLuint64 y, GLuint64 z, GLuint64 w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform4ui64ARB(%u, %d, %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ")\n", program, location, (uint64_t)x, (uint64_t)y, (uint64_t)z, (uint64_t)w);
   CALL_ProgramUniform4ui64ARB(ctx->Dispatch.RealPublished, (program, location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4ui64vARB(GLuint program, GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glProgramUniform4ui64vARB(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform4ui64vARB(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform1i64ARB(GLint location, GLint64 x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform1i64ARB(%d, %" PRId64 ")\n", location, (int64_t)x);
   CALL_Uniform1i64ARB(ctx->Dispatch.RealPublished, (location, x));
}

static void GLAPIENTRY
_mesa_trace_Uniform1i64vARB(GLint location, GLsizei count, const GLint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_INT64);
   _mesa_debug(ctx, "glUniform1i64vARB(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform1i64vARB(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform1ui64ARB(GLint location, GLuint64 x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform1ui64ARB(%d, %" PRIu64 ")\n", location, (uint64_t)x);
   CALL_Uniform1ui64ARB(ctx->Dispatch.RealPublished, (location, x));
}

static void GLAPIENTRY
_mesa_trace_Uniform1ui64vARB(GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glUniform1ui64vARB(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform1ui64vARB(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform2i64ARB(GLint location, GLint64 x, GLint64 y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform2i64ARB(%d, %" PRId64 ", %" PRId64 ")\n", location, (int64_t)x, (int64_t)y);
   CALL_Uniform2i64ARB(ctx->Dispatch.RealPublished, (location, x, y));
}

static void GLAPIENTRY
_mesa_trace_Uniform2i64vARB(GLint location, GLsizei count, const GLint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_INT64);
   _mesa_debug(ctx, "glUniform2i64vARB(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform2i64vARB(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform2ui64ARB(GLint location, GLuint64 x, GLuint64 y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform2ui64ARB(%d, %" PRIu64 ", %" PRIu64 ")\n", location, (uint64_t)x, (uint64_t)y);
   CALL_Uniform2ui64ARB(ctx->Dispatch.RealPublished, (location, x, y));
}

static void GLAPIENTRY
_mesa_trace_Uniform2ui64vARB(GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glUniform2ui64vARB(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform2ui64vARB(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform3i64ARB(GLint location, GLint64 x, GLint64 y, GLint64 z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform3i64ARB(%d, %" PRId64 ", %" PRId64 ", %" PRId64 ")\n", location, (int64_t)x, (int64_t)y, (int64_t)z);
   CALL_Uniform3i64ARB(ctx->Dispatch.RealPublished, (location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Uniform3i64vARB(GLint location, GLsizei count, const GLint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_INT64);
   _mesa_debug(ctx, "glUniform3i64vARB(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform3i64vARB(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform3ui64ARB(GLint location, GLuint64 x, GLuint64 y, GLuint64 z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform3ui64ARB(%d, %" PRIu64 ", %" PRIu64 ", %" PRIu64 ")\n", location, (uint64_t)x, (uint64_t)y, (uint64_t)z);
   CALL_Uniform3ui64ARB(ctx->Dispatch.RealPublished, (location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Uniform3ui64vARB(GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glUniform3ui64vARB(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform3ui64vARB(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform4i64ARB(GLint location, GLint64 x, GLint64 y, GLint64 z, GLint64 w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform4i64ARB(%d, %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ")\n", location, (int64_t)x, (int64_t)y, (int64_t)z, (int64_t)w);
   CALL_Uniform4i64ARB(ctx->Dispatch.RealPublished, (location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_Uniform4i64vARB(GLint location, GLsizei count, const GLint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_INT64);
   _mesa_debug(ctx, "glUniform4i64vARB(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform4i64vARB(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_Uniform4ui64ARB(GLint location, GLuint64 x, GLuint64 y, GLuint64 z, GLuint64 w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUniform4ui64ARB(%d, %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ")\n", location, (uint64_t)x, (uint64_t)y, (uint64_t)z, (uint64_t)w);
   CALL_Uniform4ui64ARB(ctx->Dispatch.RealPublished, (location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_Uniform4ui64vARB(GLint location, GLsizei count, const GLuint64 *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_UINT64);
   _mesa_debug(ctx, "glUniform4ui64vARB(%d, %d, %s)\n", location, count, value_buf);
   CALL_Uniform4ui64vARB(ctx->Dispatch.RealPublished, (location, count, value));
}

static void GLAPIENTRY
_mesa_trace_EvaluateDepthValuesARB(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEvaluateDepthValuesARB()\n");
   CALL_EvaluateDepthValuesARB(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_FramebufferSampleLocationsfvARB(GLenum target, GLuint start, GLsizei count, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)(2 * count), MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glFramebufferSampleLocationsfvARB(%s, %u, %d, %s)\n", _mesa_enum_to_string(target), start, count, v_buf);
   CALL_FramebufferSampleLocationsfvARB(ctx->Dispatch.RealPublished, (target, start, count, v));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferSampleLocationsfvARB(GLuint framebuffer, GLuint start, GLsizei count, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)(2 * count), MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glNamedFramebufferSampleLocationsfvARB(%u, %u, %d, %s)\n", framebuffer, start, count, v_buf);
   CALL_NamedFramebufferSampleLocationsfvARB(ctx->Dispatch.RealPublished, (framebuffer, start, count, v));
}

static void GLAPIENTRY
_mesa_trace_SpecializeShaderARB(GLuint shader, const GLchar *pEntryPoint, GLuint numSpecializationConstants, const GLuint *pConstantIndex, const GLuint *pConstantValue)
{
   GET_CURRENT_CONTEXT(ctx);
   char pConstantIndex_buf[512];
   _mesa_trace_format_array(pConstantIndex_buf, sizeof(pConstantIndex_buf), pConstantIndex, (size_t)numSpecializationConstants, MESA_TRACE_ELEM_UINT);
   char pConstantValue_buf[512];
   _mesa_trace_format_array(pConstantValue_buf, sizeof(pConstantValue_buf), pConstantValue, (size_t)numSpecializationConstants, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glSpecializeShaderARB(%u, %s, %u, %s, %s)\n", shader, pEntryPoint ? (const char *)pEntryPoint : "(null)", numSpecializationConstants, pConstantIndex_buf, pConstantValue_buf);
   CALL_SpecializeShaderARB(ctx->Dispatch.RealPublished, (shader, pEntryPoint, numSpecializationConstants, pConstantIndex, pConstantValue));
}

static void GLAPIENTRY
_mesa_trace_InvalidateBufferData(GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInvalidateBufferData(%u)\n", buffer);
   CALL_InvalidateBufferData(ctx->Dispatch.RealPublished, (buffer));
}

static void GLAPIENTRY
_mesa_trace_InvalidateBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr length)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInvalidateBufferSubData(%u, %" PRIdPTR ", %" PRIdPTR ")\n", buffer, (intptr_t)offset, (intptr_t)length);
   CALL_InvalidateBufferSubData(ctx->Dispatch.RealPublished, (buffer, offset, length));
}

static void GLAPIENTRY
_mesa_trace_InvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInvalidateFramebuffer(%s, %d, %p)\n", _mesa_enum_to_string(target), numAttachments, (void *)attachments);
   CALL_InvalidateFramebuffer(ctx->Dispatch.RealPublished, (target, numAttachments, attachments));
}

static void GLAPIENTRY
_mesa_trace_InvalidateSubFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInvalidateSubFramebuffer(%s, %d, %p, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), numAttachments, (void *)attachments, x, y, width, height);
   CALL_InvalidateSubFramebuffer(ctx->Dispatch.RealPublished, (target, numAttachments, attachments, x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_InvalidateTexImage(GLuint texture, GLint level)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInvalidateTexImage(%u, %d)\n", texture, level);
   CALL_InvalidateTexImage(ctx->Dispatch.RealPublished, (texture, level));
}

static void GLAPIENTRY
_mesa_trace_InvalidateTexSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInvalidateTexSubImage(%u, %d, %d, %d, %d, %d, %d, %d)\n", texture, level, xoffset, yoffset, zoffset, width, height, depth);
   CALL_InvalidateTexSubImage(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, zoffset, width, height, depth));
}

static void GLAPIENTRY
_mesa_trace_DrawTexfOES(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawTexfOES(%f, %f, %f, %f, %f)\n", x, y, z, width, height);
   CALL_DrawTexfOES(ctx->Dispatch.RealPublished, (x, y, z, width, height));
}

static void GLAPIENTRY
_mesa_trace_DrawTexfvOES(const GLfloat *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   char coords_buf[512];
   _mesa_trace_format_array(coords_buf, sizeof(coords_buf), coords, 5, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glDrawTexfvOES(%s)\n", coords_buf);
   CALL_DrawTexfvOES(ctx->Dispatch.RealPublished, (coords));
}

static void GLAPIENTRY
_mesa_trace_DrawTexiOES(GLint x, GLint y, GLint z, GLint width, GLint height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawTexiOES(%d, %d, %d, %d, %d)\n", x, y, z, width, height);
   CALL_DrawTexiOES(ctx->Dispatch.RealPublished, (x, y, z, width, height));
}

static void GLAPIENTRY
_mesa_trace_DrawTexivOES(const GLint *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   char coords_buf[512];
   _mesa_trace_format_array(coords_buf, sizeof(coords_buf), coords, 5, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glDrawTexivOES(%s)\n", coords_buf);
   CALL_DrawTexivOES(ctx->Dispatch.RealPublished, (coords));
}

static void GLAPIENTRY
_mesa_trace_DrawTexsOES(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawTexsOES(%d, %d, %d, %d, %d)\n", x, y, z, width, height);
   CALL_DrawTexsOES(ctx->Dispatch.RealPublished, (x, y, z, width, height));
}

static void GLAPIENTRY
_mesa_trace_DrawTexsvOES(const GLshort *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   char coords_buf[512];
   _mesa_trace_format_array(coords_buf, sizeof(coords_buf), coords, 5, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glDrawTexsvOES(%s)\n", coords_buf);
   CALL_DrawTexsvOES(ctx->Dispatch.RealPublished, (coords));
}

static void GLAPIENTRY
_mesa_trace_DrawTexxOES(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawTexxOES(%d, %d, %d, %d, %d)\n", x, y, z, width, height);
   CALL_DrawTexxOES(ctx->Dispatch.RealPublished, (x, y, z, width, height));
}

static void GLAPIENTRY
_mesa_trace_DrawTexxvOES(const GLfixed *coords)
{
   GET_CURRENT_CONTEXT(ctx);
   char coords_buf[512];
   _mesa_trace_format_array(coords_buf, sizeof(coords_buf), coords, 5, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glDrawTexxvOES(%s)\n", coords_buf);
   CALL_DrawTexxvOES(ctx->Dispatch.RealPublished, (coords));
}

static void GLAPIENTRY
_mesa_trace_PointSizePointerOES(GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPointSizePointerOES(%s, %d, %p)\n", _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_PointSizePointerOES(ctx->Dispatch.RealPublished, (type, stride, pointer));
}

static GLbitfield GLAPIENTRY
_mesa_trace_QueryMatrixxOES(GLfixed *mantissa, GLint *exponent)
{
   GET_CURRENT_CONTEXT(ctx);
   char mantissa_buf[512];
   _mesa_trace_format_array(mantissa_buf, sizeof(mantissa_buf), mantissa, 16, MESA_TRACE_ELEM_INT);
   char exponent_buf[512];
   _mesa_trace_format_array(exponent_buf, sizeof(exponent_buf), exponent, 16, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glQueryMatrixxOES(%s, %s)\n", mantissa_buf, exponent_buf);
   return CALL_QueryMatrixxOES(ctx->Dispatch.RealPublished, (mantissa, exponent));
}

static void GLAPIENTRY
_mesa_trace_ColorPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorPointerEXT(%d, %s, %d, %d, %p)\n", size, _mesa_enum_to_string(type), stride, count, (void *)pointer);
   CALL_ColorPointerEXT(ctx->Dispatch.RealPublished, (size, type, stride, count, pointer));
}

static void GLAPIENTRY
_mesa_trace_EdgeFlagPointerEXT(GLsizei stride, GLsizei count, const GLboolean *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEdgeFlagPointerEXT(%d, %d, %p)\n", stride, count, (void *)pointer);
   CALL_EdgeFlagPointerEXT(ctx->Dispatch.RealPublished, (stride, count, pointer));
}

static void GLAPIENTRY
_mesa_trace_IndexPointerEXT(GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIndexPointerEXT(%s, %d, %d, %p)\n", _mesa_enum_to_string(type), stride, count, (void *)pointer);
   CALL_IndexPointerEXT(ctx->Dispatch.RealPublished, (type, stride, count, pointer));
}

static void GLAPIENTRY
_mesa_trace_NormalPointerEXT(GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormalPointerEXT(%s, %d, %d, %p)\n", _mesa_enum_to_string(type), stride, count, (void *)pointer);
   CALL_NormalPointerEXT(ctx->Dispatch.RealPublished, (type, stride, count, pointer));
}

static void GLAPIENTRY
_mesa_trace_TexCoordPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoordPointerEXT(%d, %s, %d, %d, %p)\n", size, _mesa_enum_to_string(type), stride, count, (void *)pointer);
   CALL_TexCoordPointerEXT(ctx->Dispatch.RealPublished, (size, type, stride, count, pointer));
}

static void GLAPIENTRY
_mesa_trace_VertexPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexPointerEXT(%d, %s, %d, %d, %p)\n", size, _mesa_enum_to_string(type), stride, count, (void *)pointer);
   CALL_VertexPointerEXT(ctx->Dispatch.RealPublished, (size, type, stride, count, pointer));
}

static void GLAPIENTRY
_mesa_trace_DiscardFramebufferEXT(GLenum target, GLsizei numAttachments, const GLenum *attachments)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDiscardFramebufferEXT(%s, %d, %p)\n", _mesa_enum_to_string(target), numAttachments, (void *)attachments);
   CALL_DiscardFramebufferEXT(ctx->Dispatch.RealPublished, (target, numAttachments, attachments));
}

static void GLAPIENTRY
_mesa_trace_ActiveShaderProgram(GLuint pipeline, GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glActiveShaderProgram(%u, %u)\n", pipeline, program);
   CALL_ActiveShaderProgram(ctx->Dispatch.RealPublished, (pipeline, program));
}

static void GLAPIENTRY
_mesa_trace_BindProgramPipeline(GLuint pipeline)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindProgramPipeline(%u)\n", pipeline);
   CALL_BindProgramPipeline(ctx->Dispatch.RealPublished, (pipeline));
}

static GLuint GLAPIENTRY
_mesa_trace_CreateShaderProgramv(GLenum type, GLsizei count, const GLchar * const *strings)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateShaderProgramv(%s, %d, %s)\n", _mesa_enum_to_string(type), count, strings ? (const char *)strings : "(null)");
   return CALL_CreateShaderProgramv(ctx->Dispatch.RealPublished, (type, count, strings));
}

static void GLAPIENTRY
_mesa_trace_DeleteProgramPipelines(GLsizei n, const GLuint *pipelines)
{
   GET_CURRENT_CONTEXT(ctx);
   char pipelines_buf[512];
   _mesa_trace_format_array(pipelines_buf, sizeof(pipelines_buf), pipelines, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteProgramPipelines(%d, %s)\n", n, pipelines_buf);
   CALL_DeleteProgramPipelines(ctx->Dispatch.RealPublished, (n, pipelines));
}

static void GLAPIENTRY
_mesa_trace_GenProgramPipelines(GLsizei n, GLuint *pipelines)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenProgramPipelines(%d, %p)\n", n, (void *)pipelines);
   CALL_GenProgramPipelines(ctx->Dispatch.RealPublished, (n, pipelines));
}

static void GLAPIENTRY
_mesa_trace_GetProgramPipelineInfoLog(GLuint pipeline, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramPipelineInfoLog(%u, %d, %p, %p)\n", pipeline, bufSize, (void *)length, (void *)infoLog);
   CALL_GetProgramPipelineInfoLog(ctx->Dispatch.RealPublished, (pipeline, bufSize, length, infoLog));
}

static void GLAPIENTRY
_mesa_trace_GetProgramPipelineiv(GLuint pipeline, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetProgramPipelineiv(%u, %s, %p)\n", pipeline, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetProgramPipelineiv(ctx->Dispatch.RealPublished, (pipeline, pname, params));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsProgramPipeline(GLuint pipeline)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsProgramPipeline(%u)\n", pipeline);
   return CALL_IsProgramPipeline(ctx->Dispatch.RealPublished, (pipeline));
}

static void GLAPIENTRY
_mesa_trace_LockArraysEXT(GLint first, GLsizei count)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLockArraysEXT(%d, %d)\n", first, count);
   CALL_LockArraysEXT(ctx->Dispatch.RealPublished, (first, count));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1d(GLuint program, GLint location, GLdouble x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform1d(%u, %d, %f)\n", program, location, x);
   CALL_ProgramUniform1d(ctx->Dispatch.RealPublished, (program, location, x));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1dv(GLuint program, GLint location, GLsizei count, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniform1dv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform1dv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1f(GLuint program, GLint location, GLfloat x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform1f(%u, %d, %f)\n", program, location, x);
   CALL_ProgramUniform1f(ctx->Dispatch.RealPublished, (program, location, x));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1fv(GLuint program, GLint location, GLsizei count, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniform1fv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform1fv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1i(GLuint program, GLint location, GLint x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform1i(%u, %d, %d)\n", program, location, x);
   CALL_ProgramUniform1i(ctx->Dispatch.RealPublished, (program, location, x));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1iv(GLuint program, GLint location, GLsizei count, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glProgramUniform1iv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform1iv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1ui(GLuint program, GLint location, GLuint x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform1ui(%u, %d, %u)\n", program, location, x);
   CALL_ProgramUniform1ui(ctx->Dispatch.RealPublished, (program, location, x));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform1uiv(GLuint program, GLint location, GLsizei count, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glProgramUniform1uiv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform1uiv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2d(GLuint program, GLint location, GLdouble x, GLdouble y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform2d(%u, %d, %f, %f)\n", program, location, x, y);
   CALL_ProgramUniform2d(ctx->Dispatch.RealPublished, (program, location, x, y));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2dv(GLuint program, GLint location, GLsizei count, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniform2dv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform2dv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2f(GLuint program, GLint location, GLfloat x, GLfloat y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform2f(%u, %d, %f, %f)\n", program, location, x, y);
   CALL_ProgramUniform2f(ctx->Dispatch.RealPublished, (program, location, x, y));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2fv(GLuint program, GLint location, GLsizei count, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniform2fv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform2fv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2i(GLuint program, GLint location, GLint x, GLint y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform2i(%u, %d, %d, %d)\n", program, location, x, y);
   CALL_ProgramUniform2i(ctx->Dispatch.RealPublished, (program, location, x, y));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2iv(GLuint program, GLint location, GLsizei count, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glProgramUniform2iv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform2iv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2ui(GLuint program, GLint location, GLuint x, GLuint y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform2ui(%u, %d, %u, %u)\n", program, location, x, y);
   CALL_ProgramUniform2ui(ctx->Dispatch.RealPublished, (program, location, x, y));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform2uiv(GLuint program, GLint location, GLsizei count, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 2, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glProgramUniform2uiv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform2uiv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3d(GLuint program, GLint location, GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform3d(%u, %d, %f, %f, %f)\n", program, location, x, y, z);
   CALL_ProgramUniform3d(ctx->Dispatch.RealPublished, (program, location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3dv(GLuint program, GLint location, GLsizei count, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniform3dv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform3dv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3f(GLuint program, GLint location, GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform3f(%u, %d, %f, %f, %f)\n", program, location, x, y, z);
   CALL_ProgramUniform3f(ctx->Dispatch.RealPublished, (program, location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3fv(GLuint program, GLint location, GLsizei count, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniform3fv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform3fv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3i(GLuint program, GLint location, GLint x, GLint y, GLint z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform3i(%u, %d, %d, %d, %d)\n", program, location, x, y, z);
   CALL_ProgramUniform3i(ctx->Dispatch.RealPublished, (program, location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3iv(GLuint program, GLint location, GLsizei count, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glProgramUniform3iv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform3iv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3ui(GLuint program, GLint location, GLuint x, GLuint y, GLuint z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform3ui(%u, %d, %u, %u, %u)\n", program, location, x, y, z);
   CALL_ProgramUniform3ui(ctx->Dispatch.RealPublished, (program, location, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform3uiv(GLuint program, GLint location, GLsizei count, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 3, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glProgramUniform3uiv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform3uiv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4d(GLuint program, GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform4d(%u, %d, %f, %f, %f, %f)\n", program, location, x, y, z, w);
   CALL_ProgramUniform4d(ctx->Dispatch.RealPublished, (program, location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4dv(GLuint program, GLint location, GLsizei count, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniform4dv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform4dv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4f(GLuint program, GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform4f(%u, %d, %f, %f, %f, %f)\n", program, location, x, y, z, w);
   CALL_ProgramUniform4f(ctx->Dispatch.RealPublished, (program, location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4fv(GLuint program, GLint location, GLsizei count, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniform4fv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform4fv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4i(GLuint program, GLint location, GLint x, GLint y, GLint z, GLint w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform4i(%u, %d, %d, %d, %d, %d)\n", program, location, x, y, z, w);
   CALL_ProgramUniform4i(ctx->Dispatch.RealPublished, (program, location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4iv(GLuint program, GLint location, GLsizei count, const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glProgramUniform4iv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform4iv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4ui(GLuint program, GLint location, GLuint x, GLuint y, GLuint z, GLuint w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glProgramUniform4ui(%u, %d, %u, %u, %u, %u)\n", program, location, x, y, z, w);
   CALL_ProgramUniform4ui(ctx->Dispatch.RealPublished, (program, location, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniform4uiv(GLuint program, GLint location, GLsizei count, const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glProgramUniform4uiv(%u, %d, %d, %s)\n", program, location, count, value_buf);
   CALL_ProgramUniform4uiv(ctx->Dispatch.RealPublished, (program, location, count, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix2dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniformMatrix2dv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix2dv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix2fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniformMatrix2fv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix2fv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix2x3dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 6, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniformMatrix2x3dv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix2x3dv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix2x3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 6, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniformMatrix2x3fv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix2x3fv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix2x4dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 8, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniformMatrix2x4dv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix2x4dv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix2x4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 8, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniformMatrix2x4fv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix2x4fv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix3dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 9, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniformMatrix3dv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix3dv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 9, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniformMatrix3fv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix3fv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix3x2dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 6, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniformMatrix3x2dv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix3x2dv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix3x2fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 6, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniformMatrix3x2fv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix3x2fv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix3x4dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 12, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniformMatrix3x4dv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix3x4dv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix3x4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 12, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniformMatrix3x4fv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix3x4fv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix4dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniformMatrix4dv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix4dv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniformMatrix4fv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix4fv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix4x2dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 8, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniformMatrix4x2dv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix4x2dv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix4x2fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 8, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniformMatrix4x2fv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix4x2fv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix4x3dv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 12, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glProgramUniformMatrix4x3dv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix4x3dv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_ProgramUniformMatrix4x3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, (size_t)count * 12, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramUniformMatrix4x3fv(%u, %d, %d, %s, %s)\n", program, location, count, transpose ? "GL_TRUE" : "GL_FALSE", value_buf);
   CALL_ProgramUniformMatrix4x3fv(ctx->Dispatch.RealPublished, (program, location, count, transpose, value));
}

static void GLAPIENTRY
_mesa_trace_UnlockArraysEXT(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUnlockArraysEXT()\n");
   CALL_UnlockArraysEXT(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_UseProgramStages(GLuint pipeline, GLbitfield stages, GLuint program)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glUseProgramStages(%u, 0x%x, %u)\n", pipeline, stages, program);
   CALL_UseProgramStages(ctx->Dispatch.RealPublished, (pipeline, stages, program));
}

static void GLAPIENTRY
_mesa_trace_ValidateProgramPipeline(GLuint pipeline)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glValidateProgramPipeline(%u)\n", pipeline);
   CALL_ValidateProgramPipeline(ctx->Dispatch.RealPublished, (pipeline));
}

static void GLAPIENTRY
_mesa_trace_FramebufferTexture2DMultisampleEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLsizei samples)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferTexture2DMultisampleEXT(%s, %s, %s, %u, %d, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), _mesa_enum_to_string(textarget), texture, level, samples);
   CALL_FramebufferTexture2DMultisampleEXT(ctx->Dispatch.RealPublished, (target, attachment, textarget, texture, level, samples));
}

static void GLAPIENTRY
_mesa_trace_DebugMessageCallback(GLDEBUGPROC callback, const GLvoid *userParam)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDebugMessageCallback(%p, %p)\n", (void *)callback, (void *)userParam);
   CALL_DebugMessageCallback(ctx->Dispatch.RealPublished, (callback, userParam));
}

static void GLAPIENTRY
_mesa_trace_DebugMessageControl(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled)
{
   GET_CURRENT_CONTEXT(ctx);
   char ids_buf[512];
   _mesa_trace_format_array(ids_buf, sizeof(ids_buf), ids, (size_t)count, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDebugMessageControl(%s, %s, %s, %d, %s, %s)\n", _mesa_enum_to_string(source), _mesa_enum_to_string(type), _mesa_enum_to_string(severity), count, ids_buf, enabled ? "GL_TRUE" : "GL_FALSE");
   CALL_DebugMessageControl(ctx->Dispatch.RealPublished, (source, type, severity, count, ids, enabled));
}

static void GLAPIENTRY
_mesa_trace_DebugMessageInsert(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *buf)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDebugMessageInsert(%s, %s, %u, %s, %d, %s)\n", _mesa_enum_to_string(source), _mesa_enum_to_string(type), id, _mesa_enum_to_string(severity), length, buf ? (const char *)buf : "(null)");
   CALL_DebugMessageInsert(ctx->Dispatch.RealPublished, (source, type, id, severity, length, buf));
}

static GLuint GLAPIENTRY
_mesa_trace_GetDebugMessageLog(GLuint count, GLsizei bufsize, GLenum *sources, GLenum *types, GLuint *ids, GLenum *severities, GLsizei *lengths, GLchar *messageLog)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetDebugMessageLog(%u, %d, %p, %p, %p, %p, %p, %p)\n", count, bufsize, (void *)sources, (void *)types, (void *)ids, (void *)severities, (void *)lengths, (void *)messageLog);
   return CALL_GetDebugMessageLog(ctx->Dispatch.RealPublished, (count, bufsize, sources, types, ids, severities, lengths, messageLog));
}

static void GLAPIENTRY
_mesa_trace_GetObjectLabel(GLenum identifier, GLuint name, GLsizei bufSize, GLsizei *length, GLchar *label)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetObjectLabel(%s, %u, %d, %p, %p)\n", _mesa_enum_to_string(identifier), name, bufSize, (void *)length, (void *)label);
   CALL_GetObjectLabel(ctx->Dispatch.RealPublished, (identifier, name, bufSize, length, label));
}

static void GLAPIENTRY
_mesa_trace_GetObjectPtrLabel(const GLvoid *ptr, GLsizei bufSize, GLsizei *length, GLchar *label)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetObjectPtrLabel(%p, %d, %p, %p)\n", (void *)ptr, bufSize, (void *)length, (void *)label);
   CALL_GetObjectPtrLabel(ctx->Dispatch.RealPublished, (ptr, bufSize, length, label));
}

static void GLAPIENTRY
_mesa_trace_ObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar *label)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glObjectLabel(%s, %u, %d, %s)\n", _mesa_enum_to_string(identifier), name, length, label ? (const char *)label : "(null)");
   CALL_ObjectLabel(ctx->Dispatch.RealPublished, (identifier, name, length, label));
}

static void GLAPIENTRY
_mesa_trace_ObjectPtrLabel(const GLvoid *ptr, GLsizei length, const GLchar *label)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glObjectPtrLabel(%p, %d, %s)\n", (void *)ptr, length, label ? (const char *)label : "(null)");
   CALL_ObjectPtrLabel(ctx->Dispatch.RealPublished, (ptr, length, label));
}

static void GLAPIENTRY
_mesa_trace_PopDebugGroup(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPopDebugGroup()\n");
   CALL_PopDebugGroup(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_PushDebugGroup(GLenum source, GLuint id, GLsizei length, const GLchar *message)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPushDebugGroup(%s, %u, %d, %s)\n", _mesa_enum_to_string(source), id, length, message ? (const char *)message : "(null)");
   CALL_PushDebugGroup(ctx->Dispatch.RealPublished, (source, id, length, message));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3fEXT(GLfloat red, GLfloat green, GLfloat blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColor3fEXT(%f, %f, %f)\n", red, green, blue);
   CALL_SecondaryColor3fEXT(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3fvEXT(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glSecondaryColor3fvEXT(%s)\n", v_buf);
   CALL_SecondaryColor3fvEXT(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const GLvoid * const *indices, GLsizei primcount)
{
   GET_CURRENT_CONTEXT(ctx);
   char count_buf[512];
   _mesa_trace_format_array(count_buf, sizeof(count_buf), count, (size_t)primcount, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultiDrawElements(%s, %s, %s, %p, %d)\n", _mesa_enum_to_string(mode), count_buf, _mesa_enum_to_string(type), (void *)indices, primcount);
   CALL_MultiDrawElements(ctx->Dispatch.RealPublished, (mode, count, type, indices, primcount));
}

static void GLAPIENTRY
_mesa_trace_FogCoordfEXT(GLfloat coord)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogCoordfEXT(%f)\n", coord);
   CALL_FogCoordfEXT(ctx->Dispatch.RealPublished, (coord));
}

static void GLAPIENTRY
_mesa_trace_FogCoordfvEXT(const GLfloat *coord)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogCoordfvEXT(%p)\n", (void *)coord);
   CALL_FogCoordfvEXT(ctx->Dispatch.RealPublished, (coord));
}

static void GLAPIENTRY
_mesa_trace_WindowPos4dMESA(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos4dMESA(%f, %f, %f, %f)\n", x, y, z, w);
   CALL_WindowPos4dMESA(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_WindowPos4dvMESA(const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glWindowPos4dvMESA(%s)\n", v_buf);
   CALL_WindowPos4dvMESA(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos4fMESA(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos4fMESA(%f, %f, %f, %f)\n", x, y, z, w);
   CALL_WindowPos4fMESA(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_WindowPos4fvMESA(const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glWindowPos4fvMESA(%s)\n", v_buf);
   CALL_WindowPos4fvMESA(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos4iMESA(GLint x, GLint y, GLint z, GLint w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos4iMESA(%d, %d, %d, %d)\n", x, y, z, w);
   CALL_WindowPos4iMESA(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_WindowPos4ivMESA(const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glWindowPos4ivMESA(%s)\n", v_buf);
   CALL_WindowPos4ivMESA(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_WindowPos4sMESA(GLshort x, GLshort y, GLshort z, GLshort w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glWindowPos4sMESA(%d, %d, %d, %d)\n", x, y, z, w);
   CALL_WindowPos4sMESA(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_WindowPos4svMESA(const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glWindowPos4svMESA(%s)\n", v_buf);
   CALL_WindowPos4svMESA(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_MultiModeDrawArraysIBM(const GLenum *mode, const GLint *first, const GLsizei *count, GLsizei primcount, GLint modestride)
{
   GET_CURRENT_CONTEXT(ctx);
   char first_buf[512];
   _mesa_trace_format_array(first_buf, sizeof(first_buf), first, (size_t)primcount, MESA_TRACE_ELEM_INT);
   char count_buf[512];
   _mesa_trace_format_array(count_buf, sizeof(count_buf), count, (size_t)primcount, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultiModeDrawArraysIBM(%p, %s, %s, %d, %d)\n", (void *)mode, first_buf, count_buf, primcount, modestride);
   CALL_MultiModeDrawArraysIBM(ctx->Dispatch.RealPublished, (mode, first, count, primcount, modestride));
}

static void GLAPIENTRY
_mesa_trace_MultiModeDrawElementsIBM(const GLenum *mode, const GLsizei *count, GLenum type, const GLvoid * const *indices, GLsizei primcount, GLint modestride)
{
   GET_CURRENT_CONTEXT(ctx);
   char count_buf[512];
   _mesa_trace_format_array(count_buf, sizeof(count_buf), count, (size_t)primcount, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultiModeDrawElementsIBM(%p, %s, %s, %p, %d, %d)\n", (void *)mode, count_buf, _mesa_enum_to_string(type), (void *)indices, primcount, modestride);
   CALL_MultiModeDrawElementsIBM(ctx->Dispatch.RealPublished, (mode, count, type, indices, primcount, modestride));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1dNV(GLuint index, GLdouble x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1dNV(%u, %f)\n", index, x);
   CALL_VertexAttrib1dNV(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1dvNV(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1dvNV(%u, %p)\n", index, (void *)v);
   CALL_VertexAttrib1dvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1fNV(GLuint index, GLfloat x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1fNV(%u, %f)\n", index, x);
   CALL_VertexAttrib1fNV(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1fvNV(GLuint index, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1fvNV(%u, %p)\n", index, (void *)v);
   CALL_VertexAttrib1fvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1sNV(GLuint index, GLshort x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1sNV(%u, %d)\n", index, x);
   CALL_VertexAttrib1sNV(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1svNV(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1svNV(%u, %p)\n", index, (void *)v);
   CALL_VertexAttrib1svNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2dNV(GLuint index, GLdouble x, GLdouble y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib2dNV(%u, %f, %f)\n", index, x, y);
   CALL_VertexAttrib2dNV(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2dvNV(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttrib2dvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib2dvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2fNV(GLuint index, GLfloat x, GLfloat y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib2fNV(%u, %f, %f)\n", index, x, y);
   CALL_VertexAttrib2fNV(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2fvNV(GLuint index, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttrib2fvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib2fvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2sNV(GLuint index, GLshort x, GLshort y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib2sNV(%u, %d, %d)\n", index, x, y);
   CALL_VertexAttrib2sNV(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2svNV(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttrib2svNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib2svNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3dNV(GLuint index, GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib3dNV(%u, %f, %f, %f)\n", index, x, y, z);
   CALL_VertexAttrib3dNV(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3dvNV(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttrib3dvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib3dvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3fNV(GLuint index, GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib3fNV(%u, %f, %f, %f)\n", index, x, y, z);
   CALL_VertexAttrib3fNV(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3fvNV(GLuint index, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttrib3fvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib3fvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3sNV(GLuint index, GLshort x, GLshort y, GLshort z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib3sNV(%u, %d, %d, %d)\n", index, x, y, z);
   CALL_VertexAttrib3sNV(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3svNV(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttrib3svNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib3svNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4dNV(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib4dNV(%u, %f, %f, %f, %f)\n", index, x, y, z, w);
   CALL_VertexAttrib4dNV(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4dvNV(GLuint index, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttrib4dvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4dvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4fNV(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib4fNV(%u, %f, %f, %f, %f)\n", index, x, y, z, w);
   CALL_VertexAttrib4fNV(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4fvNV(GLuint index, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttrib4fvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4fvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4sNV(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib4sNV(%u, %d, %d, %d, %d)\n", index, x, y, z, w);
   CALL_VertexAttrib4sNV(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4svNV(GLuint index, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttrib4svNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4svNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4ubNV(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib4ubNV(%u, %u, %u, %u, %u)\n", index, x, y, z, w);
   CALL_VertexAttrib4ubNV(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4ubvNV(GLuint index, const GLubyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_UBYTE);
   _mesa_debug(ctx, "glVertexAttrib4ubvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4ubvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs1dvNV(GLuint index, GLsizei n, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttribs1dvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs1dvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs1fvNV(GLuint index, GLsizei n, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttribs1fvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs1fvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs1svNV(GLuint index, GLsizei n, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttribs1svNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs1svNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs2dvNV(GLuint index, GLsizei n, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 2, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttribs2dvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs2dvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs2fvNV(GLuint index, GLsizei n, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 2, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttribs2fvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs2fvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs2svNV(GLuint index, GLsizei n, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 2, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttribs2svNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs2svNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs3dvNV(GLuint index, GLsizei n, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 3, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttribs3dvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs3dvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs3fvNV(GLuint index, GLsizei n, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 3, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttribs3fvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs3fvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs3svNV(GLuint index, GLsizei n, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 3, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttribs3svNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs3svNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs4dvNV(GLuint index, GLsizei n, const GLdouble *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glVertexAttribs4dvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs4dvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs4fvNV(GLuint index, GLsizei n, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glVertexAttribs4fvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs4fvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs4svNV(GLuint index, GLsizei n, const GLshort *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 4, MESA_TRACE_ELEM_SHORT);
   _mesa_debug(ctx, "glVertexAttribs4svNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs4svNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs4ubvNV(GLuint index, GLsizei n, const GLubyte *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 4, MESA_TRACE_ELEM_UBYTE);
   _mesa_debug(ctx, "glVertexAttribs4ubvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs4ubvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_AlphaFragmentOp1ATI(GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glAlphaFragmentOp1ATI(%s, %u, %u, %u, %u, %u)\n", _mesa_enum_to_string(op), dst, dstMod, arg1, arg1Rep, arg1Mod);
   CALL_AlphaFragmentOp1ATI(ctx->Dispatch.RealPublished, (op, dst, dstMod, arg1, arg1Rep, arg1Mod));
}

static void GLAPIENTRY
_mesa_trace_AlphaFragmentOp2ATI(GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glAlphaFragmentOp2ATI(%s, %u, %u, %u, %u, %u, %u, %u, %u)\n", _mesa_enum_to_string(op), dst, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod);
   CALL_AlphaFragmentOp2ATI(ctx->Dispatch.RealPublished, (op, dst, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod));
}

static void GLAPIENTRY
_mesa_trace_AlphaFragmentOp3ATI(GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glAlphaFragmentOp3ATI(%s, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u)\n", _mesa_enum_to_string(op), dst, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod, arg3, arg3Rep, arg3Mod);
   CALL_AlphaFragmentOp3ATI(ctx->Dispatch.RealPublished, (op, dst, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod, arg3, arg3Rep, arg3Mod));
}

static void GLAPIENTRY
_mesa_trace_BeginFragmentShaderATI(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBeginFragmentShaderATI()\n");
   CALL_BeginFragmentShaderATI(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_BindFragmentShaderATI(GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindFragmentShaderATI(%u)\n", id);
   CALL_BindFragmentShaderATI(ctx->Dispatch.RealPublished, (id));
}

static void GLAPIENTRY
_mesa_trace_ColorFragmentOp1ATI(GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorFragmentOp1ATI(%s, %u, %u, %u, %u, %u, %u)\n", _mesa_enum_to_string(op), dst, dstMask, dstMod, arg1, arg1Rep, arg1Mod);
   CALL_ColorFragmentOp1ATI(ctx->Dispatch.RealPublished, (op, dst, dstMask, dstMod, arg1, arg1Rep, arg1Mod));
}

static void GLAPIENTRY
_mesa_trace_ColorFragmentOp2ATI(GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorFragmentOp2ATI(%s, %u, %u, %u, %u, %u, %u, %u, %u, %u)\n", _mesa_enum_to_string(op), dst, dstMask, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod);
   CALL_ColorFragmentOp2ATI(ctx->Dispatch.RealPublished, (op, dst, dstMask, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod));
}

static void GLAPIENTRY
_mesa_trace_ColorFragmentOp3ATI(GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorFragmentOp3ATI(%s, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u)\n", _mesa_enum_to_string(op), dst, dstMask, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod, arg3, arg3Rep, arg3Mod);
   CALL_ColorFragmentOp3ATI(ctx->Dispatch.RealPublished, (op, dst, dstMask, dstMod, arg1, arg1Rep, arg1Mod, arg2, arg2Rep, arg2Mod, arg3, arg3Rep, arg3Mod));
}

static void GLAPIENTRY
_mesa_trace_DeleteFragmentShaderATI(GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDeleteFragmentShaderATI(%u)\n", id);
   CALL_DeleteFragmentShaderATI(ctx->Dispatch.RealPublished, (id));
}

static void GLAPIENTRY
_mesa_trace_EndFragmentShaderATI(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEndFragmentShaderATI()\n");
   CALL_EndFragmentShaderATI(ctx->Dispatch.RealPublished, ());
}

static GLuint GLAPIENTRY
_mesa_trace_GenFragmentShadersATI(GLuint range)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenFragmentShadersATI(%u)\n", range);
   return CALL_GenFragmentShadersATI(ctx->Dispatch.RealPublished, (range));
}

static void GLAPIENTRY
_mesa_trace_PassTexCoordATI(GLuint dst, GLuint coord, GLenum swizzle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPassTexCoordATI(%u, %u, %s)\n", dst, coord, _mesa_enum_to_string(swizzle));
   CALL_PassTexCoordATI(ctx->Dispatch.RealPublished, (dst, coord, swizzle));
}

static void GLAPIENTRY
_mesa_trace_SampleMapATI(GLuint dst, GLuint interp, GLenum swizzle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSampleMapATI(%u, %u, %s)\n", dst, interp, _mesa_enum_to_string(swizzle));
   CALL_SampleMapATI(ctx->Dispatch.RealPublished, (dst, interp, swizzle));
}

static void GLAPIENTRY
_mesa_trace_SetFragmentShaderConstantATI(GLuint dst, const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   char value_buf[512];
   _mesa_trace_format_array(value_buf, sizeof(value_buf), value, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glSetFragmentShaderConstantATI(%u, %s)\n", dst, value_buf);
   CALL_SetFragmentShaderConstantATI(ctx->Dispatch.RealPublished, (dst, value));
}

static void GLAPIENTRY
_mesa_trace_DepthRangeArrayfvOES(GLuint first, GLsizei count, const GLfloat *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)(2 * count), MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glDepthRangeArrayfvOES(%u, %d, %s)\n", first, count, v_buf);
   CALL_DepthRangeArrayfvOES(ctx->Dispatch.RealPublished, (first, count, v));
}

static void GLAPIENTRY
_mesa_trace_DepthRangeIndexedfOES(GLuint index, GLfloat n, GLfloat f)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDepthRangeIndexedfOES(%u, %f, %f)\n", index, n, f);
   CALL_DepthRangeIndexedfOES(ctx->Dispatch.RealPublished, (index, n, f));
}

static void GLAPIENTRY
_mesa_trace_ActiveStencilFaceEXT(GLenum face)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glActiveStencilFaceEXT(%s)\n", _mesa_enum_to_string(face));
   CALL_ActiveStencilFaceEXT(ctx->Dispatch.RealPublished, (face));
}

static void GLAPIENTRY
_mesa_trace_PrimitiveRestartNV(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPrimitiveRestartNV()\n");
   CALL_PrimitiveRestartNV(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_GetTexGenxvOES(GLenum coord, GLenum pname, GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexGenxvOES(%s, %s, %p)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexGenxvOES(ctx->Dispatch.RealPublished, (coord, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexGenxOES(GLenum coord, GLenum pname, GLfixed param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexGenxOES(%s, %s, %d)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), param);
   CALL_TexGenxOES(ctx->Dispatch.RealPublished, (coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TexGenxvOES(GLenum coord, GLenum pname, const GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexGenxvOES(%s, %s, %p)\n", _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexGenxvOES(ctx->Dispatch.RealPublished, (coord, pname, params));
}

static void GLAPIENTRY
_mesa_trace_DepthBoundsEXT(GLclampd zmin, GLclampd zmax)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDepthBoundsEXT(%f, %f)\n", zmin, zmax);
   CALL_DepthBoundsEXT(ctx->Dispatch.RealPublished, (zmin, zmax));
}

static void GLAPIENTRY
_mesa_trace_BindFramebufferEXT(GLenum target, GLuint framebuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindFramebufferEXT(%s, %u)\n", _mesa_enum_to_string(target), framebuffer);
   CALL_BindFramebufferEXT(ctx->Dispatch.RealPublished, (target, framebuffer));
}

static void GLAPIENTRY
_mesa_trace_BindRenderbufferEXT(GLenum target, GLuint renderbuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindRenderbufferEXT(%s, %u)\n", _mesa_enum_to_string(target), renderbuffer);
   CALL_BindRenderbufferEXT(ctx->Dispatch.RealPublished, (target, renderbuffer));
}

static void GLAPIENTRY
_mesa_trace_StringMarkerGREMEDY(GLsizei len, const GLvoid *string)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glStringMarkerGREMEDY(%d, %p)\n", len, (void *)string);
   CALL_StringMarkerGREMEDY(ctx->Dispatch.RealPublished, (len, string));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI1iEXT(GLuint index, GLint x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI1iEXT(%u, %d)\n", index, x);
   CALL_VertexAttribI1iEXT(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI1uiEXT(GLuint index, GLuint x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI1uiEXT(%u, %u)\n", index, x);
   CALL_VertexAttribI1uiEXT(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI2iEXT(GLuint index, GLint x, GLint y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI2iEXT(%u, %d, %d)\n", index, x, y);
   CALL_VertexAttribI2iEXT(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI2ivEXT(GLuint index, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glVertexAttribI2ivEXT(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI2ivEXT(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI2uiEXT(GLuint index, GLuint x, GLuint y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI2uiEXT(%u, %u, %u)\n", index, x, y);
   CALL_VertexAttribI2uiEXT(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI2uivEXT(GLuint index, const GLuint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glVertexAttribI2uivEXT(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI2uivEXT(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI3iEXT(GLuint index, GLint x, GLint y, GLint z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI3iEXT(%u, %d, %d, %d)\n", index, x, y, z);
   CALL_VertexAttribI3iEXT(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI3ivEXT(GLuint index, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glVertexAttribI3ivEXT(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI3ivEXT(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI3uiEXT(GLuint index, GLuint x, GLuint y, GLuint z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI3uiEXT(%u, %u, %u, %u)\n", index, x, y, z);
   CALL_VertexAttribI3uiEXT(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI3uivEXT(GLuint index, const GLuint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glVertexAttribI3uivEXT(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI3uivEXT(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI4iEXT(GLuint index, GLint x, GLint y, GLint z, GLint w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI4iEXT(%u, %d, %d, %d, %d)\n", index, x, y, z, w);
   CALL_VertexAttribI4iEXT(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI4ivEXT(GLuint index, const GLint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glVertexAttribI4ivEXT(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI4ivEXT(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI4uiEXT(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttribI4uiEXT(%u, %u, %u, %u, %u)\n", index, x, y, z, w);
   CALL_VertexAttribI4uiEXT(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribI4uivEXT(GLuint index, const GLuint *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glVertexAttribI4uivEXT(%u, %s)\n", index, v_buf);
   CALL_VertexAttribI4uivEXT(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_ClearColorIiEXT(GLint r, GLint g, GLint b, GLint a)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearColorIiEXT(%d, %d, %d, %d)\n", r, g, b, a);
   CALL_ClearColorIiEXT(ctx->Dispatch.RealPublished, (r, g, b, a));
}

static void GLAPIENTRY
_mesa_trace_ClearColorIuiEXT(GLuint r, GLuint g, GLuint b, GLuint a)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearColorIuiEXT(%u, %u, %u, %u)\n", r, g, b, a);
   CALL_ClearColorIuiEXT(ctx->Dispatch.RealPublished, (r, g, b, a));
}

static void GLAPIENTRY
_mesa_trace_BindBufferOffsetEXT(GLenum target, GLuint index, GLuint buffer, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindBufferOffsetEXT(%s, %u, %u, %" PRIdPTR ")\n", _mesa_enum_to_string(target), index, buffer, (intptr_t)offset);
   CALL_BindBufferOffsetEXT(ctx->Dispatch.RealPublished, (target, index, buffer, offset));
}

static void GLAPIENTRY
_mesa_trace_BeginPerfMonitorAMD(GLuint monitor)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBeginPerfMonitorAMD(%u)\n", monitor);
   CALL_BeginPerfMonitorAMD(ctx->Dispatch.RealPublished, (monitor));
}

static void GLAPIENTRY
_mesa_trace_DeletePerfMonitorsAMD(GLsizei n, GLuint *monitors)
{
   GET_CURRENT_CONTEXT(ctx);
   char monitors_buf[512];
   _mesa_trace_format_array(monitors_buf, sizeof(monitors_buf), monitors, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeletePerfMonitorsAMD(%d, %s)\n", n, monitors_buf);
   CALL_DeletePerfMonitorsAMD(ctx->Dispatch.RealPublished, (n, monitors));
}

static void GLAPIENTRY
_mesa_trace_EndPerfMonitorAMD(GLuint monitor)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEndPerfMonitorAMD(%u)\n", monitor);
   CALL_EndPerfMonitorAMD(ctx->Dispatch.RealPublished, (monitor));
}

static void GLAPIENTRY
_mesa_trace_GenPerfMonitorsAMD(GLsizei n, GLuint *monitors)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenPerfMonitorsAMD(%d, %p)\n", n, (void *)monitors);
   CALL_GenPerfMonitorsAMD(ctx->Dispatch.RealPublished, (n, monitors));
}

static void GLAPIENTRY
_mesa_trace_GetPerfMonitorCounterDataAMD(GLuint monitor, GLenum pname, GLsizei dataSize, GLuint *data, GLint *bytesWritten)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfMonitorCounterDataAMD(%u, %s, %d, %p, %p)\n", monitor, _mesa_enum_to_string(pname), dataSize, (void *)data, (void *)bytesWritten);
   CALL_GetPerfMonitorCounterDataAMD(ctx->Dispatch.RealPublished, (monitor, pname, dataSize, data, bytesWritten));
}

static void GLAPIENTRY
_mesa_trace_GetPerfMonitorCounterInfoAMD(GLuint group, GLuint counter, GLenum pname, GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfMonitorCounterInfoAMD(%u, %u, %s, %p)\n", group, counter, _mesa_enum_to_string(pname), (void *)data);
   CALL_GetPerfMonitorCounterInfoAMD(ctx->Dispatch.RealPublished, (group, counter, pname, data));
}

static void GLAPIENTRY
_mesa_trace_GetPerfMonitorCounterStringAMD(GLuint group, GLuint counter, GLsizei bufSize, GLsizei *length, GLchar *counterString)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfMonitorCounterStringAMD(%u, %u, %d, %p, %p)\n", group, counter, bufSize, (void *)length, (void *)counterString);
   CALL_GetPerfMonitorCounterStringAMD(ctx->Dispatch.RealPublished, (group, counter, bufSize, length, counterString));
}

static void GLAPIENTRY
_mesa_trace_GetPerfMonitorCountersAMD(GLuint group, GLint *numCounters, GLint *maxActiveCounters, GLsizei countersSize, GLuint *counters)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfMonitorCountersAMD(%u, %p, %p, %d, %p)\n", group, (void *)numCounters, (void *)maxActiveCounters, countersSize, (void *)counters);
   CALL_GetPerfMonitorCountersAMD(ctx->Dispatch.RealPublished, (group, numCounters, maxActiveCounters, countersSize, counters));
}

static void GLAPIENTRY
_mesa_trace_GetPerfMonitorGroupStringAMD(GLuint group, GLsizei bufSize, GLsizei *length, GLchar *groupString)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfMonitorGroupStringAMD(%u, %d, %p, %p)\n", group, bufSize, (void *)length, (void *)groupString);
   CALL_GetPerfMonitorGroupStringAMD(ctx->Dispatch.RealPublished, (group, bufSize, length, groupString));
}

static void GLAPIENTRY
_mesa_trace_GetPerfMonitorGroupsAMD(GLint *numGroups, GLsizei groupsSize, GLuint *groups)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfMonitorGroupsAMD(%p, %d, %p)\n", (void *)numGroups, groupsSize, (void *)groups);
   CALL_GetPerfMonitorGroupsAMD(ctx->Dispatch.RealPublished, (numGroups, groupsSize, groups));
}

static void GLAPIENTRY
_mesa_trace_SelectPerfMonitorCountersAMD(GLuint monitor, GLboolean enable, GLuint group, GLint numCounters, GLuint *counterList)
{
   GET_CURRENT_CONTEXT(ctx);
   char counterList_buf[512];
   _mesa_trace_format_array(counterList_buf, sizeof(counterList_buf), counterList, (size_t)numCounters, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glSelectPerfMonitorCountersAMD(%u, %s, %u, %d, %s)\n", monitor, enable ? "GL_TRUE" : "GL_FALSE", group, numCounters, counterList_buf);
   CALL_SelectPerfMonitorCountersAMD(ctx->Dispatch.RealPublished, (monitor, enable, group, numCounters, counterList));
}

static void GLAPIENTRY
_mesa_trace_TextureBarrierNV(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureBarrierNV()\n");
   CALL_TextureBarrierNV(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_BeginPerfQueryINTEL(GLuint queryHandle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBeginPerfQueryINTEL(%u)\n", queryHandle);
   CALL_BeginPerfQueryINTEL(ctx->Dispatch.RealPublished, (queryHandle));
}

static void GLAPIENTRY
_mesa_trace_CreatePerfQueryINTEL(GLuint queryId, GLuint *queryHandle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreatePerfQueryINTEL(%u, %p)\n", queryId, (void *)queryHandle);
   CALL_CreatePerfQueryINTEL(ctx->Dispatch.RealPublished, (queryId, queryHandle));
}

static void GLAPIENTRY
_mesa_trace_DeletePerfQueryINTEL(GLuint queryHandle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDeletePerfQueryINTEL(%u)\n", queryHandle);
   CALL_DeletePerfQueryINTEL(ctx->Dispatch.RealPublished, (queryHandle));
}

static void GLAPIENTRY
_mesa_trace_EndPerfQueryINTEL(GLuint queryHandle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEndPerfQueryINTEL(%u)\n", queryHandle);
   CALL_EndPerfQueryINTEL(ctx->Dispatch.RealPublished, (queryHandle));
}

static void GLAPIENTRY
_mesa_trace_GetFirstPerfQueryIdINTEL(GLuint *queryId)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFirstPerfQueryIdINTEL(%p)\n", (void *)queryId);
   CALL_GetFirstPerfQueryIdINTEL(ctx->Dispatch.RealPublished, (queryId));
}

static void GLAPIENTRY
_mesa_trace_GetNextPerfQueryIdINTEL(GLuint queryId, GLuint *nextQueryId)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNextPerfQueryIdINTEL(%u, %p)\n", queryId, (void *)nextQueryId);
   CALL_GetNextPerfQueryIdINTEL(ctx->Dispatch.RealPublished, (queryId, nextQueryId));
}

static void GLAPIENTRY
_mesa_trace_GetPerfCounterInfoINTEL(GLuint queryId, GLuint counterId, GLuint counterNameLength, GLchar *counterName, GLuint counterDescLength, GLchar *counterDesc, GLuint *counterOffset, GLuint *counterDataSize, GLuint *counterTypeEnum, GLuint *counterDataTypeEnum, GLuint64 *rawCounterMaxValue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfCounterInfoINTEL(%u, %u, %u, %p, %u, %p, %p, %p, %p, %p, %p)\n", queryId, counterId, counterNameLength, (void *)counterName, counterDescLength, (void *)counterDesc, (void *)counterOffset, (void *)counterDataSize, (void *)counterTypeEnum, (void *)counterDataTypeEnum, (void *)rawCounterMaxValue);
   CALL_GetPerfCounterInfoINTEL(ctx->Dispatch.RealPublished, (queryId, counterId, counterNameLength, counterName, counterDescLength, counterDesc, counterOffset, counterDataSize, counterTypeEnum, counterDataTypeEnum, rawCounterMaxValue));
}

static void GLAPIENTRY
_mesa_trace_GetPerfQueryDataINTEL(GLuint queryHandle, GLuint flags, GLsizei dataSize, GLvoid *data, GLuint *bytesWritten)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfQueryDataINTEL(%u, %u, %d, %p, %p)\n", queryHandle, flags, dataSize, (void *)data, (void *)bytesWritten);
   CALL_GetPerfQueryDataINTEL(ctx->Dispatch.RealPublished, (queryHandle, flags, dataSize, data, bytesWritten));
}

static void GLAPIENTRY
_mesa_trace_GetPerfQueryIdByNameINTEL(GLchar *queryName, GLuint *queryId)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfQueryIdByNameINTEL(%p, %p)\n", (void *)queryName, (void *)queryId);
   CALL_GetPerfQueryIdByNameINTEL(ctx->Dispatch.RealPublished, (queryName, queryId));
}

static void GLAPIENTRY
_mesa_trace_GetPerfQueryInfoINTEL(GLuint queryId, GLuint queryNameLength, GLchar *queryName, GLuint *dataSize, GLuint *noCounters, GLuint *noInstances, GLuint *capsMask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPerfQueryInfoINTEL(%u, %u, %p, %p, %p, %p, %p)\n", queryId, queryNameLength, (void *)queryName, (void *)dataSize, (void *)noCounters, (void *)noInstances, (void *)capsMask);
   CALL_GetPerfQueryInfoINTEL(ctx->Dispatch.RealPublished, (queryId, queryNameLength, queryName, dataSize, noCounters, noInstances, capsMask));
}

static void GLAPIENTRY
_mesa_trace_PolygonOffsetClampEXT(GLfloat factor, GLfloat units, GLfloat clamp)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPolygonOffsetClampEXT(%f, %f, %f)\n", factor, units, clamp);
   CALL_PolygonOffsetClampEXT(ctx->Dispatch.RealPublished, (factor, units, clamp));
}

static void GLAPIENTRY
_mesa_trace_SubpixelPrecisionBiasNV(GLuint xbits, GLuint ybits)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSubpixelPrecisionBiasNV(%u, %u)\n", xbits, ybits);
   CALL_SubpixelPrecisionBiasNV(ctx->Dispatch.RealPublished, (xbits, ybits));
}

static void GLAPIENTRY
_mesa_trace_ConservativeRasterParameterfNV(GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glConservativeRasterParameterfNV(%s, %f)\n", _mesa_enum_to_string(pname), param);
   CALL_ConservativeRasterParameterfNV(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_ConservativeRasterParameteriNV(GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glConservativeRasterParameteriNV(%s, %d)\n", _mesa_enum_to_string(pname), param);
   CALL_ConservativeRasterParameteriNV(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_WindowRectanglesEXT(GLenum mode, GLsizei count, const GLint *box)
{
   GET_CURRENT_CONTEXT(ctx);
   char box_buf[512];
   _mesa_trace_format_array(box_buf, sizeof(box_buf), box, (size_t)(4 * count), MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glWindowRectanglesEXT(%s, %d, %s)\n", _mesa_enum_to_string(mode), count, box_buf);
   CALL_WindowRectanglesEXT(ctx->Dispatch.RealPublished, (mode, count, box));
}

static void GLAPIENTRY
_mesa_trace_BufferStorageMemEXT(GLenum target, GLsizeiptr size, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBufferStorageMemEXT(%s, %" PRIdPTR ", %u, %" PRIu64 ")\n", _mesa_enum_to_string(target), (intptr_t)size, memory, (uint64_t)offset);
   CALL_BufferStorageMemEXT(ctx->Dispatch.RealPublished, (target, size, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_CreateMemoryObjectsEXT(GLsizei n, GLuint *memoryObjects)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateMemoryObjectsEXT(%d, %p)\n", n, (void *)memoryObjects);
   CALL_CreateMemoryObjectsEXT(ctx->Dispatch.RealPublished, (n, memoryObjects));
}

static void GLAPIENTRY
_mesa_trace_DeleteMemoryObjectsEXT(GLsizei n, const GLuint *memoryObjects)
{
   GET_CURRENT_CONTEXT(ctx);
   char memoryObjects_buf[512];
   _mesa_trace_format_array(memoryObjects_buf, sizeof(memoryObjects_buf), memoryObjects, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteMemoryObjectsEXT(%d, %s)\n", n, memoryObjects_buf);
   CALL_DeleteMemoryObjectsEXT(ctx->Dispatch.RealPublished, (n, memoryObjects));
}

static void GLAPIENTRY
_mesa_trace_DeleteSemaphoresEXT(GLsizei n, const GLuint *semaphores)
{
   GET_CURRENT_CONTEXT(ctx);
   char semaphores_buf[512];
   _mesa_trace_format_array(semaphores_buf, sizeof(semaphores_buf), semaphores, (size_t)n, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glDeleteSemaphoresEXT(%d, %s)\n", n, semaphores_buf);
   CALL_DeleteSemaphoresEXT(ctx->Dispatch.RealPublished, (n, semaphores));
}

static void GLAPIENTRY
_mesa_trace_GenSemaphoresEXT(GLsizei n, GLuint *semaphores)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenSemaphoresEXT(%d, %p)\n", n, (void *)semaphores);
   CALL_GenSemaphoresEXT(ctx->Dispatch.RealPublished, (n, semaphores));
}

static void GLAPIENTRY
_mesa_trace_GetMemoryObjectParameterivEXT(GLuint memoryObject, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMemoryObjectParameterivEXT(%u, %s, %p)\n", memoryObject, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMemoryObjectParameterivEXT(ctx->Dispatch.RealPublished, (memoryObject, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetSemaphoreParameterui64vEXT(GLuint semaphore, GLenum pname, GLuint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSemaphoreParameterui64vEXT(%u, %s, %p)\n", semaphore, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetSemaphoreParameterui64vEXT(ctx->Dispatch.RealPublished, (semaphore, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetUnsignedBytei_vEXT(GLenum target, GLuint index, GLubyte *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUnsignedBytei_vEXT(%s, %u, %p)\n", _mesa_enum_to_string(target), index, (void *)data);
   CALL_GetUnsignedBytei_vEXT(ctx->Dispatch.RealPublished, (target, index, data));
}

static void GLAPIENTRY
_mesa_trace_GetUnsignedBytevEXT(GLenum pname, GLubyte *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetUnsignedBytevEXT(%s, %p)\n", _mesa_enum_to_string(pname), (void *)data);
   CALL_GetUnsignedBytevEXT(ctx->Dispatch.RealPublished, (pname, data));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsMemoryObjectEXT(GLuint memoryObject)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsMemoryObjectEXT(%u)\n", memoryObject);
   return CALL_IsMemoryObjectEXT(ctx->Dispatch.RealPublished, (memoryObject));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsSemaphoreEXT(GLuint semaphore)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsSemaphoreEXT(%u)\n", semaphore);
   return CALL_IsSemaphoreEXT(ctx->Dispatch.RealPublished, (semaphore));
}

static void GLAPIENTRY
_mesa_trace_MemoryObjectParameterivEXT(GLuint memoryObject, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMemoryObjectParameterivEXT(%u, %s, %p)\n", memoryObject, _mesa_enum_to_string(pname), (void *)params);
   CALL_MemoryObjectParameterivEXT(ctx->Dispatch.RealPublished, (memoryObject, pname, params));
}

static void GLAPIENTRY
_mesa_trace_NamedBufferStorageMemEXT(GLuint buffer, GLsizeiptr size, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedBufferStorageMemEXT(%u, %" PRIdPTR ", %u, %" PRIu64 ")\n", buffer, (intptr_t)size, memory, (uint64_t)offset);
   CALL_NamedBufferStorageMemEXT(ctx->Dispatch.RealPublished, (buffer, size, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_SemaphoreParameterui64vEXT(GLuint semaphore, GLenum pname, const GLuint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSemaphoreParameterui64vEXT(%u, %s, %p)\n", semaphore, _mesa_enum_to_string(pname), (void *)params);
   CALL_SemaphoreParameterui64vEXT(ctx->Dispatch.RealPublished, (semaphore, pname, params));
}

static void GLAPIENTRY
_mesa_trace_SignalSemaphoreEXT(GLuint semaphore, GLuint numBufferBarriers, const GLuint *buffers, GLuint numTextureBarriers, const GLuint *textures, const GLenum *dstLayouts)
{
   GET_CURRENT_CONTEXT(ctx);
   char buffers_buf[512];
   _mesa_trace_format_array(buffers_buf, sizeof(buffers_buf), buffers, (size_t)numBufferBarriers, MESA_TRACE_ELEM_UINT);
   char textures_buf[512];
   _mesa_trace_format_array(textures_buf, sizeof(textures_buf), textures, (size_t)numTextureBarriers, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glSignalSemaphoreEXT(%u, %u, %s, %u, %s, %p)\n", semaphore, numBufferBarriers, buffers_buf, numTextureBarriers, textures_buf, (void *)dstLayouts);
   CALL_SignalSemaphoreEXT(ctx->Dispatch.RealPublished, (semaphore, numBufferBarriers, buffers, numTextureBarriers, textures, dstLayouts));
}

static void GLAPIENTRY
_mesa_trace_TexStorageMem1DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorageMem1DEXT(%s, %d, %s, %d, %u, %" PRIu64 ")\n", _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width, memory, (uint64_t)offset);
   CALL_TexStorageMem1DEXT(ctx->Dispatch.RealPublished, (target, levels, internalFormat, width, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_TexStorageMem2DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorageMem2DEXT(%s, %d, %s, %d, %d, %u, %" PRIu64 ")\n", _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width, height, memory, (uint64_t)offset);
   CALL_TexStorageMem2DEXT(ctx->Dispatch.RealPublished, (target, levels, internalFormat, width, height, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_TexStorageMem2DMultisampleEXT(GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorageMem2DMultisampleEXT(%s, %d, %s, %d, %d, %s, %u, %" PRIu64 ")\n", _mesa_enum_to_string(target), samples, _mesa_enum_to_string(internalFormat), width, height, fixedSampleLocations ? "GL_TRUE" : "GL_FALSE", memory, (uint64_t)offset);
   CALL_TexStorageMem2DMultisampleEXT(ctx->Dispatch.RealPublished, (target, samples, internalFormat, width, height, fixedSampleLocations, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_TexStorageMem3DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorageMem3DEXT(%s, %d, %s, %d, %d, %d, %u, %" PRIu64 ")\n", _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width, height, depth, memory, (uint64_t)offset);
   CALL_TexStorageMem3DEXT(ctx->Dispatch.RealPublished, (target, levels, internalFormat, width, height, depth, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_TexStorageMem3DMultisampleEXT(GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorageMem3DMultisampleEXT(%s, %d, %s, %d, %d, %d, %s, %u, %" PRIu64 ")\n", _mesa_enum_to_string(target), samples, _mesa_enum_to_string(internalFormat), width, height, depth, fixedSampleLocations ? "GL_TRUE" : "GL_FALSE", memory, (uint64_t)offset);
   CALL_TexStorageMem3DMultisampleEXT(ctx->Dispatch.RealPublished, (target, samples, internalFormat, width, height, depth, fixedSampleLocations, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_TextureStorageMem1DEXT(GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorageMem1DEXT(%u, %d, %s, %d, %u, %" PRIu64 ")\n", texture, levels, _mesa_enum_to_string(internalFormat), width, memory, (uint64_t)offset);
   CALL_TextureStorageMem1DEXT(ctx->Dispatch.RealPublished, (texture, levels, internalFormat, width, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_TextureStorageMem2DEXT(GLenum texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorageMem2DEXT(%s, %d, %s, %d, %d, %u, %" PRIu64 ")\n", _mesa_enum_to_string(texture), levels, _mesa_enum_to_string(internalFormat), width, height, memory, (uint64_t)offset);
   CALL_TextureStorageMem2DEXT(ctx->Dispatch.RealPublished, (texture, levels, internalFormat, width, height, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_TextureStorageMem2DMultisampleEXT(GLuint texture, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorageMem2DMultisampleEXT(%u, %d, %s, %d, %d, %s, %u, %" PRIu64 ")\n", texture, samples, _mesa_enum_to_string(internalFormat), width, height, fixedSampleLocations ? "GL_TRUE" : "GL_FALSE", memory, (uint64_t)offset);
   CALL_TextureStorageMem2DMultisampleEXT(ctx->Dispatch.RealPublished, (texture, samples, internalFormat, width, height, fixedSampleLocations, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_TextureStorageMem3DEXT(GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorageMem3DEXT(%u, %d, %s, %d, %d, %d, %u, %" PRIu64 ")\n", texture, levels, _mesa_enum_to_string(internalFormat), width, height, depth, memory, (uint64_t)offset);
   CALL_TextureStorageMem3DEXT(ctx->Dispatch.RealPublished, (texture, levels, internalFormat, width, height, depth, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_TextureStorageMem3DMultisampleEXT(GLuint texture, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorageMem3DMultisampleEXT(%u, %d, %s, %d, %d, %d, %s, %u, %" PRIu64 ")\n", texture, samples, _mesa_enum_to_string(internalFormat), width, height, depth, fixedSampleLocations ? "GL_TRUE" : "GL_FALSE", memory, (uint64_t)offset);
   CALL_TextureStorageMem3DMultisampleEXT(ctx->Dispatch.RealPublished, (texture, samples, internalFormat, width, height, depth, fixedSampleLocations, memory, offset));
}

static void GLAPIENTRY
_mesa_trace_WaitSemaphoreEXT(GLuint semaphore, GLuint numBufferBarriers, const GLuint *buffers, GLuint numTextureBarriers, const GLuint *textures, const GLenum *srcLayouts)
{
   GET_CURRENT_CONTEXT(ctx);
   char buffers_buf[512];
   _mesa_trace_format_array(buffers_buf, sizeof(buffers_buf), buffers, (size_t)numBufferBarriers, MESA_TRACE_ELEM_UINT);
   char textures_buf[512];
   _mesa_trace_format_array(textures_buf, sizeof(textures_buf), textures, (size_t)numTextureBarriers, MESA_TRACE_ELEM_UINT);
   _mesa_debug(ctx, "glWaitSemaphoreEXT(%u, %u, %s, %u, %s, %p)\n", semaphore, numBufferBarriers, buffers_buf, numTextureBarriers, textures_buf, (void *)srcLayouts);
   CALL_WaitSemaphoreEXT(ctx->Dispatch.RealPublished, (semaphore, numBufferBarriers, buffers, numTextureBarriers, textures, srcLayouts));
}

static void GLAPIENTRY
_mesa_trace_ImportMemoryFdEXT(GLuint memory, GLuint64 size, GLenum handleType, GLint fd)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glImportMemoryFdEXT(%u, %" PRIu64 ", %s, %d)\n", memory, (uint64_t)size, _mesa_enum_to_string(handleType), fd);
   CALL_ImportMemoryFdEXT(ctx->Dispatch.RealPublished, (memory, size, handleType, fd));
}

static void GLAPIENTRY
_mesa_trace_ImportSemaphoreFdEXT(GLuint semaphore, GLenum handleType, GLint fd)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glImportSemaphoreFdEXT(%u, %s, %d)\n", semaphore, _mesa_enum_to_string(handleType), fd);
   CALL_ImportSemaphoreFdEXT(ctx->Dispatch.RealPublished, (semaphore, handleType, fd));
}

static void GLAPIENTRY
_mesa_trace_FramebufferFetchBarrierEXT(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferFetchBarrierEXT()\n");
   CALL_FramebufferFetchBarrierEXT(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_NamedRenderbufferStorageMultisampleAdvancedAMD(GLuint renderbuffer, GLsizei samples, GLsizei storageSamples, GLenum internalformat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedRenderbufferStorageMultisampleAdvancedAMD(%u, %d, %d, %s, %d, %d)\n", renderbuffer, samples, storageSamples, _mesa_enum_to_string(internalformat), width, height);
   CALL_NamedRenderbufferStorageMultisampleAdvancedAMD(ctx->Dispatch.RealPublished, (renderbuffer, samples, storageSamples, internalformat, width, height));
}

static void GLAPIENTRY
_mesa_trace_RenderbufferStorageMultisampleAdvancedAMD(GLenum target, GLsizei samples, GLsizei storageSamples, GLenum internalformat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRenderbufferStorageMultisampleAdvancedAMD(%s, %d, %d, %s, %d, %d)\n", _mesa_enum_to_string(target), samples, storageSamples, _mesa_enum_to_string(internalformat), width, height);
   CALL_RenderbufferStorageMultisampleAdvancedAMD(ctx->Dispatch.RealPublished, (target, samples, storageSamples, internalformat, width, height));
}

static void GLAPIENTRY
_mesa_trace_StencilFuncSeparateATI(GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glStencilFuncSeparateATI(%s, %s, %d, %u)\n", _mesa_enum_to_string(frontfunc), _mesa_enum_to_string(backfunc), ref, mask);
   CALL_StencilFuncSeparateATI(ctx->Dispatch.RealPublished, (frontfunc, backfunc, ref, mask));
}

static void GLAPIENTRY
_mesa_trace_ProgramEnvParameters4fvEXT(GLenum target, GLuint index, GLsizei count, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   char params_buf[512];
   _mesa_trace_format_array(params_buf, sizeof(params_buf), params, (size_t)count * 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramEnvParameters4fvEXT(%s, %u, %d, %s)\n", _mesa_enum_to_string(target), index, count, params_buf);
   CALL_ProgramEnvParameters4fvEXT(ctx->Dispatch.RealPublished, (target, index, count, params));
}

static void GLAPIENTRY
_mesa_trace_ProgramLocalParameters4fvEXT(GLenum target, GLuint index, GLsizei count, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   char params_buf[512];
   _mesa_trace_format_array(params_buf, sizeof(params_buf), params, (size_t)count * 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glProgramLocalParameters4fvEXT(%s, %u, %d, %s)\n", _mesa_enum_to_string(target), index, count, params_buf);
   CALL_ProgramLocalParameters4fvEXT(ctx->Dispatch.RealPublished, (target, index, count, params));
}

static void GLAPIENTRY
_mesa_trace_EGLImageTargetRenderbufferStorageOES(GLenum target, GLvoid *writeOffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEGLImageTargetRenderbufferStorageOES(%s, %p)\n", _mesa_enum_to_string(target), (void *)writeOffset);
   CALL_EGLImageTargetRenderbufferStorageOES(ctx->Dispatch.RealPublished, (target, writeOffset));
}

static void GLAPIENTRY
_mesa_trace_EGLImageTargetTexture2DOES(GLenum target, GLvoid *writeOffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEGLImageTargetTexture2DOES(%s, %p)\n", _mesa_enum_to_string(target), (void *)writeOffset);
   CALL_EGLImageTargetTexture2DOES(ctx->Dispatch.RealPublished, (target, writeOffset));
}

static void GLAPIENTRY
_mesa_trace_AlphaFuncx(GLenum func, GLclampx ref)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glAlphaFuncx(%s, %d)\n", _mesa_enum_to_string(func), ref);
   CALL_AlphaFuncx(ctx->Dispatch.RealPublished, (func, ref));
}

static void GLAPIENTRY
_mesa_trace_ClearColorx(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearColorx(%d, %d, %d, %d)\n", red, green, blue, alpha);
   CALL_ClearColorx(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_ClearDepthx(GLclampx depth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearDepthx(%d)\n", depth);
   CALL_ClearDepthx(ctx->Dispatch.RealPublished, (depth));
}

static void GLAPIENTRY
_mesa_trace_Color4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4x(%d, %d, %d, %d)\n", red, green, blue, alpha);
   CALL_Color4x(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_DepthRangex(GLclampx zNear, GLclampx zFar)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDepthRangex(%d, %d)\n", zNear, zFar);
   CALL_DepthRangex(ctx->Dispatch.RealPublished, (zNear, zFar));
}

static void GLAPIENTRY
_mesa_trace_Fogx(GLenum pname, GLfixed param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogx(%s, %d)\n", _mesa_enum_to_string(pname), param);
   CALL_Fogx(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_Fogxv(GLenum pname, const GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogxv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_Fogxv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_Frustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFrustumf(%f, %f, %f, %f, %f, %f)\n", left, right, bottom, top, zNear, zFar);
   CALL_Frustumf(ctx->Dispatch.RealPublished, (left, right, bottom, top, zNear, zFar));
}

static void GLAPIENTRY
_mesa_trace_Frustumx(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFrustumx(%d, %d, %d, %d, %d, %d)\n", left, right, bottom, top, zNear, zFar);
   CALL_Frustumx(ctx->Dispatch.RealPublished, (left, right, bottom, top, zNear, zFar));
}

static void GLAPIENTRY
_mesa_trace_LightModelx(GLenum pname, GLfixed param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightModelx(%s, %d)\n", _mesa_enum_to_string(pname), param);
   CALL_LightModelx(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_LightModelxv(GLenum pname, const GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightModelxv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_LightModelxv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_Lightx(GLenum light, GLenum pname, GLfixed param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightx(%s, %s, %d)\n", _mesa_enum_to_string(light), _mesa_enum_to_string(pname), param);
   CALL_Lightx(ctx->Dispatch.RealPublished, (light, pname, param));
}

static void GLAPIENTRY
_mesa_trace_Lightxv(GLenum light, GLenum pname, const GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLightxv(%s, %s, %p)\n", _mesa_enum_to_string(light), _mesa_enum_to_string(pname), (void *)params);
   CALL_Lightxv(ctx->Dispatch.RealPublished, (light, pname, params));
}

static void GLAPIENTRY
_mesa_trace_LineWidthx(GLfixed width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLineWidthx(%d)\n", width);
   CALL_LineWidthx(ctx->Dispatch.RealPublished, (width));
}

static void GLAPIENTRY
_mesa_trace_LoadMatrixx(const GLfixed *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glLoadMatrixx(%s)\n", m_buf);
   CALL_LoadMatrixx(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_Materialx(GLenum face, GLenum pname, GLfixed param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMaterialx(%s, %s, %d)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(pname), param);
   CALL_Materialx(ctx->Dispatch.RealPublished, (face, pname, param));
}

static void GLAPIENTRY
_mesa_trace_Materialxv(GLenum face, GLenum pname, const GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMaterialxv(%s, %s, %p)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(pname), (void *)params);
   CALL_Materialxv(ctx->Dispatch.RealPublished, (face, pname, params));
}

static void GLAPIENTRY
_mesa_trace_MultMatrixx(const GLfixed *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultMatrixx(%s)\n", m_buf);
   CALL_MultMatrixx(ctx->Dispatch.RealPublished, (m));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4x(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord4x(%s, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), s, t, r, q);
   CALL_MultiTexCoord4x(ctx->Dispatch.RealPublished, (target, s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_Normal3x(GLfixed nx, GLfixed ny, GLfixed nz)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormal3x(%d, %d, %d)\n", nx, ny, nz);
   CALL_Normal3x(ctx->Dispatch.RealPublished, (nx, ny, nz));
}

static void GLAPIENTRY
_mesa_trace_Orthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glOrthof(%f, %f, %f, %f, %f, %f)\n", left, right, bottom, top, zNear, zFar);
   CALL_Orthof(ctx->Dispatch.RealPublished, (left, right, bottom, top, zNear, zFar));
}

static void GLAPIENTRY
_mesa_trace_Orthox(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glOrthox(%d, %d, %d, %d, %d, %d)\n", left, right, bottom, top, zNear, zFar);
   CALL_Orthox(ctx->Dispatch.RealPublished, (left, right, bottom, top, zNear, zFar));
}

static void GLAPIENTRY
_mesa_trace_PointSizex(GLfixed size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPointSizex(%d)\n", size);
   CALL_PointSizex(ctx->Dispatch.RealPublished, (size));
}

static void GLAPIENTRY
_mesa_trace_PolygonOffsetx(GLfixed factor, GLfixed units)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPolygonOffsetx(%d, %d)\n", factor, units);
   CALL_PolygonOffsetx(ctx->Dispatch.RealPublished, (factor, units));
}

static void GLAPIENTRY
_mesa_trace_Rotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glRotatex(%d, %d, %d, %d)\n", angle, x, y, z);
   CALL_Rotatex(ctx->Dispatch.RealPublished, (angle, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_SampleCoveragex(GLclampx value, GLboolean invert)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSampleCoveragex(%d, %s)\n", value, invert ? "GL_TRUE" : "GL_FALSE");
   CALL_SampleCoveragex(ctx->Dispatch.RealPublished, (value, invert));
}

static void GLAPIENTRY
_mesa_trace_Scalex(GLfixed x, GLfixed y, GLfixed z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glScalex(%d, %d, %d)\n", x, y, z);
   CALL_Scalex(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_TexEnvx(GLenum target, GLenum pname, GLfixed param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexEnvx(%s, %s, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_TexEnvx(ctx->Dispatch.RealPublished, (target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TexEnvxv(GLenum target, GLenum pname, const GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexEnvxv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexEnvxv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexParameterx(GLenum target, GLenum pname, GLfixed param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexParameterx(%s, %s, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_TexParameterx(ctx->Dispatch.RealPublished, (target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_Translatex(GLfixed x, GLfixed y, GLfixed z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTranslatex(%d, %d, %d)\n", x, y, z);
   CALL_Translatex(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_ClipPlanef(GLenum plane, const GLfloat *equation)
{
   GET_CURRENT_CONTEXT(ctx);
   char equation_buf[512];
   _mesa_trace_format_array(equation_buf, sizeof(equation_buf), equation, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glClipPlanef(%s, %s)\n", _mesa_enum_to_string(plane), equation_buf);
   CALL_ClipPlanef(ctx->Dispatch.RealPublished, (plane, equation));
}

static void GLAPIENTRY
_mesa_trace_ClipPlanex(GLenum plane, const GLfixed *equation)
{
   GET_CURRENT_CONTEXT(ctx);
   char equation_buf[512];
   _mesa_trace_format_array(equation_buf, sizeof(equation_buf), equation, 4, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glClipPlanex(%s, %s)\n", _mesa_enum_to_string(plane), equation_buf);
   CALL_ClipPlanex(ctx->Dispatch.RealPublished, (plane, equation));
}

static void GLAPIENTRY
_mesa_trace_GetClipPlanef(GLenum plane, GLfloat *equation)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetClipPlanef(%s, %p)\n", _mesa_enum_to_string(plane), (void *)equation);
   CALL_GetClipPlanef(ctx->Dispatch.RealPublished, (plane, equation));
}

static void GLAPIENTRY
_mesa_trace_GetClipPlanex(GLenum plane, GLfixed *equation)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetClipPlanex(%s, %p)\n", _mesa_enum_to_string(plane), (void *)equation);
   CALL_GetClipPlanex(ctx->Dispatch.RealPublished, (plane, equation));
}

static void GLAPIENTRY
_mesa_trace_GetFixedv(GLenum pname, GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFixedv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_GetFixedv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetLightxv(GLenum light, GLenum pname, GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetLightxv(%s, %s, %p)\n", _mesa_enum_to_string(light), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetLightxv(ctx->Dispatch.RealPublished, (light, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetMaterialxv(GLenum face, GLenum pname, GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMaterialxv(%s, %s, %p)\n", _mesa_enum_to_string(face), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMaterialxv(ctx->Dispatch.RealPublished, (face, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexEnvxv(GLenum target, GLenum pname, GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexEnvxv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexEnvxv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTexParameterxv(GLenum target, GLenum pname, GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTexParameterxv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTexParameterxv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_PointParameterx(GLenum pname, GLfixed param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPointParameterx(%s, %d)\n", _mesa_enum_to_string(pname), param);
   CALL_PointParameterx(ctx->Dispatch.RealPublished, (pname, param));
}

static void GLAPIENTRY
_mesa_trace_PointParameterxv(GLenum pname, const GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPointParameterxv(%s, %p)\n", _mesa_enum_to_string(pname), (void *)params);
   CALL_PointParameterxv(ctx->Dispatch.RealPublished, (pname, params));
}

static void GLAPIENTRY
_mesa_trace_TexParameterxv(GLenum target, GLenum pname, const GLfixed *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexParameterxv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TexParameterxv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_BlendBarrier(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBlendBarrier()\n");
   CALL_BlendBarrier(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_PrimitiveBoundingBox(GLfloat minX, GLfloat minY, GLfloat minZ, GLfloat minW, GLfloat maxX, GLfloat maxY, GLfloat maxZ, GLfloat maxW)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPrimitiveBoundingBox(%f, %f, %f, %f, %f, %f, %f, %f)\n", minX, minY, minZ, minW, maxX, maxY, maxZ, maxW);
   CALL_PrimitiveBoundingBox(ctx->Dispatch.RealPublished, (minX, minY, minZ, minW, maxX, maxY, maxZ, maxW));
}

static void GLAPIENTRY
_mesa_trace_MaxShaderCompilerThreadsKHR(GLuint count)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMaxShaderCompilerThreadsKHR(%u)\n", count);
   CALL_MaxShaderCompilerThreadsKHR(ctx->Dispatch.RealPublished, (count));
}

static void GLAPIENTRY
_mesa_trace_MatrixLoadfEXT(GLenum matrixMode, const GLfloat *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glMatrixLoadfEXT(%s, %s)\n", _mesa_enum_to_string(matrixMode), m_buf);
   CALL_MatrixLoadfEXT(ctx->Dispatch.RealPublished, (matrixMode, m));
}

static void GLAPIENTRY
_mesa_trace_MatrixLoaddEXT(GLenum matrixMode, const GLdouble *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glMatrixLoaddEXT(%s, %s)\n", _mesa_enum_to_string(matrixMode), m_buf);
   CALL_MatrixLoaddEXT(ctx->Dispatch.RealPublished, (matrixMode, m));
}

static void GLAPIENTRY
_mesa_trace_MatrixMultfEXT(GLenum matrixMode, const GLfloat *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glMatrixMultfEXT(%s, %s)\n", _mesa_enum_to_string(matrixMode), m_buf);
   CALL_MatrixMultfEXT(ctx->Dispatch.RealPublished, (matrixMode, m));
}

static void GLAPIENTRY
_mesa_trace_MatrixMultdEXT(GLenum matrixMode, const GLdouble *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glMatrixMultdEXT(%s, %s)\n", _mesa_enum_to_string(matrixMode), m_buf);
   CALL_MatrixMultdEXT(ctx->Dispatch.RealPublished, (matrixMode, m));
}

static void GLAPIENTRY
_mesa_trace_MatrixLoadIdentityEXT(GLenum matrixMode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixLoadIdentityEXT(%s)\n", _mesa_enum_to_string(matrixMode));
   CALL_MatrixLoadIdentityEXT(ctx->Dispatch.RealPublished, (matrixMode));
}

static void GLAPIENTRY
_mesa_trace_MatrixRotatefEXT(GLenum matrixMode, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixRotatefEXT(%s, %f, %f, %f, %f)\n", _mesa_enum_to_string(matrixMode), angle, x, y, z);
   CALL_MatrixRotatefEXT(ctx->Dispatch.RealPublished, (matrixMode, angle, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_MatrixRotatedEXT(GLenum matrixMode, GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixRotatedEXT(%s, %f, %f, %f, %f)\n", _mesa_enum_to_string(matrixMode), angle, x, y, z);
   CALL_MatrixRotatedEXT(ctx->Dispatch.RealPublished, (matrixMode, angle, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_MatrixScalefEXT(GLenum matrixMode, GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixScalefEXT(%s, %f, %f, %f)\n", _mesa_enum_to_string(matrixMode), x, y, z);
   CALL_MatrixScalefEXT(ctx->Dispatch.RealPublished, (matrixMode, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_MatrixScaledEXT(GLenum matrixMode, GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixScaledEXT(%s, %f, %f, %f)\n", _mesa_enum_to_string(matrixMode), x, y, z);
   CALL_MatrixScaledEXT(ctx->Dispatch.RealPublished, (matrixMode, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_MatrixTranslatefEXT(GLenum matrixMode, GLfloat x, GLfloat y, GLfloat z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixTranslatefEXT(%s, %f, %f, %f)\n", _mesa_enum_to_string(matrixMode), x, y, z);
   CALL_MatrixTranslatefEXT(ctx->Dispatch.RealPublished, (matrixMode, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_MatrixTranslatedEXT(GLenum matrixMode, GLdouble x, GLdouble y, GLdouble z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixTranslatedEXT(%s, %f, %f, %f)\n", _mesa_enum_to_string(matrixMode), x, y, z);
   CALL_MatrixTranslatedEXT(ctx->Dispatch.RealPublished, (matrixMode, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_MatrixOrthoEXT(GLenum matrixMode, GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixOrthoEXT(%s, %f, %f, %f, %f, %f, %f)\n", _mesa_enum_to_string(matrixMode), l, r, b, t, n, f);
   CALL_MatrixOrthoEXT(ctx->Dispatch.RealPublished, (matrixMode, l, r, b, t, n, f));
}

static void GLAPIENTRY
_mesa_trace_MatrixFrustumEXT(GLenum matrixMode, GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixFrustumEXT(%s, %f, %f, %f, %f, %f, %f)\n", _mesa_enum_to_string(matrixMode), l, r, b, t, n, f);
   CALL_MatrixFrustumEXT(ctx->Dispatch.RealPublished, (matrixMode, l, r, b, t, n, f));
}

static void GLAPIENTRY
_mesa_trace_MatrixPushEXT(GLenum matrixMode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixPushEXT(%s)\n", _mesa_enum_to_string(matrixMode));
   CALL_MatrixPushEXT(ctx->Dispatch.RealPublished, (matrixMode));
}

static void GLAPIENTRY
_mesa_trace_MatrixPopEXT(GLenum matrixMode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMatrixPopEXT(%s)\n", _mesa_enum_to_string(matrixMode));
   CALL_MatrixPopEXT(ctx->Dispatch.RealPublished, (matrixMode));
}

static void GLAPIENTRY
_mesa_trace_MatrixLoadTransposefEXT(GLenum matrixMode, const GLfloat *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glMatrixLoadTransposefEXT(%s, %s)\n", _mesa_enum_to_string(matrixMode), m_buf);
   CALL_MatrixLoadTransposefEXT(ctx->Dispatch.RealPublished, (matrixMode, m));
}

static void GLAPIENTRY
_mesa_trace_MatrixLoadTransposedEXT(GLenum matrixMode, const GLdouble *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glMatrixLoadTransposedEXT(%s, %s)\n", _mesa_enum_to_string(matrixMode), m_buf);
   CALL_MatrixLoadTransposedEXT(ctx->Dispatch.RealPublished, (matrixMode, m));
}

static void GLAPIENTRY
_mesa_trace_MatrixMultTransposefEXT(GLenum matrixMode, const GLfloat *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glMatrixMultTransposefEXT(%s, %s)\n", _mesa_enum_to_string(matrixMode), m_buf);
   CALL_MatrixMultTransposefEXT(ctx->Dispatch.RealPublished, (matrixMode, m));
}

static void GLAPIENTRY
_mesa_trace_MatrixMultTransposedEXT(GLenum matrixMode, const GLdouble *m)
{
   GET_CURRENT_CONTEXT(ctx);
   char m_buf[512];
   _mesa_trace_format_array(m_buf, sizeof(m_buf), m, 16, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glMatrixMultTransposedEXT(%s, %s)\n", _mesa_enum_to_string(matrixMode), m_buf);
   CALL_MatrixMultTransposedEXT(ctx->Dispatch.RealPublished, (matrixMode, m));
}

static void GLAPIENTRY
_mesa_trace_BindMultiTextureEXT(GLenum texunit, GLenum target, GLuint texture)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindMultiTextureEXT(%s, %s, %u)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), texture);
   CALL_BindMultiTextureEXT(ctx->Dispatch.RealPublished, (texunit, target, texture));
}

static void GLAPIENTRY
_mesa_trace_NamedBufferDataEXT(GLuint buffer, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedBufferDataEXT(%u, %" PRIdPTR ", %p, %s)\n", buffer, (intptr_t)size, (void *)data, _mesa_enum_to_string(usage));
   CALL_NamedBufferDataEXT(ctx->Dispatch.RealPublished, (buffer, size, data, usage));
}

static void GLAPIENTRY
_mesa_trace_NamedBufferSubDataEXT(GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedBufferSubDataEXT(%u, %" PRIdPTR ", %" PRIdPTR ", %p)\n", buffer, (intptr_t)offset, (intptr_t)size, (void *)data);
   CALL_NamedBufferSubDataEXT(ctx->Dispatch.RealPublished, (buffer, offset, size, data));
}

static void GLAPIENTRY
_mesa_trace_NamedBufferStorageEXT(GLuint buffer, GLsizeiptr size, const GLvoid *data, GLbitfield flags)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedBufferStorageEXT(%u, %" PRIdPTR ", %p, 0x%x)\n", buffer, (intptr_t)size, (void *)data, flags);
   CALL_NamedBufferStorageEXT(ctx->Dispatch.RealPublished, (buffer, size, data, flags));
}

static GLvoid * GLAPIENTRY
_mesa_trace_MapNamedBufferRangeEXT(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapNamedBufferRangeEXT(%u, %" PRIdPTR ", %" PRIdPTR ", 0x%x)\n", buffer, (intptr_t)offset, (intptr_t)length, access);
   return CALL_MapNamedBufferRangeEXT(ctx->Dispatch.RealPublished, (buffer, offset, length, access));
}

static void GLAPIENTRY
_mesa_trace_TextureImage1DEXT(GLuint texture, GLenum target, GLint level, GLint internalFormat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureImage1DEXT(%u, %s, %d, %d, %d, %d, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), level, internalFormat, width, border, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TextureImage1DEXT(ctx->Dispatch.RealPublished, (texture, target, level, internalFormat, width, border, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TextureImage2DEXT(GLuint texture, GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureImage2DEXT(%u, %s, %d, %d, %d, %d, %d, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), level, internalFormat, width, height, border, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TextureImage2DEXT(ctx->Dispatch.RealPublished, (texture, target, level, internalFormat, width, height, border, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TextureImage3DEXT(GLuint texture, GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureImage3DEXT(%u, %s, %d, %d, %d, %d, %d, %d, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), level, internalFormat, width, height, depth, border, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TextureImage3DEXT(ctx->Dispatch.RealPublished, (texture, target, level, internalFormat, width, height, depth, border, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TextureSubImage1DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureSubImage1DEXT(%u, %s, %d, %d, %d, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), level, xoffset, width, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TextureSubImage1DEXT(ctx->Dispatch.RealPublished, (texture, target, level, xoffset, width, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TextureSubImage2DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureSubImage2DEXT(%u, %s, %d, %d, %d, %d, %d, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), level, xoffset, yoffset, width, height, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TextureSubImage2DEXT(ctx->Dispatch.RealPublished, (texture, target, level, xoffset, yoffset, width, height, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_TextureSubImage3DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureSubImage3DEXT(%u, %s, %d, %d, %d, %d, %d, %d, %d, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_TextureSubImage3DEXT(ctx->Dispatch.RealPublished, (texture, target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_CopyTextureImage1DEXT(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, int border)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTextureImage1DEXT(%u, %s, %d, %s, %d, %d, %d, %d)\n", texture, _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalFormat), x, y, width, border);
   CALL_CopyTextureImage1DEXT(ctx->Dispatch.RealPublished, (texture, target, level, internalFormat, x, y, width, border));
}

static void GLAPIENTRY
_mesa_trace_CopyTextureImage2DEXT(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, int border)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTextureImage2DEXT(%u, %s, %d, %s, %d, %d, %d, %d, %d)\n", texture, _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalFormat), x, y, width, height, border);
   CALL_CopyTextureImage2DEXT(ctx->Dispatch.RealPublished, (texture, target, level, internalFormat, x, y, width, height, border));
}

static void GLAPIENTRY
_mesa_trace_CopyTextureSubImage1DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTextureSubImage1DEXT(%u, %s, %d, %d, %d, %d, %d)\n", texture, _mesa_enum_to_string(target), level, xoffset, x, y, width);
   CALL_CopyTextureSubImage1DEXT(ctx->Dispatch.RealPublished, (texture, target, level, xoffset, x, y, width));
}

static void GLAPIENTRY
_mesa_trace_CopyTextureSubImage2DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTextureSubImage2DEXT(%u, %s, %d, %d, %d, %d, %d, %d, %d)\n", texture, _mesa_enum_to_string(target), level, xoffset, yoffset, x, y, width, height);
   CALL_CopyTextureSubImage2DEXT(ctx->Dispatch.RealPublished, (texture, target, level, xoffset, yoffset, x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_CopyTextureSubImage3DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyTextureSubImage3DEXT(%u, %s, %d, %d, %d, %d, %d, %d, %d, %d)\n", texture, _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, x, y, width, height);
   CALL_CopyTextureSubImage3DEXT(ctx->Dispatch.RealPublished, (texture, target, level, xoffset, yoffset, zoffset, x, y, width, height));
}

static GLvoid * GLAPIENTRY
_mesa_trace_MapNamedBufferEXT(GLuint buffer, GLenum access)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMapNamedBufferEXT(%u, %s)\n", buffer, _mesa_enum_to_string(access));
   return CALL_MapNamedBufferEXT(ctx->Dispatch.RealPublished, (buffer, access));
}

static void GLAPIENTRY
_mesa_trace_GetTextureParameterivEXT(GLuint texture, GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureParameterivEXT(%u, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureParameterivEXT(ctx->Dispatch.RealPublished, (texture, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureParameterfvEXT(GLuint texture, GLenum target, GLenum pname, float *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureParameterfvEXT(%u, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureParameterfvEXT(ctx->Dispatch.RealPublished, (texture, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TextureParameteriEXT(GLuint texture, GLenum target, GLenum pname, int param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameteriEXT(%u, %s, %s, %d)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_TextureParameteriEXT(ctx->Dispatch.RealPublished, (texture, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TextureParameterivEXT(GLuint texture, GLenum target, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameterivEXT(%u, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TextureParameterivEXT(ctx->Dispatch.RealPublished, (texture, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TextureParameterfEXT(GLuint texture, GLenum target, GLenum pname, float param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameterfEXT(%u, %s, %s, %f)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_TextureParameterfEXT(ctx->Dispatch.RealPublished, (texture, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_TextureParameterfvEXT(GLuint texture, GLenum target, GLenum pname, const float *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameterfvEXT(%u, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TextureParameterfvEXT(ctx->Dispatch.RealPublished, (texture, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureImageEXT(GLuint texture, GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureImageEXT(%u, %s, %d, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), level, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_GetTextureImageEXT(ctx->Dispatch.RealPublished, (texture, target, level, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_GetTextureLevelParameterivEXT(GLuint texture, GLenum target, GLint level, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureLevelParameterivEXT(%u, %s, %d, %s, %p)\n", texture, _mesa_enum_to_string(target), level, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureLevelParameterivEXT(ctx->Dispatch.RealPublished, (texture, target, level, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureLevelParameterfvEXT(GLuint texture, GLenum target, GLint level, GLenum pname, float *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureLevelParameterfvEXT(%u, %s, %d, %s, %p)\n", texture, _mesa_enum_to_string(target), level, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureLevelParameterfvEXT(ctx->Dispatch.RealPublished, (texture, target, level, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetNamedBufferSubDataEXT(GLuint buffer, GLintptr offset, GLsizeiptr size, GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedBufferSubDataEXT(%u, %" PRIdPTR ", %" PRIdPTR ", %p)\n", buffer, (intptr_t)offset, (intptr_t)size, (void *)data);
   CALL_GetNamedBufferSubDataEXT(ctx->Dispatch.RealPublished, (buffer, offset, size, data));
}

static void GLAPIENTRY
_mesa_trace_GetNamedBufferPointervEXT(GLuint buffer, GLenum pname, GLvoid **params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedBufferPointervEXT(%u, %s, %p)\n", buffer, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedBufferPointervEXT(ctx->Dispatch.RealPublished, (buffer, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetNamedBufferParameterivEXT(GLuint buffer, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedBufferParameterivEXT(%u, %s, %p)\n", buffer, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedBufferParameterivEXT(ctx->Dispatch.RealPublished, (buffer, pname, params));
}

static void GLAPIENTRY
_mesa_trace_FlushMappedNamedBufferRangeEXT(GLuint buffer, GLintptr offset, GLsizeiptr length)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFlushMappedNamedBufferRangeEXT(%u, %" PRIdPTR ", %" PRIdPTR ")\n", buffer, (intptr_t)offset, (intptr_t)length);
   CALL_FlushMappedNamedBufferRangeEXT(ctx->Dispatch.RealPublished, (buffer, offset, length));
}

static void GLAPIENTRY
_mesa_trace_FramebufferDrawBufferEXT(GLuint framebuffer, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferDrawBufferEXT(%u, %s)\n", framebuffer, _mesa_enum_to_string(mode));
   CALL_FramebufferDrawBufferEXT(ctx->Dispatch.RealPublished, (framebuffer, mode));
}

static void GLAPIENTRY
_mesa_trace_FramebufferDrawBuffersEXT(GLuint framebuffer, GLsizei n, const GLenum *bufs)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferDrawBuffersEXT(%u, %d, %p)\n", framebuffer, n, (void *)bufs);
   CALL_FramebufferDrawBuffersEXT(ctx->Dispatch.RealPublished, (framebuffer, n, bufs));
}

static void GLAPIENTRY
_mesa_trace_FramebufferReadBufferEXT(GLuint framebuffer, GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferReadBufferEXT(%u, %s)\n", framebuffer, _mesa_enum_to_string(mode));
   CALL_FramebufferReadBufferEXT(ctx->Dispatch.RealPublished, (framebuffer, mode));
}

static void GLAPIENTRY
_mesa_trace_GetFramebufferParameterivEXT(GLuint framebuffer, GLenum pname, GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFramebufferParameterivEXT(%u, %s, %p)\n", framebuffer, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetFramebufferParameterivEXT(ctx->Dispatch.RealPublished, (framebuffer, pname, param));
}

static GLenum GLAPIENTRY
_mesa_trace_CheckNamedFramebufferStatusEXT(GLuint framebuffer, GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCheckNamedFramebufferStatusEXT(%u, %s)\n", framebuffer, _mesa_enum_to_string(target));
   return CALL_CheckNamedFramebufferStatusEXT(ctx->Dispatch.RealPublished, (framebuffer, target));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferTexture1DEXT(GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferTexture1DEXT(%u, %s, %s, %u, %d)\n", framebuffer, _mesa_enum_to_string(attachment), _mesa_enum_to_string(textarget), texture, level);
   CALL_NamedFramebufferTexture1DEXT(ctx->Dispatch.RealPublished, (framebuffer, attachment, textarget, texture, level));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferTexture2DEXT(GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferTexture2DEXT(%u, %s, %s, %u, %d)\n", framebuffer, _mesa_enum_to_string(attachment), _mesa_enum_to_string(textarget), texture, level);
   CALL_NamedFramebufferTexture2DEXT(ctx->Dispatch.RealPublished, (framebuffer, attachment, textarget, texture, level));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferTexture3DEXT(GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferTexture3DEXT(%u, %s, %s, %u, %d, %d)\n", framebuffer, _mesa_enum_to_string(attachment), _mesa_enum_to_string(textarget), texture, level, zoffset);
   CALL_NamedFramebufferTexture3DEXT(ctx->Dispatch.RealPublished, (framebuffer, attachment, textarget, texture, level, zoffset));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferRenderbufferEXT(GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferRenderbufferEXT(%u, %s, %s, %u)\n", framebuffer, _mesa_enum_to_string(attachment), _mesa_enum_to_string(renderbuffertarget), renderbuffer);
   CALL_NamedFramebufferRenderbufferEXT(ctx->Dispatch.RealPublished, (framebuffer, attachment, renderbuffertarget, renderbuffer));
}

static void GLAPIENTRY
_mesa_trace_GetNamedFramebufferAttachmentParameterivEXT(GLuint framebuffer, GLenum attachment, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedFramebufferAttachmentParameterivEXT(%u, %s, %s, %p)\n", framebuffer, _mesa_enum_to_string(attachment), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedFramebufferAttachmentParameterivEXT(ctx->Dispatch.RealPublished, (framebuffer, attachment, pname, params));
}

static void GLAPIENTRY
_mesa_trace_EnableClientStateiEXT(GLenum array, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEnableClientStateiEXT(%s, %u)\n", _mesa_enum_to_string(array), index);
   CALL_EnableClientStateiEXT(ctx->Dispatch.RealPublished, (array, index));
}

static void GLAPIENTRY
_mesa_trace_DisableClientStateiEXT(GLenum array, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDisableClientStateiEXT(%s, %u)\n", _mesa_enum_to_string(array), index);
   CALL_DisableClientStateiEXT(ctx->Dispatch.RealPublished, (array, index));
}

static void GLAPIENTRY
_mesa_trace_GetPointerIndexedvEXT(GLenum target, GLuint index, GLvoid**params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetPointerIndexedvEXT(%s, %u, %p)\n", _mesa_enum_to_string(target), index, (void *)params);
   CALL_GetPointerIndexedvEXT(ctx->Dispatch.RealPublished, (target, index, params));
}

static void GLAPIENTRY
_mesa_trace_MultiTexEnviEXT(GLenum texunit, GLenum target, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexEnviEXT(%s, %s, %s, %d)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_MultiTexEnviEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexEnvivEXT(GLenum texunit, GLenum target, GLenum pname, const GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexEnvivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)param);
   CALL_MultiTexEnvivEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexEnvfEXT(GLenum texunit, GLenum target, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexEnvfEXT(%s, %s, %s, %f)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_MultiTexEnvfEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexEnvfvEXT(GLenum texunit, GLenum target, GLenum pname, const GLfloat *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexEnvfvEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)param);
   CALL_MultiTexEnvfvEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexEnvivEXT(GLenum texunit, GLenum target, GLenum pname, GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexEnvivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)param);
   CALL_GetMultiTexEnvivEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexEnvfvEXT(GLenum texunit, GLenum target, GLenum pname, GLfloat *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexEnvfvEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)param);
   CALL_GetMultiTexEnvfvEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexParameteriEXT(GLenum texunit, GLenum target, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexParameteriEXT(%s, %s, %s, %d)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_MultiTexParameteriEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexParameterivEXT(GLenum texunit, GLenum target, GLenum pname, const GLint*param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexParameterivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)param);
   CALL_MultiTexParameterivEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexParameterfEXT(GLenum texunit, GLenum target, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexParameterfEXT(%s, %s, %s, %f)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_MultiTexParameterfEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexParameterfvEXT(GLenum texunit, GLenum target, GLenum pname, const GLfloat*param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexParameterfvEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)param);
   CALL_MultiTexParameterfvEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexImageEXT(GLenum texunit, GLenum target, GLint level, GLenum format, GLenum type, GLvoid*pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexImageEXT(%s, %s, %d, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_GetMultiTexImageEXT(ctx->Dispatch.RealPublished, (texunit, target, level, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_MultiTexImage1DEXT(GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid*pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexImage1DEXT(%s, %s, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, internalformat, width, border, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_MultiTexImage1DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, internalformat, width, border, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_MultiTexImage2DEXT(GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid*pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexImage2DEXT(%s, %s, %d, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, internalformat, width, height, border, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_MultiTexImage2DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, internalformat, width, height, border, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_MultiTexImage3DEXT(GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid*pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexImage3DEXT(%s, %s, %d, %d, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, internalformat, width, height, depth, border, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_MultiTexImage3DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, internalformat, width, height, depth, border, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_MultiTexSubImage1DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid*pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexSubImage1DEXT(%s, %s, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, xoffset, width, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_MultiTexSubImage1DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, xoffset, width, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_MultiTexSubImage2DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid*pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexSubImage2DEXT(%s, %s, %d, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, xoffset, yoffset, width, height, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_MultiTexSubImage2DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, xoffset, yoffset, width, height, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_MultiTexSubImage3DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid*pixels)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexSubImage3DEXT(%s, %s, %d, %d, %d, %d, %d, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)pixels);
   CALL_MultiTexSubImage3DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexParameterivEXT(GLenum texunit, GLenum target, GLenum pname, GLint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexParameterivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMultiTexParameterivEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexParameterfvEXT(GLenum texunit, GLenum target, GLenum pname, GLfloat*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexParameterfvEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMultiTexParameterfvEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_CopyMultiTexImage1DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyMultiTexImage1DEXT(%s, %s, %d, %s, %d, %d, %d, %d)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalformat), x, y, width, border);
   CALL_CopyMultiTexImage1DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, internalformat, x, y, width, border));
}

static void GLAPIENTRY
_mesa_trace_CopyMultiTexImage2DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyMultiTexImage2DEXT(%s, %s, %d, %s, %d, %d, %d, %d, %d)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalformat), x, y, width, height, border);
   CALL_CopyMultiTexImage2DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, internalformat, x, y, width, height, border));
}

static void GLAPIENTRY
_mesa_trace_CopyMultiTexSubImage1DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyMultiTexSubImage1DEXT(%s, %s, %d, %d, %d, %d, %d)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, xoffset, x, y, width);
   CALL_CopyMultiTexSubImage1DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, xoffset, x, y, width));
}

static void GLAPIENTRY
_mesa_trace_CopyMultiTexSubImage2DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyMultiTexSubImage2DEXT(%s, %s, %d, %d, %d, %d, %d, %d, %d)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, xoffset, yoffset, x, y, width, height);
   CALL_CopyMultiTexSubImage2DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, xoffset, yoffset, x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_CopyMultiTexSubImage3DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyMultiTexSubImage3DEXT(%s, %s, %d, %d, %d, %d, %d, %d, %d, %d)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, x, y, width, height);
   CALL_CopyMultiTexSubImage3DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, xoffset, yoffset, zoffset, x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_MultiTexGendEXT(GLenum texunit, GLenum coord, GLenum pname, GLdouble param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexGendEXT(%s, %s, %s, %f)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), param);
   CALL_MultiTexGendEXT(ctx->Dispatch.RealPublished, (texunit, coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexGendvEXT(GLenum texunit, GLenum coord, GLenum pname, const GLdouble*param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexGendvEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)param);
   CALL_MultiTexGendvEXT(ctx->Dispatch.RealPublished, (texunit, coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexGenfEXT(GLenum texunit, GLenum coord, GLenum pname, GLfloat param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexGenfEXT(%s, %s, %s, %f)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), param);
   CALL_MultiTexGenfEXT(ctx->Dispatch.RealPublished, (texunit, coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexGenfvEXT(GLenum texunit, GLenum coord, GLenum pname, const GLfloat *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexGenfvEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)param);
   CALL_MultiTexGenfvEXT(ctx->Dispatch.RealPublished, (texunit, coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexGeniEXT(GLenum texunit, GLenum coord, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexGeniEXT(%s, %s, %s, %d)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), param);
   CALL_MultiTexGeniEXT(ctx->Dispatch.RealPublished, (texunit, coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexGenivEXT(GLenum texunit, GLenum coord, GLenum pname, const GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexGenivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)param);
   CALL_MultiTexGenivEXT(ctx->Dispatch.RealPublished, (texunit, coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexGendvEXT(GLenum texunit, GLenum coord, GLenum pname, GLdouble *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexGendvEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)param);
   CALL_GetMultiTexGendvEXT(ctx->Dispatch.RealPublished, (texunit, coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexGenfvEXT(GLenum texunit, GLenum coord, GLenum pname, GLfloat *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexGenfvEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)param);
   CALL_GetMultiTexGenfvEXT(ctx->Dispatch.RealPublished, (texunit, coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexGenivEXT(GLenum texunit, GLenum coord, GLenum pname, GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexGenivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(coord), _mesa_enum_to_string(pname), (void *)param);
   CALL_GetMultiTexGenivEXT(ctx->Dispatch.RealPublished, (texunit, coord, pname, param));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoordPointerEXT(GLenum texunit, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoordPointerEXT(%s, %d, %s, %d, %p)\n", _mesa_enum_to_string(texunit), size, _mesa_enum_to_string(type), stride, (void *)pointer);
   CALL_MultiTexCoordPointerEXT(ctx->Dispatch.RealPublished, (texunit, size, type, stride, pointer));
}

static void GLAPIENTRY
_mesa_trace_BindImageTextureEXT(GLuint index, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLint format)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glBindImageTextureEXT(%u, %u, %d, %s, %d, %s, %d)\n", index, texture, level, layered ? "GL_TRUE" : "GL_FALSE", layer, _mesa_enum_to_string(access), format);
   CALL_BindImageTextureEXT(ctx->Dispatch.RealPublished, (index, texture, level, layered, layer, access, format));
}

static void GLAPIENTRY
_mesa_trace_CompressedTextureImage1DEXT(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei border, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTextureImage1DEXT(%u, %s, %d, %s, %d, %d, %d, %p)\n", texture, _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalFormat), width, border, imageSize, (void *)data);
   CALL_CompressedTextureImage1DEXT(ctx->Dispatch.RealPublished, (texture, target, level, internalFormat, width, border, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTextureImage2DEXT(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei border, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTextureImage2DEXT(%u, %s, %d, %s, %d, %d, %d, %d, %p)\n", texture, _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalFormat), width, height, border, imageSize, (void *)data);
   CALL_CompressedTextureImage2DEXT(ctx->Dispatch.RealPublished, (texture, target, level, internalFormat, width, height, border, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTextureImage3DEXT(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLsizei border, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTextureImage3DEXT(%u, %s, %d, %s, %d, %d, %d, %d, %d, %p)\n", texture, _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalFormat), width, height, depth, border, imageSize, (void *)data);
   CALL_CompressedTextureImage3DEXT(ctx->Dispatch.RealPublished, (texture, target, level, internalFormat, width, height, depth, border, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTextureSubImage1DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTextureSubImage1DEXT(%u, %s, %d, %d, %d, %s, %d, %p)\n", texture, _mesa_enum_to_string(target), level, xoffset, width, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedTextureSubImage1DEXT(ctx->Dispatch.RealPublished, (texture, target, level, xoffset, width, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTextureSubImage2DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTextureSubImage2DEXT(%u, %s, %d, %d, %d, %d, %d, %s, %d, %p)\n", texture, _mesa_enum_to_string(target), level, xoffset, yoffset, width, height, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedTextureSubImage2DEXT(ctx->Dispatch.RealPublished, (texture, target, level, xoffset, yoffset, width, height, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedTextureSubImage3DEXT(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedTextureSubImage3DEXT(%u, %s, %d, %d, %d, %d, %d, %d, %d, %s, %d, %p)\n", texture, _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedTextureSubImage3DEXT(ctx->Dispatch.RealPublished, (texture, target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_GetCompressedTextureImageEXT(GLuint texture, GLenum target, GLint level, GLvoid *img)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetCompressedTextureImageEXT(%u, %s, %d, %p)\n", texture, _mesa_enum_to_string(target), level, (void *)img);
   CALL_GetCompressedTextureImageEXT(ctx->Dispatch.RealPublished, (texture, target, level, img));
}

static void GLAPIENTRY
_mesa_trace_CompressedMultiTexImage1DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei border, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedMultiTexImage1DEXT(%s, %s, %d, %s, %d, %d, %d, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalFormat), width, border, imageSize, (void *)data);
   CALL_CompressedMultiTexImage1DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, internalFormat, width, border, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedMultiTexImage2DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei border, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedMultiTexImage2DEXT(%s, %s, %d, %s, %d, %d, %d, %d, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalFormat), width, height, border, imageSize, (void *)data);
   CALL_CompressedMultiTexImage2DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, internalFormat, width, height, border, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedMultiTexImage3DEXT(GLenum texunit, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLsizei border, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedMultiTexImage3DEXT(%s, %s, %d, %s, %d, %d, %d, %d, %d, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, _mesa_enum_to_string(internalFormat), width, height, depth, border, imageSize, (void *)data);
   CALL_CompressedMultiTexImage3DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, internalFormat, width, height, depth, border, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedMultiTexSubImage1DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedMultiTexSubImage1DEXT(%s, %s, %d, %d, %d, %s, %d, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, xoffset, width, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedMultiTexSubImage1DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, xoffset, width, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedMultiTexSubImage2DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedMultiTexSubImage2DEXT(%s, %s, %d, %d, %d, %d, %d, %s, %d, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, xoffset, yoffset, width, height, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedMultiTexSubImage2DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, xoffset, yoffset, width, height, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_CompressedMultiTexSubImage3DEXT(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompressedMultiTexSubImage3DEXT(%s, %s, %d, %d, %d, %d, %d, %d, %d, %s, %d, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, width, height, depth, _mesa_enum_to_string(format), imageSize, (void *)data);
   CALL_CompressedMultiTexSubImage3DEXT(ctx->Dispatch.RealPublished, (texunit, target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data));
}

static void GLAPIENTRY
_mesa_trace_GetCompressedMultiTexImageEXT(GLenum texunit, GLenum target, GLint level, GLvoid *img)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetCompressedMultiTexImageEXT(%s, %s, %d, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, (void *)img);
   CALL_GetCompressedMultiTexImageEXT(ctx->Dispatch.RealPublished, (texunit, target, level, img));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexLevelParameterivEXT(GLenum texunit, GLenum target, GLint level, GLenum pname, GLint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexLevelParameterivEXT(%s, %s, %d, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMultiTexLevelParameterivEXT(ctx->Dispatch.RealPublished, (texunit, target, level, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexLevelParameterfvEXT(GLenum texunit, GLenum target, GLint level, GLenum pname, GLfloat*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexLevelParameterfvEXT(%s, %s, %d, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), level, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMultiTexLevelParameterfvEXT(ctx->Dispatch.RealPublished, (texunit, target, level, pname, params));
}

static void GLAPIENTRY
_mesa_trace_FramebufferParameteriMESA(GLenum target, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferParameteriMESA(%s, %s, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), param);
   CALL_FramebufferParameteriMESA(ctx->Dispatch.RealPublished, (target, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetFramebufferParameterivMESA(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetFramebufferParameterivMESA(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetFramebufferParameterivMESA(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_NamedRenderbufferStorageEXT(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedRenderbufferStorageEXT(%u, %s, %d, %d)\n", renderbuffer, _mesa_enum_to_string(internalformat), width, height);
   CALL_NamedRenderbufferStorageEXT(ctx->Dispatch.RealPublished, (renderbuffer, internalformat, width, height));
}

static void GLAPIENTRY
_mesa_trace_GetNamedRenderbufferParameterivEXT(GLuint renderbuffer, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedRenderbufferParameterivEXT(%u, %s, %p)\n", renderbuffer, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedRenderbufferParameterivEXT(ctx->Dispatch.RealPublished, (renderbuffer, pname, params));
}

static void GLAPIENTRY
_mesa_trace_ClientAttribDefaultEXT(GLbitfield mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClientAttribDefaultEXT(0x%x)\n", mask);
   CALL_ClientAttribDefaultEXT(ctx->Dispatch.RealPublished, (mask));
}

static void GLAPIENTRY
_mesa_trace_PushClientAttribDefaultEXT(GLbitfield mask)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glPushClientAttribDefaultEXT(0x%x)\n", mask);
   CALL_PushClientAttribDefaultEXT(ctx->Dispatch.RealPublished, (mask));
}

static void GLAPIENTRY
_mesa_trace_NamedProgramStringEXT(GLuint program, GLenum target, GLenum format, GLsizei len, const GLvoid*string)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedProgramStringEXT(%u, %s, %s, %d, %p)\n", program, _mesa_enum_to_string(target), _mesa_enum_to_string(format), len, (void *)string);
   CALL_NamedProgramStringEXT(ctx->Dispatch.RealPublished, (program, target, format, len, string));
}

static void GLAPIENTRY
_mesa_trace_GetNamedProgramStringEXT(GLuint program, GLenum target, GLenum pname, GLvoid*string)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedProgramStringEXT(%u, %s, %s, %p)\n", program, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)string);
   CALL_GetNamedProgramStringEXT(ctx->Dispatch.RealPublished, (program, target, pname, string));
}

static void GLAPIENTRY
_mesa_trace_NamedProgramLocalParameter4fEXT(GLuint program, GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedProgramLocalParameter4fEXT(%u, %s, %u, %f, %f, %f, %f)\n", program, _mesa_enum_to_string(target), index, x, y, z, w);
   CALL_NamedProgramLocalParameter4fEXT(ctx->Dispatch.RealPublished, (program, target, index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_NamedProgramLocalParameter4fvEXT(GLuint program, GLenum target, GLuint index, const GLfloat*params)
{
   GET_CURRENT_CONTEXT(ctx);
   char params_buf[512];
   _mesa_trace_format_array(params_buf, sizeof(params_buf), params, 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glNamedProgramLocalParameter4fvEXT(%u, %s, %u, %s)\n", program, _mesa_enum_to_string(target), index, params_buf);
   CALL_NamedProgramLocalParameter4fvEXT(ctx->Dispatch.RealPublished, (program, target, index, params));
}

static void GLAPIENTRY
_mesa_trace_GetNamedProgramLocalParameterfvEXT(GLuint program, GLenum target, GLuint index, GLfloat*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedProgramLocalParameterfvEXT(%u, %s, %u, %p)\n", program, _mesa_enum_to_string(target), index, (void *)params);
   CALL_GetNamedProgramLocalParameterfvEXT(ctx->Dispatch.RealPublished, (program, target, index, params));
}

static void GLAPIENTRY
_mesa_trace_NamedProgramLocalParameter4dEXT(GLuint program, GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedProgramLocalParameter4dEXT(%u, %s, %u, %f, %f, %f, %f)\n", program, _mesa_enum_to_string(target), index, x, y, z, w);
   CALL_NamedProgramLocalParameter4dEXT(ctx->Dispatch.RealPublished, (program, target, index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_NamedProgramLocalParameter4dvEXT(GLuint program, GLenum target, GLuint index, const GLdouble*params)
{
   GET_CURRENT_CONTEXT(ctx);
   char params_buf[512];
   _mesa_trace_format_array(params_buf, sizeof(params_buf), params, 4, MESA_TRACE_ELEM_DOUBLE);
   _mesa_debug(ctx, "glNamedProgramLocalParameter4dvEXT(%u, %s, %u, %s)\n", program, _mesa_enum_to_string(target), index, params_buf);
   CALL_NamedProgramLocalParameter4dvEXT(ctx->Dispatch.RealPublished, (program, target, index, params));
}

static void GLAPIENTRY
_mesa_trace_GetNamedProgramLocalParameterdvEXT(GLuint program, GLenum target, GLuint index, GLdouble*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedProgramLocalParameterdvEXT(%u, %s, %u, %p)\n", program, _mesa_enum_to_string(target), index, (void *)params);
   CALL_GetNamedProgramLocalParameterdvEXT(ctx->Dispatch.RealPublished, (program, target, index, params));
}

static void GLAPIENTRY
_mesa_trace_GetNamedProgramivEXT(GLuint program, GLenum target, GLenum pname, GLint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedProgramivEXT(%u, %s, %s, %p)\n", program, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedProgramivEXT(ctx->Dispatch.RealPublished, (program, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TextureBufferEXT(GLuint texture, GLenum target, GLenum internalformat, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureBufferEXT(%u, %s, %s, %u)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), buffer);
   CALL_TextureBufferEXT(ctx->Dispatch.RealPublished, (texture, target, internalformat, buffer));
}

static void GLAPIENTRY
_mesa_trace_MultiTexBufferEXT(GLenum texunit, GLenum target, GLenum internalformat, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexBufferEXT(%s, %s, %s, %u)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), buffer);
   CALL_MultiTexBufferEXT(ctx->Dispatch.RealPublished, (texunit, target, internalformat, buffer));
}

static void GLAPIENTRY
_mesa_trace_TextureParameterIivEXT(GLuint texture, GLenum target, GLenum pname, const GLint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameterIivEXT(%u, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TextureParameterIivEXT(ctx->Dispatch.RealPublished, (texture, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_TextureParameterIuivEXT(GLuint texture, GLenum target, GLenum pname, const GLuint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureParameterIuivEXT(%u, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_TextureParameterIuivEXT(ctx->Dispatch.RealPublished, (texture, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureParameterIivEXT(GLuint texture, GLenum target, GLenum pname, GLint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureParameterIivEXT(%u, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureParameterIivEXT(ctx->Dispatch.RealPublished, (texture, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetTextureParameterIuivEXT(GLuint texture, GLenum target, GLenum pname, GLuint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetTextureParameterIuivEXT(%u, %s, %s, %p)\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetTextureParameterIuivEXT(ctx->Dispatch.RealPublished, (texture, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_MultiTexParameterIivEXT(GLenum texunit, GLenum target, GLenum pname, const GLint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexParameterIivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_MultiTexParameterIivEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_MultiTexParameterIuivEXT(GLenum texunit, GLenum target, GLenum pname, const GLuint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexParameterIuivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_MultiTexParameterIuivEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexParameterIivEXT(GLenum texunit, GLenum target, GLenum pname, GLint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexParameterIivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMultiTexParameterIivEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetMultiTexParameterIuivEXT(GLenum texunit, GLenum target, GLenum pname, GLuint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMultiTexParameterIuivEXT(%s, %s, %s, %p)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMultiTexParameterIuivEXT(ctx->Dispatch.RealPublished, (texunit, target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_NamedProgramLocalParameters4fvEXT(GLuint program, GLenum target, GLuint index, GLsizei count, const GLfloat*params)
{
   GET_CURRENT_CONTEXT(ctx);
   char params_buf[512];
   _mesa_trace_format_array(params_buf, sizeof(params_buf), params, (size_t)count * 4, MESA_TRACE_ELEM_FLOAT);
   _mesa_debug(ctx, "glNamedProgramLocalParameters4fvEXT(%u, %s, %u, %d, %s)\n", program, _mesa_enum_to_string(target), index, count, params_buf);
   CALL_NamedProgramLocalParameters4fvEXT(ctx->Dispatch.RealPublished, (program, target, index, count, params));
}

static void GLAPIENTRY
_mesa_trace_GenerateTextureMipmapEXT(GLuint texture, GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenerateTextureMipmapEXT(%u, %s)\n", texture, _mesa_enum_to_string(target));
   CALL_GenerateTextureMipmapEXT(ctx->Dispatch.RealPublished, (texture, target));
}

static void GLAPIENTRY
_mesa_trace_GenerateMultiTexMipmapEXT(GLenum texunit, GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGenerateMultiTexMipmapEXT(%s, %s)\n", _mesa_enum_to_string(texunit), _mesa_enum_to_string(target));
   CALL_GenerateMultiTexMipmapEXT(ctx->Dispatch.RealPublished, (texunit, target));
}

static void GLAPIENTRY
_mesa_trace_NamedRenderbufferStorageMultisampleEXT(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedRenderbufferStorageMultisampleEXT(%u, %d, %s, %d, %d)\n", renderbuffer, samples, _mesa_enum_to_string(internalformat), width, height);
   CALL_NamedRenderbufferStorageMultisampleEXT(ctx->Dispatch.RealPublished, (renderbuffer, samples, internalformat, width, height));
}

static void GLAPIENTRY
_mesa_trace_NamedCopyBufferSubDataEXT(GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedCopyBufferSubDataEXT(%u, %u, %" PRIdPTR ", %" PRIdPTR ", %" PRIdPTR ")\n", readBuffer, writeBuffer, (intptr_t)readOffset, (intptr_t)writeOffset, (intptr_t)size);
   CALL_NamedCopyBufferSubDataEXT(ctx->Dispatch.RealPublished, (readBuffer, writeBuffer, readOffset, writeOffset, size));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexOffsetEXT(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexOffsetEXT(%u, %u, %d, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, size, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArrayVertexOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, size, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayColorOffsetEXT(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayColorOffsetEXT(%u, %u, %d, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, size, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArrayColorOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, size, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayEdgeFlagOffsetEXT(GLuint vaobj, GLuint buffer, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayEdgeFlagOffsetEXT(%u, %u, %d, %" PRIdPTR ")\n", vaobj, buffer, stride, (intptr_t)offset);
   CALL_VertexArrayEdgeFlagOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayIndexOffsetEXT(GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayIndexOffsetEXT(%u, %u, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArrayIndexOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayNormalOffsetEXT(GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayNormalOffsetEXT(%u, %u, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArrayNormalOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayTexCoordOffsetEXT(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayTexCoordOffsetEXT(%u, %u, %d, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, size, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArrayTexCoordOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, size, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayMultiTexCoordOffsetEXT(GLuint vaobj, GLuint buffer, GLenum texunit, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayMultiTexCoordOffsetEXT(%u, %u, %s, %d, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, _mesa_enum_to_string(texunit), size, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArrayMultiTexCoordOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, texunit, size, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayFogCoordOffsetEXT(GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayFogCoordOffsetEXT(%u, %u, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArrayFogCoordOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArraySecondaryColorOffsetEXT(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArraySecondaryColorOffsetEXT(%u, %u, %d, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, size, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArraySecondaryColorOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, size, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexAttribOffsetEXT(GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexAttribOffsetEXT(%u, %u, %u, %d, %s, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, index, size, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", stride, (intptr_t)offset);
   CALL_VertexArrayVertexAttribOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, index, size, type, normalized, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexAttribIOffsetEXT(GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexAttribIOffsetEXT(%u, %u, %u, %d, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, index, size, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArrayVertexAttribIOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, index, size, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_EnableVertexArrayEXT(GLuint vaobj, GLenum array)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEnableVertexArrayEXT(%u, %s)\n", vaobj, _mesa_enum_to_string(array));
   CALL_EnableVertexArrayEXT(ctx->Dispatch.RealPublished, (vaobj, array));
}

static void GLAPIENTRY
_mesa_trace_DisableVertexArrayEXT(GLuint vaobj, GLenum array)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDisableVertexArrayEXT(%u, %s)\n", vaobj, _mesa_enum_to_string(array));
   CALL_DisableVertexArrayEXT(ctx->Dispatch.RealPublished, (vaobj, array));
}

static void GLAPIENTRY
_mesa_trace_EnableVertexArrayAttribEXT(GLuint vaobj, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEnableVertexArrayAttribEXT(%u, %u)\n", vaobj, index);
   CALL_EnableVertexArrayAttribEXT(ctx->Dispatch.RealPublished, (vaobj, index));
}

static void GLAPIENTRY
_mesa_trace_DisableVertexArrayAttribEXT(GLuint vaobj, GLuint index)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDisableVertexArrayAttribEXT(%u, %u)\n", vaobj, index);
   CALL_DisableVertexArrayAttribEXT(ctx->Dispatch.RealPublished, (vaobj, index));
}

static void GLAPIENTRY
_mesa_trace_GetVertexArrayIntegervEXT(GLuint vaobj, GLenum pname, GLint*param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexArrayIntegervEXT(%u, %s, %p)\n", vaobj, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetVertexArrayIntegervEXT(ctx->Dispatch.RealPublished, (vaobj, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetVertexArrayPointervEXT(GLuint vaobj, GLenum pname, GLvoid**param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexArrayPointervEXT(%u, %s, %p)\n", vaobj, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetVertexArrayPointervEXT(ctx->Dispatch.RealPublished, (vaobj, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetVertexArrayIntegeri_vEXT(GLuint vaobj, GLuint index, GLenum pname, GLint*param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexArrayIntegeri_vEXT(%u, %u, %s, %p)\n", vaobj, index, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetVertexArrayIntegeri_vEXT(ctx->Dispatch.RealPublished, (vaobj, index, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetVertexArrayPointeri_vEXT(GLuint vaobj, GLuint index, GLenum pname, GLvoid**param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetVertexArrayPointeri_vEXT(%u, %u, %s, %p)\n", vaobj, index, _mesa_enum_to_string(pname), (void *)param);
   CALL_GetVertexArrayPointeri_vEXT(ctx->Dispatch.RealPublished, (vaobj, index, pname, param));
}

static void GLAPIENTRY
_mesa_trace_ClearNamedBufferDataEXT(GLuint buffer, GLenum internalformat, GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearNamedBufferDataEXT(%u, %s, %s, %s, %p)\n", buffer, _mesa_enum_to_string(internalformat), _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)data);
   CALL_ClearNamedBufferDataEXT(ctx->Dispatch.RealPublished, (buffer, internalformat, format, type, data));
}

static void GLAPIENTRY
_mesa_trace_ClearNamedBufferSubDataEXT(GLuint buffer, GLenum internalformat, GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glClearNamedBufferSubDataEXT(%u, %s, %" PRIdPTR ", %" PRIdPTR ", %s, %s, %p)\n", buffer, _mesa_enum_to_string(internalformat), (intptr_t)offset, (intptr_t)size, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)data);
   CALL_ClearNamedBufferSubDataEXT(ctx->Dispatch.RealPublished, (buffer, internalformat, offset, size, format, type, data));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferParameteriEXT(GLuint framebuffer, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferParameteriEXT(%u, %s, %d)\n", framebuffer, _mesa_enum_to_string(pname), param);
   CALL_NamedFramebufferParameteriEXT(ctx->Dispatch.RealPublished, (framebuffer, pname, param));
}

static void GLAPIENTRY
_mesa_trace_GetNamedFramebufferParameterivEXT(GLuint framebuffer, GLenum pname, GLint*params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedFramebufferParameterivEXT(%u, %s, %p)\n", framebuffer, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedFramebufferParameterivEXT(ctx->Dispatch.RealPublished, (framebuffer, pname, params));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexAttribLOffsetEXT(GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexAttribLOffsetEXT(%u, %u, %u, %d, %s, %d, %" PRIdPTR ")\n", vaobj, buffer, index, size, _mesa_enum_to_string(type), stride, (intptr_t)offset);
   CALL_VertexArrayVertexAttribLOffsetEXT(ctx->Dispatch.RealPublished, (vaobj, buffer, index, size, type, stride, offset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexAttribDivisorEXT(GLuint vaobj, GLuint index, GLuint divisor)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexAttribDivisorEXT(%u, %u, %u)\n", vaobj, index, divisor);
   CALL_VertexArrayVertexAttribDivisorEXT(ctx->Dispatch.RealPublished, (vaobj, index, divisor));
}

static void GLAPIENTRY
_mesa_trace_TextureBufferRangeEXT(GLuint texture, GLenum target, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureBufferRangeEXT(%u, %s, %s, %u, %" PRIdPTR ", %" PRIdPTR ")\n", texture, _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), buffer, (intptr_t)offset, (intptr_t)size);
   CALL_TextureBufferRangeEXT(ctx->Dispatch.RealPublished, (texture, target, internalformat, buffer, offset, size));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage2DMultisampleEXT(GLuint texture, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage2DMultisampleEXT(%u, %s, %d, %s, %d, %d, %s)\n", texture, _mesa_enum_to_string(target), samples, _mesa_enum_to_string(internalformat), width, height, fixedsamplelocations ? "GL_TRUE" : "GL_FALSE");
   CALL_TextureStorage2DMultisampleEXT(ctx->Dispatch.RealPublished, (texture, target, samples, internalformat, width, height, fixedsamplelocations));
}

static void GLAPIENTRY
_mesa_trace_TextureStorage3DMultisampleEXT(GLuint texture, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTextureStorage3DMultisampleEXT(%u, %s, %d, %s, %d, %d, %d, %s)\n", texture, _mesa_enum_to_string(target), samples, _mesa_enum_to_string(internalformat), width, height, depth, fixedsamplelocations ? "GL_TRUE" : "GL_FALSE");
   CALL_TextureStorage3DMultisampleEXT(ctx->Dispatch.RealPublished, (texture, target, samples, internalformat, width, height, depth, fixedsamplelocations));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayBindVertexBufferEXT(GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayBindVertexBufferEXT(%u, %u, %u, %" PRIdPTR ", %d)\n", vaobj, bindingindex, buffer, (intptr_t)offset, stride);
   CALL_VertexArrayBindVertexBufferEXT(ctx->Dispatch.RealPublished, (vaobj, bindingindex, buffer, offset, stride));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexAttribFormatEXT(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexAttribFormatEXT(%u, %u, %d, %s, %s, %u)\n", vaobj, attribindex, size, _mesa_enum_to_string(type), normalized ? "GL_TRUE" : "GL_FALSE", relativeoffset);
   CALL_VertexArrayVertexAttribFormatEXT(ctx->Dispatch.RealPublished, (vaobj, attribindex, size, type, normalized, relativeoffset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexAttribIFormatEXT(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexAttribIFormatEXT(%u, %u, %d, %s, %u)\n", vaobj, attribindex, size, _mesa_enum_to_string(type), relativeoffset);
   CALL_VertexArrayVertexAttribIFormatEXT(ctx->Dispatch.RealPublished, (vaobj, attribindex, size, type, relativeoffset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexAttribLFormatEXT(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexAttribLFormatEXT(%u, %u, %d, %s, %u)\n", vaobj, attribindex, size, _mesa_enum_to_string(type), relativeoffset);
   CALL_VertexArrayVertexAttribLFormatEXT(ctx->Dispatch.RealPublished, (vaobj, attribindex, size, type, relativeoffset));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexAttribBindingEXT(GLuint vaobj, GLuint attribindex, GLuint bindingindex)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexAttribBindingEXT(%u, %u, %u)\n", vaobj, attribindex, bindingindex);
   CALL_VertexArrayVertexAttribBindingEXT(ctx->Dispatch.RealPublished, (vaobj, attribindex, bindingindex));
}

static void GLAPIENTRY
_mesa_trace_VertexArrayVertexBindingDivisorEXT(GLuint vaobj, GLuint bindingindex, GLuint divisor)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexArrayVertexBindingDivisorEXT(%u, %u, %u)\n", vaobj, bindingindex, divisor);
   CALL_VertexArrayVertexBindingDivisorEXT(ctx->Dispatch.RealPublished, (vaobj, bindingindex, divisor));
}

static void GLAPIENTRY
_mesa_trace_NamedBufferPageCommitmentEXT(GLuint buffer, GLintptr offset, GLsizeiptr size, GLboolean commit)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedBufferPageCommitmentEXT(%u, %" PRIdPTR ", %" PRIdPTR ", %s)\n", buffer, (intptr_t)offset, (intptr_t)size, commit ? "GL_TRUE" : "GL_FALSE");
   CALL_NamedBufferPageCommitmentEXT(ctx->Dispatch.RealPublished, (buffer, offset, size, commit));
}

static void GLAPIENTRY
_mesa_trace_NamedStringARB(GLenum type, GLint namelen, const GLchar *name, GLint stringlen, const GLchar *string)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedStringARB(%s, %d, %s, %d, %s)\n", _mesa_enum_to_string(type), namelen, name ? (const char *)name : "(null)", stringlen, string ? (const char *)string : "(null)");
   CALL_NamedStringARB(ctx->Dispatch.RealPublished, (type, namelen, name, stringlen, string));
}

static void GLAPIENTRY
_mesa_trace_DeleteNamedStringARB(GLint namelen, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDeleteNamedStringARB(%d, %s)\n", namelen, name ? (const char *)name : "(null)");
   CALL_DeleteNamedStringARB(ctx->Dispatch.RealPublished, (namelen, name));
}

static void GLAPIENTRY
_mesa_trace_CompileShaderIncludeARB(GLuint shader, GLsizei count, const GLchar * const *path, const GLint *length)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCompileShaderIncludeARB(%u, %d, %s, %p)\n", shader, count, path ? (const char *)path : "(null)", (void *)length);
   CALL_CompileShaderIncludeARB(ctx->Dispatch.RealPublished, (shader, count, path, length));
}

static GLboolean GLAPIENTRY
_mesa_trace_IsNamedStringARB(GLint namelen, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glIsNamedStringARB(%d, %s)\n", namelen, name ? (const char *)name : "(null)");
   return CALL_IsNamedStringARB(ctx->Dispatch.RealPublished, (namelen, name));
}

static void GLAPIENTRY
_mesa_trace_GetNamedStringARB(GLint namelen, const GLchar *name, GLsizei bufSize, GLint *stringlen, GLchar *string)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedStringARB(%d, %s, %d, %p, %p)\n", namelen, name ? (const char *)name : "(null)", bufSize, (void *)stringlen, (void *)string);
   CALL_GetNamedStringARB(ctx->Dispatch.RealPublished, (namelen, name, bufSize, stringlen, string));
}

static void GLAPIENTRY
_mesa_trace_GetNamedStringivARB(GLint namelen, const GLchar *name, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetNamedStringivARB(%d, %s, %s, %p)\n", namelen, name ? (const char *)name : "(null)", _mesa_enum_to_string(pname), (void *)params);
   CALL_GetNamedStringivARB(ctx->Dispatch.RealPublished, (namelen, name, pname, params));
}

static void GLAPIENTRY
_mesa_trace_EGLImageTargetTexStorageEXT(GLenum target, GLvoid *image, const GLint *attrib_list)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEGLImageTargetTexStorageEXT(%s, %p, %p)\n", _mesa_enum_to_string(target), (void *)image, (void *)attrib_list);
   CALL_EGLImageTargetTexStorageEXT(ctx->Dispatch.RealPublished, (target, image, attrib_list));
}

static void GLAPIENTRY
_mesa_trace_EGLImageTargetTextureStorageEXT(GLuint texture, GLvoid *image, const GLint *attrib_list)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glEGLImageTargetTextureStorageEXT(%u, %p, %p)\n", texture, (void *)image, (void *)attrib_list);
   CALL_EGLImageTargetTextureStorageEXT(ctx->Dispatch.RealPublished, (texture, image, attrib_list));
}

static void GLAPIENTRY
_mesa_trace_CopyImageSubDataNV(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei width, GLsizei height, GLsizei depth)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyImageSubDataNV(%u, %s, %d, %d, %d, %d, %u, %s, %d, %d, %d, %d, %d, %d, %d)\n", srcName, _mesa_enum_to_string(srcTarget), srcLevel, srcX, srcY, srcZ, dstName, _mesa_enum_to_string(dstTarget), dstLevel, dstX, dstY, dstZ, width, height, depth);
   CALL_CopyImageSubDataNV(ctx->Dispatch.RealPublished, (srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, width, height, depth));
}

static void GLAPIENTRY
_mesa_trace_ViewportSwizzleNV(GLuint index, GLenum swizzlex, GLenum swizzley, GLenum swizzlez, GLenum swizzlew)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glViewportSwizzleNV(%u, %s, %s, %s, %s)\n", index, _mesa_enum_to_string(swizzlex), _mesa_enum_to_string(swizzley), _mesa_enum_to_string(swizzlez), _mesa_enum_to_string(swizzlew));
   CALL_ViewportSwizzleNV(ctx->Dispatch.RealPublished, (index, swizzlex, swizzley, swizzlez, swizzlew));
}

static void GLAPIENTRY
_mesa_trace_AlphaToCoverageDitherControlNV(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glAlphaToCoverageDitherControlNV(%s)\n", _mesa_enum_to_string(mode));
   CALL_AlphaToCoverageDitherControlNV(ctx->Dispatch.RealPublished, (mode));
}

static void GLAPIENTRY
_mesa_trace_InternalBufferSubDataCopyMESA(GLintptr srcBuffer, GLuint srcOffset, GLuint dstTargetOrName, GLintptr dstOffset, GLsizeiptr size, GLboolean named, GLboolean ext_dsa)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInternalBufferSubDataCopyMESA(%" PRIdPTR ", %u, %u, %" PRIdPTR ", %" PRIdPTR ", %s, %s)\n", (intptr_t)srcBuffer, srcOffset, dstTargetOrName, (intptr_t)dstOffset, (intptr_t)size, named ? "GL_TRUE" : "GL_FALSE", ext_dsa ? "GL_TRUE" : "GL_FALSE");
   CALL_InternalBufferSubDataCopyMESA(ctx->Dispatch.RealPublished, (srcBuffer, srcOffset, dstTargetOrName, dstOffset, size, named, ext_dsa));
}

static void GLAPIENTRY
_mesa_trace_Vertex2hNV(GLhalfNV x, GLhalfNV y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex2hNV(0x%x, 0x%x)\n", x, y);
   CALL_Vertex2hNV(ctx->Dispatch.RealPublished, (x, y));
}

static void GLAPIENTRY
_mesa_trace_Vertex2hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertex2hvNV(%s)\n", v_buf);
   CALL_Vertex2hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex3hNV(GLhalfNV x, GLhalfNV y, GLhalfNV z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex3hNV(0x%x, 0x%x, 0x%x)\n", x, y, z);
   CALL_Vertex3hNV(ctx->Dispatch.RealPublished, (x, y, z));
}

static void GLAPIENTRY
_mesa_trace_Vertex3hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertex3hvNV(%s)\n", v_buf);
   CALL_Vertex3hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Vertex4hNV(GLhalfNV x, GLhalfNV y, GLhalfNV z, GLhalfNV w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertex4hNV(0x%x, 0x%x, 0x%x, 0x%x)\n", x, y, z, w);
   CALL_Vertex4hNV(ctx->Dispatch.RealPublished, (x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_Vertex4hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertex4hvNV(%s)\n", v_buf);
   CALL_Vertex4hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Normal3hNV(GLhalfNV nx, GLhalfNV ny, GLhalfNV nz)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNormal3hNV(0x%x, 0x%x, 0x%x)\n", nx, ny, nz);
   CALL_Normal3hNV(ctx->Dispatch.RealPublished, (nx, ny, nz));
}

static void GLAPIENTRY
_mesa_trace_Normal3hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glNormal3hvNV(%s)\n", v_buf);
   CALL_Normal3hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color3hNV(GLhalfNV red, GLhalfNV green, GLhalfNV blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor3hNV(0x%x, 0x%x, 0x%x)\n", red, green, blue);
   CALL_Color3hNV(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_Color3hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glColor3hvNV(%s)\n", v_buf);
   CALL_Color3hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_Color4hNV(GLhalfNV red, GLhalfNV green, GLhalfNV blue, GLhalfNV alpha)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColor4hNV(0x%x, 0x%x, 0x%x, 0x%x)\n", red, green, blue, alpha);
   CALL_Color4hNV(ctx->Dispatch.RealPublished, (red, green, blue, alpha));
}

static void GLAPIENTRY
_mesa_trace_Color4hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glColor4hvNV(%s)\n", v_buf);
   CALL_Color4hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1hNV(GLhalfNV s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1hNV(0x%x)\n", s);
   CALL_TexCoord1hNV(ctx->Dispatch.RealPublished, (s));
}

static void GLAPIENTRY
_mesa_trace_TexCoord1hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord1hvNV(%p)\n", (void *)v);
   CALL_TexCoord1hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2hNV(GLhalfNV s, GLhalfNV t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord2hNV(0x%x, 0x%x)\n", s, t);
   CALL_TexCoord2hNV(ctx->Dispatch.RealPublished, (s, t));
}

static void GLAPIENTRY
_mesa_trace_TexCoord2hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glTexCoord2hvNV(%s)\n", v_buf);
   CALL_TexCoord2hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3hNV(GLhalfNV s, GLhalfNV t, GLhalfNV r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord3hNV(0x%x, 0x%x, 0x%x)\n", s, t, r);
   CALL_TexCoord3hNV(ctx->Dispatch.RealPublished, (s, t, r));
}

static void GLAPIENTRY
_mesa_trace_TexCoord3hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glTexCoord3hvNV(%s)\n", v_buf);
   CALL_TexCoord3hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4hNV(GLhalfNV s, GLhalfNV t, GLhalfNV r, GLhalfNV q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexCoord4hNV(0x%x, 0x%x, 0x%x, 0x%x)\n", s, t, r, q);
   CALL_TexCoord4hNV(ctx->Dispatch.RealPublished, (s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_TexCoord4hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glTexCoord4hvNV(%s)\n", v_buf);
   CALL_TexCoord4hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1hNV(GLenum target, GLhalfNV s)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1hNV(%s, 0x%x)\n", _mesa_enum_to_string(target), s);
   CALL_MultiTexCoord1hNV(ctx->Dispatch.RealPublished, (target, s));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord1hvNV(GLenum target, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord1hvNV(%s, %p)\n", _mesa_enum_to_string(target), (void *)v);
   CALL_MultiTexCoord1hvNV(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2hNV(GLenum target, GLhalfNV s, GLhalfNV t)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord2hNV(%s, 0x%x, 0x%x)\n", _mesa_enum_to_string(target), s, t);
   CALL_MultiTexCoord2hNV(ctx->Dispatch.RealPublished, (target, s, t));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord2hvNV(GLenum target, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glMultiTexCoord2hvNV(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord2hvNV(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3hNV(GLenum target, GLhalfNV s, GLhalfNV t, GLhalfNV r)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord3hNV(%s, 0x%x, 0x%x, 0x%x)\n", _mesa_enum_to_string(target), s, t, r);
   CALL_MultiTexCoord3hNV(ctx->Dispatch.RealPublished, (target, s, t, r));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord3hvNV(GLenum target, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glMultiTexCoord3hvNV(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord3hvNV(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4hNV(GLenum target, GLhalfNV s, GLhalfNV t, GLhalfNV r, GLhalfNV q)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiTexCoord4hNV(%s, 0x%x, 0x%x, 0x%x, 0x%x)\n", _mesa_enum_to_string(target), s, t, r, q);
   CALL_MultiTexCoord4hNV(ctx->Dispatch.RealPublished, (target, s, t, r, q));
}

static void GLAPIENTRY
_mesa_trace_MultiTexCoord4hvNV(GLenum target, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glMultiTexCoord4hvNV(%s, %s)\n", _mesa_enum_to_string(target), v_buf);
   CALL_MultiTexCoord4hvNV(ctx->Dispatch.RealPublished, (target, v));
}

static void GLAPIENTRY
_mesa_trace_FogCoordhNV(GLhalfNV x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogCoordhNV(0x%x)\n", x);
   CALL_FogCoordhNV(ctx->Dispatch.RealPublished, (x));
}

static void GLAPIENTRY
_mesa_trace_FogCoordhvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFogCoordhvNV(%p)\n", (void *)v);
   CALL_FogCoordhvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3hNV(GLhalfNV red, GLhalfNV green, GLhalfNV blue)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSecondaryColor3hNV(0x%x, 0x%x, 0x%x)\n", red, green, blue);
   CALL_SecondaryColor3hNV(ctx->Dispatch.RealPublished, (red, green, blue));
}

static void GLAPIENTRY
_mesa_trace_SecondaryColor3hvNV(const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glSecondaryColor3hvNV(%s)\n", v_buf);
   CALL_SecondaryColor3hvNV(ctx->Dispatch.RealPublished, (v));
}

static void GLAPIENTRY
_mesa_trace_InternalSetError(GLenum error)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInternalSetError(%s)\n", _mesa_enum_to_string(error));
   CALL_InternalSetError(ctx->Dispatch.RealPublished, (error));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1hNV(GLuint index, GLhalfNV x)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1hNV(%u, 0x%x)\n", index, x);
   CALL_VertexAttrib1hNV(ctx->Dispatch.RealPublished, (index, x));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib1hvNV(GLuint index, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib1hvNV(%u, %p)\n", index, (void *)v);
   CALL_VertexAttrib1hvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2hNV(GLuint index, GLhalfNV x, GLhalfNV y)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib2hNV(%u, 0x%x, 0x%x)\n", index, x, y);
   CALL_VertexAttrib2hNV(ctx->Dispatch.RealPublished, (index, x, y));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib2hvNV(GLuint index, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 2, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertexAttrib2hvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib2hvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3hNV(GLuint index, GLhalfNV x, GLhalfNV y, GLhalfNV z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib3hNV(%u, 0x%x, 0x%x, 0x%x)\n", index, x, y, z);
   CALL_VertexAttrib3hNV(ctx->Dispatch.RealPublished, (index, x, y, z));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib3hvNV(GLuint index, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 3, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertexAttrib3hvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib3hvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4hNV(GLuint index, GLhalfNV x, GLhalfNV y, GLhalfNV z, GLhalfNV w)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glVertexAttrib4hNV(%u, 0x%x, 0x%x, 0x%x, 0x%x)\n", index, x, y, z, w);
   CALL_VertexAttrib4hNV(ctx->Dispatch.RealPublished, (index, x, y, z, w));
}

static void GLAPIENTRY
_mesa_trace_VertexAttrib4hvNV(GLuint index, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, 4, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertexAttrib4hvNV(%u, %s)\n", index, v_buf);
   CALL_VertexAttrib4hvNV(ctx->Dispatch.RealPublished, (index, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs1hvNV(GLuint index, GLsizei n, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertexAttribs1hvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs1hvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs2hvNV(GLuint index, GLsizei n, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 2, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertexAttribs2hvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs2hvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs3hvNV(GLuint index, GLsizei n, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 3, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertexAttribs3hvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs3hvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_VertexAttribs4hvNV(GLuint index, GLsizei n, const GLhalfNV *v)
{
   GET_CURRENT_CONTEXT(ctx);
   char v_buf[512];
   _mesa_trace_format_array(v_buf, sizeof(v_buf), v, (size_t)n * 4, MESA_TRACE_ELEM_HALF);
   _mesa_debug(ctx, "glVertexAttribs4hvNV(%u, %d, %s)\n", index, n, v_buf);
   CALL_VertexAttribs4hvNV(ctx->Dispatch.RealPublished, (index, n, v));
}

static void GLAPIENTRY
_mesa_trace_TexPageCommitmentARB(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLboolean commit)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexPageCommitmentARB(%s, %d, %d, %d, %d, %d, %d, %d, %s)\n", _mesa_enum_to_string(target), level, xoffset, yoffset, zoffset, width, height, depth, commit ? "GL_TRUE" : "GL_FALSE");
   CALL_TexPageCommitmentARB(ctx->Dispatch.RealPublished, (target, level, xoffset, yoffset, zoffset, width, height, depth, commit));
}

static void GLAPIENTRY
_mesa_trace_TexturePageCommitmentEXT(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLboolean commit)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexturePageCommitmentEXT(%u, %d, %d, %d, %d, %d, %d, %d, %s)\n", texture, level, xoffset, yoffset, zoffset, width, height, depth, commit ? "GL_TRUE" : "GL_FALSE");
   CALL_TexturePageCommitmentEXT(ctx->Dispatch.RealPublished, (texture, level, xoffset, yoffset, zoffset, width, height, depth, commit));
}

static void GLAPIENTRY
_mesa_trace_ImportMemoryWin32HandleEXT(GLuint memory, GLuint64 size, GLenum handleType, GLvoid *handle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glImportMemoryWin32HandleEXT(%u, %" PRIu64 ", %s, %p)\n", memory, (uint64_t)size, _mesa_enum_to_string(handleType), (void *)handle);
   CALL_ImportMemoryWin32HandleEXT(ctx->Dispatch.RealPublished, (memory, size, handleType, handle));
}

static void GLAPIENTRY
_mesa_trace_ImportSemaphoreWin32HandleEXT(GLuint semaphore, GLenum handleType, GLvoid *handle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glImportSemaphoreWin32HandleEXT(%u, %s, %p)\n", semaphore, _mesa_enum_to_string(handleType), (void *)handle);
   CALL_ImportSemaphoreWin32HandleEXT(ctx->Dispatch.RealPublished, (semaphore, handleType, handle));
}

static void GLAPIENTRY
_mesa_trace_ImportMemoryWin32NameEXT(GLuint memory, GLuint64 size, GLenum handleType, const GLvoid *name)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glImportMemoryWin32NameEXT(%u, %" PRIu64 ", %s, %p)\n", memory, (uint64_t)size, _mesa_enum_to_string(handleType), (void *)name);
   CALL_ImportMemoryWin32NameEXT(ctx->Dispatch.RealPublished, (memory, size, handleType, name));
}

static void GLAPIENTRY
_mesa_trace_ImportSemaphoreWin32NameEXT(GLuint semaphore, GLenum handleType, const GLvoid *handle)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glImportSemaphoreWin32NameEXT(%u, %s, %p)\n", semaphore, _mesa_enum_to_string(handleType), (void *)handle);
   CALL_ImportSemaphoreWin32NameEXT(ctx->Dispatch.RealPublished, (semaphore, handleType, handle));
}

static void GLAPIENTRY
_mesa_trace_GetObjectLabelEXT(GLenum type, GLuint object, GLsizei bufSize, GLsizei *length, GLchar *label)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetObjectLabelEXT(%s, %u, %d, %p, %p)\n", _mesa_enum_to_string(type), object, bufSize, (void *)length, (void *)label);
   CALL_GetObjectLabelEXT(ctx->Dispatch.RealPublished, (type, object, bufSize, length, label));
}

static void GLAPIENTRY
_mesa_trace_LabelObjectEXT(GLenum type, GLuint object, GLsizei length, const GLchar *label)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glLabelObjectEXT(%s, %u, %d, %s)\n", _mesa_enum_to_string(type), object, length, label ? (const char *)label : "(null)");
   CALL_LabelObjectEXT(ctx->Dispatch.RealPublished, (type, object, length, label));
}

static void GLAPIENTRY
_mesa_trace_DrawArraysUserBuf(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawArraysUserBuf()\n");
   CALL_DrawArraysUserBuf(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_DrawElementsUserBuf(const GLvoid *cmd)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsUserBuf(%p)\n", (void *)cmd);
   CALL_DrawElementsUserBuf(ctx->Dispatch.RealPublished, (cmd));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawArraysUserBuf(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiDrawArraysUserBuf()\n");
   CALL_MultiDrawArraysUserBuf(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_MultiDrawElementsUserBuf(GLintptr indexBuf, GLenum mode, const GLsizei *count, GLenum type, const GLvoid * const *indices, GLsizei primcount, const GLint *basevertex)
{
   GET_CURRENT_CONTEXT(ctx);
   char count_buf[512];
   _mesa_trace_format_array(count_buf, sizeof(count_buf), count, (size_t)primcount, MESA_TRACE_ELEM_INT);
   char basevertex_buf[512];
   _mesa_trace_format_array(basevertex_buf, sizeof(basevertex_buf), basevertex, (size_t)primcount, MESA_TRACE_ELEM_INT);
   _mesa_debug(ctx, "glMultiDrawElementsUserBuf(%" PRIdPTR ", %s, %s, %s, %p, %d, %s)\n", (intptr_t)indexBuf, _mesa_enum_to_string(mode), count_buf, _mesa_enum_to_string(type), (void *)indices, primcount, basevertex_buf);
   CALL_MultiDrawElementsUserBuf(ctx->Dispatch.RealPublished, (indexBuf, mode, count, type, indices, primcount, basevertex));
}

static void GLAPIENTRY
_mesa_trace_DrawArraysInstancedBaseInstanceDrawID(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawArraysInstancedBaseInstanceDrawID()\n");
   CALL_DrawArraysInstancedBaseInstanceDrawID(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_DrawElementsInstancedBaseVertexBaseInstanceDrawID(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instance_count, GLint basevertex, GLuint baseinstance, GLuint drawid)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsInstancedBaseVertexBaseInstanceDrawID(%s, %d, %s, %p, %d, %d, %u, %u)\n", _mesa_enum_to_string(mode), count, _mesa_enum_to_string(type), (void *)indices, instance_count, basevertex, baseinstance, drawid);
   CALL_DrawElementsInstancedBaseVertexBaseInstanceDrawID(ctx->Dispatch.RealPublished, (mode, count, type, indices, instance_count, basevertex, baseinstance, drawid));
}

static void GLAPIENTRY
_mesa_trace_InternalInvalidateFramebufferAncillaryMESA(void)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInternalInvalidateFramebufferAncillaryMESA()\n");
   CALL_InternalInvalidateFramebufferAncillaryMESA(ctx->Dispatch.RealPublished, ());
}

static void GLAPIENTRY
_mesa_trace_InternalReleaseBufferMESA(GLvoid *buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glInternalReleaseBufferMESA(%p)\n", (void *)buffer);
   CALL_InternalReleaseBufferMESA(ctx->Dispatch.RealPublished, (buffer));
}

static void GLAPIENTRY
_mesa_trace_DrawElementsPacked(GLenum mode, GLenum type, GLushort count, GLushort indices)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsPacked(%s, %s, %u, %u)\n", _mesa_enum_to_string(mode), _mesa_enum_to_string(type), count, indices);
   CALL_DrawElementsPacked(ctx->Dispatch.RealPublished, (mode, type, count, indices));
}

static void GLAPIENTRY
_mesa_trace_DrawElementsUserBufPacked(const GLvoid *cmd)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawElementsUserBufPacked(%p)\n", (void *)cmd);
   CALL_DrawElementsUserBufPacked(ctx->Dispatch.RealPublished, (cmd));
}

static void GLAPIENTRY
_mesa_trace_TexStorageAttribs2DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, const GLint *attrib_list)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorageAttribs2DEXT(%s, %d, %s, %d, %d, %p)\n", _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width, height, (void *)attrib_list);
   CALL_TexStorageAttribs2DEXT(ctx->Dispatch.RealPublished, (target, levels, internalFormat, width, height, attrib_list));
}

static void GLAPIENTRY
_mesa_trace_TexStorageAttribs3DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, const GLint *attrib_list)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glTexStorageAttribs3DEXT(%s, %d, %s, %d, %d, %d, %p)\n", _mesa_enum_to_string(target), levels, _mesa_enum_to_string(internalFormat), width, height, depth, (void *)attrib_list);
   CALL_TexStorageAttribs3DEXT(ctx->Dispatch.RealPublished, (target, levels, internalFormat, width, height, depth, attrib_list));
}

static void GLAPIENTRY
_mesa_trace_FramebufferTextureMultiviewOVR(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint baseviewindex, GLsizei numviews)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferTextureMultiviewOVR(%s, %s, %u, %d, %d, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), texture, level, baseviewindex, numviews);
   CALL_FramebufferTextureMultiviewOVR(ctx->Dispatch.RealPublished, (target, attachment, texture, level, baseviewindex, numviews));
}

static void GLAPIENTRY
_mesa_trace_NamedFramebufferTextureMultiviewOVR(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint baseviewindex, GLsizei numviews)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glNamedFramebufferTextureMultiviewOVR(%u, %s, %u, %d, %d, %d)\n", framebuffer, _mesa_enum_to_string(attachment), texture, level, baseviewindex, numviews);
   CALL_NamedFramebufferTextureMultiviewOVR(ctx->Dispatch.RealPublished, (framebuffer, attachment, texture, level, baseviewindex, numviews));
}

static void GLAPIENTRY
_mesa_trace_FramebufferTextureMultisampleMultiviewOVR(GLenum target, GLenum attachment, GLuint texture, GLint level, GLsizei samples, GLint baseviewindex, GLsizei numviews)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glFramebufferTextureMultisampleMultiviewOVR(%s, %s, %u, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(attachment), texture, level, samples, baseviewindex, numviews);
   CALL_FramebufferTextureMultisampleMultiviewOVR(ctx->Dispatch.RealPublished, (target, attachment, texture, level, samples, baseviewindex, numviews));
}

static void GLAPIENTRY
_mesa_trace_CreateSemaphoresNV(GLsizei n, GLuint *semaphores)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCreateSemaphoresNV(%d, %p)\n", n, (void *)semaphores);
   CALL_CreateSemaphoresNV(ctx->Dispatch.RealPublished, (n, semaphores));
}

static void GLAPIENTRY
_mesa_trace_GetSemaphoreParameterivNV(GLuint semaphore, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSemaphoreParameterivNV(%u, %s, %p)\n", semaphore, _mesa_enum_to_string(pname), (void *)params);
   CALL_GetSemaphoreParameterivNV(ctx->Dispatch.RealPublished, (semaphore, pname, params));
}

static void GLAPIENTRY
_mesa_trace_SemaphoreParameterivNV(GLuint semaphore, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSemaphoreParameterivNV(%u, %s, %p)\n", semaphore, _mesa_enum_to_string(pname), (void *)params);
   CALL_SemaphoreParameterivNV(ctx->Dispatch.RealPublished, (semaphore, pname, params));
}

static void GLAPIENTRY
_mesa_trace_DrawMeshTasksEXT(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawMeshTasksEXT(%u, %u, %u)\n", num_groups_x, num_groups_y, num_groups_z);
   CALL_DrawMeshTasksEXT(ctx->Dispatch.RealPublished, (num_groups_x, num_groups_y, num_groups_z));
}

static void GLAPIENTRY
_mesa_trace_DrawMeshTasksIndirectEXT(GLintptr indirect)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glDrawMeshTasksIndirectEXT(%" PRIdPTR ")\n", (intptr_t)indirect);
   CALL_DrawMeshTasksIndirectEXT(ctx->Dispatch.RealPublished, (indirect));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawMeshTasksIndirectEXT(GLintptr indirect, GLsizei drawcount, GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiDrawMeshTasksIndirectEXT(%" PRIdPTR ", %d, %d)\n", (intptr_t)indirect, drawcount, stride);
   CALL_MultiDrawMeshTasksIndirectEXT(ctx->Dispatch.RealPublished, (indirect, drawcount, stride));
}

static void GLAPIENTRY
_mesa_trace_MultiDrawMeshTasksIndirectCountEXT(GLintptr indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMultiDrawMeshTasksIndirectCountEXT(%" PRIdPTR ", %" PRIdPTR ", %d, %d)\n", (intptr_t)indirect, (intptr_t)drawcount, maxdrawcount, stride);
   CALL_MultiDrawMeshTasksIndirectCountEXT(ctx->Dispatch.RealPublished, (indirect, drawcount, maxdrawcount, stride));
}

static void GLAPIENTRY
_mesa_trace_ColorTable(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const GLvoid *table)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorTable(%s, %s, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), width, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)table);
   CALL_ColorTable(ctx->Dispatch.RealPublished, (target, internalformat, width, format, type, table));
}

static void GLAPIENTRY
_mesa_trace_ColorTableParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorTableParameterfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_ColorTableParameterfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_ColorTableParameteriv(GLenum target, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorTableParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_ColorTableParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_CopyColorTable(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyColorTable(%s, %s, %d, %d, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), x, y, width);
   CALL_CopyColorTable(ctx->Dispatch.RealPublished, (target, internalformat, x, y, width));
}

static void GLAPIENTRY
_mesa_trace_GetColorTable(GLenum target, GLenum format, GLenum type, GLvoid *table)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetColorTable(%s, %s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)table);
   CALL_GetColorTable(ctx->Dispatch.RealPublished, (target, format, type, table));
}

static void GLAPIENTRY
_mesa_trace_GetColorTableParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetColorTableParameterfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetColorTableParameterfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetColorTableParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetColorTableParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetColorTableParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_ColorSubTable(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glColorSubTable(%s, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), start, count, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)data);
   CALL_ColorSubTable(ctx->Dispatch.RealPublished, (target, start, count, format, type, data));
}

static void GLAPIENTRY
_mesa_trace_CopyColorSubTable(GLenum target, GLsizei start, GLint x, GLint y, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyColorSubTable(%s, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), start, x, y, width);
   CALL_CopyColorSubTable(ctx->Dispatch.RealPublished, (target, start, x, y, width));
}

static void GLAPIENTRY
_mesa_trace_ConvolutionFilter1D(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const GLvoid *image)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glConvolutionFilter1D(%s, %s, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), width, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)image);
   CALL_ConvolutionFilter1D(ctx->Dispatch.RealPublished, (target, internalformat, width, format, type, image));
}

static void GLAPIENTRY
_mesa_trace_ConvolutionFilter2D(GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *image)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glConvolutionFilter2D(%s, %s, %d, %d, %s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), width, height, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)image);
   CALL_ConvolutionFilter2D(ctx->Dispatch.RealPublished, (target, internalformat, width, height, format, type, image));
}

static void GLAPIENTRY
_mesa_trace_ConvolutionParameterf(GLenum target, GLenum pname, GLfloat params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glConvolutionParameterf(%s, %s, %f)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), params);
   CALL_ConvolutionParameterf(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_ConvolutionParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glConvolutionParameterfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_ConvolutionParameterfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_ConvolutionParameteri(GLenum target, GLenum pname, GLint params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glConvolutionParameteri(%s, %s, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), params);
   CALL_ConvolutionParameteri(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_ConvolutionParameteriv(GLenum target, GLenum pname, const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glConvolutionParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_ConvolutionParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_CopyConvolutionFilter1D(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyConvolutionFilter1D(%s, %s, %d, %d, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), x, y, width);
   CALL_CopyConvolutionFilter1D(ctx->Dispatch.RealPublished, (target, internalformat, x, y, width));
}

static void GLAPIENTRY
_mesa_trace_CopyConvolutionFilter2D(GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glCopyConvolutionFilter2D(%s, %s, %d, %d, %d, %d)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), x, y, width, height);
   CALL_CopyConvolutionFilter2D(ctx->Dispatch.RealPublished, (target, internalformat, x, y, width, height));
}

static void GLAPIENTRY
_mesa_trace_GetConvolutionFilter(GLenum target, GLenum format, GLenum type, GLvoid *image)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetConvolutionFilter(%s, %s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)image);
   CALL_GetConvolutionFilter(ctx->Dispatch.RealPublished, (target, format, type, image));
}

static void GLAPIENTRY
_mesa_trace_GetConvolutionParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetConvolutionParameterfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetConvolutionParameterfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetConvolutionParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetConvolutionParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetConvolutionParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetSeparableFilter(GLenum target, GLenum format, GLenum type, GLvoid *row, GLvoid *column, GLvoid *span)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetSeparableFilter(%s, %s, %s, %p, %p, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)row, (void *)column, (void *)span);
   CALL_GetSeparableFilter(ctx->Dispatch.RealPublished, (target, format, type, row, column, span));
}

static void GLAPIENTRY
_mesa_trace_SeparableFilter2D(GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *row, const GLvoid *column)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glSeparableFilter2D(%s, %s, %d, %d, %s, %s, %p, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), width, height, _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)row, (void *)column);
   CALL_SeparableFilter2D(ctx->Dispatch.RealPublished, (target, internalformat, width, height, format, type, row, column));
}

static void GLAPIENTRY
_mesa_trace_GetHistogram(GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetHistogram(%s, %s, %s, %s, %p)\n", _mesa_enum_to_string(target), reset ? "GL_TRUE" : "GL_FALSE", _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)values);
   CALL_GetHistogram(ctx->Dispatch.RealPublished, (target, reset, format, type, values));
}

static void GLAPIENTRY
_mesa_trace_GetHistogramParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetHistogramParameterfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetHistogramParameterfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetHistogramParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetHistogramParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetHistogramParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetMinmax(GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMinmax(%s, %s, %s, %s, %p)\n", _mesa_enum_to_string(target), reset ? "GL_TRUE" : "GL_FALSE", _mesa_enum_to_string(format), _mesa_enum_to_string(type), (void *)values);
   CALL_GetMinmax(ctx->Dispatch.RealPublished, (target, reset, format, type, values));
}

static void GLAPIENTRY
_mesa_trace_GetMinmaxParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMinmaxParameterfv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMinmaxParameterfv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_GetMinmaxParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetMinmaxParameteriv(%s, %s, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(pname), (void *)params);
   CALL_GetMinmaxParameteriv(ctx->Dispatch.RealPublished, (target, pname, params));
}

static void GLAPIENTRY
_mesa_trace_Histogram(GLenum target, GLsizei width, GLenum internalformat, GLboolean sink)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glHistogram(%s, %d, %s, %s)\n", _mesa_enum_to_string(target), width, _mesa_enum_to_string(internalformat), sink ? "GL_TRUE" : "GL_FALSE");
   CALL_Histogram(ctx->Dispatch.RealPublished, (target, width, internalformat, sink));
}

static void GLAPIENTRY
_mesa_trace_Minmax(GLenum target, GLenum internalformat, GLboolean sink)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glMinmax(%s, %s, %s)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(internalformat), sink ? "GL_TRUE" : "GL_FALSE");
   CALL_Minmax(ctx->Dispatch.RealPublished, (target, internalformat, sink));
}

static void GLAPIENTRY
_mesa_trace_ResetHistogram(GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glResetHistogram(%s)\n", _mesa_enum_to_string(target));
   CALL_ResetHistogram(ctx->Dispatch.RealPublished, (target));
}

static void GLAPIENTRY
_mesa_trace_ResetMinmax(GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glResetMinmax(%s)\n", _mesa_enum_to_string(target));
   CALL_ResetMinmax(ctx->Dispatch.RealPublished, (target));
}

static void GLAPIENTRY
_mesa_trace_GetnColorTableARB(GLenum target, GLenum format, GLenum type, GLsizei bufSize, GLvoid *table)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnColorTableARB(%s, %s, %s, %d, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(format), _mesa_enum_to_string(type), bufSize, (void *)table);
   CALL_GetnColorTableARB(ctx->Dispatch.RealPublished, (target, format, type, bufSize, table));
}

static void GLAPIENTRY
_mesa_trace_GetnConvolutionFilterARB(GLenum target, GLenum format, GLenum type, GLsizei bufSize, GLvoid *image)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnConvolutionFilterARB(%s, %s, %s, %d, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(format), _mesa_enum_to_string(type), bufSize, (void *)image);
   CALL_GetnConvolutionFilterARB(ctx->Dispatch.RealPublished, (target, format, type, bufSize, image));
}

static void GLAPIENTRY
_mesa_trace_GetnHistogramARB(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei bufSize, GLvoid *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnHistogramARB(%s, %s, %s, %s, %d, %p)\n", _mesa_enum_to_string(target), reset ? "GL_TRUE" : "GL_FALSE", _mesa_enum_to_string(format), _mesa_enum_to_string(type), bufSize, (void *)values);
   CALL_GetnHistogramARB(ctx->Dispatch.RealPublished, (target, reset, format, type, bufSize, values));
}

static void GLAPIENTRY
_mesa_trace_GetnMinmaxARB(GLenum target, GLboolean reset, GLenum format, GLenum type, GLsizei bufSize, GLvoid *values)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnMinmaxARB(%s, %s, %s, %s, %d, %p)\n", _mesa_enum_to_string(target), reset ? "GL_TRUE" : "GL_FALSE", _mesa_enum_to_string(format), _mesa_enum_to_string(type), bufSize, (void *)values);
   CALL_GetnMinmaxARB(ctx->Dispatch.RealPublished, (target, reset, format, type, bufSize, values));
}

static void GLAPIENTRY
_mesa_trace_GetnSeparableFilterARB(GLenum target, GLenum format, GLenum type, GLsizei rowBufSize, GLvoid *row, GLsizei columnBufSize, GLvoid *column, GLvoid *span)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_debug(ctx, "glGetnSeparableFilterARB(%s, %s, %s, %d, %p, %d, %p, %p)\n", _mesa_enum_to_string(target), _mesa_enum_to_string(format), _mesa_enum_to_string(type), rowBufSize, (void *)row, columnBufSize, (void *)column, (void *)span);
   CALL_GetnSeparableFilterARB(ctx->Dispatch.RealPublished, (target, format, type, rowBufSize, row, columnBufSize, column, span));
}

bool
_mesa_init_dispatch_trace(struct gl_context *ctx)
{
   struct _glapi_table *table = _mesa_alloc_dispatch_table(false);
   if (!table)
      return false;

   SET_NewList(table, _mesa_trace_NewList);
   SET_EndList(table, _mesa_trace_EndList);
   SET_CallList(table, _mesa_trace_CallList);
   SET_CallLists(table, _mesa_trace_CallLists);
   SET_DeleteLists(table, _mesa_trace_DeleteLists);
   SET_GenLists(table, _mesa_trace_GenLists);
   SET_ListBase(table, _mesa_trace_ListBase);
   SET_Begin(table, _mesa_trace_Begin);
   SET_Bitmap(table, _mesa_trace_Bitmap);
   SET_Color3b(table, _mesa_trace_Color3b);
   SET_Color3bv(table, _mesa_trace_Color3bv);
   SET_Color3d(table, _mesa_trace_Color3d);
   SET_Color3dv(table, _mesa_trace_Color3dv);
   SET_Color3f(table, _mesa_trace_Color3f);
   SET_Color3fv(table, _mesa_trace_Color3fv);
   SET_Color3i(table, _mesa_trace_Color3i);
   SET_Color3iv(table, _mesa_trace_Color3iv);
   SET_Color3s(table, _mesa_trace_Color3s);
   SET_Color3sv(table, _mesa_trace_Color3sv);
   SET_Color3ub(table, _mesa_trace_Color3ub);
   SET_Color3ubv(table, _mesa_trace_Color3ubv);
   SET_Color3ui(table, _mesa_trace_Color3ui);
   SET_Color3uiv(table, _mesa_trace_Color3uiv);
   SET_Color3us(table, _mesa_trace_Color3us);
   SET_Color3usv(table, _mesa_trace_Color3usv);
   SET_Color4b(table, _mesa_trace_Color4b);
   SET_Color4bv(table, _mesa_trace_Color4bv);
   SET_Color4d(table, _mesa_trace_Color4d);
   SET_Color4dv(table, _mesa_trace_Color4dv);
   SET_Color4f(table, _mesa_trace_Color4f);
   SET_Color4fv(table, _mesa_trace_Color4fv);
   SET_Color4i(table, _mesa_trace_Color4i);
   SET_Color4iv(table, _mesa_trace_Color4iv);
   SET_Color4s(table, _mesa_trace_Color4s);
   SET_Color4sv(table, _mesa_trace_Color4sv);
   SET_Color4ub(table, _mesa_trace_Color4ub);
   SET_Color4ubv(table, _mesa_trace_Color4ubv);
   SET_Color4ui(table, _mesa_trace_Color4ui);
   SET_Color4uiv(table, _mesa_trace_Color4uiv);
   SET_Color4us(table, _mesa_trace_Color4us);
   SET_Color4usv(table, _mesa_trace_Color4usv);
   SET_EdgeFlag(table, _mesa_trace_EdgeFlag);
   SET_EdgeFlagv(table, _mesa_trace_EdgeFlagv);
   SET_End(table, _mesa_trace_End);
   SET_Indexd(table, _mesa_trace_Indexd);
   SET_Indexdv(table, _mesa_trace_Indexdv);
   SET_Indexf(table, _mesa_trace_Indexf);
   SET_Indexfv(table, _mesa_trace_Indexfv);
   SET_Indexi(table, _mesa_trace_Indexi);
   SET_Indexiv(table, _mesa_trace_Indexiv);
   SET_Indexs(table, _mesa_trace_Indexs);
   SET_Indexsv(table, _mesa_trace_Indexsv);
   SET_Normal3b(table, _mesa_trace_Normal3b);
   SET_Normal3bv(table, _mesa_trace_Normal3bv);
   SET_Normal3d(table, _mesa_trace_Normal3d);
   SET_Normal3dv(table, _mesa_trace_Normal3dv);
   SET_Normal3f(table, _mesa_trace_Normal3f);
   SET_Normal3fv(table, _mesa_trace_Normal3fv);
   SET_Normal3i(table, _mesa_trace_Normal3i);
   SET_Normal3iv(table, _mesa_trace_Normal3iv);
   SET_Normal3s(table, _mesa_trace_Normal3s);
   SET_Normal3sv(table, _mesa_trace_Normal3sv);
   SET_RasterPos2d(table, _mesa_trace_RasterPos2d);
   SET_RasterPos2dv(table, _mesa_trace_RasterPos2dv);
   SET_RasterPos2f(table, _mesa_trace_RasterPos2f);
   SET_RasterPos2fv(table, _mesa_trace_RasterPos2fv);
   SET_RasterPos2i(table, _mesa_trace_RasterPos2i);
   SET_RasterPos2iv(table, _mesa_trace_RasterPos2iv);
   SET_RasterPos2s(table, _mesa_trace_RasterPos2s);
   SET_RasterPos2sv(table, _mesa_trace_RasterPos2sv);
   SET_RasterPos3d(table, _mesa_trace_RasterPos3d);
   SET_RasterPos3dv(table, _mesa_trace_RasterPos3dv);
   SET_RasterPos3f(table, _mesa_trace_RasterPos3f);
   SET_RasterPos3fv(table, _mesa_trace_RasterPos3fv);
   SET_RasterPos3i(table, _mesa_trace_RasterPos3i);
   SET_RasterPos3iv(table, _mesa_trace_RasterPos3iv);
   SET_RasterPos3s(table, _mesa_trace_RasterPos3s);
   SET_RasterPos3sv(table, _mesa_trace_RasterPos3sv);
   SET_RasterPos4d(table, _mesa_trace_RasterPos4d);
   SET_RasterPos4dv(table, _mesa_trace_RasterPos4dv);
   SET_RasterPos4f(table, _mesa_trace_RasterPos4f);
   SET_RasterPos4fv(table, _mesa_trace_RasterPos4fv);
   SET_RasterPos4i(table, _mesa_trace_RasterPos4i);
   SET_RasterPos4iv(table, _mesa_trace_RasterPos4iv);
   SET_RasterPos4s(table, _mesa_trace_RasterPos4s);
   SET_RasterPos4sv(table, _mesa_trace_RasterPos4sv);
   SET_Rectd(table, _mesa_trace_Rectd);
   SET_Rectdv(table, _mesa_trace_Rectdv);
   SET_Rectf(table, _mesa_trace_Rectf);
   SET_Rectfv(table, _mesa_trace_Rectfv);
   SET_Recti(table, _mesa_trace_Recti);
   SET_Rectiv(table, _mesa_trace_Rectiv);
   SET_Rects(table, _mesa_trace_Rects);
   SET_Rectsv(table, _mesa_trace_Rectsv);
   SET_TexCoord1d(table, _mesa_trace_TexCoord1d);
   SET_TexCoord1dv(table, _mesa_trace_TexCoord1dv);
   SET_TexCoord1f(table, _mesa_trace_TexCoord1f);
   SET_TexCoord1fv(table, _mesa_trace_TexCoord1fv);
   SET_TexCoord1i(table, _mesa_trace_TexCoord1i);
   SET_TexCoord1iv(table, _mesa_trace_TexCoord1iv);
   SET_TexCoord1s(table, _mesa_trace_TexCoord1s);
   SET_TexCoord1sv(table, _mesa_trace_TexCoord1sv);
   SET_TexCoord2d(table, _mesa_trace_TexCoord2d);
   SET_TexCoord2dv(table, _mesa_trace_TexCoord2dv);
   SET_TexCoord2f(table, _mesa_trace_TexCoord2f);
   SET_TexCoord2fv(table, _mesa_trace_TexCoord2fv);
   SET_TexCoord2i(table, _mesa_trace_TexCoord2i);
   SET_TexCoord2iv(table, _mesa_trace_TexCoord2iv);
   SET_TexCoord2s(table, _mesa_trace_TexCoord2s);
   SET_TexCoord2sv(table, _mesa_trace_TexCoord2sv);
   SET_TexCoord3d(table, _mesa_trace_TexCoord3d);
   SET_TexCoord3dv(table, _mesa_trace_TexCoord3dv);
   SET_TexCoord3f(table, _mesa_trace_TexCoord3f);
   SET_TexCoord3fv(table, _mesa_trace_TexCoord3fv);
   SET_TexCoord3i(table, _mesa_trace_TexCoord3i);
   SET_TexCoord3iv(table, _mesa_trace_TexCoord3iv);
   SET_TexCoord3s(table, _mesa_trace_TexCoord3s);
   SET_TexCoord3sv(table, _mesa_trace_TexCoord3sv);
   SET_TexCoord4d(table, _mesa_trace_TexCoord4d);
   SET_TexCoord4dv(table, _mesa_trace_TexCoord4dv);
   SET_TexCoord4f(table, _mesa_trace_TexCoord4f);
   SET_TexCoord4fv(table, _mesa_trace_TexCoord4fv);
   SET_TexCoord4i(table, _mesa_trace_TexCoord4i);
   SET_TexCoord4iv(table, _mesa_trace_TexCoord4iv);
   SET_TexCoord4s(table, _mesa_trace_TexCoord4s);
   SET_TexCoord4sv(table, _mesa_trace_TexCoord4sv);
   SET_Vertex2d(table, _mesa_trace_Vertex2d);
   SET_Vertex2dv(table, _mesa_trace_Vertex2dv);
   SET_Vertex2f(table, _mesa_trace_Vertex2f);
   SET_Vertex2fv(table, _mesa_trace_Vertex2fv);
   SET_Vertex2i(table, _mesa_trace_Vertex2i);
   SET_Vertex2iv(table, _mesa_trace_Vertex2iv);
   SET_Vertex2s(table, _mesa_trace_Vertex2s);
   SET_Vertex2sv(table, _mesa_trace_Vertex2sv);
   SET_Vertex3d(table, _mesa_trace_Vertex3d);
   SET_Vertex3dv(table, _mesa_trace_Vertex3dv);
   SET_Vertex3f(table, _mesa_trace_Vertex3f);
   SET_Vertex3fv(table, _mesa_trace_Vertex3fv);
   SET_Vertex3i(table, _mesa_trace_Vertex3i);
   SET_Vertex3iv(table, _mesa_trace_Vertex3iv);
   SET_Vertex3s(table, _mesa_trace_Vertex3s);
   SET_Vertex3sv(table, _mesa_trace_Vertex3sv);
   SET_Vertex4d(table, _mesa_trace_Vertex4d);
   SET_Vertex4dv(table, _mesa_trace_Vertex4dv);
   SET_Vertex4f(table, _mesa_trace_Vertex4f);
   SET_Vertex4fv(table, _mesa_trace_Vertex4fv);
   SET_Vertex4i(table, _mesa_trace_Vertex4i);
   SET_Vertex4iv(table, _mesa_trace_Vertex4iv);
   SET_Vertex4s(table, _mesa_trace_Vertex4s);
   SET_Vertex4sv(table, _mesa_trace_Vertex4sv);
   SET_ClipPlane(table, _mesa_trace_ClipPlane);
   SET_ColorMaterial(table, _mesa_trace_ColorMaterial);
   SET_CullFace(table, _mesa_trace_CullFace);
   SET_Fogf(table, _mesa_trace_Fogf);
   SET_Fogfv(table, _mesa_trace_Fogfv);
   SET_Fogi(table, _mesa_trace_Fogi);
   SET_Fogiv(table, _mesa_trace_Fogiv);
   SET_FrontFace(table, _mesa_trace_FrontFace);
   SET_Hint(table, _mesa_trace_Hint);
   SET_Lightf(table, _mesa_trace_Lightf);
   SET_Lightfv(table, _mesa_trace_Lightfv);
   SET_Lighti(table, _mesa_trace_Lighti);
   SET_Lightiv(table, _mesa_trace_Lightiv);
   SET_LightModelf(table, _mesa_trace_LightModelf);
   SET_LightModelfv(table, _mesa_trace_LightModelfv);
   SET_LightModeli(table, _mesa_trace_LightModeli);
   SET_LightModeliv(table, _mesa_trace_LightModeliv);
   SET_LineStipple(table, _mesa_trace_LineStipple);
   SET_LineWidth(table, _mesa_trace_LineWidth);
   SET_Materialf(table, _mesa_trace_Materialf);
   SET_Materialfv(table, _mesa_trace_Materialfv);
   SET_Materiali(table, _mesa_trace_Materiali);
   SET_Materialiv(table, _mesa_trace_Materialiv);
   SET_PointSize(table, _mesa_trace_PointSize);
   SET_PolygonMode(table, _mesa_trace_PolygonMode);
   SET_PolygonStipple(table, _mesa_trace_PolygonStipple);
   SET_Scissor(table, _mesa_trace_Scissor);
   SET_ShadeModel(table, _mesa_trace_ShadeModel);
   SET_TexParameterf(table, _mesa_trace_TexParameterf);
   SET_TexParameterfv(table, _mesa_trace_TexParameterfv);
   SET_TexParameteri(table, _mesa_trace_TexParameteri);
   SET_TexParameteriv(table, _mesa_trace_TexParameteriv);
   SET_TexImage1D(table, _mesa_trace_TexImage1D);
   SET_TexImage2D(table, _mesa_trace_TexImage2D);
   SET_TexEnvf(table, _mesa_trace_TexEnvf);
   SET_TexEnvfv(table, _mesa_trace_TexEnvfv);
   SET_TexEnvi(table, _mesa_trace_TexEnvi);
   SET_TexEnviv(table, _mesa_trace_TexEnviv);
   SET_TexGend(table, _mesa_trace_TexGend);
   SET_TexGendv(table, _mesa_trace_TexGendv);
   SET_TexGenf(table, _mesa_trace_TexGenf);
   SET_TexGenfv(table, _mesa_trace_TexGenfv);
   SET_TexGeni(table, _mesa_trace_TexGeni);
   SET_TexGeniv(table, _mesa_trace_TexGeniv);
   SET_FeedbackBuffer(table, _mesa_trace_FeedbackBuffer);
   SET_SelectBuffer(table, _mesa_trace_SelectBuffer);
   SET_RenderMode(table, _mesa_trace_RenderMode);
   SET_InitNames(table, _mesa_trace_InitNames);
   SET_LoadName(table, _mesa_trace_LoadName);
   SET_PassThrough(table, _mesa_trace_PassThrough);
   SET_PopName(table, _mesa_trace_PopName);
   SET_PushName(table, _mesa_trace_PushName);
   SET_DrawBuffer(table, _mesa_trace_DrawBuffer);
   SET_Clear(table, _mesa_trace_Clear);
   SET_ClearAccum(table, _mesa_trace_ClearAccum);
   SET_ClearIndex(table, _mesa_trace_ClearIndex);
   SET_ClearColor(table, _mesa_trace_ClearColor);
   SET_ClearStencil(table, _mesa_trace_ClearStencil);
   SET_ClearDepth(table, _mesa_trace_ClearDepth);
   SET_StencilMask(table, _mesa_trace_StencilMask);
   SET_ColorMask(table, _mesa_trace_ColorMask);
   SET_DepthMask(table, _mesa_trace_DepthMask);
   SET_IndexMask(table, _mesa_trace_IndexMask);
   SET_Accum(table, _mesa_trace_Accum);
   SET_Disable(table, _mesa_trace_Disable);
   SET_Enable(table, _mesa_trace_Enable);
   SET_Finish(table, _mesa_trace_Finish);
   SET_Flush(table, _mesa_trace_Flush);
   SET_PopAttrib(table, _mesa_trace_PopAttrib);
   SET_PushAttrib(table, _mesa_trace_PushAttrib);
   SET_Map1d(table, _mesa_trace_Map1d);
   SET_Map1f(table, _mesa_trace_Map1f);
   SET_Map2d(table, _mesa_trace_Map2d);
   SET_Map2f(table, _mesa_trace_Map2f);
   SET_MapGrid1d(table, _mesa_trace_MapGrid1d);
   SET_MapGrid1f(table, _mesa_trace_MapGrid1f);
   SET_MapGrid2d(table, _mesa_trace_MapGrid2d);
   SET_MapGrid2f(table, _mesa_trace_MapGrid2f);
   SET_EvalCoord1d(table, _mesa_trace_EvalCoord1d);
   SET_EvalCoord1dv(table, _mesa_trace_EvalCoord1dv);
   SET_EvalCoord1f(table, _mesa_trace_EvalCoord1f);
   SET_EvalCoord1fv(table, _mesa_trace_EvalCoord1fv);
   SET_EvalCoord2d(table, _mesa_trace_EvalCoord2d);
   SET_EvalCoord2dv(table, _mesa_trace_EvalCoord2dv);
   SET_EvalCoord2f(table, _mesa_trace_EvalCoord2f);
   SET_EvalCoord2fv(table, _mesa_trace_EvalCoord2fv);
   SET_EvalMesh1(table, _mesa_trace_EvalMesh1);
   SET_EvalPoint1(table, _mesa_trace_EvalPoint1);
   SET_EvalMesh2(table, _mesa_trace_EvalMesh2);
   SET_EvalPoint2(table, _mesa_trace_EvalPoint2);
   SET_AlphaFunc(table, _mesa_trace_AlphaFunc);
   SET_BlendFunc(table, _mesa_trace_BlendFunc);
   SET_LogicOp(table, _mesa_trace_LogicOp);
   SET_StencilFunc(table, _mesa_trace_StencilFunc);
   SET_StencilOp(table, _mesa_trace_StencilOp);
   SET_DepthFunc(table, _mesa_trace_DepthFunc);
   SET_PixelZoom(table, _mesa_trace_PixelZoom);
   SET_PixelTransferf(table, _mesa_trace_PixelTransferf);
   SET_PixelTransferi(table, _mesa_trace_PixelTransferi);
   SET_PixelStoref(table, _mesa_trace_PixelStoref);
   SET_PixelStorei(table, _mesa_trace_PixelStorei);
   SET_PixelMapfv(table, _mesa_trace_PixelMapfv);
   SET_PixelMapuiv(table, _mesa_trace_PixelMapuiv);
   SET_PixelMapusv(table, _mesa_trace_PixelMapusv);
   SET_ReadBuffer(table, _mesa_trace_ReadBuffer);
   SET_CopyPixels(table, _mesa_trace_CopyPixels);
   SET_ReadPixels(table, _mesa_trace_ReadPixels);
   SET_DrawPixels(table, _mesa_trace_DrawPixels);
   SET_GetBooleanv(table, _mesa_trace_GetBooleanv);
   SET_GetClipPlane(table, _mesa_trace_GetClipPlane);
   SET_GetDoublev(table, _mesa_trace_GetDoublev);
   SET_GetError(table, _mesa_trace_GetError);
   SET_GetFloatv(table, _mesa_trace_GetFloatv);
   SET_GetIntegerv(table, _mesa_trace_GetIntegerv);
   SET_GetLightfv(table, _mesa_trace_GetLightfv);
   SET_GetLightiv(table, _mesa_trace_GetLightiv);
   SET_GetMapdv(table, _mesa_trace_GetMapdv);
   SET_GetMapfv(table, _mesa_trace_GetMapfv);
   SET_GetMapiv(table, _mesa_trace_GetMapiv);
   SET_GetMaterialfv(table, _mesa_trace_GetMaterialfv);
   SET_GetMaterialiv(table, _mesa_trace_GetMaterialiv);
   SET_GetPixelMapfv(table, _mesa_trace_GetPixelMapfv);
   SET_GetPixelMapuiv(table, _mesa_trace_GetPixelMapuiv);
   SET_GetPixelMapusv(table, _mesa_trace_GetPixelMapusv);
   SET_GetPolygonStipple(table, _mesa_trace_GetPolygonStipple);
   SET_GetString(table, _mesa_trace_GetString);
   SET_GetTexEnvfv(table, _mesa_trace_GetTexEnvfv);
   SET_GetTexEnviv(table, _mesa_trace_GetTexEnviv);
   SET_GetTexGendv(table, _mesa_trace_GetTexGendv);
   SET_GetTexGenfv(table, _mesa_trace_GetTexGenfv);
   SET_GetTexGeniv(table, _mesa_trace_GetTexGeniv);
   SET_GetTexImage(table, _mesa_trace_GetTexImage);
   SET_GetTexParameterfv(table, _mesa_trace_GetTexParameterfv);
   SET_GetTexParameteriv(table, _mesa_trace_GetTexParameteriv);
   SET_GetTexLevelParameterfv(table, _mesa_trace_GetTexLevelParameterfv);
   SET_GetTexLevelParameteriv(table, _mesa_trace_GetTexLevelParameteriv);
   SET_IsEnabled(table, _mesa_trace_IsEnabled);
   SET_IsList(table, _mesa_trace_IsList);
   SET_DepthRange(table, _mesa_trace_DepthRange);
   SET_Frustum(table, _mesa_trace_Frustum);
   SET_LoadIdentity(table, _mesa_trace_LoadIdentity);
   SET_LoadMatrixf(table, _mesa_trace_LoadMatrixf);
   SET_LoadMatrixd(table, _mesa_trace_LoadMatrixd);
   SET_MatrixMode(table, _mesa_trace_MatrixMode);
   SET_MultMatrixf(table, _mesa_trace_MultMatrixf);
   SET_MultMatrixd(table, _mesa_trace_MultMatrixd);
   SET_Ortho(table, _mesa_trace_Ortho);
   SET_PopMatrix(table, _mesa_trace_PopMatrix);
   SET_PushMatrix(table, _mesa_trace_PushMatrix);
   SET_Rotated(table, _mesa_trace_Rotated);
   SET_Rotatef(table, _mesa_trace_Rotatef);
   SET_Scaled(table, _mesa_trace_Scaled);
   SET_Scalef(table, _mesa_trace_Scalef);
   SET_Translated(table, _mesa_trace_Translated);
   SET_Translatef(table, _mesa_trace_Translatef);
   SET_Viewport(table, _mesa_trace_Viewport);
   SET_ArrayElement(table, _mesa_trace_ArrayElement);
   SET_BindTexture(table, _mesa_trace_BindTexture);
   SET_ColorPointer(table, _mesa_trace_ColorPointer);
   SET_DisableClientState(table, _mesa_trace_DisableClientState);
   SET_DrawArrays(table, _mesa_trace_DrawArrays);
   SET_DrawElements(table, _mesa_trace_DrawElements);
   SET_EdgeFlagPointer(table, _mesa_trace_EdgeFlagPointer);
   SET_EnableClientState(table, _mesa_trace_EnableClientState);
   SET_IndexPointer(table, _mesa_trace_IndexPointer);
   SET_Indexub(table, _mesa_trace_Indexub);
   SET_Indexubv(table, _mesa_trace_Indexubv);
   SET_InterleavedArrays(table, _mesa_trace_InterleavedArrays);
   SET_NormalPointer(table, _mesa_trace_NormalPointer);
   SET_PolygonOffset(table, _mesa_trace_PolygonOffset);
   SET_TexCoordPointer(table, _mesa_trace_TexCoordPointer);
   SET_VertexPointer(table, _mesa_trace_VertexPointer);
   SET_AreTexturesResident(table, _mesa_trace_AreTexturesResident);
   SET_CopyTexImage1D(table, _mesa_trace_CopyTexImage1D);
   SET_CopyTexImage2D(table, _mesa_trace_CopyTexImage2D);
   SET_CopyTexSubImage1D(table, _mesa_trace_CopyTexSubImage1D);
   SET_CopyTexSubImage2D(table, _mesa_trace_CopyTexSubImage2D);
   SET_DeleteTextures(table, _mesa_trace_DeleteTextures);
   SET_GenTextures(table, _mesa_trace_GenTextures);
   SET_GetPointerv(table, _mesa_trace_GetPointerv);
   SET_IsTexture(table, _mesa_trace_IsTexture);
   SET_PrioritizeTextures(table, _mesa_trace_PrioritizeTextures);
   SET_TexSubImage1D(table, _mesa_trace_TexSubImage1D);
   SET_TexSubImage2D(table, _mesa_trace_TexSubImage2D);
   SET_PopClientAttrib(table, _mesa_trace_PopClientAttrib);
   SET_PushClientAttrib(table, _mesa_trace_PushClientAttrib);
   SET_BlendColor(table, _mesa_trace_BlendColor);
   SET_BlendEquation(table, _mesa_trace_BlendEquation);
   SET_DrawRangeElements(table, _mesa_trace_DrawRangeElements);
   SET_TexImage3D(table, _mesa_trace_TexImage3D);
   SET_TexSubImage3D(table, _mesa_trace_TexSubImage3D);
   SET_CopyTexSubImage3D(table, _mesa_trace_CopyTexSubImage3D);
   SET_ActiveTexture(table, _mesa_trace_ActiveTexture);
   SET_ClientActiveTexture(table, _mesa_trace_ClientActiveTexture);
   SET_MultiTexCoord1d(table, _mesa_trace_MultiTexCoord1d);
   SET_MultiTexCoord1dv(table, _mesa_trace_MultiTexCoord1dv);
   SET_MultiTexCoord1fARB(table, _mesa_trace_MultiTexCoord1fARB);
   SET_MultiTexCoord1fvARB(table, _mesa_trace_MultiTexCoord1fvARB);
   SET_MultiTexCoord1i(table, _mesa_trace_MultiTexCoord1i);
   SET_MultiTexCoord1iv(table, _mesa_trace_MultiTexCoord1iv);
   SET_MultiTexCoord1s(table, _mesa_trace_MultiTexCoord1s);
   SET_MultiTexCoord1sv(table, _mesa_trace_MultiTexCoord1sv);
   SET_MultiTexCoord2d(table, _mesa_trace_MultiTexCoord2d);
   SET_MultiTexCoord2dv(table, _mesa_trace_MultiTexCoord2dv);
   SET_MultiTexCoord2fARB(table, _mesa_trace_MultiTexCoord2fARB);
   SET_MultiTexCoord2fvARB(table, _mesa_trace_MultiTexCoord2fvARB);
   SET_MultiTexCoord2i(table, _mesa_trace_MultiTexCoord2i);
   SET_MultiTexCoord2iv(table, _mesa_trace_MultiTexCoord2iv);
   SET_MultiTexCoord2s(table, _mesa_trace_MultiTexCoord2s);
   SET_MultiTexCoord2sv(table, _mesa_trace_MultiTexCoord2sv);
   SET_MultiTexCoord3d(table, _mesa_trace_MultiTexCoord3d);
   SET_MultiTexCoord3dv(table, _mesa_trace_MultiTexCoord3dv);
   SET_MultiTexCoord3fARB(table, _mesa_trace_MultiTexCoord3fARB);
   SET_MultiTexCoord3fvARB(table, _mesa_trace_MultiTexCoord3fvARB);
   SET_MultiTexCoord3i(table, _mesa_trace_MultiTexCoord3i);
   SET_MultiTexCoord3iv(table, _mesa_trace_MultiTexCoord3iv);
   SET_MultiTexCoord3s(table, _mesa_trace_MultiTexCoord3s);
   SET_MultiTexCoord3sv(table, _mesa_trace_MultiTexCoord3sv);
   SET_MultiTexCoord4d(table, _mesa_trace_MultiTexCoord4d);
   SET_MultiTexCoord4dv(table, _mesa_trace_MultiTexCoord4dv);
   SET_MultiTexCoord4fARB(table, _mesa_trace_MultiTexCoord4fARB);
   SET_MultiTexCoord4fvARB(table, _mesa_trace_MultiTexCoord4fvARB);
   SET_MultiTexCoord4i(table, _mesa_trace_MultiTexCoord4i);
   SET_MultiTexCoord4iv(table, _mesa_trace_MultiTexCoord4iv);
   SET_MultiTexCoord4s(table, _mesa_trace_MultiTexCoord4s);
   SET_MultiTexCoord4sv(table, _mesa_trace_MultiTexCoord4sv);
   SET_CompressedTexImage1D(table, _mesa_trace_CompressedTexImage1D);
   SET_CompressedTexImage2D(table, _mesa_trace_CompressedTexImage2D);
   SET_CompressedTexImage3D(table, _mesa_trace_CompressedTexImage3D);
   SET_CompressedTexSubImage1D(table, _mesa_trace_CompressedTexSubImage1D);
   SET_CompressedTexSubImage2D(table, _mesa_trace_CompressedTexSubImage2D);
   SET_CompressedTexSubImage3D(table, _mesa_trace_CompressedTexSubImage3D);
   SET_GetCompressedTexImage(table, _mesa_trace_GetCompressedTexImage);
   SET_LoadTransposeMatrixd(table, _mesa_trace_LoadTransposeMatrixd);
   SET_LoadTransposeMatrixf(table, _mesa_trace_LoadTransposeMatrixf);
   SET_MultTransposeMatrixd(table, _mesa_trace_MultTransposeMatrixd);
   SET_MultTransposeMatrixf(table, _mesa_trace_MultTransposeMatrixf);
   SET_SampleCoverage(table, _mesa_trace_SampleCoverage);
   SET_BlendFuncSeparate(table, _mesa_trace_BlendFuncSeparate);
   SET_FogCoordPointer(table, _mesa_trace_FogCoordPointer);
   SET_FogCoordd(table, _mesa_trace_FogCoordd);
   SET_FogCoorddv(table, _mesa_trace_FogCoorddv);
   SET_MultiDrawArrays(table, _mesa_trace_MultiDrawArrays);
   SET_PointParameterf(table, _mesa_trace_PointParameterf);
   SET_PointParameterfv(table, _mesa_trace_PointParameterfv);
   SET_PointParameteri(table, _mesa_trace_PointParameteri);
   SET_PointParameteriv(table, _mesa_trace_PointParameteriv);
   SET_SecondaryColor3b(table, _mesa_trace_SecondaryColor3b);
   SET_SecondaryColor3bv(table, _mesa_trace_SecondaryColor3bv);
   SET_SecondaryColor3d(table, _mesa_trace_SecondaryColor3d);
   SET_SecondaryColor3dv(table, _mesa_trace_SecondaryColor3dv);
   SET_SecondaryColor3i(table, _mesa_trace_SecondaryColor3i);
   SET_SecondaryColor3iv(table, _mesa_trace_SecondaryColor3iv);
   SET_SecondaryColor3s(table, _mesa_trace_SecondaryColor3s);
   SET_SecondaryColor3sv(table, _mesa_trace_SecondaryColor3sv);
   SET_SecondaryColor3ub(table, _mesa_trace_SecondaryColor3ub);
   SET_SecondaryColor3ubv(table, _mesa_trace_SecondaryColor3ubv);
   SET_SecondaryColor3ui(table, _mesa_trace_SecondaryColor3ui);
   SET_SecondaryColor3uiv(table, _mesa_trace_SecondaryColor3uiv);
   SET_SecondaryColor3us(table, _mesa_trace_SecondaryColor3us);
   SET_SecondaryColor3usv(table, _mesa_trace_SecondaryColor3usv);
   SET_SecondaryColorPointer(table, _mesa_trace_SecondaryColorPointer);
   SET_WindowPos2d(table, _mesa_trace_WindowPos2d);
   SET_WindowPos2dv(table, _mesa_trace_WindowPos2dv);
   SET_WindowPos2f(table, _mesa_trace_WindowPos2f);
   SET_WindowPos2fv(table, _mesa_trace_WindowPos2fv);
   SET_WindowPos2i(table, _mesa_trace_WindowPos2i);
   SET_WindowPos2iv(table, _mesa_trace_WindowPos2iv);
   SET_WindowPos2s(table, _mesa_trace_WindowPos2s);
   SET_WindowPos2sv(table, _mesa_trace_WindowPos2sv);
   SET_WindowPos3d(table, _mesa_trace_WindowPos3d);
   SET_WindowPos3dv(table, _mesa_trace_WindowPos3dv);
   SET_WindowPos3f(table, _mesa_trace_WindowPos3f);
   SET_WindowPos3fv(table, _mesa_trace_WindowPos3fv);
   SET_WindowPos3i(table, _mesa_trace_WindowPos3i);
   SET_WindowPos3iv(table, _mesa_trace_WindowPos3iv);
   SET_WindowPos3s(table, _mesa_trace_WindowPos3s);
   SET_WindowPos3sv(table, _mesa_trace_WindowPos3sv);
   SET_BeginQuery(table, _mesa_trace_BeginQuery);
   SET_BindBuffer(table, _mesa_trace_BindBuffer);
   SET_BufferData(table, _mesa_trace_BufferData);
   SET_BufferSubData(table, _mesa_trace_BufferSubData);
   SET_DeleteBuffers(table, _mesa_trace_DeleteBuffers);
   SET_DeleteQueries(table, _mesa_trace_DeleteQueries);
   SET_EndQuery(table, _mesa_trace_EndQuery);
   SET_GenBuffers(table, _mesa_trace_GenBuffers);
   SET_GenQueries(table, _mesa_trace_GenQueries);
   SET_GetBufferParameteriv(table, _mesa_trace_GetBufferParameteriv);
   SET_GetBufferPointerv(table, _mesa_trace_GetBufferPointerv);
   SET_GetBufferSubData(table, _mesa_trace_GetBufferSubData);
   SET_GetQueryObjectiv(table, _mesa_trace_GetQueryObjectiv);
   SET_GetQueryObjectuiv(table, _mesa_trace_GetQueryObjectuiv);
   SET_GetQueryiv(table, _mesa_trace_GetQueryiv);
   SET_IsBuffer(table, _mesa_trace_IsBuffer);
   SET_IsQuery(table, _mesa_trace_IsQuery);
   SET_MapBuffer(table, _mesa_trace_MapBuffer);
   SET_UnmapBuffer(table, _mesa_trace_UnmapBuffer);
   SET_AttachShader(table, _mesa_trace_AttachShader);
   SET_BindAttribLocation(table, _mesa_trace_BindAttribLocation);
   SET_BlendEquationSeparate(table, _mesa_trace_BlendEquationSeparate);
   SET_CompileShader(table, _mesa_trace_CompileShader);
   SET_CreateProgram(table, _mesa_trace_CreateProgram);
   SET_CreateShader(table, _mesa_trace_CreateShader);
   SET_DeleteProgram(table, _mesa_trace_DeleteProgram);
   SET_DeleteShader(table, _mesa_trace_DeleteShader);
   SET_DetachShader(table, _mesa_trace_DetachShader);
   SET_DisableVertexAttribArray(table, _mesa_trace_DisableVertexAttribArray);
   SET_DrawBuffers(table, _mesa_trace_DrawBuffers);
   SET_EnableVertexAttribArray(table, _mesa_trace_EnableVertexAttribArray);
   SET_GetActiveAttrib(table, _mesa_trace_GetActiveAttrib);
   SET_GetActiveUniform(table, _mesa_trace_GetActiveUniform);
   SET_GetAttachedShaders(table, _mesa_trace_GetAttachedShaders);
   SET_GetAttribLocation(table, _mesa_trace_GetAttribLocation);
   SET_GetProgramInfoLog(table, _mesa_trace_GetProgramInfoLog);
   SET_GetProgramiv(table, _mesa_trace_GetProgramiv);
   SET_GetShaderInfoLog(table, _mesa_trace_GetShaderInfoLog);
   SET_GetShaderSource(table, _mesa_trace_GetShaderSource);
   SET_GetShaderiv(table, _mesa_trace_GetShaderiv);
   SET_GetUniformLocation(table, _mesa_trace_GetUniformLocation);
   SET_GetUniformfv(table, _mesa_trace_GetUniformfv);
   SET_GetUniformiv(table, _mesa_trace_GetUniformiv);
   SET_GetVertexAttribPointerv(table, _mesa_trace_GetVertexAttribPointerv);
   SET_GetVertexAttribdv(table, _mesa_trace_GetVertexAttribdv);
   SET_GetVertexAttribfv(table, _mesa_trace_GetVertexAttribfv);
   SET_GetVertexAttribiv(table, _mesa_trace_GetVertexAttribiv);
   SET_IsProgram(table, _mesa_trace_IsProgram);
   SET_IsShader(table, _mesa_trace_IsShader);
   SET_LinkProgram(table, _mesa_trace_LinkProgram);
   SET_ShaderSource(table, _mesa_trace_ShaderSource);
   SET_StencilFuncSeparate(table, _mesa_trace_StencilFuncSeparate);
   SET_StencilMaskSeparate(table, _mesa_trace_StencilMaskSeparate);
   SET_StencilOpSeparate(table, _mesa_trace_StencilOpSeparate);
   SET_Uniform1f(table, _mesa_trace_Uniform1f);
   SET_Uniform1fv(table, _mesa_trace_Uniform1fv);
   SET_Uniform1i(table, _mesa_trace_Uniform1i);
   SET_Uniform1iv(table, _mesa_trace_Uniform1iv);
   SET_Uniform2f(table, _mesa_trace_Uniform2f);
   SET_Uniform2fv(table, _mesa_trace_Uniform2fv);
   SET_Uniform2i(table, _mesa_trace_Uniform2i);
   SET_Uniform2iv(table, _mesa_trace_Uniform2iv);
   SET_Uniform3f(table, _mesa_trace_Uniform3f);
   SET_Uniform3fv(table, _mesa_trace_Uniform3fv);
   SET_Uniform3i(table, _mesa_trace_Uniform3i);
   SET_Uniform3iv(table, _mesa_trace_Uniform3iv);
   SET_Uniform4f(table, _mesa_trace_Uniform4f);
   SET_Uniform4fv(table, _mesa_trace_Uniform4fv);
   SET_Uniform4i(table, _mesa_trace_Uniform4i);
   SET_Uniform4iv(table, _mesa_trace_Uniform4iv);
   SET_UniformMatrix2fv(table, _mesa_trace_UniformMatrix2fv);
   SET_UniformMatrix3fv(table, _mesa_trace_UniformMatrix3fv);
   SET_UniformMatrix4fv(table, _mesa_trace_UniformMatrix4fv);
   SET_UseProgram(table, _mesa_trace_UseProgram);
   SET_ValidateProgram(table, _mesa_trace_ValidateProgram);
   SET_VertexAttrib1d(table, _mesa_trace_VertexAttrib1d);
   SET_VertexAttrib1dv(table, _mesa_trace_VertexAttrib1dv);
   SET_VertexAttrib1s(table, _mesa_trace_VertexAttrib1s);
   SET_VertexAttrib1sv(table, _mesa_trace_VertexAttrib1sv);
   SET_VertexAttrib2d(table, _mesa_trace_VertexAttrib2d);
   SET_VertexAttrib2dv(table, _mesa_trace_VertexAttrib2dv);
   SET_VertexAttrib2s(table, _mesa_trace_VertexAttrib2s);
   SET_VertexAttrib2sv(table, _mesa_trace_VertexAttrib2sv);
   SET_VertexAttrib3d(table, _mesa_trace_VertexAttrib3d);
   SET_VertexAttrib3dv(table, _mesa_trace_VertexAttrib3dv);
   SET_VertexAttrib3s(table, _mesa_trace_VertexAttrib3s);
   SET_VertexAttrib3sv(table, _mesa_trace_VertexAttrib3sv);
   SET_VertexAttrib4Nbv(table, _mesa_trace_VertexAttrib4Nbv);
   SET_VertexAttrib4Niv(table, _mesa_trace_VertexAttrib4Niv);
   SET_VertexAttrib4Nsv(table, _mesa_trace_VertexAttrib4Nsv);
   SET_VertexAttrib4Nub(table, _mesa_trace_VertexAttrib4Nub);
   SET_VertexAttrib4Nubv(table, _mesa_trace_VertexAttrib4Nubv);
   SET_VertexAttrib4Nuiv(table, _mesa_trace_VertexAttrib4Nuiv);
   SET_VertexAttrib4Nusv(table, _mesa_trace_VertexAttrib4Nusv);
   SET_VertexAttrib4bv(table, _mesa_trace_VertexAttrib4bv);
   SET_VertexAttrib4d(table, _mesa_trace_VertexAttrib4d);
   SET_VertexAttrib4dv(table, _mesa_trace_VertexAttrib4dv);
   SET_VertexAttrib4iv(table, _mesa_trace_VertexAttrib4iv);
   SET_VertexAttrib4s(table, _mesa_trace_VertexAttrib4s);
   SET_VertexAttrib4sv(table, _mesa_trace_VertexAttrib4sv);
   SET_VertexAttrib4ubv(table, _mesa_trace_VertexAttrib4ubv);
   SET_VertexAttrib4uiv(table, _mesa_trace_VertexAttrib4uiv);
   SET_VertexAttrib4usv(table, _mesa_trace_VertexAttrib4usv);
   SET_VertexAttribPointer(table, _mesa_trace_VertexAttribPointer);
   SET_UniformMatrix2x3fv(table, _mesa_trace_UniformMatrix2x3fv);
   SET_UniformMatrix2x4fv(table, _mesa_trace_UniformMatrix2x4fv);
   SET_UniformMatrix3x2fv(table, _mesa_trace_UniformMatrix3x2fv);
   SET_UniformMatrix3x4fv(table, _mesa_trace_UniformMatrix3x4fv);
   SET_UniformMatrix4x2fv(table, _mesa_trace_UniformMatrix4x2fv);
   SET_UniformMatrix4x3fv(table, _mesa_trace_UniformMatrix4x3fv);
   SET_BeginConditionalRender(table, _mesa_trace_BeginConditionalRender);
   SET_BeginTransformFeedback(table, _mesa_trace_BeginTransformFeedback);
   SET_BindBufferBase(table, _mesa_trace_BindBufferBase);
   SET_BindBufferRange(table, _mesa_trace_BindBufferRange);
   SET_BindFragDataLocation(table, _mesa_trace_BindFragDataLocation);
   SET_ClampColor(table, _mesa_trace_ClampColor);
   SET_ClearBufferfi(table, _mesa_trace_ClearBufferfi);
   SET_ClearBufferfv(table, _mesa_trace_ClearBufferfv);
   SET_ClearBufferiv(table, _mesa_trace_ClearBufferiv);
   SET_ClearBufferuiv(table, _mesa_trace_ClearBufferuiv);
   SET_ColorMaski(table, _mesa_trace_ColorMaski);
   SET_Disablei(table, _mesa_trace_Disablei);
   SET_Enablei(table, _mesa_trace_Enablei);
   SET_EndConditionalRender(table, _mesa_trace_EndConditionalRender);
   SET_EndTransformFeedback(table, _mesa_trace_EndTransformFeedback);
   SET_GetBooleani_v(table, _mesa_trace_GetBooleani_v);
   SET_GetFragDataLocation(table, _mesa_trace_GetFragDataLocation);
   SET_GetIntegeri_v(table, _mesa_trace_GetIntegeri_v);
   SET_GetStringi(table, _mesa_trace_GetStringi);
   SET_GetTexParameterIiv(table, _mesa_trace_GetTexParameterIiv);
   SET_GetTexParameterIuiv(table, _mesa_trace_GetTexParameterIuiv);
   SET_GetTransformFeedbackVarying(table, _mesa_trace_GetTransformFeedbackVarying);
   SET_GetUniformuiv(table, _mesa_trace_GetUniformuiv);
   SET_GetVertexAttribIiv(table, _mesa_trace_GetVertexAttribIiv);
   SET_GetVertexAttribIuiv(table, _mesa_trace_GetVertexAttribIuiv);
   SET_IsEnabledi(table, _mesa_trace_IsEnabledi);
   SET_TexParameterIiv(table, _mesa_trace_TexParameterIiv);
   SET_TexParameterIuiv(table, _mesa_trace_TexParameterIuiv);
   SET_TransformFeedbackVaryings(table, _mesa_trace_TransformFeedbackVaryings);
   SET_Uniform1ui(table, _mesa_trace_Uniform1ui);
   SET_Uniform1uiv(table, _mesa_trace_Uniform1uiv);
   SET_Uniform2ui(table, _mesa_trace_Uniform2ui);
   SET_Uniform2uiv(table, _mesa_trace_Uniform2uiv);
   SET_Uniform3ui(table, _mesa_trace_Uniform3ui);
   SET_Uniform3uiv(table, _mesa_trace_Uniform3uiv);
   SET_Uniform4ui(table, _mesa_trace_Uniform4ui);
   SET_Uniform4uiv(table, _mesa_trace_Uniform4uiv);
   SET_VertexAttribI1iv(table, _mesa_trace_VertexAttribI1iv);
   SET_VertexAttribI1uiv(table, _mesa_trace_VertexAttribI1uiv);
   SET_VertexAttribI4bv(table, _mesa_trace_VertexAttribI4bv);
   SET_VertexAttribI4sv(table, _mesa_trace_VertexAttribI4sv);
   SET_VertexAttribI4ubv(table, _mesa_trace_VertexAttribI4ubv);
   SET_VertexAttribI4usv(table, _mesa_trace_VertexAttribI4usv);
   SET_VertexAttribIPointer(table, _mesa_trace_VertexAttribIPointer);
   SET_PrimitiveRestartIndex(table, _mesa_trace_PrimitiveRestartIndex);
   SET_TexBuffer(table, _mesa_trace_TexBuffer);
   SET_FramebufferTexture(table, _mesa_trace_FramebufferTexture);
   SET_GetBufferParameteri64v(table, _mesa_trace_GetBufferParameteri64v);
   SET_GetInteger64i_v(table, _mesa_trace_GetInteger64i_v);
   SET_VertexAttribDivisor(table, _mesa_trace_VertexAttribDivisor);
   SET_MinSampleShading(table, _mesa_trace_MinSampleShading);
   SET_MemoryBarrierByRegion(table, _mesa_trace_MemoryBarrierByRegion);
   SET_BindProgramARB(table, _mesa_trace_BindProgramARB);
   SET_DeleteProgramsARB(table, _mesa_trace_DeleteProgramsARB);
   SET_GenProgramsARB(table, _mesa_trace_GenProgramsARB);
   SET_GetProgramEnvParameterdvARB(table, _mesa_trace_GetProgramEnvParameterdvARB);
   SET_GetProgramEnvParameterfvARB(table, _mesa_trace_GetProgramEnvParameterfvARB);
   SET_GetProgramLocalParameterdvARB(table, _mesa_trace_GetProgramLocalParameterdvARB);
   SET_GetProgramLocalParameterfvARB(table, _mesa_trace_GetProgramLocalParameterfvARB);
   SET_GetProgramStringARB(table, _mesa_trace_GetProgramStringARB);
   SET_GetProgramivARB(table, _mesa_trace_GetProgramivARB);
   SET_IsProgramARB(table, _mesa_trace_IsProgramARB);
   SET_ProgramEnvParameter4dARB(table, _mesa_trace_ProgramEnvParameter4dARB);
   SET_ProgramEnvParameter4dvARB(table, _mesa_trace_ProgramEnvParameter4dvARB);
   SET_ProgramEnvParameter4fARB(table, _mesa_trace_ProgramEnvParameter4fARB);
   SET_ProgramEnvParameter4fvARB(table, _mesa_trace_ProgramEnvParameter4fvARB);
   SET_ProgramLocalParameter4dARB(table, _mesa_trace_ProgramLocalParameter4dARB);
   SET_ProgramLocalParameter4dvARB(table, _mesa_trace_ProgramLocalParameter4dvARB);
   SET_ProgramLocalParameter4fARB(table, _mesa_trace_ProgramLocalParameter4fARB);
   SET_ProgramLocalParameter4fvARB(table, _mesa_trace_ProgramLocalParameter4fvARB);
   SET_ProgramStringARB(table, _mesa_trace_ProgramStringARB);
   SET_VertexAttrib1fARB(table, _mesa_trace_VertexAttrib1fARB);
   SET_VertexAttrib1fvARB(table, _mesa_trace_VertexAttrib1fvARB);
   SET_VertexAttrib2fARB(table, _mesa_trace_VertexAttrib2fARB);
   SET_VertexAttrib2fvARB(table, _mesa_trace_VertexAttrib2fvARB);
   SET_VertexAttrib3fARB(table, _mesa_trace_VertexAttrib3fARB);
   SET_VertexAttrib3fvARB(table, _mesa_trace_VertexAttrib3fvARB);
   SET_VertexAttrib4fARB(table, _mesa_trace_VertexAttrib4fARB);
   SET_VertexAttrib4fvARB(table, _mesa_trace_VertexAttrib4fvARB);
   SET_AttachObjectARB(table, _mesa_trace_AttachObjectARB);
   SET_CreateProgramObjectARB(table, _mesa_trace_CreateProgramObjectARB);
   SET_CreateShaderObjectARB(table, _mesa_trace_CreateShaderObjectARB);
   SET_DeleteObjectARB(table, _mesa_trace_DeleteObjectARB);
   SET_DetachObjectARB(table, _mesa_trace_DetachObjectARB);
   SET_GetAttachedObjectsARB(table, _mesa_trace_GetAttachedObjectsARB);
   SET_GetHandleARB(table, _mesa_trace_GetHandleARB);
   SET_GetInfoLogARB(table, _mesa_trace_GetInfoLogARB);
   SET_GetObjectParameterfvARB(table, _mesa_trace_GetObjectParameterfvARB);
   SET_GetObjectParameterivARB(table, _mesa_trace_GetObjectParameterivARB);
   SET_DrawArraysInstanced(table, _mesa_trace_DrawArraysInstanced);
   SET_DrawElementsInstanced(table, _mesa_trace_DrawElementsInstanced);
   SET_BindFramebuffer(table, _mesa_trace_BindFramebuffer);
   SET_BindRenderbuffer(table, _mesa_trace_BindRenderbuffer);
   SET_BlitFramebuffer(table, _mesa_trace_BlitFramebuffer);
   SET_CheckFramebufferStatus(table, _mesa_trace_CheckFramebufferStatus);
   SET_DeleteFramebuffers(table, _mesa_trace_DeleteFramebuffers);
   SET_DeleteRenderbuffers(table, _mesa_trace_DeleteRenderbuffers);
   SET_FramebufferRenderbuffer(table, _mesa_trace_FramebufferRenderbuffer);
   SET_FramebufferTexture1D(table, _mesa_trace_FramebufferTexture1D);
   SET_FramebufferTexture2D(table, _mesa_trace_FramebufferTexture2D);
   SET_FramebufferTexture3D(table, _mesa_trace_FramebufferTexture3D);
   SET_FramebufferTextureLayer(table, _mesa_trace_FramebufferTextureLayer);
   SET_GenFramebuffers(table, _mesa_trace_GenFramebuffers);
   SET_GenRenderbuffers(table, _mesa_trace_GenRenderbuffers);
   SET_GenerateMipmap(table, _mesa_trace_GenerateMipmap);
   SET_GetFramebufferAttachmentParameteriv(table, _mesa_trace_GetFramebufferAttachmentParameteriv);
   SET_GetRenderbufferParameteriv(table, _mesa_trace_GetRenderbufferParameteriv);
   SET_IsFramebuffer(table, _mesa_trace_IsFramebuffer);
   SET_IsRenderbuffer(table, _mesa_trace_IsRenderbuffer);
   SET_RenderbufferStorage(table, _mesa_trace_RenderbufferStorage);
   SET_RenderbufferStorageMultisample(table, _mesa_trace_RenderbufferStorageMultisample);
   SET_FlushMappedBufferRange(table, _mesa_trace_FlushMappedBufferRange);
   SET_MapBufferRange(table, _mesa_trace_MapBufferRange);
   SET_BindVertexArray(table, _mesa_trace_BindVertexArray);
   SET_DeleteVertexArrays(table, _mesa_trace_DeleteVertexArrays);
   SET_GenVertexArrays(table, _mesa_trace_GenVertexArrays);
   SET_IsVertexArray(table, _mesa_trace_IsVertexArray);
   SET_GetActiveUniformBlockName(table, _mesa_trace_GetActiveUniformBlockName);
   SET_GetActiveUniformBlockiv(table, _mesa_trace_GetActiveUniformBlockiv);
   SET_GetActiveUniformName(table, _mesa_trace_GetActiveUniformName);
   SET_GetActiveUniformsiv(table, _mesa_trace_GetActiveUniformsiv);
   SET_GetUniformBlockIndex(table, _mesa_trace_GetUniformBlockIndex);
   SET_GetUniformIndices(table, _mesa_trace_GetUniformIndices);
   SET_UniformBlockBinding(table, _mesa_trace_UniformBlockBinding);
   SET_CopyBufferSubData(table, _mesa_trace_CopyBufferSubData);
   SET_ClientWaitSync(table, _mesa_trace_ClientWaitSync);
   SET_DeleteSync(table, _mesa_trace_DeleteSync);
   SET_FenceSync(table, _mesa_trace_FenceSync);
   SET_GetInteger64v(table, _mesa_trace_GetInteger64v);
   SET_GetSynciv(table, _mesa_trace_GetSynciv);
   SET_IsSync(table, _mesa_trace_IsSync);
   SET_WaitSync(table, _mesa_trace_WaitSync);
   SET_DrawElementsBaseVertex(table, _mesa_trace_DrawElementsBaseVertex);
   SET_DrawElementsInstancedBaseVertex(table, _mesa_trace_DrawElementsInstancedBaseVertex);
   SET_DrawRangeElementsBaseVertex(table, _mesa_trace_DrawRangeElementsBaseVertex);
   SET_MultiDrawElementsBaseVertex(table, _mesa_trace_MultiDrawElementsBaseVertex);
   SET_ProvokingVertex(table, _mesa_trace_ProvokingVertex);
   SET_GetMultisamplefv(table, _mesa_trace_GetMultisamplefv);
   SET_SampleMaski(table, _mesa_trace_SampleMaski);
   SET_TexImage2DMultisample(table, _mesa_trace_TexImage2DMultisample);
   SET_TexImage3DMultisample(table, _mesa_trace_TexImage3DMultisample);
   SET_BlendEquationSeparateiARB(table, _mesa_trace_BlendEquationSeparateiARB);
   SET_BlendEquationiARB(table, _mesa_trace_BlendEquationiARB);
   SET_BlendFuncSeparateiARB(table, _mesa_trace_BlendFuncSeparateiARB);
   SET_BlendFunciARB(table, _mesa_trace_BlendFunciARB);
   SET_BindFragDataLocationIndexed(table, _mesa_trace_BindFragDataLocationIndexed);
   SET_GetFragDataIndex(table, _mesa_trace_GetFragDataIndex);
   SET_BindSampler(table, _mesa_trace_BindSampler);
   SET_DeleteSamplers(table, _mesa_trace_DeleteSamplers);
   SET_GenSamplers(table, _mesa_trace_GenSamplers);
   SET_GetSamplerParameterIiv(table, _mesa_trace_GetSamplerParameterIiv);
   SET_GetSamplerParameterIuiv(table, _mesa_trace_GetSamplerParameterIuiv);
   SET_GetSamplerParameterfv(table, _mesa_trace_GetSamplerParameterfv);
   SET_GetSamplerParameteriv(table, _mesa_trace_GetSamplerParameteriv);
   SET_IsSampler(table, _mesa_trace_IsSampler);
   SET_SamplerParameterIiv(table, _mesa_trace_SamplerParameterIiv);
   SET_SamplerParameterIuiv(table, _mesa_trace_SamplerParameterIuiv);
   SET_SamplerParameterf(table, _mesa_trace_SamplerParameterf);
   SET_SamplerParameterfv(table, _mesa_trace_SamplerParameterfv);
   SET_SamplerParameteri(table, _mesa_trace_SamplerParameteri);
   SET_SamplerParameteriv(table, _mesa_trace_SamplerParameteriv);
   SET_GetQueryObjecti64v(table, _mesa_trace_GetQueryObjecti64v);
   SET_GetQueryObjectui64v(table, _mesa_trace_GetQueryObjectui64v);
   SET_QueryCounter(table, _mesa_trace_QueryCounter);
   SET_ColorP3ui(table, _mesa_trace_ColorP3ui);
   SET_ColorP3uiv(table, _mesa_trace_ColorP3uiv);
   SET_ColorP4ui(table, _mesa_trace_ColorP4ui);
   SET_ColorP4uiv(table, _mesa_trace_ColorP4uiv);
   SET_MultiTexCoordP1ui(table, _mesa_trace_MultiTexCoordP1ui);
   SET_MultiTexCoordP1uiv(table, _mesa_trace_MultiTexCoordP1uiv);
   SET_MultiTexCoordP2ui(table, _mesa_trace_MultiTexCoordP2ui);
   SET_MultiTexCoordP2uiv(table, _mesa_trace_MultiTexCoordP2uiv);
   SET_MultiTexCoordP3ui(table, _mesa_trace_MultiTexCoordP3ui);
   SET_MultiTexCoordP3uiv(table, _mesa_trace_MultiTexCoordP3uiv);
   SET_MultiTexCoordP4ui(table, _mesa_trace_MultiTexCoordP4ui);
   SET_MultiTexCoordP4uiv(table, _mesa_trace_MultiTexCoordP4uiv);
   SET_NormalP3ui(table, _mesa_trace_NormalP3ui);
   SET_NormalP3uiv(table, _mesa_trace_NormalP3uiv);
   SET_SecondaryColorP3ui(table, _mesa_trace_SecondaryColorP3ui);
   SET_SecondaryColorP3uiv(table, _mesa_trace_SecondaryColorP3uiv);
   SET_TexCoordP1ui(table, _mesa_trace_TexCoordP1ui);
   SET_TexCoordP1uiv(table, _mesa_trace_TexCoordP1uiv);
   SET_TexCoordP2ui(table, _mesa_trace_TexCoordP2ui);
   SET_TexCoordP2uiv(table, _mesa_trace_TexCoordP2uiv);
   SET_TexCoordP3ui(table, _mesa_trace_TexCoordP3ui);
   SET_TexCoordP3uiv(table, _mesa_trace_TexCoordP3uiv);
   SET_TexCoordP4ui(table, _mesa_trace_TexCoordP4ui);
   SET_TexCoordP4uiv(table, _mesa_trace_TexCoordP4uiv);
   SET_VertexAttribP1ui(table, _mesa_trace_VertexAttribP1ui);
   SET_VertexAttribP1uiv(table, _mesa_trace_VertexAttribP1uiv);
   SET_VertexAttribP2ui(table, _mesa_trace_VertexAttribP2ui);
   SET_VertexAttribP2uiv(table, _mesa_trace_VertexAttribP2uiv);
   SET_VertexAttribP3ui(table, _mesa_trace_VertexAttribP3ui);
   SET_VertexAttribP3uiv(table, _mesa_trace_VertexAttribP3uiv);
   SET_VertexAttribP4ui(table, _mesa_trace_VertexAttribP4ui);
   SET_VertexAttribP4uiv(table, _mesa_trace_VertexAttribP4uiv);
   SET_VertexP2ui(table, _mesa_trace_VertexP2ui);
   SET_VertexP2uiv(table, _mesa_trace_VertexP2uiv);
   SET_VertexP3ui(table, _mesa_trace_VertexP3ui);
   SET_VertexP3uiv(table, _mesa_trace_VertexP3uiv);
   SET_VertexP4ui(table, _mesa_trace_VertexP4ui);
   SET_VertexP4uiv(table, _mesa_trace_VertexP4uiv);
   SET_DrawArraysIndirect(table, _mesa_trace_DrawArraysIndirect);
   SET_DrawElementsIndirect(table, _mesa_trace_DrawElementsIndirect);
   SET_GetUniformdv(table, _mesa_trace_GetUniformdv);
   SET_Uniform1d(table, _mesa_trace_Uniform1d);
   SET_Uniform1dv(table, _mesa_trace_Uniform1dv);
   SET_Uniform2d(table, _mesa_trace_Uniform2d);
   SET_Uniform2dv(table, _mesa_trace_Uniform2dv);
   SET_Uniform3d(table, _mesa_trace_Uniform3d);
   SET_Uniform3dv(table, _mesa_trace_Uniform3dv);
   SET_Uniform4d(table, _mesa_trace_Uniform4d);
   SET_Uniform4dv(table, _mesa_trace_Uniform4dv);
   SET_UniformMatrix2dv(table, _mesa_trace_UniformMatrix2dv);
   SET_UniformMatrix2x3dv(table, _mesa_trace_UniformMatrix2x3dv);
   SET_UniformMatrix2x4dv(table, _mesa_trace_UniformMatrix2x4dv);
   SET_UniformMatrix3dv(table, _mesa_trace_UniformMatrix3dv);
   SET_UniformMatrix3x2dv(table, _mesa_trace_UniformMatrix3x2dv);
   SET_UniformMatrix3x4dv(table, _mesa_trace_UniformMatrix3x4dv);
   SET_UniformMatrix4dv(table, _mesa_trace_UniformMatrix4dv);
   SET_UniformMatrix4x2dv(table, _mesa_trace_UniformMatrix4x2dv);
   SET_UniformMatrix4x3dv(table, _mesa_trace_UniformMatrix4x3dv);
   SET_GetActiveSubroutineName(table, _mesa_trace_GetActiveSubroutineName);
   SET_GetActiveSubroutineUniformName(table, _mesa_trace_GetActiveSubroutineUniformName);
   SET_GetActiveSubroutineUniformiv(table, _mesa_trace_GetActiveSubroutineUniformiv);
   SET_GetProgramStageiv(table, _mesa_trace_GetProgramStageiv);
   SET_GetSubroutineIndex(table, _mesa_trace_GetSubroutineIndex);
   SET_GetSubroutineUniformLocation(table, _mesa_trace_GetSubroutineUniformLocation);
   SET_GetUniformSubroutineuiv(table, _mesa_trace_GetUniformSubroutineuiv);
   SET_UniformSubroutinesuiv(table, _mesa_trace_UniformSubroutinesuiv);
   SET_PatchParameterfv(table, _mesa_trace_PatchParameterfv);
   SET_PatchParameteri(table, _mesa_trace_PatchParameteri);
   SET_BindTransformFeedback(table, _mesa_trace_BindTransformFeedback);
   SET_DeleteTransformFeedbacks(table, _mesa_trace_DeleteTransformFeedbacks);
   SET_DrawTransformFeedback(table, _mesa_trace_DrawTransformFeedback);
   SET_GenTransformFeedbacks(table, _mesa_trace_GenTransformFeedbacks);
   SET_IsTransformFeedback(table, _mesa_trace_IsTransformFeedback);
   SET_PauseTransformFeedback(table, _mesa_trace_PauseTransformFeedback);
   SET_ResumeTransformFeedback(table, _mesa_trace_ResumeTransformFeedback);
   SET_BeginQueryIndexed(table, _mesa_trace_BeginQueryIndexed);
   SET_DrawTransformFeedbackStream(table, _mesa_trace_DrawTransformFeedbackStream);
   SET_EndQueryIndexed(table, _mesa_trace_EndQueryIndexed);
   SET_GetQueryIndexediv(table, _mesa_trace_GetQueryIndexediv);
   SET_ClearDepthf(table, _mesa_trace_ClearDepthf);
   SET_DepthRangef(table, _mesa_trace_DepthRangef);
   SET_GetShaderPrecisionFormat(table, _mesa_trace_GetShaderPrecisionFormat);
   SET_ReleaseShaderCompiler(table, _mesa_trace_ReleaseShaderCompiler);
   SET_ShaderBinary(table, _mesa_trace_ShaderBinary);
   SET_GetProgramBinary(table, _mesa_trace_GetProgramBinary);
   SET_ProgramBinary(table, _mesa_trace_ProgramBinary);
   SET_ProgramParameteri(table, _mesa_trace_ProgramParameteri);
   SET_GetVertexAttribLdv(table, _mesa_trace_GetVertexAttribLdv);
   SET_VertexAttribL1d(table, _mesa_trace_VertexAttribL1d);
   SET_VertexAttribL1dv(table, _mesa_trace_VertexAttribL1dv);
   SET_VertexAttribL2d(table, _mesa_trace_VertexAttribL2d);
   SET_VertexAttribL2dv(table, _mesa_trace_VertexAttribL2dv);
   SET_VertexAttribL3d(table, _mesa_trace_VertexAttribL3d);
   SET_VertexAttribL3dv(table, _mesa_trace_VertexAttribL3dv);
   SET_VertexAttribL4d(table, _mesa_trace_VertexAttribL4d);
   SET_VertexAttribL4dv(table, _mesa_trace_VertexAttribL4dv);
   SET_VertexAttribLPointer(table, _mesa_trace_VertexAttribLPointer);
   SET_DepthRangeArrayv(table, _mesa_trace_DepthRangeArrayv);
   SET_DepthRangeIndexed(table, _mesa_trace_DepthRangeIndexed);
   SET_GetDoublei_v(table, _mesa_trace_GetDoublei_v);
   SET_GetFloati_v(table, _mesa_trace_GetFloati_v);
   SET_ScissorArrayv(table, _mesa_trace_ScissorArrayv);
   SET_ScissorIndexed(table, _mesa_trace_ScissorIndexed);
   SET_ScissorIndexedv(table, _mesa_trace_ScissorIndexedv);
   SET_ViewportArrayv(table, _mesa_trace_ViewportArrayv);
   SET_ViewportIndexedf(table, _mesa_trace_ViewportIndexedf);
   SET_ViewportIndexedfv(table, _mesa_trace_ViewportIndexedfv);
   SET_GetGraphicsResetStatusARB(table, _mesa_trace_GetGraphicsResetStatusARB);
   SET_GetnCompressedTexImageARB(table, _mesa_trace_GetnCompressedTexImageARB);
   SET_GetnMapdvARB(table, _mesa_trace_GetnMapdvARB);
   SET_GetnMapfvARB(table, _mesa_trace_GetnMapfvARB);
   SET_GetnMapivARB(table, _mesa_trace_GetnMapivARB);
   SET_GetnPixelMapfvARB(table, _mesa_trace_GetnPixelMapfvARB);
   SET_GetnPixelMapuivARB(table, _mesa_trace_GetnPixelMapuivARB);
   SET_GetnPixelMapusvARB(table, _mesa_trace_GetnPixelMapusvARB);
   SET_GetnPolygonStippleARB(table, _mesa_trace_GetnPolygonStippleARB);
   SET_GetnTexImageARB(table, _mesa_trace_GetnTexImageARB);
   SET_GetnUniformdvARB(table, _mesa_trace_GetnUniformdvARB);
   SET_GetnUniformfvARB(table, _mesa_trace_GetnUniformfvARB);
   SET_GetnUniformivARB(table, _mesa_trace_GetnUniformivARB);
   SET_GetnUniformuivARB(table, _mesa_trace_GetnUniformuivARB);
   SET_ReadnPixelsARB(table, _mesa_trace_ReadnPixelsARB);
   SET_DrawArraysInstancedBaseInstance(table, _mesa_trace_DrawArraysInstancedBaseInstance);
   SET_DrawElementsInstancedBaseInstance(table, _mesa_trace_DrawElementsInstancedBaseInstance);
   SET_DrawElementsInstancedBaseVertexBaseInstance(table, _mesa_trace_DrawElementsInstancedBaseVertexBaseInstance);
   SET_DrawTransformFeedbackInstanced(table, _mesa_trace_DrawTransformFeedbackInstanced);
   SET_DrawTransformFeedbackStreamInstanced(table, _mesa_trace_DrawTransformFeedbackStreamInstanced);
   SET_GetInternalformativ(table, _mesa_trace_GetInternalformativ);
   SET_GetActiveAtomicCounterBufferiv(table, _mesa_trace_GetActiveAtomicCounterBufferiv);
   SET_BindImageTexture(table, _mesa_trace_BindImageTexture);
   SET_MemoryBarrier(table, _mesa_trace_MemoryBarrier);
   SET_TexStorage1D(table, _mesa_trace_TexStorage1D);
   SET_TexStorage2D(table, _mesa_trace_TexStorage2D);
   SET_TexStorage3D(table, _mesa_trace_TexStorage3D);
   SET_TextureStorage1DEXT(table, _mesa_trace_TextureStorage1DEXT);
   SET_TextureStorage2DEXT(table, _mesa_trace_TextureStorage2DEXT);
   SET_TextureStorage3DEXT(table, _mesa_trace_TextureStorage3DEXT);
   SET_ClearBufferData(table, _mesa_trace_ClearBufferData);
   SET_ClearBufferSubData(table, _mesa_trace_ClearBufferSubData);
   SET_DispatchCompute(table, _mesa_trace_DispatchCompute);
   SET_DispatchComputeIndirect(table, _mesa_trace_DispatchComputeIndirect);
   SET_CopyImageSubData(table, _mesa_trace_CopyImageSubData);
   SET_TextureView(table, _mesa_trace_TextureView);
   SET_BindVertexBuffer(table, _mesa_trace_BindVertexBuffer);
   SET_VertexAttribBinding(table, _mesa_trace_VertexAttribBinding);
   SET_VertexAttribFormat(table, _mesa_trace_VertexAttribFormat);
   SET_VertexAttribIFormat(table, _mesa_trace_VertexAttribIFormat);
   SET_VertexAttribLFormat(table, _mesa_trace_VertexAttribLFormat);
   SET_VertexBindingDivisor(table, _mesa_trace_VertexBindingDivisor);
   SET_FramebufferParameteri(table, _mesa_trace_FramebufferParameteri);
   SET_GetFramebufferParameteriv(table, _mesa_trace_GetFramebufferParameteriv);
   SET_GetInternalformati64v(table, _mesa_trace_GetInternalformati64v);
   SET_MultiDrawArraysIndirect(table, _mesa_trace_MultiDrawArraysIndirect);
   SET_MultiDrawElementsIndirect(table, _mesa_trace_MultiDrawElementsIndirect);
   SET_GetProgramInterfaceiv(table, _mesa_trace_GetProgramInterfaceiv);
   SET_GetProgramResourceIndex(table, _mesa_trace_GetProgramResourceIndex);
   SET_GetProgramResourceLocation(table, _mesa_trace_GetProgramResourceLocation);
   SET_GetProgramResourceLocationIndex(table, _mesa_trace_GetProgramResourceLocationIndex);
   SET_GetProgramResourceName(table, _mesa_trace_GetProgramResourceName);
   SET_GetProgramResourceiv(table, _mesa_trace_GetProgramResourceiv);
   SET_ShaderStorageBlockBinding(table, _mesa_trace_ShaderStorageBlockBinding);
   SET_TexBufferRange(table, _mesa_trace_TexBufferRange);
   SET_TexStorage2DMultisample(table, _mesa_trace_TexStorage2DMultisample);
   SET_TexStorage3DMultisample(table, _mesa_trace_TexStorage3DMultisample);
   SET_BufferStorage(table, _mesa_trace_BufferStorage);
   SET_ClearTexImage(table, _mesa_trace_ClearTexImage);
   SET_ClearTexSubImage(table, _mesa_trace_ClearTexSubImage);
   SET_BindBuffersBase(table, _mesa_trace_BindBuffersBase);
   SET_BindBuffersRange(table, _mesa_trace_BindBuffersRange);
   SET_BindImageTextures(table, _mesa_trace_BindImageTextures);
   SET_BindSamplers(table, _mesa_trace_BindSamplers);
   SET_BindTextures(table, _mesa_trace_BindTextures);
   SET_BindVertexBuffers(table, _mesa_trace_BindVertexBuffers);
   SET_GetImageHandleARB(table, _mesa_trace_GetImageHandleARB);
   SET_GetTextureHandleARB(table, _mesa_trace_GetTextureHandleARB);
   SET_GetTextureSamplerHandleARB(table, _mesa_trace_GetTextureSamplerHandleARB);
   SET_GetVertexAttribLui64vARB(table, _mesa_trace_GetVertexAttribLui64vARB);
   SET_IsImageHandleResidentARB(table, _mesa_trace_IsImageHandleResidentARB);
   SET_IsTextureHandleResidentARB(table, _mesa_trace_IsTextureHandleResidentARB);
   SET_MakeImageHandleNonResidentARB(table, _mesa_trace_MakeImageHandleNonResidentARB);
   SET_MakeImageHandleResidentARB(table, _mesa_trace_MakeImageHandleResidentARB);
   SET_MakeTextureHandleNonResidentARB(table, _mesa_trace_MakeTextureHandleNonResidentARB);
   SET_MakeTextureHandleResidentARB(table, _mesa_trace_MakeTextureHandleResidentARB);
   SET_ProgramUniformHandleui64ARB(table, _mesa_trace_ProgramUniformHandleui64ARB);
   SET_ProgramUniformHandleui64vARB(table, _mesa_trace_ProgramUniformHandleui64vARB);
   SET_UniformHandleui64ARB(table, _mesa_trace_UniformHandleui64ARB);
   SET_UniformHandleui64vARB(table, _mesa_trace_UniformHandleui64vARB);
   SET_VertexAttribL1ui64ARB(table, _mesa_trace_VertexAttribL1ui64ARB);
   SET_VertexAttribL1ui64vARB(table, _mesa_trace_VertexAttribL1ui64vARB);
   SET_DispatchComputeGroupSizeARB(table, _mesa_trace_DispatchComputeGroupSizeARB);
   SET_MultiDrawArraysIndirectCountARB(table, _mesa_trace_MultiDrawArraysIndirectCountARB);
   SET_MultiDrawElementsIndirectCountARB(table, _mesa_trace_MultiDrawElementsIndirectCountARB);
   SET_ClipControl(table, _mesa_trace_ClipControl);
   SET_BindTextureUnit(table, _mesa_trace_BindTextureUnit);
   SET_BlitNamedFramebuffer(table, _mesa_trace_BlitNamedFramebuffer);
   SET_CheckNamedFramebufferStatus(table, _mesa_trace_CheckNamedFramebufferStatus);
   SET_ClearNamedBufferData(table, _mesa_trace_ClearNamedBufferData);
   SET_ClearNamedBufferSubData(table, _mesa_trace_ClearNamedBufferSubData);
   SET_ClearNamedFramebufferfi(table, _mesa_trace_ClearNamedFramebufferfi);
   SET_ClearNamedFramebufferfv(table, _mesa_trace_ClearNamedFramebufferfv);
   SET_ClearNamedFramebufferiv(table, _mesa_trace_ClearNamedFramebufferiv);
   SET_ClearNamedFramebufferuiv(table, _mesa_trace_ClearNamedFramebufferuiv);
   SET_CompressedTextureSubImage1D(table, _mesa_trace_CompressedTextureSubImage1D);
   SET_CompressedTextureSubImage2D(table, _mesa_trace_CompressedTextureSubImage2D);
   SET_CompressedTextureSubImage3D(table, _mesa_trace_CompressedTextureSubImage3D);
   SET_CopyNamedBufferSubData(table, _mesa_trace_CopyNamedBufferSubData);
   SET_CopyTextureSubImage1D(table, _mesa_trace_CopyTextureSubImage1D);
   SET_CopyTextureSubImage2D(table, _mesa_trace_CopyTextureSubImage2D);
   SET_CopyTextureSubImage3D(table, _mesa_trace_CopyTextureSubImage3D);
   SET_CreateBuffers(table, _mesa_trace_CreateBuffers);
   SET_CreateFramebuffers(table, _mesa_trace_CreateFramebuffers);
   SET_CreateProgramPipelines(table, _mesa_trace_CreateProgramPipelines);
   SET_CreateQueries(table, _mesa_trace_CreateQueries);
   SET_CreateRenderbuffers(table, _mesa_trace_CreateRenderbuffers);
   SET_CreateSamplers(table, _mesa_trace_CreateSamplers);
   SET_CreateTextures(table, _mesa_trace_CreateTextures);
   SET_CreateTransformFeedbacks(table, _mesa_trace_CreateTransformFeedbacks);
   SET_CreateVertexArrays(table, _mesa_trace_CreateVertexArrays);
   SET_DisableVertexArrayAttrib(table, _mesa_trace_DisableVertexArrayAttrib);
   SET_EnableVertexArrayAttrib(table, _mesa_trace_EnableVertexArrayAttrib);
   SET_FlushMappedNamedBufferRange(table, _mesa_trace_FlushMappedNamedBufferRange);
   SET_GenerateTextureMipmap(table, _mesa_trace_GenerateTextureMipmap);
   SET_GetCompressedTextureImage(table, _mesa_trace_GetCompressedTextureImage);
   SET_GetNamedBufferParameteri64v(table, _mesa_trace_GetNamedBufferParameteri64v);
   SET_GetNamedBufferParameteriv(table, _mesa_trace_GetNamedBufferParameteriv);
   SET_GetNamedBufferPointerv(table, _mesa_trace_GetNamedBufferPointerv);
   SET_GetNamedBufferSubData(table, _mesa_trace_GetNamedBufferSubData);
   SET_GetNamedFramebufferAttachmentParameteriv(table, _mesa_trace_GetNamedFramebufferAttachmentParameteriv);
   SET_GetNamedFramebufferParameteriv(table, _mesa_trace_GetNamedFramebufferParameteriv);
   SET_GetNamedRenderbufferParameteriv(table, _mesa_trace_GetNamedRenderbufferParameteriv);
   SET_GetQueryBufferObjecti64v(table, _mesa_trace_GetQueryBufferObjecti64v);
   SET_GetQueryBufferObjectiv(table, _mesa_trace_GetQueryBufferObjectiv);
   SET_GetQueryBufferObjectui64v(table, _mesa_trace_GetQueryBufferObjectui64v);
   SET_GetQueryBufferObjectuiv(table, _mesa_trace_GetQueryBufferObjectuiv);
   SET_GetTextureImage(table, _mesa_trace_GetTextureImage);
   SET_GetTextureLevelParameterfv(table, _mesa_trace_GetTextureLevelParameterfv);
   SET_GetTextureLevelParameteriv(table, _mesa_trace_GetTextureLevelParameteriv);
   SET_GetTextureParameterIiv(table, _mesa_trace_GetTextureParameterIiv);
   SET_GetTextureParameterIuiv(table, _mesa_trace_GetTextureParameterIuiv);
   SET_GetTextureParameterfv(table, _mesa_trace_GetTextureParameterfv);
   SET_GetTextureParameteriv(table, _mesa_trace_GetTextureParameteriv);
   SET_GetTransformFeedbacki64_v(table, _mesa_trace_GetTransformFeedbacki64_v);
   SET_GetTransformFeedbacki_v(table, _mesa_trace_GetTransformFeedbacki_v);
   SET_GetTransformFeedbackiv(table, _mesa_trace_GetTransformFeedbackiv);
   SET_GetVertexArrayIndexed64iv(table, _mesa_trace_GetVertexArrayIndexed64iv);
   SET_GetVertexArrayIndexediv(table, _mesa_trace_GetVertexArrayIndexediv);
   SET_GetVertexArrayiv(table, _mesa_trace_GetVertexArrayiv);
   SET_InvalidateNamedFramebufferData(table, _mesa_trace_InvalidateNamedFramebufferData);
   SET_InvalidateNamedFramebufferSubData(table, _mesa_trace_InvalidateNamedFramebufferSubData);
   SET_MapNamedBuffer(table, _mesa_trace_MapNamedBuffer);
   SET_MapNamedBufferRange(table, _mesa_trace_MapNamedBufferRange);
   SET_NamedBufferData(table, _mesa_trace_NamedBufferData);
   SET_NamedBufferStorage(table, _mesa_trace_NamedBufferStorage);
   SET_NamedBufferSubData(table, _mesa_trace_NamedBufferSubData);
   SET_NamedFramebufferDrawBuffer(table, _mesa_trace_NamedFramebufferDrawBuffer);
   SET_NamedFramebufferDrawBuffers(table, _mesa_trace_NamedFramebufferDrawBuffers);
   SET_NamedFramebufferParameteri(table, _mesa_trace_NamedFramebufferParameteri);
   SET_NamedFramebufferReadBuffer(table, _mesa_trace_NamedFramebufferReadBuffer);
   SET_NamedFramebufferRenderbuffer(table, _mesa_trace_NamedFramebufferRenderbuffer);
   SET_NamedFramebufferTexture(table, _mesa_trace_NamedFramebufferTexture);
   SET_NamedFramebufferTextureLayer(table, _mesa_trace_NamedFramebufferTextureLayer);
   SET_NamedRenderbufferStorage(table, _mesa_trace_NamedRenderbufferStorage);
   SET_NamedRenderbufferStorageMultisample(table, _mesa_trace_NamedRenderbufferStorageMultisample);
   SET_TextureBuffer(table, _mesa_trace_TextureBuffer);
   SET_TextureBufferRange(table, _mesa_trace_TextureBufferRange);
   SET_TextureParameterIiv(table, _mesa_trace_TextureParameterIiv);
   SET_TextureParameterIuiv(table, _mesa_trace_TextureParameterIuiv);
   SET_TextureParameterf(table, _mesa_trace_TextureParameterf);
   SET_TextureParameterfv(table, _mesa_trace_TextureParameterfv);
   SET_TextureParameteri(table, _mesa_trace_TextureParameteri);
   SET_TextureParameteriv(table, _mesa_trace_TextureParameteriv);
   SET_TextureStorage1D(table, _mesa_trace_TextureStorage1D);
   SET_TextureStorage2D(table, _mesa_trace_TextureStorage2D);
   SET_TextureStorage2DMultisample(table, _mesa_trace_TextureStorage2DMultisample);
   SET_TextureStorage3D(table, _mesa_trace_TextureStorage3D);
   SET_TextureStorage3DMultisample(table, _mesa_trace_TextureStorage3DMultisample);
   SET_TextureSubImage1D(table, _mesa_trace_TextureSubImage1D);
   SET_TextureSubImage2D(table, _mesa_trace_TextureSubImage2D);
   SET_TextureSubImage3D(table, _mesa_trace_TextureSubImage3D);
   SET_TransformFeedbackBufferBase(table, _mesa_trace_TransformFeedbackBufferBase);
   SET_TransformFeedbackBufferRange(table, _mesa_trace_TransformFeedbackBufferRange);
   SET_UnmapNamedBufferEXT(table, _mesa_trace_UnmapNamedBufferEXT);
   SET_VertexArrayAttribBinding(table, _mesa_trace_VertexArrayAttribBinding);
   SET_VertexArrayAttribFormat(table, _mesa_trace_VertexArrayAttribFormat);
   SET_VertexArrayAttribIFormat(table, _mesa_trace_VertexArrayAttribIFormat);
   SET_VertexArrayAttribLFormat(table, _mesa_trace_VertexArrayAttribLFormat);
   SET_VertexArrayBindingDivisor(table, _mesa_trace_VertexArrayBindingDivisor);
   SET_VertexArrayElementBuffer(table, _mesa_trace_VertexArrayElementBuffer);
   SET_VertexArrayVertexBuffer(table, _mesa_trace_VertexArrayVertexBuffer);
   SET_VertexArrayVertexBuffers(table, _mesa_trace_VertexArrayVertexBuffers);
   SET_GetCompressedTextureSubImage(table, _mesa_trace_GetCompressedTextureSubImage);
   SET_GetTextureSubImage(table, _mesa_trace_GetTextureSubImage);
   SET_BufferPageCommitmentARB(table, _mesa_trace_BufferPageCommitmentARB);
   SET_NamedBufferPageCommitmentARB(table, _mesa_trace_NamedBufferPageCommitmentARB);
   SET_GetUniformi64vARB(table, _mesa_trace_GetUniformi64vARB);
   SET_GetUniformui64vARB(table, _mesa_trace_GetUniformui64vARB);
   SET_GetnUniformi64vARB(table, _mesa_trace_GetnUniformi64vARB);
   SET_GetnUniformui64vARB(table, _mesa_trace_GetnUniformui64vARB);
   SET_ProgramUniform1i64ARB(table, _mesa_trace_ProgramUniform1i64ARB);
   SET_ProgramUniform1i64vARB(table, _mesa_trace_ProgramUniform1i64vARB);
   SET_ProgramUniform1ui64ARB(table, _mesa_trace_ProgramUniform1ui64ARB);
   SET_ProgramUniform1ui64vARB(table, _mesa_trace_ProgramUniform1ui64vARB);
   SET_ProgramUniform2i64ARB(table, _mesa_trace_ProgramUniform2i64ARB);
   SET_ProgramUniform2i64vARB(table, _mesa_trace_ProgramUniform2i64vARB);
   SET_ProgramUniform2ui64ARB(table, _mesa_trace_ProgramUniform2ui64ARB);
   SET_ProgramUniform2ui64vARB(table, _mesa_trace_ProgramUniform2ui64vARB);
   SET_ProgramUniform3i64ARB(table, _mesa_trace_ProgramUniform3i64ARB);
   SET_ProgramUniform3i64vARB(table, _mesa_trace_ProgramUniform3i64vARB);
   SET_ProgramUniform3ui64ARB(table, _mesa_trace_ProgramUniform3ui64ARB);
   SET_ProgramUniform3ui64vARB(table, _mesa_trace_ProgramUniform3ui64vARB);
   SET_ProgramUniform4i64ARB(table, _mesa_trace_ProgramUniform4i64ARB);
   SET_ProgramUniform4i64vARB(table, _mesa_trace_ProgramUniform4i64vARB);
   SET_ProgramUniform4ui64ARB(table, _mesa_trace_ProgramUniform4ui64ARB);
   SET_ProgramUniform4ui64vARB(table, _mesa_trace_ProgramUniform4ui64vARB);
   SET_Uniform1i64ARB(table, _mesa_trace_Uniform1i64ARB);
   SET_Uniform1i64vARB(table, _mesa_trace_Uniform1i64vARB);
   SET_Uniform1ui64ARB(table, _mesa_trace_Uniform1ui64ARB);
   SET_Uniform1ui64vARB(table, _mesa_trace_Uniform1ui64vARB);
   SET_Uniform2i64ARB(table, _mesa_trace_Uniform2i64ARB);
   SET_Uniform2i64vARB(table, _mesa_trace_Uniform2i64vARB);
   SET_Uniform2ui64ARB(table, _mesa_trace_Uniform2ui64ARB);
   SET_Uniform2ui64vARB(table, _mesa_trace_Uniform2ui64vARB);
   SET_Uniform3i64ARB(table, _mesa_trace_Uniform3i64ARB);
   SET_Uniform3i64vARB(table, _mesa_trace_Uniform3i64vARB);
   SET_Uniform3ui64ARB(table, _mesa_trace_Uniform3ui64ARB);
   SET_Uniform3ui64vARB(table, _mesa_trace_Uniform3ui64vARB);
   SET_Uniform4i64ARB(table, _mesa_trace_Uniform4i64ARB);
   SET_Uniform4i64vARB(table, _mesa_trace_Uniform4i64vARB);
   SET_Uniform4ui64ARB(table, _mesa_trace_Uniform4ui64ARB);
   SET_Uniform4ui64vARB(table, _mesa_trace_Uniform4ui64vARB);
   SET_EvaluateDepthValuesARB(table, _mesa_trace_EvaluateDepthValuesARB);
   SET_FramebufferSampleLocationsfvARB(table, _mesa_trace_FramebufferSampleLocationsfvARB);
   SET_NamedFramebufferSampleLocationsfvARB(table, _mesa_trace_NamedFramebufferSampleLocationsfvARB);
   SET_SpecializeShaderARB(table, _mesa_trace_SpecializeShaderARB);
   SET_InvalidateBufferData(table, _mesa_trace_InvalidateBufferData);
   SET_InvalidateBufferSubData(table, _mesa_trace_InvalidateBufferSubData);
   SET_InvalidateFramebuffer(table, _mesa_trace_InvalidateFramebuffer);
   SET_InvalidateSubFramebuffer(table, _mesa_trace_InvalidateSubFramebuffer);
   SET_InvalidateTexImage(table, _mesa_trace_InvalidateTexImage);
   SET_InvalidateTexSubImage(table, _mesa_trace_InvalidateTexSubImage);
   SET_DrawTexfOES(table, _mesa_trace_DrawTexfOES);
   SET_DrawTexfvOES(table, _mesa_trace_DrawTexfvOES);
   SET_DrawTexiOES(table, _mesa_trace_DrawTexiOES);
   SET_DrawTexivOES(table, _mesa_trace_DrawTexivOES);
   SET_DrawTexsOES(table, _mesa_trace_DrawTexsOES);
   SET_DrawTexsvOES(table, _mesa_trace_DrawTexsvOES);
   SET_DrawTexxOES(table, _mesa_trace_DrawTexxOES);
   SET_DrawTexxvOES(table, _mesa_trace_DrawTexxvOES);
   SET_PointSizePointerOES(table, _mesa_trace_PointSizePointerOES);
   SET_QueryMatrixxOES(table, _mesa_trace_QueryMatrixxOES);
   SET_ColorPointerEXT(table, _mesa_trace_ColorPointerEXT);
   SET_EdgeFlagPointerEXT(table, _mesa_trace_EdgeFlagPointerEXT);
   SET_IndexPointerEXT(table, _mesa_trace_IndexPointerEXT);
   SET_NormalPointerEXT(table, _mesa_trace_NormalPointerEXT);
   SET_TexCoordPointerEXT(table, _mesa_trace_TexCoordPointerEXT);
   SET_VertexPointerEXT(table, _mesa_trace_VertexPointerEXT);
   SET_DiscardFramebufferEXT(table, _mesa_trace_DiscardFramebufferEXT);
   SET_ActiveShaderProgram(table, _mesa_trace_ActiveShaderProgram);
   SET_BindProgramPipeline(table, _mesa_trace_BindProgramPipeline);
   SET_CreateShaderProgramv(table, _mesa_trace_CreateShaderProgramv);
   SET_DeleteProgramPipelines(table, _mesa_trace_DeleteProgramPipelines);
   SET_GenProgramPipelines(table, _mesa_trace_GenProgramPipelines);
   SET_GetProgramPipelineInfoLog(table, _mesa_trace_GetProgramPipelineInfoLog);
   SET_GetProgramPipelineiv(table, _mesa_trace_GetProgramPipelineiv);
   SET_IsProgramPipeline(table, _mesa_trace_IsProgramPipeline);
   SET_LockArraysEXT(table, _mesa_trace_LockArraysEXT);
   SET_ProgramUniform1d(table, _mesa_trace_ProgramUniform1d);
   SET_ProgramUniform1dv(table, _mesa_trace_ProgramUniform1dv);
   SET_ProgramUniform1f(table, _mesa_trace_ProgramUniform1f);
   SET_ProgramUniform1fv(table, _mesa_trace_ProgramUniform1fv);
   SET_ProgramUniform1i(table, _mesa_trace_ProgramUniform1i);
   SET_ProgramUniform1iv(table, _mesa_trace_ProgramUniform1iv);
   SET_ProgramUniform1ui(table, _mesa_trace_ProgramUniform1ui);
   SET_ProgramUniform1uiv(table, _mesa_trace_ProgramUniform1uiv);
   SET_ProgramUniform2d(table, _mesa_trace_ProgramUniform2d);
   SET_ProgramUniform2dv(table, _mesa_trace_ProgramUniform2dv);
   SET_ProgramUniform2f(table, _mesa_trace_ProgramUniform2f);
   SET_ProgramUniform2fv(table, _mesa_trace_ProgramUniform2fv);
   SET_ProgramUniform2i(table, _mesa_trace_ProgramUniform2i);
   SET_ProgramUniform2iv(table, _mesa_trace_ProgramUniform2iv);
   SET_ProgramUniform2ui(table, _mesa_trace_ProgramUniform2ui);
   SET_ProgramUniform2uiv(table, _mesa_trace_ProgramUniform2uiv);
   SET_ProgramUniform3d(table, _mesa_trace_ProgramUniform3d);
   SET_ProgramUniform3dv(table, _mesa_trace_ProgramUniform3dv);
   SET_ProgramUniform3f(table, _mesa_trace_ProgramUniform3f);
   SET_ProgramUniform3fv(table, _mesa_trace_ProgramUniform3fv);
   SET_ProgramUniform3i(table, _mesa_trace_ProgramUniform3i);
   SET_ProgramUniform3iv(table, _mesa_trace_ProgramUniform3iv);
   SET_ProgramUniform3ui(table, _mesa_trace_ProgramUniform3ui);
   SET_ProgramUniform3uiv(table, _mesa_trace_ProgramUniform3uiv);
   SET_ProgramUniform4d(table, _mesa_trace_ProgramUniform4d);
   SET_ProgramUniform4dv(table, _mesa_trace_ProgramUniform4dv);
   SET_ProgramUniform4f(table, _mesa_trace_ProgramUniform4f);
   SET_ProgramUniform4fv(table, _mesa_trace_ProgramUniform4fv);
   SET_ProgramUniform4i(table, _mesa_trace_ProgramUniform4i);
   SET_ProgramUniform4iv(table, _mesa_trace_ProgramUniform4iv);
   SET_ProgramUniform4ui(table, _mesa_trace_ProgramUniform4ui);
   SET_ProgramUniform4uiv(table, _mesa_trace_ProgramUniform4uiv);
   SET_ProgramUniformMatrix2dv(table, _mesa_trace_ProgramUniformMatrix2dv);
   SET_ProgramUniformMatrix2fv(table, _mesa_trace_ProgramUniformMatrix2fv);
   SET_ProgramUniformMatrix2x3dv(table, _mesa_trace_ProgramUniformMatrix2x3dv);
   SET_ProgramUniformMatrix2x3fv(table, _mesa_trace_ProgramUniformMatrix2x3fv);
   SET_ProgramUniformMatrix2x4dv(table, _mesa_trace_ProgramUniformMatrix2x4dv);
   SET_ProgramUniformMatrix2x4fv(table, _mesa_trace_ProgramUniformMatrix2x4fv);
   SET_ProgramUniformMatrix3dv(table, _mesa_trace_ProgramUniformMatrix3dv);
   SET_ProgramUniformMatrix3fv(table, _mesa_trace_ProgramUniformMatrix3fv);
   SET_ProgramUniformMatrix3x2dv(table, _mesa_trace_ProgramUniformMatrix3x2dv);
   SET_ProgramUniformMatrix3x2fv(table, _mesa_trace_ProgramUniformMatrix3x2fv);
   SET_ProgramUniformMatrix3x4dv(table, _mesa_trace_ProgramUniformMatrix3x4dv);
   SET_ProgramUniformMatrix3x4fv(table, _mesa_trace_ProgramUniformMatrix3x4fv);
   SET_ProgramUniformMatrix4dv(table, _mesa_trace_ProgramUniformMatrix4dv);
   SET_ProgramUniformMatrix4fv(table, _mesa_trace_ProgramUniformMatrix4fv);
   SET_ProgramUniformMatrix4x2dv(table, _mesa_trace_ProgramUniformMatrix4x2dv);
   SET_ProgramUniformMatrix4x2fv(table, _mesa_trace_ProgramUniformMatrix4x2fv);
   SET_ProgramUniformMatrix4x3dv(table, _mesa_trace_ProgramUniformMatrix4x3dv);
   SET_ProgramUniformMatrix4x3fv(table, _mesa_trace_ProgramUniformMatrix4x3fv);
   SET_UnlockArraysEXT(table, _mesa_trace_UnlockArraysEXT);
   SET_UseProgramStages(table, _mesa_trace_UseProgramStages);
   SET_ValidateProgramPipeline(table, _mesa_trace_ValidateProgramPipeline);
   SET_FramebufferTexture2DMultisampleEXT(table, _mesa_trace_FramebufferTexture2DMultisampleEXT);
   SET_DebugMessageCallback(table, _mesa_trace_DebugMessageCallback);
   SET_DebugMessageControl(table, _mesa_trace_DebugMessageControl);
   SET_DebugMessageInsert(table, _mesa_trace_DebugMessageInsert);
   SET_GetDebugMessageLog(table, _mesa_trace_GetDebugMessageLog);
   SET_GetObjectLabel(table, _mesa_trace_GetObjectLabel);
   SET_GetObjectPtrLabel(table, _mesa_trace_GetObjectPtrLabel);
   SET_ObjectLabel(table, _mesa_trace_ObjectLabel);
   SET_ObjectPtrLabel(table, _mesa_trace_ObjectPtrLabel);
   SET_PopDebugGroup(table, _mesa_trace_PopDebugGroup);
   SET_PushDebugGroup(table, _mesa_trace_PushDebugGroup);
   SET_SecondaryColor3fEXT(table, _mesa_trace_SecondaryColor3fEXT);
   SET_SecondaryColor3fvEXT(table, _mesa_trace_SecondaryColor3fvEXT);
   SET_MultiDrawElements(table, _mesa_trace_MultiDrawElements);
   SET_FogCoordfEXT(table, _mesa_trace_FogCoordfEXT);
   SET_FogCoordfvEXT(table, _mesa_trace_FogCoordfvEXT);
   SET_WindowPos4dMESA(table, _mesa_trace_WindowPos4dMESA);
   SET_WindowPos4dvMESA(table, _mesa_trace_WindowPos4dvMESA);
   SET_WindowPos4fMESA(table, _mesa_trace_WindowPos4fMESA);
   SET_WindowPos4fvMESA(table, _mesa_trace_WindowPos4fvMESA);
   SET_WindowPos4iMESA(table, _mesa_trace_WindowPos4iMESA);
   SET_WindowPos4ivMESA(table, _mesa_trace_WindowPos4ivMESA);
   SET_WindowPos4sMESA(table, _mesa_trace_WindowPos4sMESA);
   SET_WindowPos4svMESA(table, _mesa_trace_WindowPos4svMESA);
   SET_MultiModeDrawArraysIBM(table, _mesa_trace_MultiModeDrawArraysIBM);
   SET_MultiModeDrawElementsIBM(table, _mesa_trace_MultiModeDrawElementsIBM);
   SET_VertexAttrib1dNV(table, _mesa_trace_VertexAttrib1dNV);
   SET_VertexAttrib1dvNV(table, _mesa_trace_VertexAttrib1dvNV);
   SET_VertexAttrib1fNV(table, _mesa_trace_VertexAttrib1fNV);
   SET_VertexAttrib1fvNV(table, _mesa_trace_VertexAttrib1fvNV);
   SET_VertexAttrib1sNV(table, _mesa_trace_VertexAttrib1sNV);
   SET_VertexAttrib1svNV(table, _mesa_trace_VertexAttrib1svNV);
   SET_VertexAttrib2dNV(table, _mesa_trace_VertexAttrib2dNV);
   SET_VertexAttrib2dvNV(table, _mesa_trace_VertexAttrib2dvNV);
   SET_VertexAttrib2fNV(table, _mesa_trace_VertexAttrib2fNV);
   SET_VertexAttrib2fvNV(table, _mesa_trace_VertexAttrib2fvNV);
   SET_VertexAttrib2sNV(table, _mesa_trace_VertexAttrib2sNV);
   SET_VertexAttrib2svNV(table, _mesa_trace_VertexAttrib2svNV);
   SET_VertexAttrib3dNV(table, _mesa_trace_VertexAttrib3dNV);
   SET_VertexAttrib3dvNV(table, _mesa_trace_VertexAttrib3dvNV);
   SET_VertexAttrib3fNV(table, _mesa_trace_VertexAttrib3fNV);
   SET_VertexAttrib3fvNV(table, _mesa_trace_VertexAttrib3fvNV);
   SET_VertexAttrib3sNV(table, _mesa_trace_VertexAttrib3sNV);
   SET_VertexAttrib3svNV(table, _mesa_trace_VertexAttrib3svNV);
   SET_VertexAttrib4dNV(table, _mesa_trace_VertexAttrib4dNV);
   SET_VertexAttrib4dvNV(table, _mesa_trace_VertexAttrib4dvNV);
   SET_VertexAttrib4fNV(table, _mesa_trace_VertexAttrib4fNV);
   SET_VertexAttrib4fvNV(table, _mesa_trace_VertexAttrib4fvNV);
   SET_VertexAttrib4sNV(table, _mesa_trace_VertexAttrib4sNV);
   SET_VertexAttrib4svNV(table, _mesa_trace_VertexAttrib4svNV);
   SET_VertexAttrib4ubNV(table, _mesa_trace_VertexAttrib4ubNV);
   SET_VertexAttrib4ubvNV(table, _mesa_trace_VertexAttrib4ubvNV);
   SET_VertexAttribs1dvNV(table, _mesa_trace_VertexAttribs1dvNV);
   SET_VertexAttribs1fvNV(table, _mesa_trace_VertexAttribs1fvNV);
   SET_VertexAttribs1svNV(table, _mesa_trace_VertexAttribs1svNV);
   SET_VertexAttribs2dvNV(table, _mesa_trace_VertexAttribs2dvNV);
   SET_VertexAttribs2fvNV(table, _mesa_trace_VertexAttribs2fvNV);
   SET_VertexAttribs2svNV(table, _mesa_trace_VertexAttribs2svNV);
   SET_VertexAttribs3dvNV(table, _mesa_trace_VertexAttribs3dvNV);
   SET_VertexAttribs3fvNV(table, _mesa_trace_VertexAttribs3fvNV);
   SET_VertexAttribs3svNV(table, _mesa_trace_VertexAttribs3svNV);
   SET_VertexAttribs4dvNV(table, _mesa_trace_VertexAttribs4dvNV);
   SET_VertexAttribs4fvNV(table, _mesa_trace_VertexAttribs4fvNV);
   SET_VertexAttribs4svNV(table, _mesa_trace_VertexAttribs4svNV);
   SET_VertexAttribs4ubvNV(table, _mesa_trace_VertexAttribs4ubvNV);
   SET_AlphaFragmentOp1ATI(table, _mesa_trace_AlphaFragmentOp1ATI);
   SET_AlphaFragmentOp2ATI(table, _mesa_trace_AlphaFragmentOp2ATI);
   SET_AlphaFragmentOp3ATI(table, _mesa_trace_AlphaFragmentOp3ATI);
   SET_BeginFragmentShaderATI(table, _mesa_trace_BeginFragmentShaderATI);
   SET_BindFragmentShaderATI(table, _mesa_trace_BindFragmentShaderATI);
   SET_ColorFragmentOp1ATI(table, _mesa_trace_ColorFragmentOp1ATI);
   SET_ColorFragmentOp2ATI(table, _mesa_trace_ColorFragmentOp2ATI);
   SET_ColorFragmentOp3ATI(table, _mesa_trace_ColorFragmentOp3ATI);
   SET_DeleteFragmentShaderATI(table, _mesa_trace_DeleteFragmentShaderATI);
   SET_EndFragmentShaderATI(table, _mesa_trace_EndFragmentShaderATI);
   SET_GenFragmentShadersATI(table, _mesa_trace_GenFragmentShadersATI);
   SET_PassTexCoordATI(table, _mesa_trace_PassTexCoordATI);
   SET_SampleMapATI(table, _mesa_trace_SampleMapATI);
   SET_SetFragmentShaderConstantATI(table, _mesa_trace_SetFragmentShaderConstantATI);
   SET_DepthRangeArrayfvOES(table, _mesa_trace_DepthRangeArrayfvOES);
   SET_DepthRangeIndexedfOES(table, _mesa_trace_DepthRangeIndexedfOES);
   SET_ActiveStencilFaceEXT(table, _mesa_trace_ActiveStencilFaceEXT);
   SET_PrimitiveRestartNV(table, _mesa_trace_PrimitiveRestartNV);
   SET_GetTexGenxvOES(table, _mesa_trace_GetTexGenxvOES);
   SET_TexGenxOES(table, _mesa_trace_TexGenxOES);
   SET_TexGenxvOES(table, _mesa_trace_TexGenxvOES);
   SET_DepthBoundsEXT(table, _mesa_trace_DepthBoundsEXT);
   SET_BindFramebufferEXT(table, _mesa_trace_BindFramebufferEXT);
   SET_BindRenderbufferEXT(table, _mesa_trace_BindRenderbufferEXT);
   SET_StringMarkerGREMEDY(table, _mesa_trace_StringMarkerGREMEDY);
   SET_VertexAttribI1iEXT(table, _mesa_trace_VertexAttribI1iEXT);
   SET_VertexAttribI1uiEXT(table, _mesa_trace_VertexAttribI1uiEXT);
   SET_VertexAttribI2iEXT(table, _mesa_trace_VertexAttribI2iEXT);
   SET_VertexAttribI2ivEXT(table, _mesa_trace_VertexAttribI2ivEXT);
   SET_VertexAttribI2uiEXT(table, _mesa_trace_VertexAttribI2uiEXT);
   SET_VertexAttribI2uivEXT(table, _mesa_trace_VertexAttribI2uivEXT);
   SET_VertexAttribI3iEXT(table, _mesa_trace_VertexAttribI3iEXT);
   SET_VertexAttribI3ivEXT(table, _mesa_trace_VertexAttribI3ivEXT);
   SET_VertexAttribI3uiEXT(table, _mesa_trace_VertexAttribI3uiEXT);
   SET_VertexAttribI3uivEXT(table, _mesa_trace_VertexAttribI3uivEXT);
   SET_VertexAttribI4iEXT(table, _mesa_trace_VertexAttribI4iEXT);
   SET_VertexAttribI4ivEXT(table, _mesa_trace_VertexAttribI4ivEXT);
   SET_VertexAttribI4uiEXT(table, _mesa_trace_VertexAttribI4uiEXT);
   SET_VertexAttribI4uivEXT(table, _mesa_trace_VertexAttribI4uivEXT);
   SET_ClearColorIiEXT(table, _mesa_trace_ClearColorIiEXT);
   SET_ClearColorIuiEXT(table, _mesa_trace_ClearColorIuiEXT);
   SET_BindBufferOffsetEXT(table, _mesa_trace_BindBufferOffsetEXT);
   SET_BeginPerfMonitorAMD(table, _mesa_trace_BeginPerfMonitorAMD);
   SET_DeletePerfMonitorsAMD(table, _mesa_trace_DeletePerfMonitorsAMD);
   SET_EndPerfMonitorAMD(table, _mesa_trace_EndPerfMonitorAMD);
   SET_GenPerfMonitorsAMD(table, _mesa_trace_GenPerfMonitorsAMD);
   SET_GetPerfMonitorCounterDataAMD(table, _mesa_trace_GetPerfMonitorCounterDataAMD);
   SET_GetPerfMonitorCounterInfoAMD(table, _mesa_trace_GetPerfMonitorCounterInfoAMD);
   SET_GetPerfMonitorCounterStringAMD(table, _mesa_trace_GetPerfMonitorCounterStringAMD);
   SET_GetPerfMonitorCountersAMD(table, _mesa_trace_GetPerfMonitorCountersAMD);
   SET_GetPerfMonitorGroupStringAMD(table, _mesa_trace_GetPerfMonitorGroupStringAMD);
   SET_GetPerfMonitorGroupsAMD(table, _mesa_trace_GetPerfMonitorGroupsAMD);
   SET_SelectPerfMonitorCountersAMD(table, _mesa_trace_SelectPerfMonitorCountersAMD);
   SET_TextureBarrierNV(table, _mesa_trace_TextureBarrierNV);
   SET_BeginPerfQueryINTEL(table, _mesa_trace_BeginPerfQueryINTEL);
   SET_CreatePerfQueryINTEL(table, _mesa_trace_CreatePerfQueryINTEL);
   SET_DeletePerfQueryINTEL(table, _mesa_trace_DeletePerfQueryINTEL);
   SET_EndPerfQueryINTEL(table, _mesa_trace_EndPerfQueryINTEL);
   SET_GetFirstPerfQueryIdINTEL(table, _mesa_trace_GetFirstPerfQueryIdINTEL);
   SET_GetNextPerfQueryIdINTEL(table, _mesa_trace_GetNextPerfQueryIdINTEL);
   SET_GetPerfCounterInfoINTEL(table, _mesa_trace_GetPerfCounterInfoINTEL);
   SET_GetPerfQueryDataINTEL(table, _mesa_trace_GetPerfQueryDataINTEL);
   SET_GetPerfQueryIdByNameINTEL(table, _mesa_trace_GetPerfQueryIdByNameINTEL);
   SET_GetPerfQueryInfoINTEL(table, _mesa_trace_GetPerfQueryInfoINTEL);
   SET_PolygonOffsetClampEXT(table, _mesa_trace_PolygonOffsetClampEXT);
   SET_SubpixelPrecisionBiasNV(table, _mesa_trace_SubpixelPrecisionBiasNV);
   SET_ConservativeRasterParameterfNV(table, _mesa_trace_ConservativeRasterParameterfNV);
   SET_ConservativeRasterParameteriNV(table, _mesa_trace_ConservativeRasterParameteriNV);
   SET_WindowRectanglesEXT(table, _mesa_trace_WindowRectanglesEXT);
   SET_BufferStorageMemEXT(table, _mesa_trace_BufferStorageMemEXT);
   SET_CreateMemoryObjectsEXT(table, _mesa_trace_CreateMemoryObjectsEXT);
   SET_DeleteMemoryObjectsEXT(table, _mesa_trace_DeleteMemoryObjectsEXT);
   SET_DeleteSemaphoresEXT(table, _mesa_trace_DeleteSemaphoresEXT);
   SET_GenSemaphoresEXT(table, _mesa_trace_GenSemaphoresEXT);
   SET_GetMemoryObjectParameterivEXT(table, _mesa_trace_GetMemoryObjectParameterivEXT);
   SET_GetSemaphoreParameterui64vEXT(table, _mesa_trace_GetSemaphoreParameterui64vEXT);
   SET_GetUnsignedBytei_vEXT(table, _mesa_trace_GetUnsignedBytei_vEXT);
   SET_GetUnsignedBytevEXT(table, _mesa_trace_GetUnsignedBytevEXT);
   SET_IsMemoryObjectEXT(table, _mesa_trace_IsMemoryObjectEXT);
   SET_IsSemaphoreEXT(table, _mesa_trace_IsSemaphoreEXT);
   SET_MemoryObjectParameterivEXT(table, _mesa_trace_MemoryObjectParameterivEXT);
   SET_NamedBufferStorageMemEXT(table, _mesa_trace_NamedBufferStorageMemEXT);
   SET_SemaphoreParameterui64vEXT(table, _mesa_trace_SemaphoreParameterui64vEXT);
   SET_SignalSemaphoreEXT(table, _mesa_trace_SignalSemaphoreEXT);
   SET_TexStorageMem1DEXT(table, _mesa_trace_TexStorageMem1DEXT);
   SET_TexStorageMem2DEXT(table, _mesa_trace_TexStorageMem2DEXT);
   SET_TexStorageMem2DMultisampleEXT(table, _mesa_trace_TexStorageMem2DMultisampleEXT);
   SET_TexStorageMem3DEXT(table, _mesa_trace_TexStorageMem3DEXT);
   SET_TexStorageMem3DMultisampleEXT(table, _mesa_trace_TexStorageMem3DMultisampleEXT);
   SET_TextureStorageMem1DEXT(table, _mesa_trace_TextureStorageMem1DEXT);
   SET_TextureStorageMem2DEXT(table, _mesa_trace_TextureStorageMem2DEXT);
   SET_TextureStorageMem2DMultisampleEXT(table, _mesa_trace_TextureStorageMem2DMultisampleEXT);
   SET_TextureStorageMem3DEXT(table, _mesa_trace_TextureStorageMem3DEXT);
   SET_TextureStorageMem3DMultisampleEXT(table, _mesa_trace_TextureStorageMem3DMultisampleEXT);
   SET_WaitSemaphoreEXT(table, _mesa_trace_WaitSemaphoreEXT);
   SET_ImportMemoryFdEXT(table, _mesa_trace_ImportMemoryFdEXT);
   SET_ImportSemaphoreFdEXT(table, _mesa_trace_ImportSemaphoreFdEXT);
   SET_FramebufferFetchBarrierEXT(table, _mesa_trace_FramebufferFetchBarrierEXT);
   SET_NamedRenderbufferStorageMultisampleAdvancedAMD(table, _mesa_trace_NamedRenderbufferStorageMultisampleAdvancedAMD);
   SET_RenderbufferStorageMultisampleAdvancedAMD(table, _mesa_trace_RenderbufferStorageMultisampleAdvancedAMD);
   SET_StencilFuncSeparateATI(table, _mesa_trace_StencilFuncSeparateATI);
   SET_ProgramEnvParameters4fvEXT(table, _mesa_trace_ProgramEnvParameters4fvEXT);
   SET_ProgramLocalParameters4fvEXT(table, _mesa_trace_ProgramLocalParameters4fvEXT);
   SET_EGLImageTargetRenderbufferStorageOES(table, _mesa_trace_EGLImageTargetRenderbufferStorageOES);
   SET_EGLImageTargetTexture2DOES(table, _mesa_trace_EGLImageTargetTexture2DOES);
   SET_AlphaFuncx(table, _mesa_trace_AlphaFuncx);
   SET_ClearColorx(table, _mesa_trace_ClearColorx);
   SET_ClearDepthx(table, _mesa_trace_ClearDepthx);
   SET_Color4x(table, _mesa_trace_Color4x);
   SET_DepthRangex(table, _mesa_trace_DepthRangex);
   SET_Fogx(table, _mesa_trace_Fogx);
   SET_Fogxv(table, _mesa_trace_Fogxv);
   SET_Frustumf(table, _mesa_trace_Frustumf);
   SET_Frustumx(table, _mesa_trace_Frustumx);
   SET_LightModelx(table, _mesa_trace_LightModelx);
   SET_LightModelxv(table, _mesa_trace_LightModelxv);
   SET_Lightx(table, _mesa_trace_Lightx);
   SET_Lightxv(table, _mesa_trace_Lightxv);
   SET_LineWidthx(table, _mesa_trace_LineWidthx);
   SET_LoadMatrixx(table, _mesa_trace_LoadMatrixx);
   SET_Materialx(table, _mesa_trace_Materialx);
   SET_Materialxv(table, _mesa_trace_Materialxv);
   SET_MultMatrixx(table, _mesa_trace_MultMatrixx);
   SET_MultiTexCoord4x(table, _mesa_trace_MultiTexCoord4x);
   SET_Normal3x(table, _mesa_trace_Normal3x);
   SET_Orthof(table, _mesa_trace_Orthof);
   SET_Orthox(table, _mesa_trace_Orthox);
   SET_PointSizex(table, _mesa_trace_PointSizex);
   SET_PolygonOffsetx(table, _mesa_trace_PolygonOffsetx);
   SET_Rotatex(table, _mesa_trace_Rotatex);
   SET_SampleCoveragex(table, _mesa_trace_SampleCoveragex);
   SET_Scalex(table, _mesa_trace_Scalex);
   SET_TexEnvx(table, _mesa_trace_TexEnvx);
   SET_TexEnvxv(table, _mesa_trace_TexEnvxv);
   SET_TexParameterx(table, _mesa_trace_TexParameterx);
   SET_Translatex(table, _mesa_trace_Translatex);
   SET_ClipPlanef(table, _mesa_trace_ClipPlanef);
   SET_ClipPlanex(table, _mesa_trace_ClipPlanex);
   SET_GetClipPlanef(table, _mesa_trace_GetClipPlanef);
   SET_GetClipPlanex(table, _mesa_trace_GetClipPlanex);
   SET_GetFixedv(table, _mesa_trace_GetFixedv);
   SET_GetLightxv(table, _mesa_trace_GetLightxv);
   SET_GetMaterialxv(table, _mesa_trace_GetMaterialxv);
   SET_GetTexEnvxv(table, _mesa_trace_GetTexEnvxv);
   SET_GetTexParameterxv(table, _mesa_trace_GetTexParameterxv);
   SET_PointParameterx(table, _mesa_trace_PointParameterx);
   SET_PointParameterxv(table, _mesa_trace_PointParameterxv);
   SET_TexParameterxv(table, _mesa_trace_TexParameterxv);
   SET_BlendBarrier(table, _mesa_trace_BlendBarrier);
   SET_PrimitiveBoundingBox(table, _mesa_trace_PrimitiveBoundingBox);
   SET_MaxShaderCompilerThreadsKHR(table, _mesa_trace_MaxShaderCompilerThreadsKHR);
   SET_MatrixLoadfEXT(table, _mesa_trace_MatrixLoadfEXT);
   SET_MatrixLoaddEXT(table, _mesa_trace_MatrixLoaddEXT);
   SET_MatrixMultfEXT(table, _mesa_trace_MatrixMultfEXT);
   SET_MatrixMultdEXT(table, _mesa_trace_MatrixMultdEXT);
   SET_MatrixLoadIdentityEXT(table, _mesa_trace_MatrixLoadIdentityEXT);
   SET_MatrixRotatefEXT(table, _mesa_trace_MatrixRotatefEXT);
   SET_MatrixRotatedEXT(table, _mesa_trace_MatrixRotatedEXT);
   SET_MatrixScalefEXT(table, _mesa_trace_MatrixScalefEXT);
   SET_MatrixScaledEXT(table, _mesa_trace_MatrixScaledEXT);
   SET_MatrixTranslatefEXT(table, _mesa_trace_MatrixTranslatefEXT);
   SET_MatrixTranslatedEXT(table, _mesa_trace_MatrixTranslatedEXT);
   SET_MatrixOrthoEXT(table, _mesa_trace_MatrixOrthoEXT);
   SET_MatrixFrustumEXT(table, _mesa_trace_MatrixFrustumEXT);
   SET_MatrixPushEXT(table, _mesa_trace_MatrixPushEXT);
   SET_MatrixPopEXT(table, _mesa_trace_MatrixPopEXT);
   SET_MatrixLoadTransposefEXT(table, _mesa_trace_MatrixLoadTransposefEXT);
   SET_MatrixLoadTransposedEXT(table, _mesa_trace_MatrixLoadTransposedEXT);
   SET_MatrixMultTransposefEXT(table, _mesa_trace_MatrixMultTransposefEXT);
   SET_MatrixMultTransposedEXT(table, _mesa_trace_MatrixMultTransposedEXT);
   SET_BindMultiTextureEXT(table, _mesa_trace_BindMultiTextureEXT);
   SET_NamedBufferDataEXT(table, _mesa_trace_NamedBufferDataEXT);
   SET_NamedBufferSubDataEXT(table, _mesa_trace_NamedBufferSubDataEXT);
   SET_NamedBufferStorageEXT(table, _mesa_trace_NamedBufferStorageEXT);
   SET_MapNamedBufferRangeEXT(table, _mesa_trace_MapNamedBufferRangeEXT);
   SET_TextureImage1DEXT(table, _mesa_trace_TextureImage1DEXT);
   SET_TextureImage2DEXT(table, _mesa_trace_TextureImage2DEXT);
   SET_TextureImage3DEXT(table, _mesa_trace_TextureImage3DEXT);
   SET_TextureSubImage1DEXT(table, _mesa_trace_TextureSubImage1DEXT);
   SET_TextureSubImage2DEXT(table, _mesa_trace_TextureSubImage2DEXT);
   SET_TextureSubImage3DEXT(table, _mesa_trace_TextureSubImage3DEXT);
   SET_CopyTextureImage1DEXT(table, _mesa_trace_CopyTextureImage1DEXT);
   SET_CopyTextureImage2DEXT(table, _mesa_trace_CopyTextureImage2DEXT);
   SET_CopyTextureSubImage1DEXT(table, _mesa_trace_CopyTextureSubImage1DEXT);
   SET_CopyTextureSubImage2DEXT(table, _mesa_trace_CopyTextureSubImage2DEXT);
   SET_CopyTextureSubImage3DEXT(table, _mesa_trace_CopyTextureSubImage3DEXT);
   SET_MapNamedBufferEXT(table, _mesa_trace_MapNamedBufferEXT);
   SET_GetTextureParameterivEXT(table, _mesa_trace_GetTextureParameterivEXT);
   SET_GetTextureParameterfvEXT(table, _mesa_trace_GetTextureParameterfvEXT);
   SET_TextureParameteriEXT(table, _mesa_trace_TextureParameteriEXT);
   SET_TextureParameterivEXT(table, _mesa_trace_TextureParameterivEXT);
   SET_TextureParameterfEXT(table, _mesa_trace_TextureParameterfEXT);
   SET_TextureParameterfvEXT(table, _mesa_trace_TextureParameterfvEXT);
   SET_GetTextureImageEXT(table, _mesa_trace_GetTextureImageEXT);
   SET_GetTextureLevelParameterivEXT(table, _mesa_trace_GetTextureLevelParameterivEXT);
   SET_GetTextureLevelParameterfvEXT(table, _mesa_trace_GetTextureLevelParameterfvEXT);
   SET_GetNamedBufferSubDataEXT(table, _mesa_trace_GetNamedBufferSubDataEXT);
   SET_GetNamedBufferPointervEXT(table, _mesa_trace_GetNamedBufferPointervEXT);
   SET_GetNamedBufferParameterivEXT(table, _mesa_trace_GetNamedBufferParameterivEXT);
   SET_FlushMappedNamedBufferRangeEXT(table, _mesa_trace_FlushMappedNamedBufferRangeEXT);
   SET_FramebufferDrawBufferEXT(table, _mesa_trace_FramebufferDrawBufferEXT);
   SET_FramebufferDrawBuffersEXT(table, _mesa_trace_FramebufferDrawBuffersEXT);
   SET_FramebufferReadBufferEXT(table, _mesa_trace_FramebufferReadBufferEXT);
   SET_GetFramebufferParameterivEXT(table, _mesa_trace_GetFramebufferParameterivEXT);
   SET_CheckNamedFramebufferStatusEXT(table, _mesa_trace_CheckNamedFramebufferStatusEXT);
   SET_NamedFramebufferTexture1DEXT(table, _mesa_trace_NamedFramebufferTexture1DEXT);
   SET_NamedFramebufferTexture2DEXT(table, _mesa_trace_NamedFramebufferTexture2DEXT);
   SET_NamedFramebufferTexture3DEXT(table, _mesa_trace_NamedFramebufferTexture3DEXT);
   SET_NamedFramebufferRenderbufferEXT(table, _mesa_trace_NamedFramebufferRenderbufferEXT);
   SET_GetNamedFramebufferAttachmentParameterivEXT(table, _mesa_trace_GetNamedFramebufferAttachmentParameterivEXT);
   SET_EnableClientStateiEXT(table, _mesa_trace_EnableClientStateiEXT);
   SET_DisableClientStateiEXT(table, _mesa_trace_DisableClientStateiEXT);
   SET_GetPointerIndexedvEXT(table, _mesa_trace_GetPointerIndexedvEXT);
   SET_MultiTexEnviEXT(table, _mesa_trace_MultiTexEnviEXT);
   SET_MultiTexEnvivEXT(table, _mesa_trace_MultiTexEnvivEXT);
   SET_MultiTexEnvfEXT(table, _mesa_trace_MultiTexEnvfEXT);
   SET_MultiTexEnvfvEXT(table, _mesa_trace_MultiTexEnvfvEXT);
   SET_GetMultiTexEnvivEXT(table, _mesa_trace_GetMultiTexEnvivEXT);
   SET_GetMultiTexEnvfvEXT(table, _mesa_trace_GetMultiTexEnvfvEXT);
   SET_MultiTexParameteriEXT(table, _mesa_trace_MultiTexParameteriEXT);
   SET_MultiTexParameterivEXT(table, _mesa_trace_MultiTexParameterivEXT);
   SET_MultiTexParameterfEXT(table, _mesa_trace_MultiTexParameterfEXT);
   SET_MultiTexParameterfvEXT(table, _mesa_trace_MultiTexParameterfvEXT);
   SET_GetMultiTexImageEXT(table, _mesa_trace_GetMultiTexImageEXT);
   SET_MultiTexImage1DEXT(table, _mesa_trace_MultiTexImage1DEXT);
   SET_MultiTexImage2DEXT(table, _mesa_trace_MultiTexImage2DEXT);
   SET_MultiTexImage3DEXT(table, _mesa_trace_MultiTexImage3DEXT);
   SET_MultiTexSubImage1DEXT(table, _mesa_trace_MultiTexSubImage1DEXT);
   SET_MultiTexSubImage2DEXT(table, _mesa_trace_MultiTexSubImage2DEXT);
   SET_MultiTexSubImage3DEXT(table, _mesa_trace_MultiTexSubImage3DEXT);
   SET_GetMultiTexParameterivEXT(table, _mesa_trace_GetMultiTexParameterivEXT);
   SET_GetMultiTexParameterfvEXT(table, _mesa_trace_GetMultiTexParameterfvEXT);
   SET_CopyMultiTexImage1DEXT(table, _mesa_trace_CopyMultiTexImage1DEXT);
   SET_CopyMultiTexImage2DEXT(table, _mesa_trace_CopyMultiTexImage2DEXT);
   SET_CopyMultiTexSubImage1DEXT(table, _mesa_trace_CopyMultiTexSubImage1DEXT);
   SET_CopyMultiTexSubImage2DEXT(table, _mesa_trace_CopyMultiTexSubImage2DEXT);
   SET_CopyMultiTexSubImage3DEXT(table, _mesa_trace_CopyMultiTexSubImage3DEXT);
   SET_MultiTexGendEXT(table, _mesa_trace_MultiTexGendEXT);
   SET_MultiTexGendvEXT(table, _mesa_trace_MultiTexGendvEXT);
   SET_MultiTexGenfEXT(table, _mesa_trace_MultiTexGenfEXT);
   SET_MultiTexGenfvEXT(table, _mesa_trace_MultiTexGenfvEXT);
   SET_MultiTexGeniEXT(table, _mesa_trace_MultiTexGeniEXT);
   SET_MultiTexGenivEXT(table, _mesa_trace_MultiTexGenivEXT);
   SET_GetMultiTexGendvEXT(table, _mesa_trace_GetMultiTexGendvEXT);
   SET_GetMultiTexGenfvEXT(table, _mesa_trace_GetMultiTexGenfvEXT);
   SET_GetMultiTexGenivEXT(table, _mesa_trace_GetMultiTexGenivEXT);
   SET_MultiTexCoordPointerEXT(table, _mesa_trace_MultiTexCoordPointerEXT);
   SET_BindImageTextureEXT(table, _mesa_trace_BindImageTextureEXT);
   SET_CompressedTextureImage1DEXT(table, _mesa_trace_CompressedTextureImage1DEXT);
   SET_CompressedTextureImage2DEXT(table, _mesa_trace_CompressedTextureImage2DEXT);
   SET_CompressedTextureImage3DEXT(table, _mesa_trace_CompressedTextureImage3DEXT);
   SET_CompressedTextureSubImage1DEXT(table, _mesa_trace_CompressedTextureSubImage1DEXT);
   SET_CompressedTextureSubImage2DEXT(table, _mesa_trace_CompressedTextureSubImage2DEXT);
   SET_CompressedTextureSubImage3DEXT(table, _mesa_trace_CompressedTextureSubImage3DEXT);
   SET_GetCompressedTextureImageEXT(table, _mesa_trace_GetCompressedTextureImageEXT);
   SET_CompressedMultiTexImage1DEXT(table, _mesa_trace_CompressedMultiTexImage1DEXT);
   SET_CompressedMultiTexImage2DEXT(table, _mesa_trace_CompressedMultiTexImage2DEXT);
   SET_CompressedMultiTexImage3DEXT(table, _mesa_trace_CompressedMultiTexImage3DEXT);
   SET_CompressedMultiTexSubImage1DEXT(table, _mesa_trace_CompressedMultiTexSubImage1DEXT);
   SET_CompressedMultiTexSubImage2DEXT(table, _mesa_trace_CompressedMultiTexSubImage2DEXT);
   SET_CompressedMultiTexSubImage3DEXT(table, _mesa_trace_CompressedMultiTexSubImage3DEXT);
   SET_GetCompressedMultiTexImageEXT(table, _mesa_trace_GetCompressedMultiTexImageEXT);
   SET_GetMultiTexLevelParameterivEXT(table, _mesa_trace_GetMultiTexLevelParameterivEXT);
   SET_GetMultiTexLevelParameterfvEXT(table, _mesa_trace_GetMultiTexLevelParameterfvEXT);
   SET_FramebufferParameteriMESA(table, _mesa_trace_FramebufferParameteriMESA);
   SET_GetFramebufferParameterivMESA(table, _mesa_trace_GetFramebufferParameterivMESA);
   SET_NamedRenderbufferStorageEXT(table, _mesa_trace_NamedRenderbufferStorageEXT);
   SET_GetNamedRenderbufferParameterivEXT(table, _mesa_trace_GetNamedRenderbufferParameterivEXT);
   SET_ClientAttribDefaultEXT(table, _mesa_trace_ClientAttribDefaultEXT);
   SET_PushClientAttribDefaultEXT(table, _mesa_trace_PushClientAttribDefaultEXT);
   SET_NamedProgramStringEXT(table, _mesa_trace_NamedProgramStringEXT);
   SET_GetNamedProgramStringEXT(table, _mesa_trace_GetNamedProgramStringEXT);
   SET_NamedProgramLocalParameter4fEXT(table, _mesa_trace_NamedProgramLocalParameter4fEXT);
   SET_NamedProgramLocalParameter4fvEXT(table, _mesa_trace_NamedProgramLocalParameter4fvEXT);
   SET_GetNamedProgramLocalParameterfvEXT(table, _mesa_trace_GetNamedProgramLocalParameterfvEXT);
   SET_NamedProgramLocalParameter4dEXT(table, _mesa_trace_NamedProgramLocalParameter4dEXT);
   SET_NamedProgramLocalParameter4dvEXT(table, _mesa_trace_NamedProgramLocalParameter4dvEXT);
   SET_GetNamedProgramLocalParameterdvEXT(table, _mesa_trace_GetNamedProgramLocalParameterdvEXT);
   SET_GetNamedProgramivEXT(table, _mesa_trace_GetNamedProgramivEXT);
   SET_TextureBufferEXT(table, _mesa_trace_TextureBufferEXT);
   SET_MultiTexBufferEXT(table, _mesa_trace_MultiTexBufferEXT);
   SET_TextureParameterIivEXT(table, _mesa_trace_TextureParameterIivEXT);
   SET_TextureParameterIuivEXT(table, _mesa_trace_TextureParameterIuivEXT);
   SET_GetTextureParameterIivEXT(table, _mesa_trace_GetTextureParameterIivEXT);
   SET_GetTextureParameterIuivEXT(table, _mesa_trace_GetTextureParameterIuivEXT);
   SET_MultiTexParameterIivEXT(table, _mesa_trace_MultiTexParameterIivEXT);
   SET_MultiTexParameterIuivEXT(table, _mesa_trace_MultiTexParameterIuivEXT);
   SET_GetMultiTexParameterIivEXT(table, _mesa_trace_GetMultiTexParameterIivEXT);
   SET_GetMultiTexParameterIuivEXT(table, _mesa_trace_GetMultiTexParameterIuivEXT);
   SET_NamedProgramLocalParameters4fvEXT(table, _mesa_trace_NamedProgramLocalParameters4fvEXT);
   SET_GenerateTextureMipmapEXT(table, _mesa_trace_GenerateTextureMipmapEXT);
   SET_GenerateMultiTexMipmapEXT(table, _mesa_trace_GenerateMultiTexMipmapEXT);
   SET_NamedRenderbufferStorageMultisampleEXT(table, _mesa_trace_NamedRenderbufferStorageMultisampleEXT);
   SET_NamedCopyBufferSubDataEXT(table, _mesa_trace_NamedCopyBufferSubDataEXT);
   SET_VertexArrayVertexOffsetEXT(table, _mesa_trace_VertexArrayVertexOffsetEXT);
   SET_VertexArrayColorOffsetEXT(table, _mesa_trace_VertexArrayColorOffsetEXT);
   SET_VertexArrayEdgeFlagOffsetEXT(table, _mesa_trace_VertexArrayEdgeFlagOffsetEXT);
   SET_VertexArrayIndexOffsetEXT(table, _mesa_trace_VertexArrayIndexOffsetEXT);
   SET_VertexArrayNormalOffsetEXT(table, _mesa_trace_VertexArrayNormalOffsetEXT);
   SET_VertexArrayTexCoordOffsetEXT(table, _mesa_trace_VertexArrayTexCoordOffsetEXT);
   SET_VertexArrayMultiTexCoordOffsetEXT(table, _mesa_trace_VertexArrayMultiTexCoordOffsetEXT);
   SET_VertexArrayFogCoordOffsetEXT(table, _mesa_trace_VertexArrayFogCoordOffsetEXT);
   SET_VertexArraySecondaryColorOffsetEXT(table, _mesa_trace_VertexArraySecondaryColorOffsetEXT);
   SET_VertexArrayVertexAttribOffsetEXT(table, _mesa_trace_VertexArrayVertexAttribOffsetEXT);
   SET_VertexArrayVertexAttribIOffsetEXT(table, _mesa_trace_VertexArrayVertexAttribIOffsetEXT);
   SET_EnableVertexArrayEXT(table, _mesa_trace_EnableVertexArrayEXT);
   SET_DisableVertexArrayEXT(table, _mesa_trace_DisableVertexArrayEXT);
   SET_EnableVertexArrayAttribEXT(table, _mesa_trace_EnableVertexArrayAttribEXT);
   SET_DisableVertexArrayAttribEXT(table, _mesa_trace_DisableVertexArrayAttribEXT);
   SET_GetVertexArrayIntegervEXT(table, _mesa_trace_GetVertexArrayIntegervEXT);
   SET_GetVertexArrayPointervEXT(table, _mesa_trace_GetVertexArrayPointervEXT);
   SET_GetVertexArrayIntegeri_vEXT(table, _mesa_trace_GetVertexArrayIntegeri_vEXT);
   SET_GetVertexArrayPointeri_vEXT(table, _mesa_trace_GetVertexArrayPointeri_vEXT);
   SET_ClearNamedBufferDataEXT(table, _mesa_trace_ClearNamedBufferDataEXT);
   SET_ClearNamedBufferSubDataEXT(table, _mesa_trace_ClearNamedBufferSubDataEXT);
   SET_NamedFramebufferParameteriEXT(table, _mesa_trace_NamedFramebufferParameteriEXT);
   SET_GetNamedFramebufferParameterivEXT(table, _mesa_trace_GetNamedFramebufferParameterivEXT);
   SET_VertexArrayVertexAttribLOffsetEXT(table, _mesa_trace_VertexArrayVertexAttribLOffsetEXT);
   SET_VertexArrayVertexAttribDivisorEXT(table, _mesa_trace_VertexArrayVertexAttribDivisorEXT);
   SET_TextureBufferRangeEXT(table, _mesa_trace_TextureBufferRangeEXT);
   SET_TextureStorage2DMultisampleEXT(table, _mesa_trace_TextureStorage2DMultisampleEXT);
   SET_TextureStorage3DMultisampleEXT(table, _mesa_trace_TextureStorage3DMultisampleEXT);
   SET_VertexArrayBindVertexBufferEXT(table, _mesa_trace_VertexArrayBindVertexBufferEXT);
   SET_VertexArrayVertexAttribFormatEXT(table, _mesa_trace_VertexArrayVertexAttribFormatEXT);
   SET_VertexArrayVertexAttribIFormatEXT(table, _mesa_trace_VertexArrayVertexAttribIFormatEXT);
   SET_VertexArrayVertexAttribLFormatEXT(table, _mesa_trace_VertexArrayVertexAttribLFormatEXT);
   SET_VertexArrayVertexAttribBindingEXT(table, _mesa_trace_VertexArrayVertexAttribBindingEXT);
   SET_VertexArrayVertexBindingDivisorEXT(table, _mesa_trace_VertexArrayVertexBindingDivisorEXT);
   SET_NamedBufferPageCommitmentEXT(table, _mesa_trace_NamedBufferPageCommitmentEXT);
   SET_NamedStringARB(table, _mesa_trace_NamedStringARB);
   SET_DeleteNamedStringARB(table, _mesa_trace_DeleteNamedStringARB);
   SET_CompileShaderIncludeARB(table, _mesa_trace_CompileShaderIncludeARB);
   SET_IsNamedStringARB(table, _mesa_trace_IsNamedStringARB);
   SET_GetNamedStringARB(table, _mesa_trace_GetNamedStringARB);
   SET_GetNamedStringivARB(table, _mesa_trace_GetNamedStringivARB);
   SET_EGLImageTargetTexStorageEXT(table, _mesa_trace_EGLImageTargetTexStorageEXT);
   SET_EGLImageTargetTextureStorageEXT(table, _mesa_trace_EGLImageTargetTextureStorageEXT);
   SET_CopyImageSubDataNV(table, _mesa_trace_CopyImageSubDataNV);
   SET_ViewportSwizzleNV(table, _mesa_trace_ViewportSwizzleNV);
   SET_AlphaToCoverageDitherControlNV(table, _mesa_trace_AlphaToCoverageDitherControlNV);
   SET_InternalBufferSubDataCopyMESA(table, _mesa_trace_InternalBufferSubDataCopyMESA);
   SET_Vertex2hNV(table, _mesa_trace_Vertex2hNV);
   SET_Vertex2hvNV(table, _mesa_trace_Vertex2hvNV);
   SET_Vertex3hNV(table, _mesa_trace_Vertex3hNV);
   SET_Vertex3hvNV(table, _mesa_trace_Vertex3hvNV);
   SET_Vertex4hNV(table, _mesa_trace_Vertex4hNV);
   SET_Vertex4hvNV(table, _mesa_trace_Vertex4hvNV);
   SET_Normal3hNV(table, _mesa_trace_Normal3hNV);
   SET_Normal3hvNV(table, _mesa_trace_Normal3hvNV);
   SET_Color3hNV(table, _mesa_trace_Color3hNV);
   SET_Color3hvNV(table, _mesa_trace_Color3hvNV);
   SET_Color4hNV(table, _mesa_trace_Color4hNV);
   SET_Color4hvNV(table, _mesa_trace_Color4hvNV);
   SET_TexCoord1hNV(table, _mesa_trace_TexCoord1hNV);
   SET_TexCoord1hvNV(table, _mesa_trace_TexCoord1hvNV);
   SET_TexCoord2hNV(table, _mesa_trace_TexCoord2hNV);
   SET_TexCoord2hvNV(table, _mesa_trace_TexCoord2hvNV);
   SET_TexCoord3hNV(table, _mesa_trace_TexCoord3hNV);
   SET_TexCoord3hvNV(table, _mesa_trace_TexCoord3hvNV);
   SET_TexCoord4hNV(table, _mesa_trace_TexCoord4hNV);
   SET_TexCoord4hvNV(table, _mesa_trace_TexCoord4hvNV);
   SET_MultiTexCoord1hNV(table, _mesa_trace_MultiTexCoord1hNV);
   SET_MultiTexCoord1hvNV(table, _mesa_trace_MultiTexCoord1hvNV);
   SET_MultiTexCoord2hNV(table, _mesa_trace_MultiTexCoord2hNV);
   SET_MultiTexCoord2hvNV(table, _mesa_trace_MultiTexCoord2hvNV);
   SET_MultiTexCoord3hNV(table, _mesa_trace_MultiTexCoord3hNV);
   SET_MultiTexCoord3hvNV(table, _mesa_trace_MultiTexCoord3hvNV);
   SET_MultiTexCoord4hNV(table, _mesa_trace_MultiTexCoord4hNV);
   SET_MultiTexCoord4hvNV(table, _mesa_trace_MultiTexCoord4hvNV);
   SET_FogCoordhNV(table, _mesa_trace_FogCoordhNV);
   SET_FogCoordhvNV(table, _mesa_trace_FogCoordhvNV);
   SET_SecondaryColor3hNV(table, _mesa_trace_SecondaryColor3hNV);
   SET_SecondaryColor3hvNV(table, _mesa_trace_SecondaryColor3hvNV);
   SET_InternalSetError(table, _mesa_trace_InternalSetError);
   SET_VertexAttrib1hNV(table, _mesa_trace_VertexAttrib1hNV);
   SET_VertexAttrib1hvNV(table, _mesa_trace_VertexAttrib1hvNV);
   SET_VertexAttrib2hNV(table, _mesa_trace_VertexAttrib2hNV);
   SET_VertexAttrib2hvNV(table, _mesa_trace_VertexAttrib2hvNV);
   SET_VertexAttrib3hNV(table, _mesa_trace_VertexAttrib3hNV);
   SET_VertexAttrib3hvNV(table, _mesa_trace_VertexAttrib3hvNV);
   SET_VertexAttrib4hNV(table, _mesa_trace_VertexAttrib4hNV);
   SET_VertexAttrib4hvNV(table, _mesa_trace_VertexAttrib4hvNV);
   SET_VertexAttribs1hvNV(table, _mesa_trace_VertexAttribs1hvNV);
   SET_VertexAttribs2hvNV(table, _mesa_trace_VertexAttribs2hvNV);
   SET_VertexAttribs3hvNV(table, _mesa_trace_VertexAttribs3hvNV);
   SET_VertexAttribs4hvNV(table, _mesa_trace_VertexAttribs4hvNV);
   SET_TexPageCommitmentARB(table, _mesa_trace_TexPageCommitmentARB);
   SET_TexturePageCommitmentEXT(table, _mesa_trace_TexturePageCommitmentEXT);
   SET_ImportMemoryWin32HandleEXT(table, _mesa_trace_ImportMemoryWin32HandleEXT);
   SET_ImportSemaphoreWin32HandleEXT(table, _mesa_trace_ImportSemaphoreWin32HandleEXT);
   SET_ImportMemoryWin32NameEXT(table, _mesa_trace_ImportMemoryWin32NameEXT);
   SET_ImportSemaphoreWin32NameEXT(table, _mesa_trace_ImportSemaphoreWin32NameEXT);
   SET_GetObjectLabelEXT(table, _mesa_trace_GetObjectLabelEXT);
   SET_LabelObjectEXT(table, _mesa_trace_LabelObjectEXT);
   SET_DrawArraysUserBuf(table, _mesa_trace_DrawArraysUserBuf);
   SET_DrawElementsUserBuf(table, _mesa_trace_DrawElementsUserBuf);
   SET_MultiDrawArraysUserBuf(table, _mesa_trace_MultiDrawArraysUserBuf);
   SET_MultiDrawElementsUserBuf(table, _mesa_trace_MultiDrawElementsUserBuf);
   SET_DrawArraysInstancedBaseInstanceDrawID(table, _mesa_trace_DrawArraysInstancedBaseInstanceDrawID);
   SET_DrawElementsInstancedBaseVertexBaseInstanceDrawID(table, _mesa_trace_DrawElementsInstancedBaseVertexBaseInstanceDrawID);
   SET_InternalInvalidateFramebufferAncillaryMESA(table, _mesa_trace_InternalInvalidateFramebufferAncillaryMESA);
   SET_InternalReleaseBufferMESA(table, _mesa_trace_InternalReleaseBufferMESA);
   SET_DrawElementsPacked(table, _mesa_trace_DrawElementsPacked);
   SET_DrawElementsUserBufPacked(table, _mesa_trace_DrawElementsUserBufPacked);
   SET_TexStorageAttribs2DEXT(table, _mesa_trace_TexStorageAttribs2DEXT);
   SET_TexStorageAttribs3DEXT(table, _mesa_trace_TexStorageAttribs3DEXT);
   SET_FramebufferTextureMultiviewOVR(table, _mesa_trace_FramebufferTextureMultiviewOVR);
   SET_NamedFramebufferTextureMultiviewOVR(table, _mesa_trace_NamedFramebufferTextureMultiviewOVR);
   SET_FramebufferTextureMultisampleMultiviewOVR(table, _mesa_trace_FramebufferTextureMultisampleMultiviewOVR);
   SET_CreateSemaphoresNV(table, _mesa_trace_CreateSemaphoresNV);
   SET_GetSemaphoreParameterivNV(table, _mesa_trace_GetSemaphoreParameterivNV);
   SET_SemaphoreParameterivNV(table, _mesa_trace_SemaphoreParameterivNV);
   SET_DrawMeshTasksEXT(table, _mesa_trace_DrawMeshTasksEXT);
   SET_DrawMeshTasksIndirectEXT(table, _mesa_trace_DrawMeshTasksIndirectEXT);
   SET_MultiDrawMeshTasksIndirectEXT(table, _mesa_trace_MultiDrawMeshTasksIndirectEXT);
   SET_MultiDrawMeshTasksIndirectCountEXT(table, _mesa_trace_MultiDrawMeshTasksIndirectCountEXT);
   SET_ColorTable(table, _mesa_trace_ColorTable);
   SET_ColorTableParameterfv(table, _mesa_trace_ColorTableParameterfv);
   SET_ColorTableParameteriv(table, _mesa_trace_ColorTableParameteriv);
   SET_CopyColorTable(table, _mesa_trace_CopyColorTable);
   SET_GetColorTable(table, _mesa_trace_GetColorTable);
   SET_GetColorTableParameterfv(table, _mesa_trace_GetColorTableParameterfv);
   SET_GetColorTableParameteriv(table, _mesa_trace_GetColorTableParameteriv);
   SET_ColorSubTable(table, _mesa_trace_ColorSubTable);
   SET_CopyColorSubTable(table, _mesa_trace_CopyColorSubTable);
   SET_ConvolutionFilter1D(table, _mesa_trace_ConvolutionFilter1D);
   SET_ConvolutionFilter2D(table, _mesa_trace_ConvolutionFilter2D);
   SET_ConvolutionParameterf(table, _mesa_trace_ConvolutionParameterf);
   SET_ConvolutionParameterfv(table, _mesa_trace_ConvolutionParameterfv);
   SET_ConvolutionParameteri(table, _mesa_trace_ConvolutionParameteri);
   SET_ConvolutionParameteriv(table, _mesa_trace_ConvolutionParameteriv);
   SET_CopyConvolutionFilter1D(table, _mesa_trace_CopyConvolutionFilter1D);
   SET_CopyConvolutionFilter2D(table, _mesa_trace_CopyConvolutionFilter2D);
   SET_GetConvolutionFilter(table, _mesa_trace_GetConvolutionFilter);
   SET_GetConvolutionParameterfv(table, _mesa_trace_GetConvolutionParameterfv);
   SET_GetConvolutionParameteriv(table, _mesa_trace_GetConvolutionParameteriv);
   SET_GetSeparableFilter(table, _mesa_trace_GetSeparableFilter);
   SET_SeparableFilter2D(table, _mesa_trace_SeparableFilter2D);
   SET_GetHistogram(table, _mesa_trace_GetHistogram);
   SET_GetHistogramParameterfv(table, _mesa_trace_GetHistogramParameterfv);
   SET_GetHistogramParameteriv(table, _mesa_trace_GetHistogramParameteriv);
   SET_GetMinmax(table, _mesa_trace_GetMinmax);
   SET_GetMinmaxParameterfv(table, _mesa_trace_GetMinmaxParameterfv);
   SET_GetMinmaxParameteriv(table, _mesa_trace_GetMinmaxParameteriv);
   SET_Histogram(table, _mesa_trace_Histogram);
   SET_Minmax(table, _mesa_trace_Minmax);
   SET_ResetHistogram(table, _mesa_trace_ResetHistogram);
   SET_ResetMinmax(table, _mesa_trace_ResetMinmax);
   SET_GetnColorTableARB(table, _mesa_trace_GetnColorTableARB);
   SET_GetnConvolutionFilterARB(table, _mesa_trace_GetnConvolutionFilterARB);
   SET_GetnHistogramARB(table, _mesa_trace_GetnHistogramARB);
   SET_GetnMinmaxARB(table, _mesa_trace_GetnMinmaxARB);
   SET_GetnSeparableFilterARB(table, _mesa_trace_GetnSeparableFilterARB);

   ctx->Dispatch.Trace = table;
   return true;
}
