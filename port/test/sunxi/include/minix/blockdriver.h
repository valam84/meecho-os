/* Стенд sunxi: то из обмена сообщениями, что видно в ожидании прерывания.
 * Линию стенд не выдаёт (sys_irqsetpolicy отказывает), так что этот путь
 * только собирается - и драйвер обязан дожить до опроса. */
#ifndef _H_MINIX_BLOCKDRIVER_H
#define _H_MINIX_BLOCKDRIVER_H
#include <minix/type.h>
typedef struct { endpoint_t m_source; int m_type; } message;
#define is_ipc_notify(s)	((s) == 1)
#define _ENDPOINT_P(e)		(e)
int driver_receive(endpoint_t src, message *m, int *status);
void blockdriver_mq_queue(message *m, int status);
#endif
