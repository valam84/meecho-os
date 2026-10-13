/*
 * Стенд mali: доступ службы к регистрам ускорителя - через модель.
 *
 * run.sh подкладывает этот файл к каждому файлу службы (-include): он
 * включает настоящий mali.h (там mali_rd()/mali_wr() - обычные чтение и
 * запись окна регистров), а следом переименовывает их вызовы в функции
 * модели.  Служба не правится ни строкой; модель же видит каждую запись в
 * тот момент, когда служба её делает, как видит её железо: команда
 * адресного пространства исполняется сразу (LOCK, FLUSH_PT, UPDATE - с
 * её TLB), сброс прерывания снимает бит сразу, выключение окна роняет
 * стоящее на нём задание сразу.  Иначе модель видела бы только последнюю
 * из нескольких команд.
 */
#ifndef _SHIM_REGS_HOOK_H
#define _SHIM_REGS_HOOK_H
#include "mali.h"
uint32_t model_rd(const struct mali_regs *r, unsigned off);
void model_wr(const struct mali_regs *r, unsigned off, uint32_t val);
#define mali_rd(r, off)		model_rd((r), (off))
#define mali_wr(r, off, v)	model_wr((r), (off), (v))
#endif
