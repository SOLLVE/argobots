/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

#ifdef ABT_CONFIG_HANDLE_POWER_EVENT
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>

#define ABTI_DEFAULT_MAX_CB_FN  4
#define ABTI_MSG_BUF_LEN        20
#endif
#ifdef ABT_CONFIG_PUBLISH_INFO
#include <stdio.h>
#endif

typedef struct ABTI_event_info  ABTI_event_info;

struct ABTI_event_info {
    ABTI_mutex mutex;

#ifdef ABT_CONFIG_HANDLE_POWER_EVENT
    struct pollfd pfd;

    int max_stop_xstream_fn;
    int num_stop_xstream_fn;
    ABT_event_cb_fn *stop_xstream_fn;
    void **stop_xstream_arg;

    int max_add_xstream_fn;
    int num_add_xstream_fn;
    ABT_event_cb_fn *add_xstream_fn;
    void **add_xstream_arg;
#endif
#ifdef ABT_CONFIG_PUBLISH_INFO
    char hostname[100];
    FILE *out_file;
    int max_xstream_rank;
    uint32_t *num_threads;      /* # of ULTs terminated on each ES */
    uint32_t *num_tasks;        /* # of tasklets terminated on each ES */
    double *idle_time;          /* idle time of each ES */
    uint32_t *old_num_units;    /* # of units executed at the last timestamp */
    double *old_timestamp;
    double timestamp;           /* last timestamp */
#endif
};

static ABTI_event_info *gp_einfo = NULL;

/** @defgroup EVENT Event
 * This group is for event handling.
 */

void ABTI_event_init(void)
{
    gp_ABTI_global->pm_connected = ABT_FALSE;

    gp_einfo = (ABTI_event_info *)ABTU_calloc(1, sizeof(ABTI_event_info));
    ABTI_mutex_init(&gp_einfo->mutex);

#ifdef ABT_CONFIG_HANDLE_POWER_EVENT
    gp_einfo->max_stop_xstream_fn = ABTI_DEFAULT_MAX_CB_FN;
    gp_einfo->num_stop_xstream_fn = 0;
    gp_einfo->stop_xstream_fn = (ABT_event_cb_fn *)
        ABTU_calloc(ABTI_DEFAULT_MAX_CB_FN * 2, sizeof(ABT_event_cb_fn));
    gp_einfo->stop_xstream_arg = (void **)
        ABTU_calloc(ABTI_DEFAULT_MAX_CB_FN * 2, sizeof(void *));

    gp_einfo->max_add_xstream_fn = ABTI_DEFAULT_MAX_CB_FN;
    gp_einfo->num_add_xstream_fn = 0;
    gp_einfo->add_xstream_fn = (ABT_event_cb_fn *)
        ABTU_calloc(ABTI_DEFAULT_MAX_CB_FN * 2, sizeof(ABT_event_cb_fn));
    gp_einfo->add_xstream_arg = (void **)
        ABTU_calloc(ABTI_DEFAULT_MAX_CB_FN * 2, sizeof(void *));

    ABTI_event_connect_power(gp_ABTI_global->pm_host, gp_ABTI_global->pm_port);
#endif
#ifdef ABT_CONFIG_PUBLISH_INFO
    if (gp_ABTI_global->pub_needed == ABT_TRUE) {
        if (!strcmp(gp_ABTI_global->pub_filename, "stdout")) {
            gp_einfo->out_file = stdout;
        } else if (!strcmp(gp_ABTI_global->pub_filename, "stderr")) {
            gp_einfo->out_file = stderr;
        } else {
            gp_einfo->out_file = fopen(gp_ABTI_global->pub_filename, "w");
            if (!gp_einfo->out_file) {
                gp_ABTI_global->pub_needed = ABT_FALSE;
            }
        }

        if (gp_einfo->out_file) {
            gethostname(gp_einfo->hostname, 100);
            gp_einfo->max_xstream_rank = gp_ABTI_global->max_xstreams;
            gp_einfo->num_threads = (uint32_t *)ABTU_calloc(
                    gp_einfo->max_xstream_rank, sizeof(uint32_t));
            gp_einfo->num_tasks = (uint32_t *)ABTU_calloc(
                    gp_einfo->max_xstream_rank, sizeof(uint32_t));
            gp_einfo->idle_time = (double *)ABTU_calloc(
                    gp_einfo->max_xstream_rank, sizeof(double));
            gp_einfo->old_num_units = (uint32_t *)ABTU_calloc(
                    gp_einfo->max_xstream_rank, sizeof(uint32_t));
            gp_einfo->old_timestamp = (double *)ABTU_calloc(
                    gp_einfo->max_xstream_rank, sizeof(double));
            gp_einfo->timestamp = ABT_get_wtime();
        }
    }
#endif
}

void ABTI_event_finalize(void)
{
#ifdef ABT_CONFIG_HANDLE_POWER_EVENT
    ABTI_event_disconnect_power();

    ABTU_free(gp_einfo->stop_xstream_fn);
    ABTU_free(gp_einfo->stop_xstream_arg);
    ABTU_free(gp_einfo->add_xstream_fn);
    ABTU_free(gp_einfo->add_xstream_arg);
#endif
#ifdef ABT_CONFIG_PUBLISH_INFO
    if (gp_ABTI_global->pub_needed == ABT_TRUE) {
        if (gp_einfo->out_file != stdout && gp_einfo->out_file != stderr) {
            fclose(gp_einfo->out_file);
            gp_einfo->out_file = NULL;
        }
        ABTU_free(gp_einfo->num_threads);
        ABTU_free(gp_einfo->num_tasks);
        ABTU_free(gp_einfo->idle_time);
    }
#endif

    ABTU_free(gp_einfo);
    gp_einfo = NULL;
}


#ifdef ABT_CONFIG_HANDLE_POWER_EVENT
void ABTI_event_connect_power(char *p_host, int port)
{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    int ret;

    server = gethostbyname(p_host);
    if (server == NULL) {
        LOG_DEBUG("Power mgmt. (%s:%d) not available\n", p_host, port);
        return;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG_DEBUG("Power event: socket failed%s\n", "");
        return;
    }

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(port);

    ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret < 0) {
        LOG_DEBUG("Power mgmt. (%s:%d) connect failed\n", p_host, port);
        return;
    }
    gp_einfo->pfd.fd = sockfd;
    gp_einfo->pfd.events = POLLIN;

    gp_ABTI_global->pm_connected = ABT_TRUE;

    LOG_DEBUG("Power mgmt. (%s:%d) connected\n", p_host, port);
}

void ABTI_event_disconnect_power(void)
{
    if (gp_ABTI_global->pm_connected == ABT_FALSE) return;

    close(gp_einfo->pfd.fd);
    gp_ABTI_global->pm_connected = ABT_FALSE;

    LOG_DEBUG("power mgmt. disconnected%s\n", "");
}

void ABTI_event_send_num_xstream(void)
{
    char send_buf[ABTI_MSG_BUF_LEN];
    int num_xstreams;
    int n;

    ABT_xstream_get_num(&num_xstreams);

    sprintf(send_buf, "%d", num_xstreams);
    n = write(gp_einfo->pfd.fd, send_buf, strlen(send_buf));
    ABTI_ASSERT(n == strlen(send_buf));
}

static void ABTI_event_free_xstream(void *arg)
{
    char send_buf[ABTI_MSG_BUF_LEN];
    ABT_xstream xstream = (ABT_xstream)arg;
    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    int abt_errno, n;

    while (p_xstream->state != ABT_XSTREAM_STATE_TERMINATED) {
        ABT_thread_yield();
    }

    abt_errno = ABT_xstream_join(xstream);
    ABTI_ASSERT(abt_errno == ABT_SUCCESS);
    abt_errno = ABT_xstream_free(&xstream);
    ABTI_ASSERT(abt_errno == ABT_SUCCESS);

    if (gp_ABTI_global->pm_connected == ABT_TRUE) {
        LOG_DEBUG("# of ESs: %d\n", gp_ABTI_global->num_xstreams);
        sprintf(send_buf, "done (%d)", gp_ABTI_global->num_xstreams);
        n = write(gp_einfo->pfd.fd, send_buf, strlen(send_buf));
        ABTI_ASSERT(n == strlen(send_buf));
    }
}

ABT_bool ABTI_event_stop_xstream(ABTI_xstream *p_xstream)
{
    int abt_errno = ABT_SUCCESS;
    ABT_bool can_stop = ABT_TRUE;
    ABT_event_cb_fn cb_fn;
    ABT_xstream xstream = ABTI_xstream_get_handle(p_xstream);
    ABT_xstream primary;
    ABT_pool pool;
    int i;

    /* Ask whether the target ES can be stopped */
    for (i = 0; i < gp_einfo->max_stop_xstream_fn; i++) {
        cb_fn = gp_einfo->stop_xstream_fn[i*2];
        if (cb_fn) {
            can_stop = cb_fn(gp_einfo->stop_xstream_arg[i*2], xstream);
            if (can_stop == ABT_FALSE) break;
        }
    }

    if (can_stop == ABT_TRUE) {
        ABTI_xstream_set_request(p_xstream, ABTI_XSTREAM_REQ_STOP);

        /* Execute action callback functions */
        for (i = 0; i < gp_einfo->max_stop_xstream_fn; i++) {
            cb_fn = gp_einfo->stop_xstream_fn[i*2+1];
            if (cb_fn) {
                cb_fn(gp_einfo->stop_xstream_arg[i*2+1], xstream);
            }
        }

        /* Create a ULT on the primary ES to join the target ES */
        primary = ABTI_xstream_get_handle(gp_ABTI_global->p_xstreams[0]);
        ABT_xstream_get_main_pools(primary, 1, &pool);
        abt_errno = ABT_thread_create(pool, ABTI_event_free_xstream, xstream,
                                      ABT_THREAD_ATTR_NULL, NULL);
        ABTI_ASSERT(abt_errno == ABT_SUCCESS);
    }

    return can_stop;
}

void ABTI_event_decrease_xstream(int target_rank)
{
    char send_buf[ABTI_MSG_BUF_LEN];
    ABTI_xstream *p_xstream;
    int rank, n;
    ABT_bool can_stop = ABT_FALSE;
    ABTI_global *p_global = gp_ABTI_global;

    if (p_global->num_xstreams == 1) {
        LOG_DEBUG("Cannot shrink: # of ESs (%d)\n", p_global->num_xstreams);
        sprintf(send_buf, "min");
        n = write(gp_einfo->pfd.fd, send_buf, strlen(send_buf));
        ABTI_ASSERT(n == strlen(send_buf));
        return;
    }

    if (target_rank == ABT_XSTREAM_ANY_RANK) {
        /* Determine the ES to shut down.  For now, we try to shut down the most
         * recently created one. */
        for (rank = p_global->num_xstreams - 1; rank > 0; rank--) {
            p_xstream = p_global->p_xstreams[rank];
            if (p_xstream) {
                can_stop = ABTI_event_stop_xstream(p_xstream);
                if (can_stop == ABT_TRUE) break;
            }
        }
    } else {
        /* Stop a specific ES */
        if (target_rank < p_global->max_xstreams) {
            p_xstream = p_global->p_xstreams[target_rank];
            if (p_xstream) {
                can_stop = ABTI_event_stop_xstream(p_xstream);
            }
        }
    }

    /* We couldn't stop an ES */
    if (can_stop == ABT_FALSE) {
        sprintf(send_buf, "failed");
        n = write(gp_einfo->pfd.fd, send_buf, strlen(send_buf));
        ABTI_ASSERT(n == strlen(send_buf));
    }
}

void ABTI_event_increase_xstream(int target_rank)
{
    void *abt_arg = (void *)(intptr_t)target_rank;
    char send_buf[ABTI_MSG_BUF_LEN];
    ABT_event_cb_fn cb_fn;
    ABT_bool ret;
    int i, n;

    for (i = 0; i < gp_einfo->max_add_xstream_fn; i++) {
        /* "ask" callback */
        cb_fn = gp_einfo->add_xstream_fn[i*2];
        if (!cb_fn) continue;

        /* TODO: fairness */
        ret = cb_fn(gp_einfo->add_xstream_arg[i*2], abt_arg);
        if (ret == ABT_TRUE) {
            /* "act" callback */
            cb_fn = gp_einfo->add_xstream_fn[i*2+1];
            if (!cb_fn) continue;

            ret = cb_fn(gp_einfo->add_xstream_arg[i*2+1], abt_arg);
            if (ret == ABT_TRUE) {
                LOG_DEBUG("# of ESs: %d\n", gp_ABTI_global->num_xstreams);
                sprintf(send_buf, "done (%d)", gp_ABTI_global->num_xstreams);
                goto send_ack;
            }
        }
    }

    /* We couldn't create a new ES */
    sprintf(send_buf, "failed");

  send_ack:
    n = write(gp_einfo->pfd.fd, send_buf, strlen(send_buf));
    ABTI_ASSERT(n == strlen(send_buf));
}

ABT_bool ABTI_event_check_power(void)
{
    ABT_bool stop_xstream = ABT_FALSE;
    int rank, n, ret;
    char recv_buf[ABTI_MSG_BUF_LEN];
    ABTI_xstream *p_xstream;

    if (gp_ABTI_global->pm_connected == ABT_FALSE) goto fn_exit;

    ABT_xstream_self_rank(&rank);

    ret = ABTI_mutex_trylock(&gp_einfo->mutex);
    if (ret == ABT_ERR_MUTEX_LOCKED) goto fn_exit;
    ABTI_ASSERT(ret == ABT_SUCCESS);

    ret = poll(&gp_einfo->pfd, 1, 1);
    if (ret == -1) {
        LOG_DEBUG("ERROR: poll (%d)\n", ret);
        ABTI_ASSERT(0);
    } else if (ret != 0) {
        if (gp_einfo->pfd.revents & POLLIN) {
            bzero(recv_buf, ABTI_MSG_BUF_LEN);
            n = read(gp_einfo->pfd.fd, recv_buf, ABTI_MSG_BUF_LEN);
            ABTI_ASSERT(n > 0);

            char *cmd = ABTU_strtrim(recv_buf);
            LOG_DEBUG("ES%d: received request '%s'\n", rank, cmd);
            switch (cmd[0]) {
                case 'd':
                    ABTI_event_decrease_xstream(ABT_XSTREAM_ANY_RANK);
                    break;

                case 's':
                    rank = atoi(&cmd[1]);
                    ABTI_event_decrease_xstream(rank);
                    break;

                case 'i':
                    ABTI_event_increase_xstream(ABT_XSTREAM_ANY_RANK);
                    break;

                case 'c':
                    rank = atoi(&cmd[1]);
                    ABTI_event_increase_xstream(rank);
                    break;

                case 'n':
                    ABTI_event_send_num_xstream();
                    break;

                case 'q':
                    ABTI_event_disconnect_power();
                    break;

                default:
                    LOG_DEBUG("Unknown command: %s\n", cmd);
                    break;
            }
        }

        if (gp_einfo->pfd.revents & POLLHUP) {
            gp_ABTI_global->pm_connected = ABT_FALSE;
            LOG_DEBUG("Server disconnected...%s\n", "");
        }
        gp_einfo->pfd.revents = 0;
    }

    ABTI_mutex_unlock(&gp_einfo->mutex);

    p_xstream = ABTI_local_get_xstream();
    if (p_xstream->request & ABTI_XSTREAM_REQ_STOP) {
        stop_xstream = ABT_TRUE;
    }

 fn_exit:
    return stop_xstream;
}
#endif /* ABT_CONFIG_HANDLE_POWER_EVENT */

/**
 * @ingroup EVENT
 * @brief   Add callback functions for a specific event.
 *
 * \c ABT_event_add_callback() adds two callback functions for a specified
 * event, \c event, and returns a unique ID through \c cb_id.  \c cb_id can be
 * used to delete registered callbacks in \c ABT_event_del_callback().  All
 * registered callbacks will be invoked when the event happens.
 *
 * @param[in] event         event kind
 * @param[in] ask_cb        callback to ask whether the event can be handled
 * @param[in] ask_user_arg  user argument for \c ask_cb
 * @param[in] act_cb        callback to notify that the event will be handled
 * @param[in] act_user_arg  user argument for \c act_cb
 * @param[out] cb_id        callback ID
 * @return Error code
 * @retval ABT_SUCCESS on success
 */
int ABT_event_add_callback(ABT_event_kind event,
                           ABT_event_cb_fn ask_cb, void *ask_user_arg,
                           ABT_event_cb_fn act_cb, void *act_user_arg,
                           int *cb_id)
{
#ifdef ABT_CONFIG_HANDLE_POWER_EVENT
    int abt_errno = ABT_SUCCESS;
    int cur_num, max_num;
    size_t new_size;
    int cid = -1;
    int i;

    ABTI_mutex_spinlock(&gp_einfo->mutex);
    switch (event) {
        case ABT_EVENT_STOP_XSTREAM:
            cur_num = gp_einfo->num_stop_xstream_fn;
            max_num = gp_einfo->max_stop_xstream_fn;
            if (cur_num == max_num) {
                /* We need to allocate more space */
                max_num = max_num * 2;
                gp_einfo->max_stop_xstream_fn = max_num;
                new_size = max_num * 2 * sizeof(ABT_event_cb_fn);
                gp_einfo->stop_xstream_fn = (ABT_event_cb_fn *)
                    ABTU_realloc(gp_einfo->stop_xstream_fn, new_size);
                new_size = max_num * 2 * sizeof(void *);
                gp_einfo->stop_xstream_arg = (void **)
                    ABTU_realloc(gp_einfo->stop_xstream_arg, new_size);
            }
            ABTI_ASSERT(cur_num < max_num);

            if (gp_einfo->stop_xstream_fn[cur_num*2] == NULL) {
                gp_einfo->stop_xstream_fn[cur_num*2] = ask_cb;
                gp_einfo->stop_xstream_arg[cur_num*2] = ask_user_arg;
                gp_einfo->stop_xstream_fn[cur_num*2+1] = act_cb;
                gp_einfo->stop_xstream_arg[cur_num*2+1] = act_user_arg;
                cid = cur_num;
            } else {
                for (i = 0; i < max_num; i++) {
                    if (gp_einfo->stop_xstream_fn[i*2] == NULL) {
                        gp_einfo->stop_xstream_fn[i*2] = ask_cb;
                        gp_einfo->stop_xstream_arg[i*2] = ask_user_arg;
                        gp_einfo->stop_xstream_fn[i*2+1] = act_cb;
                        gp_einfo->stop_xstream_arg[i*2+1] = act_user_arg;
                        cid = i;
                        break;
                    }
                }
                ABTI_ASSERT(i < max_num);
            }
            gp_einfo->num_stop_xstream_fn++;
            break;

        case ABT_EVENT_ADD_XSTREAM:
            cur_num = gp_einfo->num_add_xstream_fn;
            max_num = gp_einfo->max_add_xstream_fn;
            if (cur_num == max_num) {
                /* We need to allocate more space */
                max_num = max_num * 2;
                gp_einfo->max_add_xstream_fn = max_num;
                new_size = max_num * 2 * sizeof(ABT_event_cb_fn);
                gp_einfo->add_xstream_fn = (ABT_event_cb_fn *)
                    ABTU_realloc(gp_einfo->add_xstream_fn, new_size);
                new_size = max_num * 2 * sizeof(void *);
                gp_einfo->add_xstream_arg = (void **)
                    ABTU_realloc(gp_einfo->add_xstream_arg, new_size);
            }
            ABTI_ASSERT(cur_num < max_num);

            if (gp_einfo->add_xstream_fn[cur_num*2] == NULL) {
                gp_einfo->add_xstream_fn[cur_num*2] = ask_cb;
                gp_einfo->add_xstream_arg[cur_num*2] = ask_user_arg;
                gp_einfo->add_xstream_fn[cur_num*2+1] = act_cb;
                gp_einfo->add_xstream_arg[cur_num*2+1] = act_user_arg;
                cid = cur_num;
            } else {
                for (i = 0; i < max_num; i++) {
                    if (gp_einfo->add_xstream_fn[i*2] == NULL) {
                        gp_einfo->add_xstream_fn[i*2] = ask_cb;
                        gp_einfo->add_xstream_arg[i*2] = ask_user_arg;
                        gp_einfo->add_xstream_fn[i*2+1] = act_cb;
                        gp_einfo->add_xstream_arg[i*2+1] = act_user_arg;
                        cid = i;
                        break;
                    }
                }
                ABTI_ASSERT(i < max_num);
            }
            gp_einfo->num_add_xstream_fn++;
            break;

        default:
            abt_errno = ABT_ERR_INV_EVENT;
            goto fn_fail;
            break;
    }

  fn_exit:
    ABTI_mutex_unlock(&gp_einfo->mutex);
    *cb_id = cid;
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
#else
    return ABT_SUCCESS;
#endif
}

/**
 * @ingroup EVENT
 * @brief   Delete callback functions registered for a specific event.
 *
 * \c ABT_event_del_callback() deletes callback functions that are registered
 * for \c event with the ID \c cb_id.
 *
 * @param[in] event  event kind
 * @param[in] cb_id  callback ID to delete
 * @return Error code
 * @retval ABT_SUCCESS on success
 */
int ABT_event_del_callback(ABT_event_kind event, int cb_id)
{
#ifdef ABT_CONFIG_HANDLE_POWER_EVENT
    int abt_errno = ABT_SUCCESS;
    ABTI_mutex_spinlock(&gp_einfo->mutex);
    switch (event) {
        case ABT_EVENT_STOP_XSTREAM:
            gp_einfo->stop_xstream_fn[cb_id*2] = NULL;
            gp_einfo->stop_xstream_fn[cb_id*2+1] = NULL;
            gp_einfo->stop_xstream_arg[cb_id*2] = NULL;
            gp_einfo->stop_xstream_arg[cb_id*2+1] = NULL;
            gp_einfo->num_stop_xstream_fn++;
            break;

        case ABT_EVENT_ADD_XSTREAM:
            gp_einfo->add_xstream_fn[cb_id*2] = NULL;
            gp_einfo->add_xstream_fn[cb_id*2+1] = NULL;
            gp_einfo->add_xstream_arg[cb_id*2] = NULL;
            gp_einfo->add_xstream_arg[cb_id*2+1] = NULL;
            gp_einfo->num_add_xstream_fn++;
            break;

        default:
            abt_errno = ABT_ERR_INV_EVENT;
            goto fn_fail;
            break;
    }

  fn_exit:
    ABTI_mutex_unlock(&gp_einfo->mutex);
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
#else
    return ABT_SUCCESS;
#endif
}

#ifdef ABT_CONFIG_PUBLISH_INFO
void ABTI_event_realloc_pub_arrays(int size)
{
    ABTI_ASSERT(0);
}

void ABTI_event_inc_unit_cnt(ABTI_xstream *p_xstream, ABT_unit_type type)
{
    if (gp_ABTI_global->pub_needed == ABT_FALSE) return;

    int rank = (int)p_xstream->rank;

    if (rank > gp_einfo->max_xstream_rank) {
        ABTI_event_realloc_pub_arrays(rank);
    }

    if (type == ABT_UNIT_TYPE_THREAD) {
        ABTD_atomic_fetch_add_uint32(&gp_einfo->num_threads[rank], 1);
    } else if (type == ABT_UNIT_TYPE_TASK) {
        ABTD_atomic_fetch_add_uint32(&gp_einfo->num_tasks[rank], 1);
    }
}

void ABTI_event_publish_info(void)
{
    ABTI_xstream *p_xstream;
    int rank, i, ret;
    double cur_time, elapsed_time;
    double idle_time, idle_ratio;
    uint32_t cur_num_units, num_threads, num_tasks;
    FILE *fp;
    ABT_bool is_first;

    if (gp_ABTI_global->pub_needed == ABT_FALSE) return;

    p_xstream = ABTI_local_get_xstream();
    rank = (int)p_xstream->rank;
    if (rank > gp_einfo->max_xstream_rank) {
        ABTI_event_realloc_pub_arrays(rank);
    }

    cur_time = ABT_get_wtime();
    elapsed_time = cur_time - gp_einfo->timestamp;

    /* Update the idle time of the current ES */
    cur_num_units = gp_einfo->num_threads[rank]
                  + gp_einfo->num_tasks[rank];
    if (gp_einfo->old_timestamp[rank] > 0.0) {
        if (cur_num_units == gp_einfo->old_num_units[rank]) {
            idle_time = cur_time - gp_einfo->old_timestamp[rank];
            ABTD_atomic_fetch_add_double(&gp_einfo->idle_time[rank], idle_time);
        }
    }
    gp_einfo->old_num_units[rank] = cur_num_units;
    gp_einfo->old_timestamp[rank] = cur_time;

    if (elapsed_time < gp_ABTI_global->pub_interval) return;

    /* Only one scheduler has to write to the output file. */
    ret = ABTI_mutex_trylock(&gp_einfo->mutex);
    if (ret == ABT_ERR_MUTEX_LOCKED) return;
    ABTI_ASSERT(ret == ABT_SUCCESS);

    /* Update timestamp */
    gp_einfo->timestamp = cur_time;

    fp = gp_einfo->out_file;
    fprintf(fp, "{\"sample\":\"argobots\","
                "\"time\":%.3f,\"node\":\"%s\",\"num_es\":%d,",
            cur_time, gp_einfo->hostname, gp_ABTI_global->num_xstreams);

    is_first = ABT_TRUE;
    fprintf(fp, "\"num_threads\":{");
    for (i = 0; i < gp_ABTI_global->max_xstreams; i++) {
        num_threads = gp_einfo->num_threads[i];
        if (num_threads > 0) {
            ABTD_atomic_fetch_sub_uint32(&gp_einfo->num_threads[i], num_threads);
        }
        if (gp_ABTI_global->p_xstreams[i]) {
            if (is_first == ABT_TRUE) {
                is_first = ABT_FALSE;
            } else {
                fprintf(fp, ",");
            }
            fprintf(fp, "\"es%d\":%d", i, num_threads);
        }
    }
    fprintf(fp, "},");

    is_first = ABT_TRUE;
    fprintf(fp, "\"num_tasks\":{");
    for (i = 0; i < gp_ABTI_global->max_xstreams; i++) {
        num_tasks = gp_einfo->num_tasks[i];
        if (num_tasks > 0) {
            ABTD_atomic_fetch_sub_uint32(&gp_einfo->num_tasks[i], num_tasks);
        }
        if (gp_ABTI_global->p_xstreams[i]) {
            if (is_first == ABT_TRUE) {
                is_first = ABT_FALSE;
            } else {
                fprintf(fp, ",");
            }
            fprintf(fp, "\"es%d\":%d", i, num_tasks);
        }
    }
    fprintf(fp, "},");

    is_first = ABT_TRUE;
    fprintf(fp, "\"idle\":{");
    for (i = 0; i < gp_ABTI_global->max_xstreams; i++) {
        idle_time = gp_einfo->idle_time[i];
        if (idle_time > 0.0) {
            ABTD_atomic_fetch_sub_double(&gp_einfo->idle_time[i], idle_time);
        }
        if (gp_ABTI_global->p_xstreams[i]) {
            if (is_first == ABT_TRUE) {
                is_first = ABT_FALSE;
            } else {
                fprintf(fp, ",");
            }
            idle_ratio = idle_time / elapsed_time * 100.0;
            fprintf(fp, "\"es%d\":%.1f", i, idle_ratio);
        }
    }
    fprintf(fp, "}");

    fprintf(fp, "}\n");
    fflush(fp);

    ABTI_mutex_unlock(&gp_einfo->mutex);
}
#endif
