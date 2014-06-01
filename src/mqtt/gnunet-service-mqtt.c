/*
     This file is part of GNUnet-MQTT.
     (C) 2013 Ramona Popa, Artur Grunau

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
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
 * @file mqtt/gnunet-service-mqtt.c
 * @brief MQTT service implementation
 * @author Ramona Popa
 * @author Artur Grunau
 */
#include <gnunet/platform.h>
#include <gnunet/gnunet_util_lib.h>
#include <gnunet/gnunet_regex_service.h>
#include <gnunet/gnunet_dht_service.h>
#include <gnunet/gnunet_mesh_service.h>
#include <gnunet/gnunet_applications.h>
#include <gnunet/gnunet_configuration_lib.h>
#include "gnunet_protocols_mqtt.h"
#include "mqtt.h"
#include <regex.h>


/**
 * Struct representing the context for the regex search
 */
struct RegexSearchContext
{

  /**
   * Pointer to next item in the list
   */
  struct RegexSearchContext *next;

  /**
   * Pointer to previous item in the list
   */
  struct RegexSearchContext *prev;

  /**
   * Pointer to the publish message
   */
  struct GNUNET_MQTT_ClientPublishMessage *publish_msg;

  /**
   * Pointer to the filepath where the topic and the content of the
   * message will be stored
   */
  char *file_path;

  /**
   * Set of peers interested in the message associated with this context
   */
  struct GNUNET_CONTAINER_MultiPeerMap *subscribers;

  /**
   * Flag to mark a message as delivered in order to trigger its
   * deletion
   */
  int message_delivered;

  /**
   * Task responsible for freeing the context once the message
   * associated with it has been delivered
   */
  GNUNET_SCHEDULER_TaskIdentifier free_task;

  /**
   * Pointer to the regex search handle
   */
  struct GNUNET_REGEX_search_handle *regex_search_handle;
};


/**
 * Struct representing a message that needs to be sent to a client.
 */
struct PendingMessage
{
  /**
   * Pointer to next item in the list
   */
  struct PendingMessage *next;

  /**
   * Pointer to previous item in the list
   */
  struct PendingMessage *prev;

  /**
   * Pointer to the actual message to be sent
   */
  struct GNUNET_MessageHeader *msg;

  /**
   * Pointer to the filepath where the topic and the content of the
   * message will be stored
   */
  struct RegexSearchContext *context;
};


/**
 * Struct containing information about a client,
 * handle to connect to it, and any pending messages
 * that need to be sent to it.
 */
struct ClientInfo
{
  /**
   * Linked list of active clients
   */
  struct ClientInfo *next;

  /**
   * Linked list of active clients
   */
  struct ClientInfo *prev;

  /**
   * The handle to this client
   */
  struct GNUNET_SERVER_Client *client_handle;

  /**
   * Handle to the current transmission request, NULL
   * if none is pending.
   */
  struct GNUNET_SERVER_TransmitHandle *transmit_handle;

  /**
   * Linked list of pending messages for this client
   */
  struct PendingMessage *pending_head;

  /**
   * Tail of linked list of pending messages for this client
   */
  struct PendingMessage *pending_tail;
};


/**
 * Struct containing information about a client,
 * handle to connect to it, and any pending messages
 * that need to be sent to it.
 */
struct RemoteSubscriberInfo
{
  /**
   * The subscriber's identity.
   */
  struct GNUNET_PeerIdentity id;

  /**
   * Channel connecting us to the subscriber.
   */
  struct GNUNET_MESH_Channel *channel;

  /**
   * Has the subscriber been added to the channel yet?
   */
  int peer_added;

  /**
   * Are we currently trying to connect to the subscriber?
   */
  int peer_connecting;

  /**
   * Handle to the current transmission request, NULL
   * if none is pending.
   */
  struct GNUNET_MESH_TransmitHandle *transmit_handle;

  /**
   * Head of linked list of pending messages for this subscriber
   */
  struct PendingMessage *pending_head;

  /**
   * Tail of linked list of pending messages for this subscriber
   */
  struct PendingMessage *pending_tail;
};


/**
 * Struct representing one active subscription in our service.
 */
struct Subscription
{
  /**
   * Element in a DLL.
   */
  struct Subscription *prev;

  /**
   * Element in a DLL.
   */
  struct Subscription *next;

  /**
   * Handle used to cancel the annnouncement
   */
  struct GNUNET_REGEX_announce_handle *regex_announce_handle;

  /**
   * The subscribed client
   */
  struct ClientInfo *client;

  /**
   * Unique ID for this subscription
   */
  uint64_t request_id;

  /**
   * The automaton built using the subcription provided by the user
   */
  regex_t automaton;
};


/**
 * Our configuration.
 */
static const struct GNUNET_CONFIGURATION_Handle *cfg;

/**
 * Handle to the DHT.
 */
static struct GNUNET_DHT_Handle *dht_handle;

/**
 * Handle to the server.
 */
static struct GNUNET_SERVER_Handle *server_handle;

/**
 * Handle to the mesh service.
 */
static struct GNUNET_MESH_Handle *mesh_handle;

/**
 * The identity of the local peer.
 */
static struct GNUNET_PeerIdentity my_id;

/**
 * Head of active subscriptions.
 */
static struct Subscription *subscription_head;

/**
 * Head of active subscriptions.
 */
static struct Subscription *subscription_tail;

/**
 * List of active clients.
 */
static struct ClientInfo *client_head;

/**
 * List of active clients.
 */
static struct ClientInfo *client_tail;

/**
 * Map storing data identifying remote subscribers.
 */
static struct GNUNET_CONTAINER_MultiPeerMap *remote_subscribers;

/**
 * Generator for unique ids.
 */
static uint64_t uid_gen;

/**
 * Path to the current directory (configuration directory)
 */
static char *folder_name;

/**
 * Tail of doubly-Linked list storing active regex search contexts.
 */
static struct RegexSearchContext *sc_head;

/**
 * Tail of doubly-Linked list storing active regex search contexts.
 */
static struct RegexSearchContext *sc_tail;

/**
 * The time the peer that received a publish message waits before it deletes it after it was sent to a subscrier
 */
static struct GNUNET_TIME_Relative message_delete_time;

/**
 * String constant for prefixing the topic
 */
static const char *prefix = "GNUNET-MQTT 0001 00000";

/**
 * String constant for replacing '+' wildcard in the subscribed topics.
 */
static const char *plus_regex = "(a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|0|1|2|3|4|5|6|7|8|9)+";

/**
 * String constant for replacing '#' wildcard in the subscribed topics.
 */
static const char *hash_regex = "(/(a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|0|1|2|3|4|5|6|7|8|9)+)*";


/**
 * Adds the prefix to the toopic (App ID + Version + Padding)
 *
 * @param topic topic of subscription as provided by the subscriber
 * @param regex_topic client identification of the client
 */
static void
add_prefix (const char *topic,
	    char **prefixed_topic)
{
  int n;
  int i;

  *prefixed_topic = GNUNET_malloc(strlen(prefix) + strlen(topic)+1);
  n = 0;
  for (i = 0; prefix[i] != '\0'; i++)
    (*prefixed_topic)[i] = prefix[i];
  n = i;

  for (i = 0; topic[i] != '\0'; i++)
  {
    (*prefixed_topic)[n] = topic[i];
    n++;
  }

  (*prefixed_topic)[n] = '\0';
}


/**
 * Transform topics to regex expression.
 *
 * @param topic topic of subscription as provided by the subscriber
 * @param regex_topic client identification of the client
 */
static void
get_regex (char *topic,
	   char **regex_topic)
{
  char *plus;
  char *hash;
  char *prefixed_topic;
  int i;
  int j;
  int k;
  int plus_counter = 0;
  int hash_exists = 0;

  plus = strchr(topic,'+');
  while (plus != NULL)
  {
    plus_counter +=1;
    plus=strchr(plus+1,'+');
  }
  hash = strchr(topic,'#');
  if (hash != NULL)
  {
    hash_exists = 1;
  }

  add_prefix(topic, &prefixed_topic);

  *regex_topic = GNUNET_malloc (strlen(prefixed_topic) - plus_counter - hash_exists + plus_counter*strlen(plus_regex) + hash_exists*strlen(hash_regex)+1);
  j = 0;
  for (i = 0; prefixed_topic[i] != '\0'; i++)
  {
    if (prefixed_topic[i] == '+')
    {
      for (k = 0; k<strlen(plus_regex); k++)
      {
	(*regex_topic)[j] = plus_regex[k];
	j++;
      }
    }
    else if (prefixed_topic[i] == '#')
    {
      j--;
      for (k = 0; k<strlen(hash_regex); k++)
      {
	(*regex_topic)[j] = hash_regex[k];
	j++;
      }
    }
    else
    {
      (*regex_topic)[j] = prefixed_topic[i];
      j++;
    }
  }
  (*regex_topic)[j] = '\0';
}


/**
 * Free the provided ClientInfo struct.
 *
 * This function takes care to cancel any pending transmission requests
 * and discard all outstanding messages not delivered to the client yet
 * before freeing the ClientInfo struct itself.
 *
 * @param client_info pointer to a ClientInfo struct
 */
static void
client_info_free (struct ClientInfo *client_info)
{
  struct PendingMessage *pm;

  if (NULL != client_info->transmit_handle)
  {
    GNUNET_SERVER_notify_transmit_ready_cancel (client_info->transmit_handle);
    client_info->transmit_handle = NULL;
  }
  while (NULL != (pm = client_info->pending_head))
  {
    GNUNET_CONTAINER_DLL_remove (client_info->pending_head,
                                 client_info->pending_tail, pm);
    GNUNET_free (pm->msg);
    GNUNET_free (pm);
  }
  GNUNET_CONTAINER_DLL_remove (client_head, client_tail, client_info);
  GNUNET_free (client_info);
}


/**
 * Return a ClientInfo struct for the given client.
 *
 * If we communicated with this client before, we return the ClientInfo
 * struct that it has been previously assigned. Otherwise, we create,
 * save, and return a new ClientInfo struct.
 *
 * @param client the server handle to the client
 * @return a ClientInfo structure for the given client
 */
static struct ClientInfo *
find_active_client (struct GNUNET_SERVER_Client *client)
{
  struct ClientInfo *pos = client_head;
  struct ClientInfo *ret;

  while (pos != NULL)
  {
    if (pos->client_handle == client)
      return pos;
    pos = pos->next;
  }
  ret = GNUNET_new (struct ClientInfo);
  ret->client_handle = client;
  ret->transmit_handle = NULL;
  GNUNET_CONTAINER_DLL_insert (client_head, client_tail, ret);
  return ret;
}


/**
 * Free the provided Subscription struct.
 *
 * This function stops announcing the subscription and destroys the
 * regex automaton associated with it before freeing the Subscription
 * struct itself.
 *
 * @param subscription pointer to a Subscription struct
 */
static void
subscription_free (struct Subscription *subscription)
{
  regfree (&subscription->automaton);
  GNUNET_REGEX_announce_cancel (subscription->regex_announce_handle);
  GNUNET_free (subscription);
}


/**
 * Free the provided RemoteSubscriberInfo struct.
 *
 * This function takes care to cancel any pending transmission requests
 * and discard all outstanding messages not delivered to the subscriber
 * yet. Moreover, it destroys the channel connecting us to the subscriber
 * before finally freeing the given RemoteSubscriberInfo struct.
 *
 * @param subscriber pointer to a RemoteSubscriberInfo struct
 */
static void
remote_subscriber_info_free (struct RemoteSubscriberInfo *subscriber)
{
  struct PendingMessage *pm;

  if (NULL != subscriber->transmit_handle)
  {
    GNUNET_MESH_notify_transmit_ready_cancel (subscriber->transmit_handle);
    subscriber->transmit_handle = NULL;
  }
  while (NULL != (pm = subscriber->pending_head))
  {
    GNUNET_CONTAINER_DLL_remove (subscriber->pending_head,
                                 subscriber->pending_tail, pm);
    GNUNET_free (pm->msg);
    GNUNET_free (pm);
  }
  GNUNET_MESH_channel_destroy (subscriber->channel);
  GNUNET_free (subscriber);
}


/**
 * Free the provided RegexSearchContext struct.
 *
 * This function stops the regex search associated with the given
 * context and deletes the corresponding PUBLISH message (if it has been
 * successfully delivered). Finally, it frees the provided
 * RegexSearchContext struct.
 *
 * @param context pointer to a RegexSearchContext struct
 */
static void
regex_search_context_free (struct RegexSearchContext *context)
{
  if (GNUNET_YES == context->message_delivered)
  {
    char *filepath = context->file_path;

    if (GNUNET_SCHEDULER_NO_TASK != context->free_task)
      GNUNET_SCHEDULER_cancel (context->free_task);

    if (0 != UNLINK (filepath))
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING,
				"unlink",
				filepath);
  }
  GNUNET_CONTAINER_multipeermap_destroy (context->subscribers);
  GNUNET_REGEX_search_cancel (context->regex_search_handle);
  GNUNET_free (context->publish_msg);
  GNUNET_free (context->file_path);
  GNUNET_free (context);
}


/**
 * Function called when the timer expires for a delivered message,
 * triggering its deletion.
 *
 * @param cls closure (RegexSearchContext of the message to be deleted)
 */
static void
delete_delivered_message (void *cls,
                          const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct RegexSearchContext *context = cls;

  GNUNET_CONTAINER_DLL_remove (sc_head,
			       sc_tail,
			       context);
  context->free_task = GNUNET_SCHEDULER_NO_TASK;
  regex_search_context_free (context);
}


/**
 * Marks a message as delivered and sets the timer for deleting it
 *
 * @param pm pointer to the pending message
 */
static void
set_timer_for_deleting_message (struct PendingMessage *pm)
{
  if (GNUNET_NO == pm->context->message_delivered)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "scheduling task to delete delivered PUBLISH message\n");

    pm->context->message_delivered = GNUNET_YES;
    pm->context->free_task = GNUNET_SCHEDULER_add_delayed (
      message_delete_time,
      delete_delivered_message, pm->context);
  }
}


static void
process_pending_subscriber_messages (struct RemoteSubscriberInfo *subscriber);


/**
 * Callback called as a result of issuing
 * a GNUNET_MESH_notify_transmit_ready request. A RemoteSubscriberInfo
 * is passed as closure, take the head of the list and copy it into buf,
 * which has the result of sending the message to the subscriber.
 *
 * @param cls closure to this call
 * @param size maximum number of bytes available to send
 * @param buf where to copy the actual message to
 * @return the number of bytes actually copied, 0 indicates failure
 */
static size_t
send_msg_to_subscriber (void *cls,
			size_t size,
			void *buf)
{
  struct RemoteSubscriberInfo *subscriber = cls;
  char *cbuf = buf;
  struct PendingMessage *pm;
  size_t off;
  size_t msize;

  subscriber->transmit_handle = NULL;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Send message to subscriber.\n");
  if (buf == NULL)
  {
    /* subscriber disconnected */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"Subscriber %s disconnected, pending messages will be discarded\n",
                GNUNET_i2s (&subscriber->id));

    return 0;
  }

  off = 0;

  while ((NULL != (pm = subscriber->pending_head)) &&
         (size >= off + (msize = ntohs (pm->msg->size))))
  {
    GNUNET_CONTAINER_DLL_remove (subscriber->pending_head,
                                 subscriber->pending_tail, pm);
    memcpy (&cbuf[off], pm->msg, msize);
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Transmitting %u bytes to subscriber %s\n", msize,
                GNUNET_i2s (&subscriber->id));
    off += msize;
    set_timer_for_deleting_message(pm);
    GNUNET_free (pm->msg);
    GNUNET_free (pm);
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Transmitted %zu/%zu bytes to subscriber %s\n",
              off, size, GNUNET_i2s (&subscriber->id));
  process_pending_subscriber_messages (subscriber);

  return off;
}


/**
 * Task run to check for messages that need to be sent to a subscriber.
 *
 * @param client a RemoteSubscriberInfo struct, containing the channel
 *               handle and any messages to be sent to it
 */
static void
process_pending_subscriber_messages (struct RemoteSubscriberInfo *subscriber)
{
  struct GNUNET_MessageHeader *msg;

  if ((subscriber->pending_head == NULL) ||
      (subscriber->transmit_handle != NULL))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Not asking for transmission to %s now: %s\n",
                GNUNET_i2s (&subscriber->id),
                subscriber->pending_head ==
                NULL ? "no more messages" : "request already pending");
    return;
  }

  msg = subscriber->pending_head->msg;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "asking for transmission of %u bytes to client %s\n",
              ntohs (msg->size), GNUNET_i2s (&subscriber->id));

  subscriber->transmit_handle =
    GNUNET_MESH_notify_transmit_ready (subscriber->channel,
				       GNUNET_NO,
                                       GNUNET_TIME_UNIT_FOREVER_REL,
                                       ntohs (msg->size),
                                       send_msg_to_subscriber, subscriber);
}


/**
 * Add a PendingMessage to the subscriber's list of messages to be sent
 *
 * @param subscriber the subscriber to send the message to
 * @param pending_message the actual message to send
 */
static void
add_pending_subscriber_message (struct RemoteSubscriberInfo *subscriber,
                                struct PendingMessage *pending_message)
{
  GNUNET_CONTAINER_DLL_insert_tail (subscriber->pending_head,
                                    subscriber->pending_tail, pending_message);
}


static void
deliver_incoming_publish (const struct GNUNET_MQTT_ClientPublishMessage *msg,
			  struct RegexSearchContext *context);


/**
 * Search callback function called when a subscribed peer is found.
 *
 * @param cls closure provided in GNUNET_REGEX_search()
 * @param id peer providing a regex that matches the string
 * @param get_path path of the get request
 * @param get_path_length length of @a get_path
 * @param put_path Path of the put request
 * @param put_path_length length of the @a put_path
 */
static void
subscribed_peer_found (void *cls, const struct GNUNET_PeerIdentity *id,
                       const struct GNUNET_PeerIdentity *get_path,
                       unsigned int get_path_length,
                       const struct GNUNET_PeerIdentity *put_path,
                       unsigned int put_path_length)
{
  struct PendingMessage *pm;
  struct RemoteSubscriberInfo *subscriber;
  struct GNUNET_MessageHeader *msg;
  struct RegexSearchContext *context = cls;
  size_t msg_len = ntohs (context->publish_msg->header.size);

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "an active subscription found from %s\n",
	      GNUNET_i2s (id));

  /*
   * We may have delivered the message to the peer already if it has
   * other matching subscriptions; ignore this search result if that is
   * the case.
   */
  if (GNUNET_CONTAINER_multipeermap_contains (context->subscribers,
                                              id))
    return;

  GNUNET_CONTAINER_multipeermap_put (context->subscribers, id,
    NULL, GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_FAST);
  subscriber = GNUNET_CONTAINER_multipeermap_get (remote_subscribers,
                                                  id);

  if (0 == memcmp (id, &my_id, sizeof (struct GNUNET_PeerIdentity)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "fast tracking PUBLISH message to local subscribers\n");

    deliver_incoming_publish (context->publish_msg, context);
    return;
  }

  msg = GNUNET_malloc (msg_len);
  memcpy (msg, context->publish_msg, msg_len);

  pm = GNUNET_new (struct PendingMessage);
  pm->msg = msg;
  pm->context = context;

  if (NULL == subscriber)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "creating a new channel to %s\n", GNUNET_i2s(id));

    subscriber = GNUNET_new (struct RemoteSubscriberInfo);

    subscriber->channel = GNUNET_MESH_channel_create (mesh_handle,
                                                      NULL,
                                                      id,
                                                      GNUNET_APPLICATION_TYPE_MQTT,
                                                      GNUNET_MESH_OPTION_RELIABLE);
    subscriber->peer_added = GNUNET_NO;
    subscriber->peer_connecting = GNUNET_NO;

    subscriber->id = *id;

    GNUNET_CONTAINER_multipeermap_put (remote_subscribers, id,
      subscriber, GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_FAST);
  }

  add_pending_subscriber_message (subscriber, pm);
  subscriber->peer_added = GNUNET_YES;
  subscriber->peer_connecting = GNUNET_NO;
  process_pending_subscriber_messages (subscriber);
}


/**
 * Call regex search to find subscribed peers.
 *
 * @param topic of the message identification of the client
 * @param publish_msg the publish message
 */
static void
search_for_subscribers (const char *topic,
                        struct GNUNET_MQTT_ClientPublishMessage *publish_msg,
                        char *file_path)
{
  struct RegexSearchContext *context;

  context = GNUNET_new (struct RegexSearchContext);
  context->publish_msg = publish_msg;
  context->file_path = file_path;
  context->message_delivered = GNUNET_NO;
  context->free_task = GNUNET_SCHEDULER_NO_TASK;
  context->subscribers = GNUNET_CONTAINER_multipeermap_create (1, GNUNET_NO);

  context->regex_search_handle = GNUNET_REGEX_search (cfg, topic,
                                                      subscribed_peer_found,NULL);
  GNUNET_CONTAINER_DLL_insert (sc_head,
			       sc_tail,
			       context);
}


/**
 * Handle MQTT-PUBLISH-message.
 *
 * @param cls closure
 * @param client identification of the client
 * @param message the actual message
 */
static void
handle_mqtt_publish (void *cls, struct GNUNET_SERVER_Client *client,
                     const struct GNUNET_MessageHeader *msg)
{
  char *topic, *message, *prefixed_topic;
  FILE *persistence_file;
  struct GNUNET_HashCode file_name_hash;
  const char *file_name;
  char *file_path;
  size_t message_len;

  size_t msg_len = ntohs (msg->size);
  struct GNUNET_MQTT_ClientPublishMessage *publish_msg;

  if (NULL == folder_name)
  {
    GNUNET_break (0);
    return;
  }
  /* Extract topic */
  publish_msg = GNUNET_malloc (msg_len);
  memcpy (publish_msg, msg, msg_len);
  topic = GNUNET_malloc (publish_msg->topic_len);
  strncpy(topic, (char *) (publish_msg + 1), publish_msg->topic_len);
  topic[publish_msg->topic_len - 1] = '\0';

  /* Extract message */
  message_len = ntohs (publish_msg->header.size) -
    sizeof (struct GNUNET_MQTT_ClientPublishMessage) - publish_msg->topic_len;
  message = GNUNET_malloc (message_len);
  strncpy(message, ((char *) (publish_msg + 1)) + publish_msg->topic_len,
          message_len);
  message[message_len - 1] = '\0';
  add_prefix (topic, &prefixed_topic);
  GNUNET_CRYPTO_hash_create_random (GNUNET_CRYPTO_QUALITY_WEAK,
				    &file_name_hash);
  file_name = GNUNET_h2s_full(&file_name_hash);
  GNUNET_asprintf (&file_path,
		   "%s%s%s",
		   folder_name,
		   DIR_SEPARATOR_STR,
		   file_name);

  if (NULL != (persistence_file = fopen(file_path, "w+")))
  {
    fwrite(topic, 1, strlen(topic)+1, persistence_file);
    fwrite(message, 1, strlen(message), persistence_file);
    fclose(persistence_file);
    search_for_subscribers (prefixed_topic, publish_msg, file_path);
  }
  else
  {
    GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_ERROR,
			      "open",
			      file_path);
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "outgoing PUBLISH message received: %s [%d bytes] (%d overall)\n",
              topic,
	      publish_msg->topic_len,
	      ntohs (publish_msg->header.size));
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Handle MQTT SUBSCRIBE message.
 *
 * @param cls closure
 * @param client identification of the client
 * @param message the actual message
 */
static void
handle_mqtt_subscribe (void *cls, struct GNUNET_SERVER_Client *client,
                       const struct GNUNET_MessageHeader *msg)
{
  struct Subscription *subscription;
  char *topic, *regex_topic;
  struct GNUNET_TIME_Relative refresh_interval;

  const struct GNUNET_MQTT_ClientSubscribeMessage *subscribe_msg;

  /* Extract topic */
  subscribe_msg = (const struct GNUNET_MQTT_ClientSubscribeMessage *) msg;
  topic = GNUNET_malloc (subscribe_msg->topic_len);
  strncpy(topic, (char *) (subscribe_msg + 1),
          subscribe_msg->topic_len);
  topic[subscribe_msg->topic_len - 1] = '\0';
  subscription = GNUNET_new (struct Subscription);
  get_regex (topic, &regex_topic);
  if (0 != regcomp (&subscription->automaton,
		    regex_topic,
		    REG_NOSUB))
  {
    GNUNET_break (0);
    GNUNET_free (subscription);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  subscription->request_id = subscribe_msg->request_id;
  subscription->client = find_active_client (client);
  GNUNET_CONTAINER_DLL_insert (subscription_head,
			       subscription_tail,
			       subscription);

  refresh_interval = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_MINUTES, 1);
  subscription->regex_announce_handle =
    GNUNET_REGEX_announce (cfg, regex_topic, refresh_interval, NULL);

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "MQTT SUBSCRIBE message received: %s->%s\n",
	      topic,
	      regex_topic);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


static void
process_pending_client_messages (struct ClientInfo *client);


/**
 * Callback called as a result of issuing
 * a GNUNET_SERVER_notify_transmit_ready request. A ClientInfo is passed
 * as closure, take the head of the list and copy it into buf, which has
 * the result of sending the message to the client.
 *
 * @param cls closure to this call
 * @param size maximum number of bytes available to send
 * @param buf where to copy the actual message to
 *
 * @return the number of bytes actually copied, 0 indicates failure
 */
static size_t
send_reply_to_client (void *cls, size_t size, void *buf)
{
  struct ClientInfo *client = cls;
  char *cbuf = buf;
  struct PendingMessage *reply;
  size_t off;
  size_t msize;

  client->transmit_handle = NULL;
  if (buf == NULL)
  {
    /* client disconnected */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Client %p disconnected, pending messages will be discarded\n",
                client->client_handle);
    return 0;
  }
  off = 0;
  while ((NULL != (reply = client->pending_head)) &&
         (size >= off + (msize = ntohs (reply->msg->size))))
  {
    GNUNET_CONTAINER_DLL_remove (client->pending_head, client->pending_tail,
                                 reply);
    memcpy (&cbuf[off], reply->msg, msize);
    if (NULL != reply->context)
    {
      set_timer_for_deleting_message(reply);
    }
    GNUNET_free (reply->msg);
    GNUNET_free (reply);
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Transmitting %u bytes to client %p\n",
		msize,
                client->client_handle);
    off += msize;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Transmitted %u/%u bytes to client %p\n",
              (unsigned int) off,
	      (unsigned int) size,
	      client->client_handle);
  process_pending_client_messages (client);
  return off;
}


/**
 * Task run to check for messages that need to be sent to a client.
 *
 * @param client a ClientInfo struct, containing the client handle and
 *               any messages to be sent to it
 */
static void
process_pending_client_messages (struct ClientInfo *client)
{
  if ((client->pending_head == NULL) || (client->transmit_handle != NULL))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Not asking for transmission to %p now: %s\n",
                client->client_handle,
                client->pending_head ==
                NULL ? "no more messages" : "request already pending");
    return;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Asking for transmission of %u bytes to client %p\n",
              ntohs (client->pending_head->msg->size),
	      client->client_handle);
  client->transmit_handle =
    GNUNET_SERVER_notify_transmit_ready (client->client_handle,
                                         ntohs (client->pending_head->
                                                msg->size),
                                         GNUNET_TIME_UNIT_FOREVER_REL,
                                         &send_reply_to_client, client);
}


/**
 * Add a PendingMessage to the client's list of messages to be sent
 *
 * @param client the active client to send the message to
 * @param pending_message the actual message to send
 */
static void
add_pending_client_message (struct ClientInfo *client,
                            struct PendingMessage *pending_message)
{
  GNUNET_CONTAINER_DLL_insert_tail (client->pending_head, client->pending_tail,
                                    pending_message);
  process_pending_client_messages (client);
}


/**
 * Handle MQTT UNSUBSCRIBE message.
 *
 * @param cls closure
 * @param client identification of the client
 * @param message the actual message
 */
static void
handle_mqtt_unsubscribe (void *cls, struct GNUNET_SERVER_Client *client,
                         const struct GNUNET_MessageHeader *msg)
{
  struct PendingMessage *pm;
  const struct GNUNET_MQTT_ClientUnsubscribeMessage *unsubscribe_msg;
  struct GNUNET_MQTT_ClientUnsubscribeAckMessage *unsub_ack_msg;
  struct ClientInfo *client_info;
  struct Subscription *subscription;

  unsubscribe_msg = (const struct GNUNET_MQTT_ClientUnsubscribeMessage *) msg;
  for (subscription = subscription_head; NULL != subscription; subscription = subscription->next)
  {
    if (subscription->request_id == unsubscribe_msg->request_id)
    {
      GNUNET_CONTAINER_DLL_remove (subscription_head,
				   subscription_tail,
				   subscription);;
      break;
    }
  }

  if (NULL == subscription)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Active subscription with ID %lu does not exist\n",
                subscription->request_id);
    return;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Cancelling subscription with ID %lu\n",
              subscription->request_id);

  client_info = find_active_client (client);
  unsub_ack_msg =
    GNUNET_new (struct GNUNET_MQTT_ClientUnsubscribeAckMessage);

  unsub_ack_msg->header.size =
    sizeof (struct GNUNET_MQTT_ClientUnsubscribeAckMessage);
  unsub_ack_msg->header.type =
    htons (GNUNET_MESSAGE_TYPE_MQTT_CLIENT_UNSUBSCRIBE_ACK);
  unsub_ack_msg->request_id = subscription->request_id;

  pm = GNUNET_new (struct PendingMessage);
  pm->msg = (struct GNUNET_MessageHeader*) unsub_ack_msg;
  add_pending_client_message (client_info, pm);
  subscription_free (subscription);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Deliver incoming PUBLISH messages to local subscribers.
 *
 * This functioe processes and forwards PUBLISH messages to the
 * appropriate subscribing application.
 */
static void
deliver_incoming_publish (const struct GNUNET_MQTT_ClientPublishMessage *msg,
			  struct RegexSearchContext *context)
{
  char *topic;
  char *prefixed_topic;
  struct GNUNET_MQTT_ClientPublishMessage *publish_msg;
  struct Subscription *subscription;
  size_t msg_len = ntohs (msg->header.size);
  int free_publish_msg = GNUNET_YES;

  /* Extract topic */
  publish_msg = GNUNET_malloc (msg_len);
  memcpy (publish_msg, msg, msg_len);
  topic = GNUNET_malloc (publish_msg->topic_len);
  strncpy (topic,
	   (const char *) &publish_msg[1],
	   publish_msg->topic_len);
  topic[publish_msg->topic_len - 1] = '\0';

  add_prefix(topic, &prefixed_topic);

  for (subscription = subscription_head; NULL != subscription; subscription = subscription->next)
  {
    if (0 == regexec (&subscription->automaton, prefixed_topic, 0, NULL, 0))
    {
      struct PendingMessage *pm;
      struct GNUNET_MQTT_ClientPublishMessage *return_msg;
      struct ClientInfo *client_info = subscription->client;

      if (GNUNET_YES == free_publish_msg)
      {
        return_msg = publish_msg;
        free_publish_msg = GNUNET_NO;
      }
      else
      {
        return_msg = GNUNET_malloc (msg_len);
        memcpy (return_msg, msg, msg_len);
      }
      return_msg->request_id = subscription->request_id;
      pm = GNUNET_new (struct PendingMessage);
      pm->msg = (struct GNUNET_MessageHeader*) return_msg;
      pm->context = context;
      add_pending_client_message (client_info, pm);
    }
  }
  GNUNET_free (topic);
  GNUNET_free (prefixed_topic);
  if (GNUNET_YES == free_publish_msg)
    GNUNET_free (publish_msg);
}


/**
 * Handle incoming PUBLISH messages.
 *
 * This handler receives messages that were sent to topics which this
 * node is subscribed to.
 */
static int
handle_incoming_publish (void *cls, struct GNUNET_MESH_Channel *channel,
                         void **channel_ctx,
                         const struct GNUNET_MessageHeader *msg)
{
  deliver_incoming_publish ((const struct GNUNET_MQTT_ClientPublishMessage*) msg,
                            NULL);

  return GNUNET_OK;
}


static int
handle_channel_message (void *cls, struct GNUNET_MESH_Channel *channel,
                       void **channel_ctx,
                       const struct GNUNET_MessageHeader *msg)
{
  return GNUNET_OK;
}


static int
free_remote_subscriber_iterator (void *cls,
				 const struct GNUNET_PeerIdentity *key,
                                 void *value)
{
  remote_subscriber_info_free (value);
  return GNUNET_YES;
}


/**
 * A client disconnected. Remove all of its data structure entries.
 *
 * @param cls closure, NULL
 * @param client identification of the client
 */
static void
handle_client_disconnect (void *cls,
			  struct GNUNET_SERVER_Client *client)
{
  struct Subscription *subscription;
  struct Subscription *nxt;
  struct ClientInfo *client_info = NULL;

  if (NULL == client)
    return;
  for (subscription=subscription_head; NULL != subscription; subscription = nxt)
  {
    nxt = subscription->next;
    if (subscription->client->client_handle == client)
    {
      client_info = subscription->client;
      GNUNET_CONTAINER_DLL_remove (subscription_head,
				   subscription_tail,
				   subscription);
      subscription_free (subscription);
    }
  }
  if (NULL == client_info)
    for (client_info = client_head; NULL != client_info; client_info = client_info->next)
      if (client_info->client_handle == client)
        break;
  if (NULL != client_info)
    client_info_free (client_info);
}


/**
 * Task run during shutdown.
 *
 * @param cls unused
 * @param tc unused
 */
static void
shutdown_task (void *cls,
	       const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct Subscription *subscription;
  struct RegexSearchContext *context;

  while (NULL != (subscription = subscription_head))
  {
    GNUNET_CONTAINER_DLL_remove (subscription_head,
				 subscription_tail,
				 subscription);
    subscription_free (subscription);
  }
  while (NULL != (context = sc_head))
  {
    GNUNET_CONTAINER_DLL_remove (sc_head,
				 sc_tail,
				 context);
    regex_search_context_free (context);
  }
  GNUNET_CONTAINER_multipeermap_iterate (remote_subscribers,
                                         free_remote_subscriber_iterator,
                                         NULL);
  GNUNET_DHT_disconnect (dht_handle);
  GNUNET_MESH_disconnect (mesh_handle);
  GNUNET_CONTAINER_multipeermap_destroy (remote_subscribers);
}


static void *
new_incoming_channel_callback (void *cls, struct GNUNET_MESH_Channel *channel,
                               const struct GNUNET_PeerIdentity *initiator,
                               uint32_t port,
                               enum GNUNET_MESH_ChannelOption options)
{
  return NULL;
}


static void
incoming_channel_destroyed_callback (void *cls,
                                    const struct GNUNET_MESH_Channel *channel,
                                    void *channel_ctx)
{
}


/**
 * Look for old messages and call try to deliver them again by calling
 * regex search
 */
static void
look_for_old_messages ()
{
  DIR *dir;
  FILE *file;
  struct dirent *ent;
  char *file_path;
  char *topic;
  char *aux;
  char *prefixed_topic;
  size_t struct_size;
  size_t n;
  int ch;
  long long length;
  struct GNUNET_MQTT_ClientPublishMessage *old_publish_msg;

  uid_gen = GNUNET_CRYPTO_random_u64 (GNUNET_CRYPTO_QUALITY_WEAK,
				      UINT64_MAX);
  folder_name = NULL;
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_filename (cfg,
					       "MQTT",
					       "MESSAGE_FOLDER",
					       &folder_name))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
			       "MQTT", "MESSAGE_FOLDER");
    return;
  }
  if (NULL == (dir = opendir (folder_name)))
  {
    GNUNET_DISK_directory_create (folder_name);
    return;
  }

  while (NULL != (ent = readdir (dir)))
  {
    if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
      continue;

    GNUNET_asprintf (&file_path,
		     "%s%s%s",
		     folder_name,
		     DIR_SEPARATOR_STR,
		     ent->d_name);
    file = fopen(file_path, "r");
    if (NULL == file)
    {
      GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING,
				"open",
				file_path);
      GNUNET_free (file_path);
      continue;
    }
    n = 0;
    fseeko (file, 0, SEEK_END); // seek to end
    length = ftello (file); // determine offset of end
    rewind (file);  // restore position

    struct_size = sizeof(struct GNUNET_MQTT_ClientPublishMessage) + length + 1;

    old_publish_msg = GNUNET_malloc(struct_size);
    old_publish_msg->header.size = htons (struct_size);
    old_publish_msg->header.type = htons (GNUNET_MESSAGE_TYPE_MQTT_CLIENT_PUBLISH);
    old_publish_msg->request_id = ++uid_gen;

    aux = (char*)&old_publish_msg[1];
    while ((ch = fgetc(file)) != EOF && (ch != '\0') )
    {
      aux[n] = (char) ch;
      n++;
    }

    old_publish_msg->topic_len = n + 1;
    aux[n] = '\0';
    n++;
    while ((ch = fgetc(file)) != EOF )
    {
      aux[n] = (char) ch;
      n++;
    }

    aux[n] = '\0';

    topic = GNUNET_malloc (old_publish_msg->topic_len);
    strncpy(topic, (char *) (old_publish_msg + 1), old_publish_msg->topic_len);
    topic[old_publish_msg->topic_len - 1] = '\0';

    add_prefix (topic, &prefixed_topic);

    search_for_subscribers(prefixed_topic, old_publish_msg, file_path);
    GNUNET_free (file_path);
    GNUNET_free (topic);
  }
  closedir (dir);
}


/**
 * Process statistics requests.
 *
 * @param cls closure
 * @param server the initialized server
 * @param c configuration to use
 */
static void
run (void *cls, struct GNUNET_SERVER_Handle *server,
     const struct GNUNET_CONFIGURATION_Handle *c)
{
  static const struct GNUNET_SERVER_MessageHandler handlers[] = {
    {handle_mqtt_publish, NULL, GNUNET_MESSAGE_TYPE_MQTT_CLIENT_PUBLISH, 0},
    {handle_mqtt_subscribe, NULL, GNUNET_MESSAGE_TYPE_MQTT_CLIENT_SUBSCRIBE,
      0},
    {handle_mqtt_unsubscribe, NULL,
      GNUNET_MESSAGE_TYPE_MQTT_CLIENT_UNSUBSCRIBE, 0},
    {NULL, NULL, 0, 0}
  };
  static const struct GNUNET_MESH_MessageHandler mesh_handlers[] = {
    {&handle_incoming_publish, GNUNET_MESSAGE_TYPE_MQTT_CLIENT_PUBLISH, 0},
    {&handle_channel_message, GNUNET_MESSAGE_TYPE_MQTT_CLIENT_SUBSCRIBE, 0},
    {NULL, 0, 0}
  };
  static const uint32_t ports[] = {
    GNUNET_APPLICATION_TYPE_MQTT,
    GNUNET_APPLICATION_TYPE_END
  };


  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_time (c, "mqtt",
                                           "MESSAGE_DELETE_TIME",
                                           &message_delete_time))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                _("%s service is lacking key configuration settings (%s). "
                  "Exiting.\n"),
                "mqtt", "message delete time");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  remote_subscribers = GNUNET_CONTAINER_multipeermap_create (8, GNUNET_NO);
  server_handle = server;
  GNUNET_assert (GNUNET_OK ==
		 GNUNET_CRYPTO_get_peer_identity (c,
						  &my_id));
  dht_handle = GNUNET_DHT_connect (c, 32);
  mesh_handle = GNUNET_MESH_connect (c,
				     NULL,
				     &new_incoming_channel_callback,
                                     &incoming_channel_destroyed_callback,
                                     mesh_handlers, ports);

  cfg = c;
  GNUNET_SERVER_add_handlers (server, handlers);
  GNUNET_SERVER_disconnect_notify (server, &handle_client_disconnect, NULL);
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL,
				&shutdown_task,
                                NULL);
  look_for_old_messages ();
}


/**
 * The main function for the MQTT service.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  return (GNUNET_OK ==
          GNUNET_SERVICE_run (argc, argv, "mqtt", GNUNET_SERVICE_OPTION_NONE,
                              &run, NULL)) ? 0 : 1;
}
