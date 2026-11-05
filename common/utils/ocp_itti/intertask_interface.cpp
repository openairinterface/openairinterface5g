/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
*/

#include <vector>
#include <map>
#include <sys/eventfd.h>
#include <semaphore.h>


extern "C" {
#include <intertask_interface.h>
#include <common/utils/system.h>
#include <common/utils/utils.h>
#include "executables/softmodem-common.h"

typedef struct timer_elm_s {
  timer_type_t type; ///< Timer type
  long instance;
  long duration;
  uint64_t timeout;
  void *timer_arg; ///< Optional argument that will be passed when timer expires
} timer_elm_t;

  typedef struct task_list_s {
    task_info_t admin;
    ittiTask_parms_t task_parms;
    pthread_t thread;
    pthread_mutex_t queue_cond_lock;
    std::vector<MessageDef *> message_queue;
    std::map<long,timer_elm_t> timer_map;
    uint64_t next_timer=UINT64_MAX;
    int nb_fd_epoll=0;
    int epoll_fd=-1;
    int sem_fd=-1;
    size_t last_log_size = 0;
  } task_list_t;

  int timer_expired(int fd);
  static task_list_t **tasks=NULL;
  static int nb_queues=0;
  static pthread_mutex_t lock_nb_queues;

  void *itti_malloc(task_id_t origin_task_id, task_id_t destination_task_id, ssize_t size)
  {
    UNUSED(origin_task_id);
    UNUSED(destination_task_id);
    return calloc_or_fail(1, size);
  }

  int itti_free(task_id_t task_id, void *ptr) {
    AssertFatal (ptr != NULL, "Trying to free a NULL pointer (%d)!\n", task_id);
    free (ptr);
    return (EXIT_SUCCESS);
  }

  // in the two following functions, the +32 in malloc is there to deal with gcc memory alignment
  // because a struct size can be larger than sum(sizeof(struct components))
  // We should remove the itti principle of a huge union for all types of messages in paramter "msg_t ittiMsg"
  // to use a more C classical pointer casting "void * ittiMsg", later casted in the right struct
  // but we would have to change all legacy macros, as per this example
  // #define S1AP_REGISTER_ENB_REQ(mSGpTR)           (mSGpTR)->ittiMsg.s1ap_register_enb_req
  // would become
  // #define S1AP_REGISTER_ENB_REQ(mSGpTR)           (s1ap_register_enb_req) mSGpTR)->ittiMsg
  MessageDef *itti_alloc_new_message_sized(task_id_t origin_task_id, instance_t originInstance, MessagesIds message_id, MessageHeaderSize size) {
    MessageDef *temp = (MessageDef *)itti_malloc (origin_task_id, TASK_UNKNOWN, sizeof(MessageHeader) +32 + size);
    temp->ittiMsgHeader.messageId = message_id;
    temp->ittiMsgHeader.originTaskId = origin_task_id;
    temp->ittiMsgHeader.ittiMsgSize = size;
    return temp;
  }

  MessageDef *itti_alloc_new_message(task_id_t origin_task_id, instance_t originInstance, MessagesIds message_id) {
    int size=sizeof(MessageHeader) + 32 + messages_info[message_id].size;
    MessageDef *temp = (MessageDef *)itti_malloc (origin_task_id, TASK_UNKNOWN, size);
    temp->ittiMsgHeader.messageId = message_id;
    temp->ittiMsgHeader.originTaskId = origin_task_id;
    temp->ittiMsgHeader.ittiMsgSize = size;
    temp->ittiMsgHeader.destinationTaskId=TASK_UNKNOWN;
    temp->ittiMsgHeader.originInstance=originInstance;
    temp->ittiMsgHeader.destinationInstance=0;
    temp->ittiMsgHeader.lte_time= {0};
    return temp;
    //return itti_alloc_new_message_sized(origin_task_id, message_id, messages_info[message_id].size);
  }

  static inline int itti_send_msg_to_task_locked(task_id_t destination_task_id, instance_t destinationInstance, MessageDef *message) {
    task_list_t *t=tasks[destination_task_id];
    message->ittiMsgHeader.destinationTaskId = destination_task_id;
    message->ittiMsgHeader.destinationInstance = destinationInstance;
    message->ittiMsgHeader.lte_time.frame = 0;
    message->ittiMsgHeader.lte_time.slot = 0;
    int message_id = message->ittiMsgHeader.messageId;
    size_t s=t->message_queue.size();

    // to reduce the number of logs, we give a message each increase of 25%
    if ((s > t->last_log_size * 1.25) && (s > t->admin.queue_size / 10)) {
      if (s > t->admin.queue_size) {
        LOG_E(TMR, "Queue for %s task contains %ld messages\n", itti_get_task_name(destination_task_id), s);
      } else {
        LOG_I(ITTI,
              "Queue for %s task size: %ld (last message %s)\n",
              itti_get_task_name(destination_task_id),
              s + 1,
              ITTI_MSG_NAME(message));
      }
      t->last_log_size = s;
    } else if (t->last_log_size && s < t->admin.queue_size / 10) {
      // Inform when the queue decreases
      LOG_I(ITTI, "Queue for %s task size is back under 10%% of max size\n", itti_get_task_name(destination_task_id));
      t->last_log_size = 0;
    }

    t->message_queue.insert(t->message_queue.begin(), message);
    eventfd_t sem_counter = 1;
    int ret = write(t->sem_fd, &sem_counter, sizeof(sem_counter));
    AssertFatal(ret == sizeof(sem_counter), "write failed: %s", strerror(errno));
    LOG_D(ITTI, "sent messages id=%s messages_info to %s\n", messages_info[message_id].name, t->admin.name);
    return 0;
  }

  int itti_send_msg_to_task(task_id_t destination_task_id, instance_t destinationInstance, MessageDef *message) {
    task_list_t *t=tasks[destination_task_id];
    pthread_mutex_lock (&t->queue_cond_lock);
    int ret=itti_send_msg_to_task_locked(destination_task_id, destinationInstance, message);

    while (t->message_queue.size() > 0 && t->task_parms.shortcut_func != NULL) {
      if (t->message_queue.size()>1)
        LOG_W(ITTI,"queue in no thread mode is %ld\n", t->message_queue.size());

      pthread_mutex_unlock (&t->queue_cond_lock);
      t->task_parms.shortcut_func(NULL);
      pthread_mutex_lock (&t->queue_cond_lock);
    }

    pthread_mutex_unlock (&t->queue_cond_lock);
    return ret;
  }

  void itti_subscribe_event_fd(task_id_t task_id, int fd) {
    struct epoll_event event;
    task_list_t *t=tasks[task_id];
    t->nb_fd_epoll++;
    event.events  = EPOLLIN | EPOLLERR;
    event.data.u64 = 0;
    event.data.fd  = fd;
    int ret = epoll_ctl(t->epoll_fd, EPOLL_CTL_ADD, fd, &event);
    AssertFatal(!ret,
                "epoll_ctl (EPOLL_CTL_ADD) failed for task %s, fd %d: %s!\n",
                itti_get_task_name(task_id),
                fd,
                strerror(errno));
    eventfd_t sem_counter = 1;
    ret = write(t->sem_fd, &sem_counter, sizeof(sem_counter));
    AssertFatal(ret == sizeof(sem_counter), "write failed: %s", strerror(errno));
  }

  void itti_unsubscribe_event_fd(task_id_t task_id, int fd) {
    task_list_t *t=tasks[task_id];
    int ret = epoll_ctl(t->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    AssertFatal(!ret,
                "epoll_ctl (EPOLL_CTL_DEL) failed for task %s, fd %d: %s!\n",
                itti_get_task_name(task_id),
                fd,
                strerror(errno));
    t->nb_fd_epoll--;
  }

  static inline int itti_get_events_locked(task_id_t task_id, struct epoll_event *events, int max_events) {
    task_list_t *t=tasks[task_id];
    uint64_t current_time=0;
    int nb_events;
    do {
      if ( t->next_timer != UINT64_MAX ) {
        struct timespec tp;
        clock_gettime(CLOCK_MONOTONIC, &tp);
        current_time=(uint64_t)tp.tv_sec*1000+tp.tv_nsec/(1000*1000);

        if ( t->next_timer < current_time) {
          t->next_timer=UINT64_MAX;

          // Proceed expired timer
          for ( auto it=t->timer_map.begin(), next_it = it; it != t->timer_map.end() ; it = next_it ) {
            ++next_it;

            if ( it->second.timeout < current_time ) {
              MessageDef *message = itti_alloc_new_message(TASK_TIMER, it->second.instance,TIMER_HAS_EXPIRED);
              message->ittiMsg.timer_has_expired.timer_id=it->first;
              message->ittiMsg.timer_has_expired.arg=it->second.timer_arg;

              if (itti_send_msg_to_task_locked(task_id, it->second.instance, message) < 0) {
                LOG_W(ITTI,"Failed to send msg TIMER_HAS_EXPIRED to task %u\n", task_id);
                free(message);
                t->timer_map.erase(it);
                return -1;
              }

              if ( it->second.type==TIMER_PERIODIC ) {
                it->second.timeout+=it->second.duration;

                if (it->second.timeout < t->next_timer)
                  t->next_timer=it->second.timeout;
              } else
                t->timer_map.erase(it);
            } else if (it->second.timeout < t->next_timer)
              t->next_timer=it->second.timeout;
          }
        }
      }

      int epoll_timeout = -1;

      if ( t->next_timer != UINT64_MAX )
        epoll_timeout = t->next_timer-current_time;

      pthread_mutex_unlock(&t->queue_cond_lock);
      LOG_D(ITTI,"enter blocking wait for %s, timeout: %d ms\n", itti_get_task_name(task_id),  epoll_timeout);
      nb_events = epoll_wait(t->epoll_fd, events, max_events, epoll_timeout);

      if ( nb_events  < 0 && (errno == EINTR || errno == EAGAIN ) )
        pthread_mutex_lock(&t->queue_cond_lock);
    } while (nb_events  < 0 && (errno == EINTR || errno == EAGAIN ) );

    AssertFatal (nb_events >=0,
                 "epoll_wait failed for task %s, nb fds %d, timeout %lu: %s!\n",
                 itti_get_task_name(task_id), t->nb_fd_epoll, 
                 t->next_timer != UINT64_MAX ? t->next_timer-current_time : -1, 
                 strerror(errno));
    LOG_D(ITTI,"receive on %d descriptors for %s\n", nb_events, itti_get_task_name(task_id));

    if (nb_events == 0)
      /* No data to read -> return */
      return 0;

    for (int i = 0; i < nb_events; i++) {
      /* Check if there is an event for ITTI for the event fd */
      if ((events[i].events & EPOLLIN) &&
          (events[i].data.fd == t->sem_fd)) {
        eventfd_t   sem_counter;
        /* Read will always return 1 */
        int ret = read(t->sem_fd, &sem_counter, sizeof(sem_counter));
        AssertFatal(ret == sizeof(sem_counter), "write failed: %s", strerror(errno));

        /* Mark that the event has been processed */
        events[i].events &= ~EPOLLIN;
      }
    }

    return nb_events;
  }

  int itti_get_events(task_id_t task_id, struct epoll_event *events, int nb_evts) {
    pthread_mutex_lock(&tasks[task_id]->queue_cond_lock);
    return itti_get_events_locked(task_id, events, nb_evts);
  }

  void itti_receive_msg(task_id_t task_id, MessageDef **received_msg) {
    // Reception of one message, blocking caller
    task_list_t *t=tasks[task_id];
    pthread_mutex_lock(&t->queue_cond_lock);

    struct epoll_event events[t->nb_fd_epoll];
    // Weird condition to deal with crap legacy itti interface
    if (t->message_queue.empty()) {
      do {
        itti_get_events_locked(task_id, events, t->nb_fd_epoll);
        pthread_mutex_lock(&t->queue_cond_lock);
      }
      while (t->message_queue.empty() && t->nb_fd_epoll == 1);
    }

    // Legacy design: we return even if we have no message
    // in this case, *received_msg is NULL
    if (t->message_queue.empty()) {
      *received_msg=NULL;
      LOG_D(ITTI,"task %s received even from other fd (total fds: %d), returning msg NULL\n",t->admin.name, t->nb_fd_epoll);
    } else {
      *received_msg=t->message_queue.back();
      t->message_queue.pop_back();
      LOG_D(ITTI,"task %s received a message\n",t->admin.name);
    }

    pthread_mutex_unlock (&t->queue_cond_lock);
  }

  void itti_poll_msg(task_id_t task_id, MessageDef **received_msg) {
    //reception of one message, non-blocking
    task_list_t *t=tasks[task_id];
    pthread_mutex_lock(&t->queue_cond_lock);

    if (!t->message_queue.empty()) {
      LOG_D(ITTI,"task %s received a message in polling mode\n",t->admin.name);
      *received_msg=t->message_queue.back();
      t->message_queue.pop_back();
    } else
      *received_msg=NULL;

    pthread_mutex_unlock (&t->queue_cond_lock);
  }

  int itti_create_task(const task_id_t task_id, void *(*start_routine)(void *), const ittiTask_parms_t *parms)
  {
    task_list_t *t=tasks[task_id];
    if (get_softmodem_params()->no_itti && task_id < sizeofArray(tasks_info) && parms && parms->shortcut_func) {
      LOG_W(ITTI, "not starting the thread for %s, the msg processing will be done in place\n", tasks_info[task_id].name);
      t->task_parms.shortcut_func = parms->shortcut_func;
      return 0;
    }
    threadCreate(&t->thread,
                 start_routine,
                 parms ? parms->args_to_start_routine : NULL,
                 (char *)itti_get_task_name(task_id),
                 -1,
                 // rfsim runs as fast as possible, the upper layer tasks need equal priority
                 IS_SOFTMODEM_RFSIM ? OAI_PRIORITY_RT_MAX : OAI_PRIORITY_RT);
    LOG_D(ITTI,"Created Posix thread %s\n",  itti_get_task_name(task_id) );
    return 0;
  }

  void itti_exit_task(void) {
    pthread_exit (NULL);
  }

  void itti_terminate_tasks(task_id_t task_id) {
    // Sends Terminate signals to all tasks.
    itti_send_terminate_message (task_id);
    usleep(100*1000); // Allow the tasks to receive the message before going returning to main thread
  }

  int itti_create_queue(const task_info_t *taskInfo) {
    pthread_mutex_lock (&lock_nb_queues);
    int newQueue=nb_queues++;
    task_list_t **new_tasks = (task_list_t **)realloc(tasks, nb_queues * sizeof(*tasks));
    AssertFatal(new_tasks != NULL, "could not realloc() tasks list");
    tasks = new_tasks;
    tasks[newQueue]= new task_list_t;
    tasks[newQueue]->task_parms = {0};
    pthread_mutex_unlock (&lock_nb_queues);
    LOG_D(ITTI, "Starting itti queue: %s as task %d\n", taskInfo->name, newQueue);
    pthread_mutex_init(&tasks[newQueue]->queue_cond_lock, NULL);
    memcpy(&tasks[newQueue]->admin, taskInfo, sizeof(task_info_t));
    tasks[newQueue]->epoll_fd = epoll_create1(0);
    AssertFatal(tasks[newQueue]->epoll_fd >= 0, "%s", strerror(errno));
    tasks[newQueue]->sem_fd = eventfd(0, EFD_SEMAPHORE);
    AssertFatal(tasks[newQueue]->sem_fd >= 0, "%s", strerror(errno));
    itti_subscribe_event_fd((task_id_t)newQueue, tasks[newQueue]->sem_fd);

    return newQueue;
  }

  int itti_init(task_id_t task_max,
                const task_info_t *tasks
               ) {
    pthread_mutex_init(&lock_nb_queues, NULL);
    nb_queues=0;

    for(int i=0; i<task_max; ++i) {
      itti_create_queue(&tasks[i]);
    }

    return 0;
  }

  int timer_setup(
    uint32_t      interval_sec,
    uint32_t      interval_us,
    task_id_t     task_id,
    instance_t       instance,
    timer_type_t  type,
    void         *timer_arg,
    long         *timer_id) {
    task_list_t *t=tasks[task_id];

    do {
      // set the taskid in the timer id to keep compatible with the legacy API
      // timer_remove() takes only the timer id as parameter
      *timer_id=(random()%UINT16_MAX) << 16 | task_id ;
    } while ( t->timer_map.find(*timer_id) != t->timer_map.end());

    /* Allocate new timer list element */
    timer_elm_t timer;
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);

    if (interval_us%1000 != 0)
      LOG_W(ITTI, "Can't set timer precision below 1ms, rounding it\n");

    timer.duration  = interval_sec*1000+interval_us/1000;
    timer.timeout= ((uint64_t)tp.tv_sec*1000+tp.tv_nsec/(1000*1000)+timer.duration);
    timer.instance  = instance;
    timer.type      = type;
    timer.timer_arg = timer_arg;
    pthread_mutex_lock (&t->queue_cond_lock);
    t->timer_map[*timer_id]= timer;

    if (timer.timeout < t->next_timer)
      t->next_timer=timer.timeout;

    eventfd_t sem_counter = 1;
    int ret = write(t->sem_fd, &sem_counter, sizeof(sem_counter));
    AssertFatal(ret == sizeof(sem_counter), "%s", strerror(errno));
    pthread_mutex_unlock (&t->queue_cond_lock);
    return 0;
  }

  int timer_remove(long timer_id) {
    task_id_t task_id=(task_id_t)(timer_id&0xffff);
    int ret;
    pthread_mutex_lock (&tasks[task_id]->queue_cond_lock);
    ret=tasks[task_id]->timer_map.erase(timer_id);
    pthread_mutex_unlock (&tasks[task_id]->queue_cond_lock);

    if (ret==1)
      return 0;
    else {
      LOG_W(ITTI, "tried to remove a non existing timer\n");
      return 1;
    }
  }

  const char *itti_get_message_name(MessagesIds message_id) {
    return messages_info[message_id].name;
  }

  const char *itti_get_task_name(task_id_t task_id) {
    return tasks[task_id]->admin.name;
  }

  // void for compatibility
  void itti_send_terminate_message(task_id_t task_id) {
  }

  static sem_t itti_sem_block;
  void itti_wait_tasks_unblock()
  {
    int rc = sem_post(&itti_sem_block);
    AssertFatal(rc == 0, "error in sem_post(): %d %s\n", errno, strerror(errno));
  }

  static void catch_sigterm(int) {
    static const char msg[] = "\n** Caught SIGTERM, shutting down\n";
    __attribute__((unused))
    int unused = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    itti_wait_tasks_unblock();
  }

  void itti_wait_tasks_end(void (*handler)(int))
  {
    int rc = sem_init(&itti_sem_block, 0, 0);
    AssertFatal(rc == 0, "error in sem_init(): %d %s\n", errno, strerror(errno));

    if (handler == NULL) /* no handler given: install default */
      handler = catch_sigterm;
    signal(SIGTERM, handler);
    signal(SIGINT, handler);

    // POSIX sem_wait(), ERRORS: EINTR does not mean the semaphore was acquired.
    do {
      rc = sem_wait(&itti_sem_block);
    } while (rc == -1 && errno == EINTR);
    AssertFatal(rc == 0, "error in sem_wait(): %d %s\n", errno, strerror(errno));
  }

  void itti_update_lte_time(uint32_t frame, uint8_t slot) {}
  void itti_set_task_real_time(task_id_t task_id) {}
  void itti_mark_task_ready(task_id_t task_id) {
    // Function meaning is clear, but legacy implementation is wrong
    // keep it void is fine: today implementation accepts messages in the queue before task is ready
  }
  void itti_wait_ready(int wait_tasks) {
    // Stupid function, kept for compatibility (the parameter is meaningless!!!)
  }
  int signal_mask(void) {
    return 0;
  }

} // extern "C"

