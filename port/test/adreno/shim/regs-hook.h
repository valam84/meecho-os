/*
 * Стенд adreno: доступ службы к регистрам — через модель.
 *
 * run.sh подкладывает этот файл к каждому файлу службы (-include). Он
 * включает настоящий adreno.h, где ad_rd()/ad_wr() — обычные чтение и
 * запись окна, и следом переименовывает их вызовы в функции модели.
 * Служба не правится ни строкой. Модель видит каждое обращение в тот
 * момент, когда служба его делает, и проверяет правило этой SoC: чтение
 * или запись блока, чей такт или домен выключен, — зависание шины. Такое
 * обращение роняет сценарий, как на планшете оно уронило бы систему.
 *
 * qcom_soc.c читает GCC сам, своим разыменованием; модель держит
 * состояние GCC в той же памяти, которую отдаёт vm_map_phys(), поэтому
 * его чтения видят то же, что видит служба.
 */
#ifndef _SHIM_REGS_HOOK_H
#define _SHIM_REGS_HOOK_H
#include "adreno.h"
uint32_t model_rd(const struct ad_region *r, uint32_t off);
void model_wr(const struct ad_region *r, uint32_t off, uint32_t v);
uint64_t model_rd64(const struct ad_region *r, uint32_t off);
void model_wr64(const struct ad_region *r, uint32_t off, uint64_t v);
#define ad_rd(r, off)		model_rd((r), (off))
#define ad_wr(r, off, v)	model_wr((r), (off), (v))
#define ad_rd64(r, off)		model_rd64((r), (off))
#define ad_wr64(r, off, v)	model_wr64((r), (off), (v))
#endif
