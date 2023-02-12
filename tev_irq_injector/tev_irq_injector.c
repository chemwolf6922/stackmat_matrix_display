#include "tev_irq_injector.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "semphr.h"
#include "map.h"
#include <stdlib.h>
#include <string.h>

typedef struct
{
    void(*handler)(void*);
    void* ctx;
} tev_irq_t;

typedef struct
{
    tev_handle_t tev;
    SemaphoreHandle_t task_join;
    TaskHandle_t task;
    QueueHandle_t queue;
    map_handle_t irqs;
    SemaphoreHandle_t mutex;
    tev_event_handle_t event;
} tev_irq_injector_t;

static tev_irq_t* tev_irq_new(void(*handler)(void*), void* ctx);
static void tev_irq_free(tev_irq_t* tev_irq);
static void tev_irq_free_with_ctx(void* data, void* ctx);
static void injector_irq_handler_task();
static void injector_event_handler(void* data, int len, void* ctx);

/* called from event loop */
tev_irq_injector_handle_t tev_irq_injector_new(
    tev_handle_t tev, 
    int max_pending_irqs)
{
    tev_irq_injector_t* injector = NULL;

    injector = malloc(sizeof(tev_irq_injector_t));
    if(!injector)
        goto error;
    memset(injector,0,sizeof(tev_irq_injector_t));

    injector->tev = tev;

    injector->mutex = xSemaphoreCreateMutex();
    if(!injector->mutex)
        goto error;

    injector->irqs = map_create();
    if(!injector->irqs)
        goto error;

    injector->queue = xQueueCreate(max_pending_irqs,sizeof(tev_irq_handle_t));
    if(!injector->queue)
        goto error;

    injector->event = tev_set_event_handler(injector->tev,injector_event_handler,injector);
    if(!injector->event)
        goto error;

    injector->task_join = xSemaphoreCreateBinary();
    if(!injector->task_join)
        goto error;

    if(xTaskCreate(injector_irq_handler_task,"TEV irq",2048,injector,1,&injector->task) != pdPASS)
        goto error;

    return (tev_irq_injector_handle_t)injector;
error:
    tev_irq_injector_free((tev_irq_injector_handle_t)injector);
    
    return NULL;
}

/* called from event loop */
void tev_irq_injector_free(tev_irq_injector_handle_t handle)
{
    tev_irq_injector_t* injector = (tev_irq_injector_t*)handle;
    if(injector)
    {
        if(injector->task)
        {
            xQueueSendToFront(injector->queue,NULL,portMAX_DELAY);
            xSemaphoreTake(injector->task_join,portMAX_DELAY);
            vTaskDelete(injector->task);
        }
        if(injector->task_join)
            vSemaphoreDelete(injector->task_join);
        if(injector->event)
            tev_clear_event_handler(injector->tev,injector->event);
        if(injector->irqs)
            map_delete(injector->irqs,tev_irq_free_with_ctx,NULL);
        if(injector->mutex)
            vSemaphoreDelete(injector->mutex);
        if(injector->queue)
            vQueueDelete(injector->queue);
        free(injector);
    }
}

/* called from event loop */
tev_irq_handle_t tev_irq_injector_set_irq_handler(
    tev_irq_injector_handle_t handle,
    void(*handler)(void*), 
    void* ctx)
{
    tev_irq_t* tev_irq = NULL;

    tev_irq_injector_t* injector = (tev_irq_injector_t*)handle;
    if(!injector)
        goto error;

    tev_irq = tev_irq_new(handler,ctx);
    if(!tev_irq)
        goto error;
    
    {
        xSemaphoreTake(injector->mutex,portMAX_DELAY);
        if(!map_add(injector->irqs,&tev_irq,sizeof(tev_irq),tev_irq))
        {
            tev_irq_free(tev_irq);
            tev_irq = NULL;
        }
        xSemaphoreGive(injector->mutex);
    }

    return (tev_irq_handle_t)tev_irq;
error:
    if(tev_irq)
        tev_irq_free(tev_irq);
    return NULL;
}

/* called from event loop */
void tev_irq_injector_clear_irq_handler(
    tev_irq_injector_handle_t handle,
    tev_irq_handle_t tev_irq)
{
    tev_irq_injector_t* injector = (tev_irq_injector_t*)handle;
    if(!injector)
        return;

    {
        xSemaphoreTake(injector->mutex,portMAX_DELAY);
        tev_irq_t* tev_irq = map_remove(injector->irqs,&handle,sizeof(handle));
        tev_irq_free(tev_irq);
        xSemaphoreGive(injector->mutex);
    }

    return;
}

/* called from irq, handle with causion */
int tev_irq_injector_inject(
    tev_irq_injector_handle_t handle, 
    tev_irq_handle_t irq_handle)
{
    tev_irq_injector_t* injector = (tev_irq_injector_t*)handle;
    if(!injector)
        return -1;
    
    if(xQueueSendFromISR(injector->queue,&irq_handle,NULL) != pdTRUE)
        return -1;

    return 0;
}   

/* task */
static void injector_irq_handler_task(void* ctx)
{
    tev_irq_injector_t* injector = (tev_irq_injector_t*)ctx;
    for(;;)
    {
        tev_irq_handle_t irq_handle = NULL;
        xQueueReceive(injector->queue,&irq_handle,portMAX_DELAY);
        if(irq_handle == NULL)
            break;
        tev_irq_t* tev_irq = NULL;
        {
            xSemaphoreTake(injector->mutex,portMAX_DELAY);
            tev_irq = map_get(injector->irqs,&irq_handle,sizeof(irq_handle));
            xSemaphoreGive(injector->mutex);
        }
        if(tev_irq)
        {
            tev_send_event(injector->tev,injector->event,tev_irq,sizeof(tev_irq_t));
        }
    }
    xSemaphoreGive(injector->task_join);
}

/* tev callback */
static void injector_event_handler(void* data, int len, void* ctx)
{
    tev_irq_injector_t* injector = ctx;
    tev_irq_t* irq_handler = (tev_irq_t*)data;
    if(irq_handler->handler)
        irq_handler->handler(irq_handler->ctx);
}

/* data structures */
static tev_irq_t* tev_irq_new(void(*handler)(void*), void* ctx)
{
    tev_irq_t* tev_irq = malloc(sizeof(tev_irq_t));
    if(!tev_irq)
        goto error;
    memset(tev_irq,0,sizeof(tev_irq_t));

    tev_irq->handler = handler;
    tev_irq->ctx = ctx;

    return tev_irq;
error:
    if(tev_irq)
        free(tev_irq);
    return NULL;
}

static void tev_irq_free(tev_irq_t* tev_irq)
{
    if(tev_irq)
        free(tev_irq);
}

static void tev_irq_free_with_ctx(void* data, void* ctx)
{
    tev_irq_free((tev_irq_t*)data);
}
