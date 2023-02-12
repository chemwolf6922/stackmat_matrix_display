#ifndef __TEV_IRQ_INJECTOR_H
#define __TEV_IRQ_INJECTOR_H

#include "tev.h"

typedef void* tev_irq_injector_handle_t;

tev_irq_injector_handle_t tev_irq_injector_new(
    tev_handle_t tev, 
    int max_pending_irqs);
void tev_irq_injector_free(tev_irq_injector_handle_t injector);

typedef void* tev_irq_handle_t;
/* called from tev */
/* create the irq after this */
tev_irq_handle_t tev_irq_injector_set_irq_handler(
    tev_irq_injector_handle_t injector,
    void(*handler)(void*), 
    void* ctx);
/* cancel any irq before calling this */
void tev_irq_injector_clear_irq_handler(
    tev_irq_injector_handle_t injector,
    tev_irq_handle_t tev_irq);
/* called from irq */
int tev_irq_injector_inject(
    tev_irq_injector_handle_t injector, 
    tev_irq_handle_t tev_irq);

#endif

