/*
     This file is part of GNUnet-MQTT.
     (C) 2013 Ramona Popa, Artur Grunau

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file mqtt/mqtt_api.c
 * @brief API for MQTT
 * @author Ramona Popa
 * @author Artur Grunau
 */
#include <gnunet/platform.h>
#include <gnunet/gnunet_util_lib.h>
#include "gnunet_mqtt_service.h"
#include "gnunet_protocols_mqtt.h"
#include "mqtt.h"
#include "regex_utils.h"


#define LOG(kind,...) GNUNET_log_from (kind, "mqtt-api",__VA_ARGS__)


/**
 * Connection to the MQTT service.
 */
struct GNUNET_MQTT_Handle
{

  /**
   * Configuration to use.
   */
  const struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * Currently pending transmission request (or NULL).
   */
  struct GNUNET_CLIENT_TransmitHandle *th;

  /**
   * Socket (if available).
   */
  struct GNUNET_CLIENT_Connection *client;

  /**
   * Generator for unique ids.
   */
  uint64_t uid_gen;

  /**
   * Head of linked list of messages we would like to transmit.
   */
  struct PendingMessage *pending_head;

  /**
   * Tail of linked list of messages we would like to transmit.
   */
  struct PendingMessage *pending_tail;

  /**
   * Did we start our receive loop yet?
   */
  int in_receive;

  /**
   * How quickly should we retry? Used for exponential back-off on
   * connect-errors.
   */
  struct GNUNET_TIME_Relative retry_time;

  /**
   * Task for trying to reconnect.
   */
  GNUNET_SCHEDULER_TaskIdentifier reconnect_task;

  /**
   * Head of active SUBSCRIBE requests.
   */
  struct GNUNET_MQTT_SubscribeHandle *subscribe_head;

  /**
   * Tail of active SUBSCRIBE requests.
   */
  struct GNUNET_MQTT_SubscribeHandle *subscribe_tail;
};


/**
 * Entry in our list of messages to be (re-)transmitted.
 */
struct PendingMessage
{
  /**
   * This is a doubly-linked list.
   */
  struct PendingMessage *prev;

  /**
   * This is a doubly-linked list.
   */
  struct PendingMessage *next;

  /**
   * Message that is pending, allocated at the end of this struct.
   */
  const struct GNUNET_MessageHeader *msg;

  /**
   * Handle to the MQTT API context.
   */
  struct GNUNET_MQTT_Handle *handle;

  /**
   * Timeout task for this operation.
   */
  GNUNET_SCHEDULER_TaskIdentifier timeout_task;

  /**
   * Continuation to call when the request has been
   * transmitted (for the first time) to the service; can be NULL.
   */
  GNUNET_SCHEDULER_Task cont;

  /**
   * Closure for 'cont'.
   */
  void *cont_cls;

  /**
   * Unique ID for this request
   */
  uint64_t request_id;

  /**
   * GNUNET_YES if this message is in our pending queue right now.
   */
  int in_pending_queue;

  /**
   * GNUNET_YES if this message will be ACKed or otherwise responded to.
   */
  int wait_for_response;
};


/**
 * Handle to a PUBLISH request.
 */
struct GNUNET_MQTT_PublishHandle
{
  /**
   * Continuation to call when done.
   */
  GNUNET_MQTT_PublishContinuation cont;

  /**
   * Closure for 'cont'.
   */
  void *cont_cls;

  /**
   * Pending message associated with this PUBLISH operation, NULL after
   * the message has been transmitted to the service.
   */
  struct PendingMessage *pending;

  /**
   * Main handle to this MQTT API
   */
  struct GNUNET_MQTT_Handle *mqtt_handle;

  /**
   * Request ID for the PUBLISH operation.
   */
  uint64_t request_id;
};


/**
 * Handle to a SUBSCRIBE request.
 */
struct GNUNET_MQTT_SubscribeHandle
{
  /**
   * Continuation to call when done.
   */
  GNUNET_MQTT_SubscribeContinuation cont;

  /**
   * Closure for 'cont'.
   */
  void *cont_cls;

  /**
   * Callback to call when messages for this subscription are received.
   */
  GNUNET_MQTT_SubscribeResultCallback cb;

  /**
   * Closure for 'cb'.
   */
  void *cb_cls;

  /**
   * Pending message associated with this SUBSCRIBE operation, NULL
   * after the message has been transmitted to the service.
   */
  struct PendingMessage *pending;

  /**
   * Flag indicating if the subscription is being cancelled (GNUNET_YES)
   * or not (GNUNET_NO).
   */
  int unsubscribing;

  /**
   * Main handle to this MQTT API
   */
  struct GNUNET_MQTT_Handle *mqtt_handle;

  /**
   * Request ID for the SUBSCRIBE operation.
   */
  uint64_t request_id;

  /**
   * Kept in a DLL.
   */
  struct GNUNET_MQTT_SubscribeHandle *next;

  /**
   * Kept in a DLL.
   */
  struct GNUNET_MQTT_SubscribeHandle *prev;
};


/**
 * Try to (re)connect to the MQTT service.
 *
 * @param handle MQTT handle to reconnect
 * @return GNUNET_YES on success, GNUNET_NO on failure.
 */
static int
try_connect (struct GNUNET_MQTT_Handle *handle)
{
  if (NULL != handle->client)
    return GNUNET_OK;

  handle->in_receive = GNUNET_NO;
  handle->client = GNUNET_CLIENT_connect ("mqtt", handle->cfg);

  if (NULL == handle->client)
  {
    LOG (GNUNET_ERROR_TYPE_WARNING,
         _("Failed to connect to the MQTT service!\n"));
    return GNUNET_NO;
  }

  return GNUNET_YES;
}


static void
process_pending_messages (struct GNUNET_MQTT_Handle *handle);


/**
 * Try reconnecting to the MQTT service.
 *
 * @param cls GNUNET_MQTT_Handle
 * @param tc scheduler context
 */
static void
try_reconnect (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_MQTT_Handle *handle = cls;

  LOG (GNUNET_ERROR_TYPE_DEBUG, "Reconnecting with MQTT %p\n", handle);
  handle->retry_time = GNUNET_TIME_STD_BACKOFF (handle->retry_time);
  handle->reconnect_task = GNUNET_SCHEDULER_NO_TASK;

  if (GNUNET_YES != try_connect (handle))
  {
    LOG (GNUNET_ERROR_TYPE_DEBUG, "MQTT reconnect failed\n");
    return;
  }

  process_pending_messages (handle);
}


/**
 * Disconnect and try reconnecting to the MQTT service.
 *
 * @param handle handle to MQTT to (possibly) disconnect and reconnect
 */
static void
do_disconnect (struct GNUNET_MQTT_Handle *handle)
{
  if (NULL == handle->client)
    return;
  GNUNET_assert (GNUNET_SCHEDULER_NO_TASK == handle->reconnect_task);
  if (NULL != handle->th)
    GNUNET_CLIENT_notify_transmit_ready_cancel (handle->th);
  handle->th = NULL;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Disconnecting from MQTT service, will try to reconnect in %s\n",
              GNUNET_STRINGS_relative_time_to_string (handle->retry_time,
              GNUNET_YES));
  GNUNET_CLIENT_disconnect (handle->client);
  handle->client = NULL;

  handle->reconnect_task =
    GNUNET_SCHEDULER_add_delayed (handle->retry_time, &try_reconnect, handle);
}


/**
 * Initialize the connection with the MQTT service.
 *
 * @param cfg configuration to use
 * @return NULL on error
 */
struct GNUNET_MQTT_Handle *
GNUNET_MQTT_connect (const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  struct GNUNET_MQTT_Handle *handle;

  handle = GNUNET_malloc (sizeof (struct GNUNET_MQTT_Handle));
  handle->cfg = cfg;
  handle->uid_gen = GNUNET_CRYPTO_random_u64 (GNUNET_CRYPTO_QUALITY_WEAK,
                                              UINT64_MAX);

  if (GNUNET_NO == try_connect (handle))
  {
    GNUNET_MQTT_disconnect (handle);
    return NULL;
  }
  return handle;
}


/**
 * Shut down connection with the MQTT service.
 *
 * @param handle handle of the MQTT connection to stop
 */
void
GNUNET_MQTT_disconnect (struct GNUNET_MQTT_Handle *handle)
{
  struct PendingMessage *pm;

  GNUNET_assert (NULL != handle);

  if (NULL != handle->th)
  {
    GNUNET_CLIENT_notify_transmit_ready_cancel (handle->th);
    handle->th = NULL;
  }

  while (NULL != (pm = handle->pending_head))
  {
    GNUNET_assert (GNUNET_YES == pm->in_pending_queue);
    GNUNET_CONTAINER_DLL_remove (handle->pending_head, handle->pending_tail,
                                 pm);
    pm->in_pending_queue = GNUNET_NO;

    if (NULL != pm->cont)
      pm->cont (pm->cont_cls, NULL);

    GNUNET_free (pm);
  }

  if (NULL != handle->client)
  {
    GNUNET_CLIENT_disconnect (handle->client);
    handle->client = NULL;
  }

  GNUNET_free (handle);
}


/**
 * Timeout for the transmission of a PUBLISH request. Clean it up.
 *
 * @param cls the 'struct PendingMessage'
 * @param tc scheduler context
 */
static void
publish_timeout (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_MQTT_PublishHandle *ph = cls;
  struct GNUNET_MQTT_Handle *handle = ph->mqtt_handle;

  if (NULL != ph->pending)
  {
    GNUNET_CONTAINER_DLL_remove (handle->pending_head, handle->pending_tail,
                                 ph->pending);
    ph->pending->in_pending_queue = GNUNET_NO;
    ph->pending->timeout_task = GNUNET_SCHEDULER_NO_TASK;
    GNUNET_free (ph->pending);
  }

  if (NULL != ph->cont)
    ph->cont (ph->cont_cls, GNUNET_NO);

  GNUNET_free (ph);
}


/**
 * Timeout for the transmission of a SUBSCRIBE request. Clean it up.
 *
 * @param cls the 'struct PendingMessage'
 * @param tc scheduler context
 */
static void
subscribe_timeout (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_MQTT_SubscribeHandle *sh = cls;
  struct GNUNET_MQTT_Handle *handle = sh->mqtt_handle;

  if (NULL != sh->pending)
  {
    GNUNET_CONTAINER_DLL_remove (handle->pending_head, handle->pending_tail,
                                 sh->pending);
    sh->pending->in_pending_queue = GNUNET_NO;
    sh->pending->timeout_task = GNUNET_SCHEDULER_NO_TASK;
    GNUNET_free (sh->pending);
  }

  if (NULL != sh->cont)
    sh->cont (sh->cont_cls, GNUNET_NO);

  GNUNET_free (sh);
}


/**
 * Process an incoming PUBLISH message from the MQTT service.
 *
 * @param handle the MQTT handle
 * @param msg the incoming PUBLISH message from the service
 *
 * @return GNUNET_OK if everything went fine,
 *         GNUNET_SYSERR if the message is malformed
 */
static int
process_incoming_publish_message (struct GNUNET_MQTT_Handle *handle,
                                  const struct GNUNET_MQTT_ClientPublishMessage
                                    *msg)
{
  struct GNUNET_MQTT_SubscribeHandle *sh;
  char *topic, *message;
  size_t message_len;

  for (sh = handle->subscribe_head; NULL != sh; sh = sh->next)
    if (sh->request_id == msg->request_id)
      break;

  if (NULL == sh) {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Received PUBLISH message matching no subscriptions\n");
    return GNUNET_SYSERR;
  }

  /* Extract topic */
  topic = GNUNET_malloc (msg->topic_len);
  strncpy(topic, (char *) (msg + 1), msg->topic_len);
  topic[msg->topic_len - 1] = '\0';

  /* Extract message */
  message_len = ntohs (msg->header.size) -
    sizeof (struct GNUNET_MQTT_ClientPublishMessage) - msg->topic_len;
  message = GNUNET_malloc (message_len);
  strncpy(message, ((char *) (msg + 1)) + msg->topic_len,
          message_len);
  message[message_len - 1] = '\0';

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Incoming PUBLISH message received (client): %s -> %s\n", topic,
              message);

  sh->cb (sh->cb_cls, msg->topic_len, topic, message_len, message);

  return GNUNET_OK;
}


/**
 * Process an UNSUBSCRIBE_ACK message from the MQTT service.
 *
 * @param handle the MQTT handle
 * @param msg the UNSUBSCRIBE_ACK message from the service
 *
 * @return GNUNET_OK if everything went fine,
 *         GNUNET_SYSERR if the message is malformed
 */
static int
process_unsubscribe_ack (struct GNUNET_MQTT_Handle *handle,
                         const struct GNUNET_MQTT_ClientUnsubscribeAckMessage *msg)
{
  struct GNUNET_MQTT_SubscribeHandle *sh;

  for (sh = handle->subscribe_head; NULL != sh; sh = sh->next)
    if (sh->request_id == msg->request_id)
      break;

  if (NULL == sh) {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Received UNSUBSCRIBE_ACK matching no subscriptions\n");
    return GNUNET_SYSERR;
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Subscription with ID %lu cancelled\n",
              sh->request_id);

  GNUNET_CONTAINER_DLL_remove (handle->subscribe_head, handle->subscribe_tail,
                               sh);
  GNUNET_free (sh);

  return GNUNET_OK;
}


/**
 * Handler for messages received from the MQTT service
 *
 * This function acts as a demultiplexer that handles numerous message
 * types.
 *
 * @param cls the 'struct GNUNET_MQTT_Handle'
 * @param msg the incoming message
 */
static void
service_message_handler (void *cls, const struct GNUNET_MessageHeader *msg)
{
  struct GNUNET_MQTT_Handle *handle = cls;
  uint16_t msize;
  int ret;

  if (NULL == msg)
  {
    LOG (GNUNET_ERROR_TYPE_DEBUG,
         "Error receiving data from MQTT service, reconnecting\n");
    do_disconnect (handle);
    return;
  }
  GNUNET_CLIENT_receive (handle->client, &service_message_handler, handle,
                         GNUNET_TIME_UNIT_FOREVER_REL);
  ret = GNUNET_SYSERR;
  msize = ntohs (msg->size);
  switch (ntohs (msg->type))
  {
  case GNUNET_MESSAGE_TYPE_MQTT_CLIENT_PUBLISH:
    if (msize < sizeof (struct GNUNET_MQTT_ClientPublishMessage))
    {
      GNUNET_break (0);
      break;
    }
    ret = process_incoming_publish_message(
      handle, (const struct GNUNET_MQTT_ClientPublishMessage *) msg);
    break;
  case GNUNET_MESSAGE_TYPE_MQTT_CLIENT_UNSUBSCRIBE_ACK:
    if (msize < sizeof (struct GNUNET_MQTT_ClientUnsubscribeAckMessage))
    {
      GNUNET_break (0);
      break;
    }
    ret = process_unsubscribe_ack(
      handle, (const struct GNUNET_MQTT_ClientUnsubscribeAckMessage *) msg);
    break;
  default:
    GNUNET_break(0);
    LOG (GNUNET_ERROR_TYPE_WARNING,
         "Unknown MQTT message type: %hu (%hu) size: %hu\n",
         ntohs (msg->type), msg->type, msize);
    break;
  }
  if (GNUNET_OK != ret)
  {
    GNUNET_break (0);
    do_disconnect (handle);
    return;
  }
}


/**
 * Transmit the next pending message, called by notify_transmit_ready
 *
 * @param cls the MQTT handle
 * @param size number of bytes available in 'buf' for transmission
 * @param buf where to copy messages for the service
 * @return number of bytes written to 'buf'
 */
static size_t
transmit_pending (void *cls, size_t size, void *buf)
{
  struct GNUNET_MQTT_Handle *handle = cls;
  struct PendingMessage *head;
  size_t tsize;

  handle->th = NULL;

  if (NULL == buf)
  {
    LOG (GNUNET_ERROR_TYPE_DEBUG,
         "Transmission to the MQTT service failed; reconnecting\n");
    do_disconnect (handle);
    return 0;
  }

  if (NULL == (head = handle->pending_head))
    return 0;

  tsize = ntohs (head->msg->size);
  if (size < tsize)
  {
    process_pending_messages (handle);
    return 0;
  }

  memcpy (buf, head->msg, tsize);
  GNUNET_CONTAINER_DLL_remove (handle->pending_head, handle->pending_tail,
                               head);
  head->in_pending_queue = GNUNET_NO;

  if (NULL != head->cont)
  {
    head->cont (head->cont_cls, NULL);
    head->cont = NULL;
    head->cont_cls = NULL;
  }

  if (head->timeout_task != GNUNET_SCHEDULER_NO_TASK)
  {
    GNUNET_SCHEDULER_cancel (head->timeout_task);
    head->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  }

  process_pending_messages (handle);

  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Forwarded request of %zu bytes to MQTT service\n", tsize);

  if (GNUNET_YES == head->wait_for_response && GNUNET_NO == handle->in_receive)
  {
    LOG (GNUNET_ERROR_TYPE_DEBUG, "Starting to process replies from MQTT\n");
    handle->in_receive = GNUNET_YES;
    GNUNET_CLIENT_receive (handle->client, &service_message_handler, handle,
                           GNUNET_TIME_UNIT_FOREVER_REL);
  }

  GNUNET_free (head);

  return tsize;
}


/**
 * Try to send messages from list of messages to send
 *
 * @param handle handle to MQTT
 */
static void
process_pending_messages (struct GNUNET_MQTT_Handle *handle)
{
  struct PendingMessage *head;

  if (NULL == handle->client)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "process_pending_messages called, but client is NULL, reconnecting\n");
    do_disconnect (handle);
    return;
  }

  if (NULL != handle->th)
    return;

  if (NULL == (head = handle->pending_head))
    return;

  handle->th =
      GNUNET_CLIENT_notify_transmit_ready (handle->client,
                                           ntohs (head->msg->size),
                                           GNUNET_TIME_UNIT_FOREVER_REL,
                                           GNUNET_YES, &transmit_pending,
                                           handle);
  if (NULL != handle->th)
    return;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "notify_transmit_ready returned NULL, reconnecting\n");
  do_disconnect (handle);
}


/**
 * Function called whenever a SUBSCRIBE message leaves the queue.
 *
 * It invokes the user-provided callback to inform them that the message
 * was sent, and sets the message pointer in the SUBSCRIBE handle to
 * NULL.
 *
 * @param cls a struct GNUNET_MQTT_SubscribeHandle
 * @param tc unused
 */
static void
mark_subscribe_message_gone (void *cls,
                             const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_MQTT_SubscribeHandle *sh = cls;

  if (NULL != sh->cont)
  {
    sh->cont (sh->cont_cls, GNUNET_OK);
    sh->cont = NULL;
    sh->cont_cls = NULL;
  }

  sh->pending = NULL;
}


/**
 * Function called whenever an UNSUBSCRIBE message leaves the queue.
 *
 * It sets the message pointer in the SUBSCRIBE handle to NULL.
 *
 * @param cls a struct GNUNET_MQTT_SubscribeHandle
 * @param tc unused
 */
static void
mark_unsubscribe_message_gone (void *cls,
                               const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_MQTT_SubscribeHandle *sh = cls;

  sh->pending = NULL;
}


/**
 * Perform a PUBLISH operation storing data in the MQTT.
 *
 * @param handle handle to MQTT service
 * @param topic_len length of the topic the message should be published
 *                  on
 * @param topic the topic (as a NUL-terminated string) the message
 *              should be published on
 * @param size number of bytes in data; must be less than 64k
 * @param data he payload of the message
 * @param timeout how long to wait for transmission of this request
 * @param cont continuation to call when done (transmitting request to
 *             service); you must not call GNUNET_MQTT_disconnect in
 *             this continuation
 * @param cont_cls closure for cont
 * @return handle to cancel the PUBLISH operation, or NULL on error
 *         (message size too big)
 */
struct GNUNET_MQTT_PublishHandle *
GNUNET_MQTT_publish (struct GNUNET_MQTT_Handle *handle, uint8_t topic_len,
                     const char *topic, size_t size, const void *data,
                     struct GNUNET_TIME_Relative timeout,
                     GNUNET_MQTT_PublishContinuation cont, void *cont_cls)
{
  struct GNUNET_MQTT_ClientPublishMessage *publish_msg;
  size_t msize;
  struct PendingMessage *pending;
  struct GNUNET_MQTT_PublishHandle *ph;

  msize = sizeof (struct GNUNET_MQTT_ClientPublishMessage) + topic_len + size;

  if ((msize >= GNUNET_SERVER_MAX_MESSAGE_SIZE) ||
      (size >= GNUNET_SERVER_MAX_MESSAGE_SIZE))
  {
    GNUNET_break (0);
    return NULL;
  }

   if (0 == validate_publish_topic(topic))
  {
	GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
              "Unvalid publish topic!\n");
    return NULL;    
  
  }
  ph = GNUNET_malloc (sizeof (struct GNUNET_MQTT_PublishHandle));
  ph->mqtt_handle = handle;
  ph->cont = cont;
  ph->cont_cls = cont_cls;
  ph->request_id = ++handle->uid_gen;

  pending = GNUNET_malloc (sizeof (struct PendingMessage) + msize);
  ph->pending = pending;
  publish_msg = (struct GNUNET_MQTT_ClientPublishMessage *) &pending[1];

  pending->msg = &publish_msg->header;
  pending->handle = handle;
  pending->timeout_task = GNUNET_SCHEDULER_add_delayed (timeout,
                                                        &publish_timeout,
                                                        ph);

  publish_msg->header.size = htons (msize);
  publish_msg->header.type = htons (GNUNET_MESSAGE_TYPE_MQTT_CLIENT_PUBLISH);
  publish_msg->request_id = ph->request_id;
  publish_msg->topic_len = topic_len;

  memcpy (&publish_msg[1], topic, topic_len);
  memcpy (((char *) (publish_msg + 1)) + topic_len, data, size);

  GNUNET_CONTAINER_DLL_insert (handle->pending_head, handle->pending_tail,
                               pending);
  pending->in_pending_queue = GNUNET_YES;
  pending->wait_for_response = GNUNET_NO;
  process_pending_messages (handle);

  return ph;
}


/**
 * Cancel an MQTT PUBLISH operation.
 *
 * Note that the PUBLISH request may still go out over the network (we
 * can't stop that). However, if the PUBLISH has not yet been sent to
 * the service, cancelling it will stop this from happening (but there
 * is no way for the user of this API to tell if that is the case). The
 * only use for this API is to prevent a later call to 'cont' from
 * "GNUNET_MQTT_publish" (i.e. because the system is shutting down).
 *
 * @param ph PUBLISH operation to cancel ('cont' will no longer be
 *           called)
 */
void
GNUNET_MQTT_publish_cancel (struct GNUNET_MQTT_PublishHandle *ph)
{
  struct GNUNET_MQTT_Handle *handle = ph->mqtt_handle;

  if (NULL != ph->pending)
  {
      if (ph->pending->timeout_task != GNUNET_SCHEDULER_NO_TASK)
      {
        GNUNET_SCHEDULER_cancel (ph->pending->timeout_task);
        ph->pending->timeout_task = GNUNET_SCHEDULER_NO_TASK;
      }

      GNUNET_CONTAINER_DLL_remove (handle->pending_head, handle->pending_tail,
                                   ph->pending);
      GNUNET_free (ph->pending);
      ph->pending = NULL;
  }
}


/**
 * Perform a SUBSCRIBE operation.
 *
 * @param handle handle to MQTT service
 * @param topic_len length of the topic for which the registartion is
          done
 * @param topic the topic (as a NUL-terminated string) - target of
          the subscription
 * @param timeout how long to wait for transmission of this request
 * @param cont continuation to call when done (transmitting request to
 *             service); you must not call GNUNET_MQTT_disconnect in
 *             this continuation
 * @param cont_cls closure for cont
 * @return handle to cancel the SUBSCRIBE operation, or NULL on error
 */
struct GNUNET_MQTT_SubscribeHandle *
GNUNET_MQTT_subscribe (struct GNUNET_MQTT_Handle *handle, uint8_t topic_len,
                       const char *topic, struct GNUNET_TIME_Relative timeout,
                       GNUNET_MQTT_SubscribeContinuation cont, void *cont_cls,
                       GNUNET_MQTT_SubscribeResultCallback cb, void *cb_cls)
{
  struct GNUNET_MQTT_ClientSubscribeMessage *subscribe_msg;
  size_t tsize;
  struct PendingMessage *pending;
  struct GNUNET_MQTT_SubscribeHandle *sh;

  GNUNET_assert (NULL != cb);
  tsize = sizeof (struct GNUNET_MQTT_ClientSubscribeMessage) + topic_len;
  
  if (0 == validate_subscribe_topic(topic))
  {
	GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
              "Unvalid subscribe topic\n!");
    return NULL;    
  
  }

  sh = GNUNET_malloc (sizeof (struct GNUNET_MQTT_SubscribeHandle));
  sh->mqtt_handle = handle;
  sh->cont = cont;
  sh->cont_cls = cont_cls;
  sh->cb = cb;
  sh->cb_cls = cb_cls;
  sh->request_id = ++handle->uid_gen;

  pending = GNUNET_malloc (sizeof (struct PendingMessage) + tsize);
  sh->pending = pending;
  sh->unsubscribing = GNUNET_NO;
  subscribe_msg = (struct GNUNET_MQTT_ClientSubscribeMessage *) &pending[1];

  pending->msg = &subscribe_msg->header;
  pending->handle = handle;
  pending->cont = mark_subscribe_message_gone;
  pending->cont_cls = sh;
  pending->timeout_task = GNUNET_SCHEDULER_add_delayed (timeout,
                                                        &subscribe_timeout,
                                                        sh);

  subscribe_msg->header.size = htons (tsize);
  subscribe_msg->header.type = htons (GNUNET_MESSAGE_TYPE_MQTT_CLIENT_SUBSCRIBE);
  subscribe_msg->request_id = sh->request_id;
  subscribe_msg->topic_len = topic_len;

  memcpy (&subscribe_msg[1], topic, topic_len);
 
  GNUNET_CONTAINER_DLL_insert (handle->pending_head, handle->pending_tail,
                               pending);
  pending->in_pending_queue = GNUNET_YES;
  pending->wait_for_response = GNUNET_YES;
  process_pending_messages (handle);

  GNUNET_CONTAINER_DLL_insert_tail (handle->subscribe_head,
                                    handle->subscribe_tail, sh);

  return sh;
}


/**
 * Cancel the given active MQTT subscription.
 *
 * All resources associated with the subscription will be freed by this
 * function. On return `handle` will no longer be valid, and mustn't be
 * used again.
 *
 * @param handle handle to an MQTT subscription
 */
void
GNUNET_MQTT_unsubscribe (struct GNUNET_MQTT_SubscribeHandle *sh)
{
  struct GNUNET_MQTT_Handle *handle = sh->mqtt_handle;

  if (NULL != sh->pending)
  {
    if (GNUNET_YES == sh->unsubscribing)
    {
      return;
    }
    else
    {
      if (sh->pending->timeout_task != GNUNET_SCHEDULER_NO_TASK)
      {
        GNUNET_SCHEDULER_cancel (sh->pending->timeout_task);
        sh->pending->timeout_task = GNUNET_SCHEDULER_NO_TASK;
      }

      GNUNET_CONTAINER_DLL_remove (handle->pending_head, handle->pending_tail,
                                   sh->pending);
      GNUNET_free (sh->pending);
      sh->pending = NULL;
    }
  }
  else if (GNUNET_NO == sh->unsubscribing)
  {
    struct PendingMessage *pending;
    struct GNUNET_MQTT_ClientUnsubscribeMessage *unsubscribe_msg;

    pending = GNUNET_malloc (sizeof (struct PendingMessage) +
      sizeof (struct GNUNET_MQTT_ClientUnsubscribeMessage));
    sh->pending = pending;
    sh->unsubscribing = GNUNET_YES;
    unsubscribe_msg = (struct GNUNET_MQTT_ClientUnsubscribeMessage *)
      (pending + 1);

    pending->msg = &unsubscribe_msg->header;
    pending->handle = handle;
    pending->cont = mark_unsubscribe_message_gone;
    pending->cont_cls = sh;

    unsubscribe_msg->header.size =
      sizeof (struct GNUNET_MQTT_ClientUnsubscribeMessage);
    unsubscribe_msg->header.type =
      htons (GNUNET_MESSAGE_TYPE_MQTT_CLIENT_UNSUBSCRIBE);
    unsubscribe_msg->request_id = sh->request_id;

    GNUNET_CONTAINER_DLL_insert (handle->pending_head, handle->pending_tail,
                                 pending);
    pending->in_pending_queue = GNUNET_YES;
    pending->wait_for_response = GNUNET_YES;
    process_pending_messages (handle);
  }
}
