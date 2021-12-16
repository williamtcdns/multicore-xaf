/*
* Copyright (c) 2015-2021 Cadence Design Systems Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/*******************************************************************************
 * xf-core.c
 *
 * DSP processing framework core
 ******************************************************************************/

#define MODULE_TAG                      CORE

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "xf-dp.h"
#include <xtensa/config/core.h>
#include <osal-isr.h>
#include <osal-timer.h>

#define MARKER_MSG_LENGTH 0x80808080
/*******************************************************************************
 * Internal helpers
 ******************************************************************************/

/* ...translate client-id into component handle */
static inline xf_component_t * xf_client_lookup(xf_core_data_t *cd, UWORD32 client)
{
    xf_cmap_link_t *link = &cd->cmap[client];

    /* ...if link pointer is less than XF_CFG_MAX_CLIENTS, it is a free descriptor */
    return (link->next > XF_CFG_MAX_CLIENTS ? link->c : NULL);
}

/* ...allocate client-id */
static inline UWORD32 xf_client_alloc(xf_core_data_t *cd)
{
    UWORD32     client = cd->free;

    /* ...advance list head to next free id */
    (client < XF_CFG_MAX_CLIENTS ? cd->free = cd->cmap[client].next : 0);

    return client;
}

/* ...recycle client-id */
static inline void xf_client_free(xf_core_data_t *cd, UWORD32 client)
{
    /* ...put client into the head of the free id list */
    cd->cmap[client].next = cd->free, cd->free = client;
}

/*******************************************************************************
 * Process commands to a proxy
 ******************************************************************************/

/* ...register new client */
static int xf_proxy_register(UWORD32 core, xf_message_t *m)
{
    xf_core_data_t *cd = XF_CORE_DATA(core);
    UWORD32             src = XF_MSG_SRC(m->id);
    UWORD32             client;
    xf_component_t *component;
    
    /* ...allocate new client-id */
    XF_CHK_ERR((client = xf_client_alloc(cd)) != XF_CFG_MAX_CLIENTS, XAF_MEMORY_ERR);
    
    /* ...create component via class factory */
    if ((component = xf_component_factory(core, m->buffer, m->length)) == NULL)
    {
        TRACE(ERROR, _x("Component creation failed"));
        
        /* ...recycle client-id */
        xf_client_free(cd, client);
        
        /* ...return generic out-of-memory code always (tbd) */
        return XAF_MEMORY_ERR;
    }

    /* ...register component in a map */
    cd->cmap[client].c = component;

    /* ...set component "default" port specification ("destination") */
    component->id = __XF_PORT_SPEC(core, client, 0);
    
    /* ...adjust session-id to include newly created component-id */
    m->id = __XF_MSG_ID(src, component->id);

    TRACE(REG, _b("registered client: %u:%u (%s)"), core, client, (xf_id_t)m->buffer);

    /* ...Assign the default/pre-init priority to component */
    component->priority = cd->component_default_priority;
    /* ...and return success to App Interface Layer (zero-length output) */
    xf_response_ok(m);
    
    return 0;
}

/* ...shared buffer allocation request */
static int xf_proxy_alloc(UWORD32 core, xf_message_t *m)
{
    /* ...command is valid only if shared memory interface for core is specified */
    XF_CHK_ERR(xf_shmem_enabled(core), XAF_MEMORY_ERR);

    /* ...allocate shared memory buffer (system-specific function; may fail) */
    xf_shmem_alloc(core, m);

    /* ...pass result to App Interface Layer (on success buffer is non-null) */
    xf_response(m);

    return 0;
}

/* ...shared buffer freeing request */
static int xf_proxy_free(UWORD32 core, xf_message_t *m)
{
    /* ...command is valid only if shared memory interface for core is specified */
    XF_CHK_ERR(xf_shmem_enabled(core), XAF_MEMORY_ERR);
    
    /* ...pass buffer freeing request to system-specific function */
    xf_shmem_free(core, m);

    /* ...return success to App Interface Layer (function never fails) */
    xf_response(m);
        
    return 0;
}

#if 0
/* ...port routing command processing */
static int xf_proxy_route(UWORD32 core, xf_message_t *m)
{
    xf_route_port_msg_t    *cmd = m->buffer;
    UWORD32                     src = cmd->src;
    UWORD32                     dst = cmd->dst;
    xf_component_t         *component;
    xf_output_port_t       *port;

    /* ...source component must reside on the local core */
    XF_CHK_ERR(XF_MSG_SRC_CORE(src) == core, -EINVAL);
    
    /* ...make sure the "src" component is valid ("dst" may reside on other core) */
    if ((component = xf_client_lookup(XF_CORE_DATA(core), XF_PORT_CLIENT(src))) == NULL)
    {
        TRACE(ERROR, _x("Source port lookup failed: %x"), src);
        return -ENOENT;
    }
    else if (!component->port || !(port = component->port(component, XF_PORT_ID(src))))
    {
        TRACE(ERROR, _b("Source port doesn't exist: %x"), src);
        return -ENOENT;
    }
    else if (xf_output_port_routed(port))
    {
        TRACE(ERROR, _b("Source port is already routed: %x"), src);
        return -EBUSY;
    }

    /* ...route output port with source port set as destination */
    XF_CHK_API(xf_output_port_route(port, __XF_MSG_ID(dst, src), cmd->alloc_number, cmd->alloc_size, cmd->alloc_align));
  
    TRACE(ROUTE, _b("Ports routed: %03x -> %03x"), src, dst);

    /* ...invoke component data-processing function directly (ignore errors? - tbd) */
    component->entry(component, NULL);

    /* ...return success result code (no output attached) */
    xf_response_ok(m);

    return 0;
}

/* ...disconnect ports */
static int xf_proxy_unroute(UWORD32 core, xf_message_t *m)
{
    xf_unroute_port_msg_t  *cmd = m->buffer;
    UWORD32                     src = cmd->src;
    xf_component_t         *component;
    xf_output_port_t       *port;

    /* ...source component must reside on the local core */
    XF_CHK_ERR(XF_MSG_SRC_CORE(src) == core, -EINVAL);

    /* ...lookup source (output) port */
    if ((component = xf_client_lookup(XF_CORE_DATA(core), XF_PORT_CLIENT(src))) == NULL)
    {
        TRACE(ERROR, _b("Source port lookup failed: %x"), src);
        return -ENOENT;
    }
    else if (!component->port || !(port = component->port(component, XF_PORT_ID(src))))
    {
        TRACE(ERROR, _b("Source port doesn't exist: %x"), src);
        return -ENOENT;
    }
    else if (!xf_output_port_routed(port))
    {
        /* ...port is not routed; satisfy immediately */
        goto done;
    }
    else if (!xf_output_port_idle(port))
    {
        TRACE(ERROR, _b("Source port is not idle: %x"), src);
        return -EBUSY;
    }

    /* ...unroute port (call must succeed) */
    xf_output_port_unroute(port);

    /* ...we cannot satisfy the command now, and need to propagate it to a sink - tbd */
    //return 0;

done:
    /* ...pass success result code to caller */
    xf_response_ok(m);

    return 0;
}
#endif

/* ...fill-this-buffer command processing */
static int xf_proxy_output(UWORD32 core, xf_message_t *m)
{
    /* ...determine destination "client" */
    switch (XF_MSG_SRC_CLIENT(m->id))
    {
#if XF_TRACE_REMOTE
    case 0:
        /* ...destination is a tracer facility; submit buffer to tracer */
        xf_trace_submit(core, m);
        return 0;
#endif

    default:
        /* ...unrecognized destination; return general failure response */
        return XF_CHK_ERR(0, XAF_INVALIDVAL_ERR);
    }
}

/* ...flush command processing */
static int xf_proxy_flush(UWORD32 core, xf_message_t *m)
{
    /* ...determine destination "client" */
    switch (XF_MSG_SRC_CLIENT(m->id))
    {
#if XF_TRACE_REMOTE
    case 0:
        /* ...destination is a tracer facility; flush current buffer */
        xf_trace_flush(core, m);
        return 0;
#endif

    default:
        /* ...unrecognized destination; return general failure response */
        return XF_CHK_ERR(0, XAF_INVALIDVAL_ERR);
    }
}

static void *dsp_worker_entry(void *arg)
{
    struct xf_worker *worker = arg;
    xf_core_data_t *cd  = XF_CORE_DATA(worker->core);

    for (;;) {
        xf_worker_msg_t msg;
        int rc = __xf_msgq_recv_blocking(worker->queue, &msg, sizeof(msg)); /* ...wait indefinitely, TENA_2435. */

        if (rc || !msg.component)
        {
            TRACE(DISP, _b("dsp_worker_entry thread_exit, worker:%p msgq_err:%x msg.component:%p"), worker, rc, msg.component);
            break;
        }

        if (msg.msg)
        {
            UWORD32 client      = XF_MSG_DST_CLIENT(msg.msg->id);
            if (xf_client_lookup(cd, client))
            {
                xf_core_process_message(msg.component, msg.msg);
            }
            else
            {
                /* ...client look-up failed */
                if (XF_MSG_SRC_PROXY(msg.msg->id))
                {
                    TRACE(DISP, _b("In worker entry Error response to message id=%08x - client %u:%u not registered"), msg.msg->id, worker->core, client);
                    xf_response_err(msg.msg);
                }
                else if ((worker->core == XF_MSG_SRC_CORE(msg.msg->id) && xf_client_lookup(cd, XF_MSG_SRC_CLIENT(msg.msg->id))) || 
                        (worker->core != XF_MSG_SRC_CORE(msg.msg->id) && msg.msg->length != XF_MSG_LENGTH_INVALID))

                {
                    TRACE(DISP, _b("In worker entry, Lookup failure response to message id=%08x - client %u:%u not registered"), msg.msg->id, worker->core, client);
                    xf_response_failure(msg.msg);
                }
                else
                {
                    TRACE(DISP, _b("In worker entry, Discard message id=%08x - both dest client %u:%u and src client:%u not registered"), msg.msg->id, worker->core, client, XF_MSG_SRC_CLIENT(msg.msg->id));
                }
            }
        }
        else
        {
            if(!xf_msg_queue_empty(&worker->base_cancel_queue)){
                UWORD32 flag_found = 0;
                xf_message_t *mprev;
                xf_message_t *m = worker->base_cancel_queue.head;

                if(m->buffer == msg.component){
                    worker->base_cancel_queue.head = m->next;

                    /* ...release the buffer back to pool */
                    xf_msg_pool_put(&worker->base_cancel_pool, m);

                    /* ...get the next processing node. */
                    continue;
                }

                mprev = m;
                m = m->next;

                while(!flag_found && m){

                    if(m->buffer == msg.component){
                        flag_found = 1;

                        /* ...node in the queue is a match, update the queue */
                        mprev->next = m->next;

                        /* ...release the buffer back to pool */
                        xf_msg_pool_put(&worker->base_cancel_pool, m);

                        break;
                    }
                    mprev = m;
                    m = m->next;
                }

                if(flag_found){
                    /* ...get the next processing node. */
                    continue;
                }
            }
            /* ...commit te node for processing. */
            xf_core_process(msg.component);
        }
    }
    return NULL;
}

static int xaf_proxy_create_worker(struct xf_worker *worker,
                                   UWORD32 priority, UWORD32 stack_size)
{
    int ret;

#if !defined(HAVE_FREERTOS)
    worker->stack = xf_mem_alloc(stack_size, 4, worker->core, 0 /* shared */);
    if (worker->stack == NULL)
        return XAF_MEMORY_ERR;
#else /* HAVE_FREERTOS */
    worker->stack = NULL;
#endif /* HAVE_FREERTOS */

    worker->queue = __xf_msgq_create(100, sizeof(xf_worker_msg_t));
    if (!worker->queue) {
        ret = XAF_INVALIDPTR_ERR;
        goto err_queue;
    }

    if (xf_msg_pool_init(&worker->base_cancel_pool, XF_CFG_MAX_CLIENTS, worker->core, 0 /* shared */))
    {
        ret = XAF_INVALIDPTR_ERR;
        __xf_msgq_destroy(worker->queue);
        goto err_queue;
    }
    xf_msg_queue_init(&worker->base_cancel_queue);

    if (__xf_thread_create(&worker->thread, dsp_worker_entry, worker,
                           "DSP-worker", worker->stack, stack_size, priority)) {
        ret = XAF_INVALIDVAL_ERR;
        goto err_thread;
    }

    return 0;

err_thread:
    __xf_msgq_destroy(worker->queue);
    xf_msg_pool_destroy(&worker->base_cancel_pool, worker->core, 0);
err_queue:
#if !defined(HAVE_FREERTOS)
    xf_mem_free(worker->stack, stack_size, worker->core, 0);
#endif /* HAVE_FREERTOS */
    return ret;
}

static int xf_proxy_set_priorities(UWORD32 core, xf_message_t *m)
{
    UWORD32 i;
    int rc;
    xf_core_data_t *cd = XF_CORE_DATA(core);
    xf_set_priorities_msg_t *cmd = m->buffer;

    cd->worker = xf_mem_alloc((cmd->n_rt_priorities + 1) * sizeof(struct xf_worker),
                              4, core, 0 /* shared */);
    if (cd->worker == NULL)
        return XAF_MEMORY_ERR;

    cd->worker->core = core;
    rc = xaf_proxy_create_worker(cd->worker, cmd->bg_priority, cmd->stack_size);
    if (rc < 0)
    {
        TRACE(DISP, _b("dsp_thread_create failed, bgworker:%p priority:%d"), cd->worker, cmd->bg_priority);
        return rc;
    }

    for (i = 0; i < cmd->n_rt_priorities; ++i) {
        cd->worker[i+1].core = core;
        rc = xaf_proxy_create_worker(cd->worker + i + 1,
                                     cmd->rt_priority_base + i,
                                     cmd->stack_size);
        if (rc < 0)
        {
            TRACE(DISP, _b("dsp_thread_create failed, worker:%p priority:%d"), cd->worker + i + 1, cmd->rt_priority_base + i);
            return rc;
        }
    }
    
    /* ...initialize scratch memory to NULL for all DSP worker threads */
    for (i = 0; i < cmd->n_rt_priorities+1; ++i)
    {
    	 struct xf_worker *worker = cd->worker + i;
    	 worker->scratch = NULL;
    }

/*...reinitializing locks */
#if 1 
    /* ...reinitialize sched lock */
    xf_sched_preempt_reinit(&cd->sched);

    /* ...reinitialize shared pool lock */
    if(xf_shmem_enabled(core))
    {
        xf_mm_preempt_reinit(&cd->shared_pool);
    }

#if XF_CFG_CORES_NUM > 1
    /* ...DSP shared memory pool reinitialization */
    //xf_mm_preempt_reinit(&xf_dsp_shmem_pool); //divya: no need as it is always protected by global lock
#endif    // #if XF_CFG_CORES_NUM > 1

    /* ...reinitialize per-core memory loop */
    xf_mm_preempt_reinit(&(XF_CORE_DATA(core)->local_pool));

    xf_sync_queue_preempt_reinit(&cd->queue);
#if 0
    xf_sync_queue_preempt_reinit(&cd->response);
#endif

    //xf_sync_queue_preempt_reinit(&XF_CORE_RW_DATA(core)->local);
    if(xf_shmem_enabled(core))
    {
        xf_sync_queue_preempt_reinit(&XF_CORE_RW_DATA(core)->remote);
    }
#endif

    cd->n_workers = cmd->n_rt_priorities + 1;
    cd->worker_stack_size = cmd->stack_size;

    cd->component_default_priority = cmd->rt_priority_base + cmd->n_rt_priorities;

    /* ...update default priority if bg_priority is higher */
    if(cd->component_default_priority <= cmd->bg_priority)
    {
        cd->component_default_priority = 0;
    }

    xf_response_ok(m);
    return 0;
}

/* ...proxy command processing table */
static int (* const xf_proxy_cmd[])(UWORD32, xf_message_t *) = 
{
    [XF_OPCODE_TYPE(XF_REGISTER)] = xf_proxy_register,
    [XF_OPCODE_TYPE(XF_ALLOC)] = xf_proxy_alloc,
    [XF_OPCODE_TYPE(XF_FREE)] = xf_proxy_free,
#if 0
    [XF_OPCODE_TYPE(XF_ROUTE)] = xf_proxy_route,
    [XF_OPCODE_TYPE(XF_UNROUTE)] = xf_proxy_unroute,
#endif
    [XF_OPCODE_TYPE(XF_FILL_THIS_BUFFER)] = xf_proxy_output,
    [XF_OPCODE_TYPE(XF_FLUSH)] = xf_proxy_flush,
    [XF_OPCODE_TYPE(XF_SET_PRIORITIES)] = xf_proxy_set_priorities,
};

/* ...total number of commands supported */
#define XF_PROXY_CMD_NUM        (sizeof(xf_proxy_cmd) / sizeof(xf_proxy_cmd[0]))

/* ...process commands to a proxy */
static void xf_proxy_command(UWORD32 core, xf_message_t *m)
{
    UWORD32     opcode = m->opcode;
    int     res;

    /* ...dispatch command to proper hook */
    if (XF_OPCODE_TYPE(opcode) < XF_PROXY_CMD_NUM)
    {
        if ((res = xf_proxy_cmd[XF_OPCODE_TYPE(opcode)](core, m)) >= 0)
        {
            /* ...command processed successfully; do nothing */
            return;
        }
    }
    else
    {
        TRACE(ERROR, _x("invalid opcode: %x"), opcode);
    }

    /* ...command processing failed; return generic failure response */
    xf_response_err(m);
}

/*******************************************************************************
 * Message completion helper
 ******************************************************************************/
/* ...retrieve message from local queue (protected from ISR) */
static inline void xf_msg_local_put(UWORD32 core, xf_message_t *m)
{
    xf_core_data_t *cd = XF_CORE_DATA(core);
    xf_sync_enqueue(&cd->queue, m);
    xf_ipi_resume_dsp(core);
}

/* ...retrieve message from local queue (protected from ISR) */
static inline xf_message_t * xf_msg_local_get(UWORD32 core)
{
    xf_core_data_t *cd = XF_CORE_DATA(core);
    xf_message_t   *m;

    m = xf_sync_dequeue(&cd->queue);

    return m;
}

#if 0
/* ...retrieve message from local queue (protected from ISR) */
static inline xf_message_t * xf_msg_local_response_get(UWORD32 core)
{
    xf_core_data_t *cd = XF_CORE_DATA(core);
    xf_message_t   *m;

    m = xf_sync_dequeue(&cd->response);

    return m;
}
#endif

/* ...call component data processing function */
void xf_core_process(xf_component_t *component)
{
    XA_ERRORCODE error_code = 0;

    /* ...client look-up successfull */
    TRACE(DISP, _b("core[%u]::client[%u]::process"), XF_PORT_CORE(component->id), XF_PORT_CLIENT(component->id));

    /* ...call data-processing interface */
    if ((error_code = component->entry(component, NULL)) < 0)
    {
#ifndef XA_DISABLE_EVENT
        component->error_handler(component, error_code);
#else
        TRACE(ERROR, _b("execution error =%08x from component =%p (ignored)"), error_code, component);
#endif
    }
}

static void xf_respond_worker_messages(xf_core_data_t *cd, UWORD32 client, UWORD32 priority)
{
    struct xf_worker *worker = cd->worker +priority;

    xf_worker_msg_t marker_msg, msg_tmp;
    xf_message_t msg;
    memset((void *)&msg, 0, sizeof(xf_message_t));
    msg.length = MARKER_MSG_LENGTH;

    /* ...creating a marker message */
    marker_msg.msg = &msg;
    marker_msg.component = NULL;

    __xf_msgq_send(worker->queue, &marker_msg, sizeof(marker_msg));

    while (1)
    {
        int rc = __xf_msgq_recv(worker->queue, &msg_tmp, sizeof(msg_tmp)); 

        if(msg_tmp.msg == NULL)
        { 
            /* ...putting back schedule message in worker queue*/
            __xf_msgq_send(worker->queue, &msg_tmp, sizeof(msg_tmp));
        }
        else if((msg_tmp.msg->length == MARKER_MSG_LENGTH) || rc) 
        {
            /* ...break from loop once marker message is received */
            break;
        }
        else
        {
            if(XF_MSG_DST_CLIENT(msg_tmp.msg->id) == client)
            {
                if (XF_MSG_SRC_PROXY(msg_tmp.msg->id))
                {
                    TRACE(DISP, _b("Error response to proxy message id=%08x - client %u:%u getting unregistered"), msg_tmp.msg->id, worker->core, client);
                    xf_response_err(msg_tmp.msg);
                }
                else if ((worker->core == XF_MSG_SRC_CORE(msg_tmp.msg->id) && xf_client_lookup(cd, XF_MSG_SRC_CLIENT(msg_tmp.msg->id))) || 
                        (worker->core != XF_MSG_SRC_CORE(msg_tmp.msg->id) && msg_tmp.msg->length != XF_MSG_LENGTH_INVALID))

                {
                    TRACE(DISP, _b("Failure response to message id=%08x - client %u:%u getting unregistered"), msg_tmp.msg->id, worker->core, client);
                    /* ...send failure response to all messages which are received for client getting unregistered*/
                    xf_response_failure(msg_tmp.msg);
                }
                else
                {
                    TRACE(DISP, _b("Discard message id=%08x - dest client %u:%u getting unregistered and src client:%u not registered"), msg_tmp.msg->id, worker->core, client, XF_MSG_SRC_CLIENT(msg_tmp.msg->id));
                }
            }
            else
            {    
                /* ...put back all other messages in worker queue*/
                __xf_msgq_send(worker->queue, &msg_tmp, sizeof(msg_tmp));
            }
       	}
    }
}

void xf_core_process_message(xf_component_t *component, xf_message_t *m)
{
    UWORD32 client = XF_MSG_DST_CLIENT(m->id);

    /* ...pass message to component entry point */
    WORD32 ret = component->entry(component, m);

    if (ret == XF_RETVAL_UNREGISTER)
    {
    	UWORD32 priority = component->priority; 
        /* ...call component destructor */
        if (component->exit(component, m) == 0)
        {
            xf_core_data_t *cd = XF_CORE_DATA(XF_MSG_DST_CORE(m->id));

            if(cd->n_workers)
            { 
                /* ...save current priority of thread */
                UWORD32 old_priority = __xf_thread_get_priority(NULL);
                /* ...elevate priority of thread */ 
                __xf_thread_set_priority(NULL, XAF_DSP_THREAD_PRIORITY + 1);

                /* ...respond to all messages received for deleting component */
                xf_respond_worker_messages(cd, client, priority);

                /* ...component cleanup completed; recycle component-id */
                xf_client_free(cd, client);

                /* ...put back thread to its older priority */
                __xf_thread_set_priority(NULL, old_priority);
            }
            else
            {
                /* ...component cleanup completed; recycle component-id */
                xf_client_free(cd, client);
            }
        }
    }
    else if (ret < 0)
    {
        /* generic command failure response */
        xf_response_err(m);
    }
}

static void xf_comp_send(xf_component_t *component, xf_message_t *msg)
{
    xf_core_data_t *cd = XF_CORE_DATA(xf_component_core(component));

    if (cd->n_workers == 0) {
        if (msg)
            xf_core_process_message(component, msg);
        else
            xf_core_process(component);
    } else {
        xf_msgq_t q;
        xf_worker_msg_t worker_msg = {
            .component = component,
            .msg = msg,
        };

        if (component->priority < cd->n_workers)
            q = cd->worker[component->priority].queue;
        else
            q = cd->worker[cd->n_workers - 1].queue;

        __xf_msgq_send(q, &worker_msg, sizeof(worker_msg));
    }
}

static void xf_comp_process(xf_component_t *component)
{
    xf_comp_send(component, NULL);
}

static void xf_comp_process_message(xf_component_t *component, xf_message_t *msg)
{
    xf_comp_send(component, msg);
}

/* ...dispatch message queue execution */
static inline void xf_core_dispatch(xf_core_data_t *cd, UWORD32 core, xf_message_t *m)
{
    UWORD32             client;
    xf_component_t *component;

    /* ...do client-id/component lookup */
    if (XF_MSG_DST_PROXY(m->id))
    {
        TRACE(DISP, _b("core[%u]::proxy-cmd(id=%08x, opcode=%08x)"), core, m->id, m->opcode);
        
        /* ...process message addressed to proxy */
        xf_proxy_command(core, m);

        /* ...do not like this return statement... - tbd */
        return;
    }

#if (XF_CFG_CORES_NUM > 1)
    if((m->opcode == XF_STOP) && (core != XF_CORE_ID_MASTER))
    {
        TRACE(DISP, _b("CORE[%d]-Deinit received"), core);
        xf_response(m);
        xf_ipi_dsp_close(core);
        return;
    }
#endif

    /* ...message goes to local component */
    client = XF_MSG_DST_CLIENT(m->id);
    
    /* ...check if client is alive */
    if ((component = xf_client_lookup(cd, client)) != NULL)
    {
        /* ...client look-up successful */
        TRACE(DISP, _b("core[%u]::client[%u]::cmd(id=%08x, opcode=%08x)"), core, client, m->id, m->opcode);

        /* ...discard locally generated unregister messages */
        if (m->opcode == XF_UNREGISTER &&  !XF_MSG_SRC_PROXY(m->id))
        {
            TRACE(DISP, _b("Discard locally generated unregister message id=%08x"), m->id);
            /* free message somehow? */
        }
        else
        {
            xf_comp_process_message(component, m);
        }
    }
    else
    {
        if (XF_MSG_SRC_PROXY(m->id))
        {
            TRACE(DISP, _b("Error response to message id=%08x - client %u:%u not registered"), m->id, core, client);
            xf_response_err(m);
        }
        else if ((core == XF_MSG_SRC_CORE(m->id) && xf_client_lookup(cd, XF_MSG_SRC_CLIENT(m->id))) || 
                 (core != XF_MSG_SRC_CORE(m->id) && m->length != XF_MSG_LENGTH_INVALID))
        {
            /* ...complete message with general internal failure response */
            TRACE(DISP, _b("Lookup failure response to message id=%08x - client %u:%u not registered"), m->id, core, client);
            xf_response_failure(m);
        }
        else
        {
            TRACE(DISP, _b("Discard message id=%08x - both dest client %u:%u and src client %u:%u not registered"), m->id, core, client, XF_MSG_SRC_CORE(m->id), XF_MSG_SRC_CLIENT(m->id));
        }
    }
}

/*******************************************************************************
 * Entry points
 ******************************************************************************/

/* ...submit message for instant execution on some core */
void xf_msg_submit(xf_message_t *m)
{
    UWORD32     src = XF_MSG_SRC_CORE(m->id);

    /* ...check if message shall go through local IPC layer */
#if (XF_CFG_CORES_NUM > 1)
    UWORD32     dst = XF_MSG_DST_CORE(m->id);

    if (src ^ dst)
    {
        /* ...put message into local IPC queue */
        xf_ipc_send2(dst, (void*)m, sizeof(*m), m->buffer, m->length);
    }
    else
#endif //XF_CFG_CORES_NUM
    {
        /* ...message is addressed to same core */
        xf_core_data_t     *cd = XF_CORE_DATA(src);
        if (cd->n_workers)
        {
            /* ...bypass msgq of DSP-thread if at-least 1 worker thread is active */
            xf_core_dispatch(cd, src, m);
        }
        else
        {
            /* ...When there is only DSP-thread, use the msgq. */
            xf_msg_local_put(src, m);
        }
    }
}

/* ...complete message and pass response to a caller */
void xf_msg_complete(xf_message_t *m)
{
    UWORD32     src = XF_MSG_SRC(m->id);
    UWORD32     dst = XF_MSG_DST(m->id);

    /* ...swap src/dst specifiers */
    m->id = __XF_MSG_ID(dst, src);

    /* ...check if message goes to remote IPC layer */
    if (XF_MSG_DST_PROXY(m->id))
    {
#if (XF_CFG_CORES_NUM > 1) //AR7
        src = XF_MSG_SRC_CORE(m->id);
        if (xf_shmem_enabled(src))
        {
            /* ...return message to proxy */
            xf_msg_proxy_complete(m);
        }
        else
        {
            dst = XF_MSG_DST_CORE(m->id);
            /* ...put message into local IPC queue */
            xf_ipc_send2(dst, (void*)m, sizeof(*m), m->buffer, m->length);
        }
#else //XF_CFG_CORES_NUM
        /* ...return message to proxy */
        xf_msg_proxy_complete(m);
#endif //XF_CFG_CORES_NUM
    }
    else
    {
        /* ...destination is within DSP cluster; check if that is a data buffer */
        switch (m->opcode)
        {
        case XF_EMPTY_THIS_BUFFER:
            /* ...emptied buffer goes back to the output port */
            m->opcode = XF_FILL_THIS_BUFFER;
            break;

        case XF_FILL_THIS_BUFFER:
            /* ...filled buffer is passed to the input port */
            m->opcode = XF_EMPTY_THIS_BUFFER;
            break;
        }

        /* ...submit message for execution */
        xf_msg_submit(m);
    }
}

struct xf_irq_handler {
    xf_isr *irq_handler;
    xf_isr *threaded_handler;
    void *arg;
    int active;
};
static xf_lock_t xf_irq_lock;
static struct xf_irq_handler irq_table[XCHAL_NUM_INTERRUPTS];

static void xf_process_irqs(void)
{
    int i;

    __xf_lock(&xf_irq_lock);
    for (i = 0; i < XCHAL_NUM_INTERRUPTS; ++i) {
        struct xf_irq_handler *irq = irq_table + i;
        int active;

        if (!irq->threaded_handler)
            continue;

        do {
            unsigned long flags = __xf_disable_interrupts();

            active = irq->active;
            if (active)
                --irq->active;
            __xf_restore_interrupts(flags);

            if (active)
                irq->threaded_handler(irq->arg);
        } while (active > 1);
    }
    __xf_unlock(&xf_irq_lock);
}

#ifndef IRQ_THREAD_STACK_SIZE
#define IRQ_THREAD_STACK_SIZE 1024
#endif

#if defined(HAVE_FREERTOS)
static xf_thread_t xf_irq_thread_data;

static void *xf_irq_thread(void *p)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xf_process_irqs();
    }
}

static void xf_irq_init_backend(void)
{
    __xf_lock_init(&xf_irq_lock);
    __xf_thread_create(&xf_irq_thread_data, xf_irq_thread, NULL,
                       "Threaded IRQ thread",
                       NULL, IRQ_THREAD_STACK_SIZE,
                       configMAX_PRIORITIES - 1);
}

static void xf_irq_deinit_backend(void)
{
    __xf_thread_cancel(&xf_irq_thread_data);
    __xf_thread_join(&xf_irq_thread_data, NULL);
    __xf_thread_destroy(&xf_irq_thread_data);
    __xf_lock_destroy(&xf_irq_lock);
}

static void xf_threaded_irq_handler(void *arg)
{
    struct xf_irq_handler *irq = arg;

    if (irq->irq_handler)
        irq->irq_handler(irq->arg);
    if (irq->threaded_handler) {
        ++irq->active;
        vTaskNotifyGiveFromISR(xf_irq_thread_data.task, NULL);
    }
}

int __xf_set_threaded_irq_handler(int irq,
                                  xf_isr *irq_handler,
                                  xf_isr *threaded_handler,
                                  void *arg)
{
    void *rc;

    __xf_lock(&xf_irq_lock);
    irq_table[irq] = (struct xf_irq_handler){
        .irq_handler = irq_handler,
        .threaded_handler = threaded_handler,
        .arg = arg,
    };
    rc = xt_set_interrupt_handler(irq, xf_threaded_irq_handler,
                                  irq_table + irq);
    __xf_unlock(&xf_irq_lock);
    return rc != NULL;
}

int __xf_unset_threaded_irq_handler(int irq)
{
    void *rc;

    __xf_lock(&xf_irq_lock);
    memset(&irq_table[irq], 0, sizeof(struct xf_irq_handler));
    rc = xt_set_interrupt_handler(irq, NULL, NULL);
    __xf_unlock(&xf_irq_lock);
    return rc != NULL;
}

#elif defined(HAVE_XOS)
static xf_thread_t xf_irq_thread_data;
static XosSem xf_irq_semaphore;
static xf_lock_t xf_timer_lock;
static xf_timer_t *xf_timer_list;

static void xf_process_timers(void)
{
    xf_timer_t *timer;

    __xf_lock(&xf_timer_lock);
    for (timer = xf_timer_list; timer; timer = timer->next) {
        if (timer->active) {
            timer->active = 0;
            timer->fn(timer->arg);
        }
    }
    __xf_unlock(&xf_timer_lock);
}

static void *xf_irq_thread(void *p)
{
    for (;;) {
        xos_sem_get(&xf_irq_semaphore);
        xf_process_timers();
        xf_process_irqs();
    }
}

static void __xf_timer_callback(void *arg)
{
    xf_timer_t *timer = arg;

    timer->active = 1;
    xos_sem_put(&xf_irq_semaphore);
}

int __xf_timer_start(xf_timer_t *timer_arg, unsigned long period)
{
    int32_t r;
    xf_timer_t *timer;

    __xf_lock(&xf_timer_lock);
    for (timer = xf_timer_list; timer; timer = timer->next) {
        if (timer == timer_arg)
            break;
    }
    if (timer != timer_arg) {
        timer_arg->next = xf_timer_list;
        xf_timer_list = timer_arg;
    }
    __xf_unlock(&xf_timer_lock);
    timer_arg->active = 0;
    r = xos_timer_start(&timer_arg->timer, period,
                        XOS_TIMER_DELTA |
                        (timer_arg->autoreload ? XOS_TIMER_PERIODIC : 0),
                        __xf_timer_callback, timer_arg);
    return r;
}

int __xf_timer_stop(xf_timer_t *timer_arg)
{
    xf_timer_t *timer;
    int32_t r = xos_timer_stop(&timer_arg->timer);

    __xf_lock(&xf_timer_lock);
    if (xf_timer_list == timer_arg) {
        xf_timer_list = timer_arg->next;
    } else {
        for (timer = xf_timer_list; timer; timer = timer->next) {
            if (timer->next == timer_arg) {
                timer->next = timer_arg->next;
                break;
            }
        }
    }
    timer_arg->next = NULL;
    __xf_unlock(&xf_timer_lock);
    timer_arg->active = 0;
    return r;
}

static void xf_threaded_irq_handler(void *arg)
{
    struct xf_irq_handler *irq = arg;

    if (irq->irq_handler)
        irq->irq_handler(irq->arg);
    if (irq->threaded_handler) {
        ++irq->active;
        xos_sem_put(&xf_irq_semaphore);
    }
}

int __xf_set_threaded_irq_handler(int irq,
                                  xf_isr *irq_handler,
                                  xf_isr *threaded_handler,
                                  void *arg)
{
    int32_t rc;

    __xf_lock(&xf_irq_lock);
    irq_table[irq] = (struct xf_irq_handler){
        .irq_handler = irq_handler,
        .threaded_handler = threaded_handler,
        .arg = arg,
    };
    rc = xos_register_interrupt_handler(irq, xf_threaded_irq_handler,
                                        irq_table + irq);
    __xf_unlock(&xf_irq_lock);
    return rc == XOS_OK;
}

int __xf_unset_threaded_irq_handler(int irq)
{
    int32_t rc;

    __xf_lock(&xf_irq_lock);
    memset(&irq_table[irq], 0, sizeof(struct xf_irq_handler));
    rc = xos_unregister_interrupt_handler(irq);
    __xf_unlock(&xf_irq_lock);
    return rc == XOS_OK;
}

static void xf_irq_init_backend(void)
{
    static char irq_thread_stack[XOS_STACK_EXTRA + IRQ_THREAD_STACK_SIZE];

    xos_sem_create(&xf_irq_semaphore, 0, 0);
    __xf_lock_init(&xf_timer_lock);
    __xf_lock_init(&xf_irq_lock);
    __xf_thread_create(&xf_irq_thread_data, xf_irq_thread, NULL,
                       "Threaded IRQ thread",
                       irq_thread_stack, sizeof(irq_thread_stack),
                       XOS_MAX_PRIORITY - 1);
}

static void xf_irq_deinit_backend(void)
{
    __xf_thread_cancel(&xf_irq_thread_data);
    __xf_thread_join(&xf_irq_thread_data, NULL);
    __xf_thread_destroy(&xf_irq_thread_data);
    __xf_lock_destroy(&xf_irq_lock);
    __xf_lock_destroy(&xf_timer_lock);
    xos_sem_delete(&xf_irq_semaphore);
}
#else
#error Unrecognized RTOS
#endif

/* ...initialize per-core framework data */
int xf_core_init(UWORD32 core)
{
    xf_core_data_t     *cd = XF_CORE_DATA(core);
    xf_cmap_link_t     *link;
    UWORD32                 i;
    
    xf_irq_init_backend();

    /* ...create list of free client descriptors */
    for (link = &cd->cmap[i = 0]; i < XF_CFG_MAX_CLIENTS; i++, link++)
    {
        link->next = i + 1;
    }

    /* ...set head of free clients list */
    cd->free = 0;
    
    /* ...initialize local queue scheduler */
    xf_sched_init(&cd->sched);
    xf_sync_queue_init(&cd->queue);
#if 0
    xf_sync_queue_init(&cd->response);
#endif
    /* ...initialize IPI subsystem */
    XF_CHK_API(xf_ipi_init(core));
    
    /* ...initialize shared read-write memory */
    XF_CHK_API(xf_shmem_enabled(core) ? xf_shmem_init(core) : 0);

    /* ...initialize scratch memory to NULL */
    cd->scratch = NULL;
    
    /* ...okay... it's all good */
    TRACE(INIT, _b("core-%u initialized"), core);
    
    return 0;
}

int xf_core_deinit(UWORD32 core)
{
    xf_core_data_t     *cd = XF_CORE_DATA(core);

#if (XF_CFG_CORES_NUM > 1)
    /* ... deinit other cores. Use DSP-DSP shmem pool for message */
    if (xf_shmem_enabled(core))
    {
        /* ...TODO: to be separated into a function in xf-main.c itself?? */
        UWORD32 dst;
        UWORD32 src_id = 0, src_port = 0;
        UWORD32 dst_id = 0, dst_port = 0;
        xf_message_t *m;
            
        for(dst=0; dst<XF_CFG_CORES_NUM; dst++)
        {
            if(dst == XF_CORE_ID_MASTER) continue;

            m = xf_msg_pool_get(&XF_CORE_DATA(core)->dsp_dsp_shmem_pool);
            m->id = __XF_MSG_ID(__XF_PORT_SPEC(core, src_id, src_port), __XF_PORT_SPEC(dst, dst_id, dst_port));
            m->opcode = XF_STOP; //TODO: other suitable opcode??
            m->length = 0;
            m->buffer = NULL;
            
            /* ...put message into local IPC queue */
            xf_ipc_send2(dst, (void*)m, sizeof(*m), m->buffer, m->length);
        }

        /* ...wait for interrupt from other cores, for deinit confirmation */
        dst--; /* ...skip for master core*/
        do
        {
            while(1)
            {
                xf_ipc_recv2(core, (void **)&m, sizeof(*m));
                if(m == NULL) break;
                dst--;
                xf_msg_pool_put(&XF_CORE_DATA(core)->dsp_dsp_shmem_pool, m);
            }
        }while(dst && !xf_ipi_wait(core)); //TODO:why !xf_ipi_wait()??
    }
#endif
    
    if (cd->n_workers) {
        UWORD32 i;
#if !defined(HAVE_FREERTOS)
        UWORD32 stack_size = cd->worker_stack_size;
#endif /* HAVE_FREERTOS */

#if defined(HAVE_XOS)
        /* ...TENX-51553,TENA-2580: RI.2 temporary fix for XOS thread behaving inorrectly if they never execute */
        xf_worker_msg_t worker_msg = {
            .component = NULL,
            .msg = NULL,
        };
        for (i = 0; i < cd->n_workers; ++i) {
            struct xf_worker *worker = cd->worker + i;

            /* ...nudge the thread to execute with NULL parameters, thread-handle will check NULL and exit. */
            __xf_msgq_send(worker->queue, &worker_msg, sizeof(worker_msg));

            //__xf_thread_cancel(&worker->thread); //xos thread doesnt join if this is enabled
            __xf_thread_join(&worker->thread, NULL);
            __xf_thread_destroy(&worker->thread);

            xf_msg_pool_destroy(&worker->base_cancel_pool, core, 0);

            xf_mem_free(worker->stack, stack_size, core, 0);

            __xf_msgq_destroy(worker->queue);
        }
#else //HAVE_XOS
        for (i = 0; i < cd->n_workers; ++i) {
            struct xf_worker *worker = cd->worker + i;

            __xf_thread_cancel(&worker->thread);
            __xf_thread_join(&worker->thread, NULL);
            __xf_thread_destroy(&worker->thread);

            xf_msg_pool_destroy(&worker->base_cancel_pool, core, 0);

            __xf_msgq_destroy(worker->queue);
        }
#endif //HAVE_XOS
        xf_mem_free(cd->worker, cd->n_workers * sizeof(struct xf_worker),
                    core, 0);
        cd->n_workers = 0;
    }

    /* ...deinitialize shared read-write memory */
    XF_CHK_API(xf_shmem_enabled(core) ? xf_shmem_deinit(core) : 0);

    /* ...deinitialize IPI subsystem */
    XF_CHK_API(xf_ipi_deinit(core));
 
#if 0
    xf_sync_queue_deinit(&cd->response);
#endif
    xf_sync_queue_deinit(&cd->queue);

    xf_sched_deinit(&cd->sched);

    xf_irq_deinit_backend();

    return 0;
}

/* ...core executive loop function */
void xf_core_service(UWORD32 core)
{
    xf_core_data_t *cd = XF_CORE_DATA(core);
    UWORD32             status;
    xf_message_t   *m;
    xf_task_t      *t;

    do
    {           
        /* ...clear local status change */
        status = 0;
            
#if (XF_CFG_CORES_NUM > 1)
        /* ... process messages from IPC queue */
        while(1) //AR7
        {
            xf_ipc_recv2(core, (void **)&m, sizeof(*m));
            if(m == NULL) break;

            if((core == XF_CORE_ID_MASTER) && XF_MSG_DST_PROXY(m->id))
            {
                /* ...for messages to proxy, invalidate only the opcodes for which payload response is expected */
                (XF_OPCODE_RDATA(m->opcode) ? xf_ipc_a2b_invalidate(core, &m->buffer, m->length) : 0);

                /* ... process message from other cores to Proxy */
                xf_msg_proxy_complete(m);
            }
            else
            {
                /* ... message from other cores to local core. */
                UWORD32 dst = XF_MSG_DST_CORE(m->id);
                xf_ipc_a2b_invalidate(core, &m->buffer, m->length);
                xf_msg_local_put(dst, m); //AR7 && AR3
            }
            status = 1;
        }
#endif

        /* ...if core is servicing shared memory with AP, do it first - actually, they all need to support it */
        if (xf_shmem_enabled(core))
        {
            /* ...process all commands */
            xf_shmem_process_queues(core);
        }

        /* ...check if we have a backlog message placed into interim queue */
#if 1 //AR3
        while ((m = xf_msg_local_get(core))) /* ... always work with local queue. */
#else
        while ((m = xf_msg_local_ipc_get(core)) || (m = xf_msg_local_get(core)))
#endif
        {
            /* ...dispatch message execution */
            xf_core_dispatch(cd, core, m);
            /* ...set local status change */
            status = 1;
        }
#if 0
        /* ...check if we have pending responses (submitted from ISR) we need to process */
        while ((m = xf_msg_local_response_get(core)) != NULL)
        {
            /* ...call completion handler on current stack */
            xf_msg_complete(m);

            /* ...set local status change */
            status = 1;
        }
#endif
        /* ...if scheduler queue is empty, break the loop and pause the core */
        if ((t = xf_sched_get(&cd->sched)) != NULL)
        {
            /* ...data-processing execution (ignore internal errors) */
            xf_comp_process((xf_component_t *)t);

            /* ...set local status change */
            status = 1;
        }
    }
    while (status);
}

/* ...global data initialization function */
int xf_global_init(void)
{
    /* ...what global data we have to initialize? - tbd */
    TRACE(INIT, _b("Global data initialized"));
    
    return 0;
}
