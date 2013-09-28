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
#include <gnunet/gnunet_regex_lib.h>
#include <gnunet/gnunet_core_service.h>
#include <gnunet/gnunet_dht_service.h>
#include <gnunet/gnunet_mesh_service.h>
#include <gnunet/gnunet_applications.h>
#include <gnunet/gnunet_configuration_lib.h>
#include "gnunet_protocols_mqtt.h"
#include "mqtt.h"
#include "regex_utils.h"


/**
 * Struct representing the context for the regex search
 */
struct RegexSearchContext
{
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
  struct GNUNET_CONTAINER_MultiHashMap *subscribers;

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
  const struct GNUNET_PeerIdentity *id;

  /**
   * Tunnel connecting us to the subscriber.
   */
  struct GNUNET_MESH_Tunnel *tunnel;

  /**
   * Has the subscriber been added to the tunnel yet?
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


/*
 * Struct representing one active subscription in our service.
 */
struct Subscription
{
  /* Handle used to reannounce the subscription */
  struct GNUNET_REGEX_announce_handle *regex_announce_handle;

  /* The subscribed client */
  struct ClientInfo *client;

  /* Unique ID for this subscription */
  uint64_t request_id;

  /* The automaton built using the subcription provided by the user */
  struct GNUNET_REGEX_Automaton *automaton;
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
 * Handle to the core service.
 */
static struct GNUNET_CORE_Handle *core_handle;

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
static const struct GNUNET_PeerIdentity *my_id;

/**
 * Singly linked list storing active subscriptions.
 */
static struct GNUNET_CONTAINER_SList *subscriptions;

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
static struct GNUNET_CONTAINER_MultiHashMap *remote_subscribers;

/**
 * Generator for unique ids.
 */
static uint64_t uid_gen;

/**
 * Path to the current directory (configuration directory)
 */
static char *current_dir_name;

/**
 * Singly linked list storing active regex search contexts.
 */
static struct GNUNET_CONTAINER_SList *search_contexts;

/**
 * How often to reannounce active subscriptions.
 */
static struct GNUNET_TIME_Relative subscription_reannounce_time;

/**
 * The time the peer that received a publish message waits before it deletes it after it was sent to a subscrier
 */
static struct GNUNET_TIME_Relative message_delete_time;

/**
 * Task responsible for reannouncing active subscriptions.
 */
static GNUNET_SCHEDULER_TaskIdentifier reannounce_task;


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

  if (NULL != client_info->transmit_handle) {
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

  ret = GNUNET_malloc (sizeof (struct ClientInfo));
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
  GNUNET_REGEX_automaton_destroy (subscription->automaton);
  GNUNET_REGEX_announce_cancel (subscription->regex_announce_handle);
  GNUNET_free (subscription);
}


/**
 * Free the provided RemoteSubscriberInfo struct.
 *
 * This function takes care to cancel any pending transmission requests
 * and discard all outstanding messages not delivered to the subscriber
 * yet. Moreover, it destroys the tunnel connecting us to the subscriber
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

  GNUNET_MESH_tunnel_destroy (subscriber->tunnel);
  GNUNET_free ((void *) subscriber->id);
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

    if (remove (filepath) == 0)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "file `%s` deleted successfully.\n", filepath);
    }
    else
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "unable to delete file `%s`\n", filepath);
    }
  }

  GNUNET_CONTAINER_multihashmap_destroy (context->subscribers);
  GNUNET_REGEX_search_cancel (context->regex_search_handle);
  GNUNET_free (context->publish_msg);
  GNUNET_free (context->file_path);
  GNUNET_free (context);
}


static void
process_pending_subscriber_messages (struct RemoteSubscriberInfo *subscriber);

static void
delete_delivered_message (void *cls,
                          const struct GNUNET_SCHEDULER_TaskContext *tc);

static void 
set_timer_for_deleting_message (struct PendingMessage *pm);


/**
 * Callback called as a result of issuing
 * a GNUNET_MESH_notify_transmit_ready request. A RemoteSubscriberInfo
 * is passed as closure, take the head of the list and copy it into buf,
 * which has the result of sending the message to the subscriber.
 *
 * @param cls closure to this call
 * @param size maximum number of bytes available to send
 * @param buf where to copy the actual message to
 *
 * @return the number of bytes actually copied, 0 indicates failure
 */
static size_t
send_msg_to_subscriber (void *cls, size_t size, void *buf)
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
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Subscriber %s disconnected, %s",
                GNUNET_i2s (subscriber->id),
                "pending messages will be discarded\n");
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
                GNUNET_i2s (subscriber->id));
    off += msize;
	set_timer_for_deleting_message(pm);	

	GNUNET_free (pm->msg);
	GNUNET_free (pm);
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Transmitted %zu/%zu bytes to subscriber %s\n",
              off, size, GNUNET_i2s (subscriber->id));
  process_pending_subscriber_messages (subscriber);
  
  return off;
}


/**
 * Task run to check for messages that need to be sent to a subscriber.
 *
 * @param client a RemoteSubscriberInfo struct, containing the tunnel
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
                GNUNET_i2s (subscriber->id),
                subscriber->pending_head ==
                NULL ? "no more messages" : "request already pending");
    return;
  }

  msg = subscriber->pending_head->msg;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "asking for transmission of %u bytes to client %s\n",
              ntohs (msg->size), GNUNET_i2s (subscriber->id));

  subscriber->transmit_handle =
    GNUNET_MESH_notify_transmit_ready (subscriber->tunnel, GNUNET_NO,
                                       GNUNET_TIME_UNIT_FOREVER_REL,
                                       subscriber->id, ntohs (msg->size),
                                       send_msg_to_subscriber, subscriber);
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
  struct GNUNET_CONTAINER_SList_Iterator it;

  it = GNUNET_CONTAINER_slist_begin (search_contexts);

  while (GNUNET_CONTAINER_slist_end (&it) != GNUNET_YES)
  {
    if (GNUNET_CONTAINER_slist_get (&it, NULL) == context)
    {
      GNUNET_CONTAINER_slist_erase (&it);
      break;
    }
    else
      GNUNET_CONTAINER_slist_next (&it);
  }

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


/**
 * Method called whenever a subscriber has disconnected from the tunnel.
 *
 * @param cls closure
 * @param peer peer identity the tunnel stopped working with
 */
static void
subscribed_peer_disconnected (void *cls,
                              const struct GNUNET_PeerIdentity *peer)
{
  struct RemoteSubscriberInfo *subscriber = cls;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "subscribed peer %s disconnected\n",
              GNUNET_i2s (peer));

  subscriber->peer_added = GNUNET_NO;
  subscriber->peer_connecting = GNUNET_NO;
}


/**
 * Method called whenever a subscriber has connected to the tunnel.
 *
 * @param cls closure
 * @param peer peer identity the tunnel was created to, NULL on timeout
 * @param atsi performance data for the connection
 */
static void
subscribed_peer_connected (void *cls, const struct GNUNET_PeerIdentity *peer,
                           const struct GNUNET_ATS_Information *atsi)
{
  struct RemoteSubscriberInfo *subscriber = cls;

  if (NULL == peer) {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Connecting to subscribed peer %s timed out.\n",GNUNET_i2s (peer));
    /* TODO: destroy the tunnel */
    subscriber->peer_connecting = GNUNET_NO;
    return;
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "subscribed peer %s connected\n",
              GNUNET_i2s (peer));

  subscriber->peer_added = GNUNET_YES;
  subscriber->peer_connecting = GNUNET_NO;
  process_pending_subscriber_messages (subscriber);
}


static void
deliver_incoming_publish (struct GNUNET_MQTT_ClientPublishMessage *msg, struct RegexSearchContext *context);


/**
 * Search callback function called when a subscribed peer is found.
 *
 * @param cls closure provided in GNUNET_REGEX_search
 * @param id peer providing a regex that matches the string
 * @param get_path path of the get request
 * @param get_path_length lenght of get_path
 * @param put_path Path of the put request
 * @param put_path_length length of the put_path
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
              "an active subscription found from %s\n", GNUNET_i2s (id));

  /*
   * We may have delivered the message to the peer already if it has
   * other matching subscriptions; ignore this search result if that is
   * the case.
   */
  if (GNUNET_CONTAINER_multihashmap_contains (context->subscribers,
                                              &id->hashPubKey))
    return;

  GNUNET_CONTAINER_multihashmap_put (context->subscribers, &id->hashPubKey,
    NULL, GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_FAST);
  subscriber = GNUNET_CONTAINER_multihashmap_get (remote_subscribers,
                                                  &id->hashPubKey);

  if (0 == GNUNET_CRYPTO_hash_cmp (&id->hashPubKey, &my_id->hashPubKey))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "fast tracking PUBLISH message to local subscribers\n");

    deliver_incoming_publish (context->publish_msg, context);
    return;
  }

  msg = GNUNET_malloc (msg_len);
  memcpy (msg, context->publish_msg, msg_len);

  pm = GNUNET_malloc (sizeof (struct PendingMessage));
  pm->msg = msg;
  pm->context = context;

  if (NULL == subscriber)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "creating a new tunnel to %s\n", GNUNET_i2s(id));

    subscriber = GNUNET_malloc (sizeof (struct RemoteSubscriberInfo));

    subscriber->tunnel = GNUNET_MESH_tunnel_create (mesh_handle, NULL,
      subscribed_peer_connected, subscribed_peer_disconnected, subscriber);
    subscriber->peer_added = GNUNET_NO;
    subscriber->peer_connecting = GNUNET_NO;

    subscriber->id = GNUNET_malloc (sizeof (struct GNUNET_PeerIdentity));
    memcpy ((struct GNUNET_PeerIdentity*) subscriber->id, id,
            sizeof (struct GNUNET_PeerIdentity));

    GNUNET_CONTAINER_multihashmap_put (remote_subscribers, &id->hashPubKey,
      subscriber, GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_FAST);
  }

  add_pending_subscriber_message (subscriber, pm);

  if (GNUNET_YES == subscriber->peer_added)
  {
    process_pending_subscriber_messages (subscriber);
  }
  else if (GNUNET_NO == subscriber->peer_connecting)
  {
    GNUNET_MESH_peer_request_connect_add (subscriber->tunnel, id);
    subscriber->peer_connecting = GNUNET_YES;
  }
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

  context = GNUNET_malloc (sizeof (struct RegexSearchContext));
  context->publish_msg = publish_msg;
  context->file_path = file_path;
  context->message_delivered = GNUNET_NO;
  context->free_task = GNUNET_SCHEDULER_NO_TASK;
  context->subscribers = GNUNET_CONTAINER_multihashmap_create (1, GNUNET_NO);
  context->regex_search_handle = GNUNET_REGEX_search (dht_handle, topic,
                                                      subscribed_peer_found,
                                                      context, NULL);

  GNUNET_CONTAINER_slist_add (search_contexts,
                              GNUNET_CONTAINER_SLIST_DISPOSITION_STATIC,
                              context, sizeof (struct RegexSearchContext));
}

/**
 * Handle MQTT-PUBLISH-message.
 *
 * @param cls closure
 * @param client identification of the client
 * @param message the actual message
 * @return GNUNET_OK to keep the connection open,
 *         GNUNET_SYSERR to close it (signal serious error)
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
   
  if (NULL != current_dir_name)
  {
	 GNUNET_CRYPTO_hash_create_random(GNUNET_CRYPTO_QUALITY_WEAK, &file_name_hash);
	 file_name = GNUNET_h2s_full(&file_name_hash);
	 
	 GNUNET_asprintf (&file_path, "%s%s%s", current_dir_name, DIR_SEPARATOR_STR, file_name );			
	 //file_path = GNUNET_malloc (strlen(current_dir_name)+ strlen(file_name) + 2);
	 //strcpy(file_path, current_dir_name);
	 //strcat(file_path, DIR_SEPARATOR_STR);
	 //strcat(file_path, file_name);	 
	 	 
	 if (NULL != (persistence_file = fopen(file_path, "w+")))
	 {
		fwrite(topic, 1, strlen(topic)+1, persistence_file);
		fwrite(message, 1, strlen(message), persistence_file);
		fclose(persistence_file);
		
		search_for_subscribers (prefixed_topic, publish_msg, file_path);
	  }
	  else
	{
		GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
              "Not able to open file!");
	}
	}	
	else
	{
		GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
              "Not able to get current directory!");
	}
  
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "outgoing PUBLISH message received: %s [%d bytes] (%d overall)\n",
              topic, publish_msg->topic_len, ntohs(publish_msg->header.size));
 

  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Handle MQTT SUBSCRIBE message.
 *
 * @param cls closure
 * @param client identification of the client
 * @param message the actual message
 * @return GNUNET_OK to keep the connection open,
 *         GNUNET_SYSERR to close it (signal serious error)
 */
static void
handle_mqtt_subscribe (void *cls, struct GNUNET_SERVER_Client *client,
                       const struct GNUNET_MessageHeader *msg)
{
  struct Subscription *subscription;
  char *topic, *regex_topic;
  
  const struct GNUNET_MQTT_ClientSubscribeMessage *subscribe_msg;

  /* Extract topic */
  subscribe_msg = (const struct GNUNET_MQTT_ClientSubscribeMessage *) msg;
  topic = GNUNET_malloc (subscribe_msg->topic_len);
  strncpy(topic, (char *) (subscribe_msg + 1),
          subscribe_msg->topic_len);
  topic[subscribe_msg->topic_len - 1] = '\0';

  subscription = GNUNET_malloc (sizeof (struct Subscription));
  subscription->request_id = subscribe_msg->request_id;
  subscription->client = find_active_client (client);

  get_regex(topic, &regex_topic);
 
  subscription->automaton = GNUNET_REGEX_construct_dfa(regex_topic, strlen(regex_topic), 1);
 
  GNUNET_CONTAINER_slist_add (subscriptions,
                              GNUNET_CONTAINER_SLIST_DISPOSITION_STATIC,
                              subscription, sizeof (struct Subscription));
  subscription->regex_announce_handle =
    GNUNET_REGEX_announce (dht_handle, (struct GNUNET_PeerIdentity *) my_id,
                           regex_topic,1 , NULL);

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "MQTT SUBSCRIBE message received: %s->%s\n", topic, regex_topic);

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
                "Transmitting %u bytes to client %p\n", msize,
                client->client_handle);
    off += msize;
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Transmitted %u/%u bytes to client %p\n",
              (unsigned int) off, (unsigned int) size, client->client_handle);
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
              ntohs (client->pending_head->msg->size), client->client_handle);

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
 * @return GNUNET_OK to keep the connection open,
 *         GNUNET_SYSERR to close it (signal serious error)
 */
static void
handle_mqtt_unsubscribe (void *cls, struct GNUNET_SERVER_Client *client,
                         const struct GNUNET_MessageHeader *msg)
{
  struct PendingMessage *pm;
  struct GNUNET_CONTAINER_SList_Iterator it;
  const struct GNUNET_MQTT_ClientUnsubscribeMessage *unsubscribe_msg;
  struct GNUNET_MQTT_ClientUnsubscribeAckMessage *unsub_ack_msg;
  struct ClientInfo *client_info;
  struct Subscription *subscription = NULL;

  unsubscribe_msg = (const struct GNUNET_MQTT_ClientUnsubscribeMessage *) msg;

  for (it = GNUNET_CONTAINER_slist_begin (subscriptions);
       GNUNET_CONTAINER_slist_end (&it) != GNUNET_YES;)
  {
    subscription = GNUNET_CONTAINER_slist_get (&it, NULL);

    if (subscription->request_id == unsubscribe_msg->request_id)
    {
      GNUNET_CONTAINER_slist_erase (&it);
      break;
    }

    GNUNET_CONTAINER_slist_next (&it);
  }

  if (NULL == subscription)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Active subscription with ID %lu does not exist\n",
                subscription->request_id);
	return;
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Cancelling subscription with ID %lu\n",
              subscription->request_id);

  client_info = find_active_client (client);
  unsub_ack_msg =
    GNUNET_malloc (sizeof (struct GNUNET_MQTT_ClientUnsubscribeAckMessage));

  unsub_ack_msg->header.size =
    sizeof (struct GNUNET_MQTT_ClientUnsubscribeAckMessage);
  unsub_ack_msg->header.type =
    htons (GNUNET_MESSAGE_TYPE_MQTT_CLIENT_UNSUBSCRIBE_ACK);
  unsub_ack_msg->request_id = subscription->request_id;

  pm = GNUNET_malloc (sizeof (struct PendingMessage));
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
deliver_incoming_publish (struct GNUNET_MQTT_ClientPublishMessage *msg, struct RegexSearchContext *context)
{
  char *topic, *prefixed_topic;
  struct GNUNET_MQTT_ClientPublishMessage *publish_msg;
  struct GNUNET_CONTAINER_SList_Iterator it;
  size_t msg_len = ntohs (msg->header.size);
  int free_publish_msg = GNUNET_YES;

  /* Extract topic */
  publish_msg = GNUNET_malloc (msg_len);
  memcpy (publish_msg, msg, msg_len);
  topic = GNUNET_malloc (publish_msg->topic_len);
  strncpy(topic, (char *) (publish_msg + 1), publish_msg->topic_len);
  topic[publish_msg->topic_len - 1] = '\0';
  
  add_prefix(topic, &prefixed_topic);

  it = GNUNET_CONTAINER_slist_begin (subscriptions);

  while (GNUNET_CONTAINER_slist_end (&it) != GNUNET_YES)
  {
    struct Subscription *subscription = GNUNET_CONTAINER_slist_get (&it, NULL);

    if (0 == GNUNET_REGEX_eval(subscription->automaton, prefixed_topic))
    {
      struct PendingMessage *pm;
      struct GNUNET_MQTT_ClientPublishMessage *return_msg;
      struct ClientInfo *client_info = subscription->client;

      if (GNUNET_YES == free_publish_msg) {
        return_msg = publish_msg;
        free_publish_msg = GNUNET_NO;
      }
      else
      {
        return_msg = GNUNET_malloc (msg_len);
        memcpy (return_msg, msg, msg_len);
      }

      return_msg->request_id = subscription->request_id;

      pm = GNUNET_malloc (sizeof (struct PendingMessage));
      pm->msg = (struct GNUNET_MessageHeader*) return_msg;
	  pm->context = context;

      add_pending_client_message (client_info, pm);
    }

    GNUNET_CONTAINER_slist_next (&it);
  }

  GNUNET_free (topic);
  GNUNET_free (prefixed_topic);

  if (GNUNET_YES == free_publish_msg) {
    GNUNET_free (publish_msg);
  }
}


/**
 * Handle incoming PUBLISH messages.
 *
 * This handler receives messages that were sent to topics which this
 * node is subscribed to.
 */
static int
handle_incoming_publish (void *cls, struct GNUNET_MESH_Tunnel *tunnel,
                         void **tunnel_ctx,
                         const struct GNUNET_PeerIdentity *sender,
                         const struct GNUNET_MessageHeader *msg,
                         const struct GNUNET_ATS_Information *atsi)
{
  deliver_incoming_publish ((struct GNUNET_MQTT_ClientPublishMessage*) msg,
                            NULL);

  return GNUNET_OK;
}


static int
handle_tunnel_message (void *cls, struct GNUNET_MESH_Tunnel *tunnel,
                       void **tunnel_ctx,
                       const struct GNUNET_PeerIdentity *sender,
                       const struct GNUNET_MessageHeader *msg,
                       const struct GNUNET_ATS_Information *atsi)
{
  return GNUNET_OK;
}


/**
 * Task responsible for reannouncing regexes of active subscriptions.
 *
 * @param cls unused
 * @param tc unused
 */
static void
reannounce_subscriptions (void *cls,
                          const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_CONTAINER_SList_Iterator it;

  it = GNUNET_CONTAINER_slist_begin (subscriptions);

  while (GNUNET_CONTAINER_slist_end (&it) != GNUNET_YES)
  {
    struct Subscription *subscription = GNUNET_CONTAINER_slist_get (&it, NULL);

    GNUNET_REGEX_reannounce (subscription->regex_announce_handle);
    GNUNET_CONTAINER_slist_next (&it);
  }

  reannounce_task = GNUNET_SCHEDULER_add_delayed (subscription_reannounce_time,
                                                  reannounce_subscriptions,
                                                  NULL);
}


static int
free_remote_subscriber_iterator (void *cls, const struct GNUNET_HashCode *key,
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
handle_client_disconnect (void *cls, struct GNUNET_SERVER_Client *client)
{
  struct GNUNET_CONTAINER_SList_Iterator it;
  struct ClientInfo *client_info = NULL;

  if (NULL == client)
    return;

  it = GNUNET_CONTAINER_slist_begin (subscriptions);

  while (GNUNET_CONTAINER_slist_end (&it) != GNUNET_YES)
  {
    struct Subscription *subscription = GNUNET_CONTAINER_slist_get (&it, NULL);

    if (subscription->client->client_handle == client)
    {
      client_info = subscription->client;
      GNUNET_CONTAINER_slist_erase (&it);
      subscription_free (subscription);
    }
    else
      GNUNET_CONTAINER_slist_next (&it);
  }

  if (NULL == client_info)
  {
    client_info = client_head;

    while (client_info != NULL)
    {
      if (client_info->client_handle == client)
        break;

      client_info = client_info->next;
    }
  }

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
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_CONTAINER_SList_Iterator it;

  it = GNUNET_CONTAINER_slist_begin (subscriptions);

  while (GNUNET_CONTAINER_slist_end (&it) != GNUNET_YES)
  {
    struct Subscription *subscription = GNUNET_CONTAINER_slist_get (&it, NULL);

    GNUNET_CONTAINER_slist_erase (&it);
    subscription_free (subscription);
  }

  it = GNUNET_CONTAINER_slist_begin (search_contexts);

  while (GNUNET_CONTAINER_slist_end (&it) != GNUNET_YES)
  {
    struct RegexSearchContext *context = GNUNET_CONTAINER_slist_get (&it,
                                                                     NULL);

    GNUNET_CONTAINER_slist_erase (&it);
    regex_search_context_free (context);
  }

  GNUNET_CONTAINER_multihashmap_iterate (remote_subscribers,
                                         free_remote_subscriber_iterator,
                                         NULL);

  GNUNET_DHT_disconnect (dht_handle);
  GNUNET_CORE_disconnect (core_handle);
  GNUNET_MESH_disconnect (mesh_handle);
  GNUNET_CONTAINER_slist_destroy (subscriptions);
  GNUNET_CONTAINER_slist_destroy (search_contexts);
  GNUNET_CONTAINER_multihashmap_destroy (remote_subscribers);

  GNUNET_SCHEDULER_cancel (reannounce_task);
  GNUNET_SERVER_disconnect_notify_cancel (server_handle,
                                          handle_client_disconnect, NULL);
}


static void
core_connected_callback (void *cls,
                         struct GNUNET_CORE_Handle * server,
                         const struct GNUNET_PeerIdentity * my_identity)
{
  my_id = my_identity;
}



static void*
new_incoming_tunnel_callback (void *cls, struct GNUNET_MESH_Tunnel *tunnel,
                              const struct GNUNET_PeerIdentity *initiator,
                              const struct GNUNET_ATS_Information *atsi)
{
  return NULL;
}


static void
incoming_tunnel_destroyed_callback (void *cls,
                                    const struct GNUNET_MESH_Tunnel *tunnel,
                                    void *tunnel_ctx)
{
}

/**
 * Look for old messages and call try to deliver them again by calling regex search
 *
  */
static void
look_for_old_messages ()
{
	DIR *dir;
	FILE *file;
	char *current_dir;
	char *folder_name = "mqtt";
	struct dirent *ent;
	char *file_path;
	char *topic, *aux, *prefixed_topic;
	size_t struct_size;
    int ch;
	long long length;
	struct GNUNET_MQTT_ClientPublishMessage *old_publish_msg;
	
	uid_gen = GNUNET_CRYPTO_random_u64 (GNUNET_CRYPTO_QUALITY_WEAK,
                                              UINT64_MAX);
	current_dir_name = NULL;
	if (GNUNET_OK == GNUNET_CONFIGURATION_get_value_filename(cfg, "PATHS", "SERVICEHOME", &current_dir))
  {
   GNUNET_asprintf (&current_dir_name, "%s%s", current_dir, folder_name);
	 //current_dir_name = GNUNET_malloc (strlen(current_dir)+ strlen(folder_name)+1);
	 //strcpy(current_dir_name, current_dir);
	 //strcat(current_dir_name, folder_name);
	
	
	
	if ((dir = opendir (current_dir_name)) != NULL) 
	{
		while ((ent = readdir (dir)) != NULL) {
				
			 if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;
			
			GNUNET_asprintf (&file_path, "%s%s%s", current_dir_name, DIR_SEPARATOR_STR, ent->d_name );			
			//file_path = GNUNET_malloc (strlen(current_dir_name)+ strlen(ent->d_name) + 1);
			//strcpy(file_path, current_dir_name);
			//strcat(file_path, DIR_SEPARATOR_STR);
			//strcat(file_path, ent->d_name);
			file = fopen(file_path, "r");
            if (file != NULL)
			{
                size_t n = 0;

			    fseeko( file, 0, SEEK_END ); // seek to end
				length = ftello( file ); // determine offset of end
				rewind(file);  // restore position
							
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
				
				GNUNET_free(topic);
				}				
				}
			closedir (dir);
	} else {
	    mkdir(current_dir_name, S_IRWXU);
		GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "A new folder was created for persisting messages: %s\n", current_dir_name);
	}
	}else
	{
		GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
              "Not able to get current directory!");
	}
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
    {&handle_tunnel_message, GNUNET_MESSAGE_TYPE_MQTT_CLIENT_SUBSCRIBE, 0},
    {NULL, 0, 0}
  };
  static const GNUNET_MESH_ApplicationType types[] = {
    GNUNET_APPLICATION_TYPE_END
  };

  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_time (c, "mqtt",
                                           "SUBSCRIPTION_REANNOUNCE_TIME",
                                           &subscription_reannounce_time))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                _("%s service is lacking key configuration settings (%s). "
                  "Exiting.\n"),
                "mqtt", "subscription reannounce time");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  
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

  subscriptions = GNUNET_CONTAINER_slist_create ();
  search_contexts = GNUNET_CONTAINER_slist_create ();
  remote_subscribers = GNUNET_CONTAINER_multihashmap_create (8, GNUNET_NO);

  server_handle = server;
  core_handle = GNUNET_CORE_connect (c, NULL, core_connected_callback, NULL,
                                     NULL, NULL, GNUNET_NO, NULL, GNUNET_NO,
                                     NULL);
  dht_handle = GNUNET_DHT_connect (c, 32);
  mesh_handle = GNUNET_MESH_connect (c, NULL, new_incoming_tunnel_callback,
                                     incoming_tunnel_destroyed_callback,
                                     mesh_handlers, types);

  cfg = c;
  GNUNET_SERVER_add_handlers (server, handlers);
  GNUNET_SERVER_disconnect_notify (server, &handle_client_disconnect, NULL);

  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL, &shutdown_task,
                                NULL);
  reannounce_task = GNUNET_SCHEDULER_add_delayed (subscription_reannounce_time,
                                                  reannounce_subscriptions,
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
