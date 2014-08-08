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
#include <gnunet/gnunet_multicast_service.h>
#include "gnunet_protocols_mqtt.h"
#include "mqtt.h"
#include <regex.h>

#define LOG(kind,...) GNUNET_log_from (kind, "mqtt",__VA_ARGS__)
/**
 * Log debug messages for this module
 *
 * @param ... The format sting and optional arguments
 */
#define LOG_DEBUG(...) LOG (GNUNET_ERROR_TYPE_DEBUG, __VA_ARGS__)
/**
 * Log error messages for this module
 *
 * @param ... The format sting and optional arguments
 */
#define LOG_ERROR(...) LOG (GNUNET_ERROR_TYPE_ERROR, __VA_ARGS__)
/**
 * Log warning messages for this module
 *
 * @param ... The format sting and optional arguments
 */
#define LOG_WARNING(...) LOG (GNUNET_ERROR_TYPE_WARNING, __VA_ARGS__)


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
	 * Channel that serves the topic we are searching subscribers for
	 */
	struct MulticastChannel *channel;

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
  struct GNUNET_REGEX_Search *regex_search_handle;
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
 * List item used to have a record of all current DHT Monitor operations
 */
struct DhtMonitorList
{
  /**
   * Linked list of active clients
   */
  struct DhtMonitorList *next;

  /**
   * Linked list of active clients
   */
  struct DhtMonitorList *prev;

  /**
   * Handle to a running DHT Monitor operation
   */
  struct GNUNET_DHT_MonitorHandle *monitor_handle;
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
  struct GNUNET_REGEX_Announcement *regex_announce_handle;

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

  /**
   * First item in the list of running DHT Monitor operations
   */
  struct DhtMonitorList *dht_monitor_list_head;

  /**
   * Last item in the list of running DHT Monitor operations
   */
  struct DhtMonitorList *dht_monitor_list_tail;
};


/**
 * Struct describing a multicast channel, it's matching topic subscription
 * accept states keys, which are covered so far.
 */
struct MulticastChannel
{
	/**
	 * Expiration of the dht channel announcement
	 */
	struct GNUNET_TIME_Absolute expiration;

	/**
	 * Private key used to create the channel
	 */
	struct GNUNET_CRYPTO_EddsaPrivateKey channel_private_key;

	/**
	 * Element in a DLL.
	 */
	struct MulticastChannel *prev;

	/**
	 * Element in a DLL.
	 */
	struct MulticastChannel *next;

	/**
	 * Multicast Channel handle
	 */
	struct GNUNET_MULTICAST_Origin *channel;

	/**
	 * topic that the channel is broadcasting about
	 */
	char *channel_topic;

	/**
	 * DLL head of accept states that are served by this Channel
	 */
	struct AcceptState *as_head;

	/**
	 * DLL tail of accept states that are served by this Channel
	 */
	struct AcceptState *as_tail;

	/**
	 * pending messegaes DLL
	 */
	struct PendingMulticastMessage *pending_head;

	/**
	 * pending messegaes DLL
	 */
	struct PendingMulticastMessage *pending_tail;
};


/**
 * DHT keys of regex accept states
 */
struct AcceptState
{
	/**
	 * Element in a DLL.
	 */
	struct AcceptState *prev;

	/**
	 * Element in a DLL.
	 */
	struct AcceptState *next;

	/**
	 * DHT key of the accept state
	 */
	struct GNUNET_HashCode hash;

	/**
	 * If the accept state was announced in the DHT, takes GNUNET_YES and GNUNET_NO as values
	 */
	int announced;
};


struct PendingMulticastMessage
{
	/**
	 * Element in a DLL.
	 */
	struct PendingMulticastMessage *prev;

	/**
	 * Element in a DLL.
	 */
	struct PendingMulticastMessage *next;

	/**
	 * actual message
	 */
	struct GNUNET_MQTT_ClientPublishMessage msg;
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
 * Tail of active subscriptions.
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
static const char *prefix = "GNUNET-MQTT 0001 00000 ";

/**
 * String constant for replacing '+' wildcard in the subscribed topics.
 */
static const char *plus_regex = "(a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|0|1|2|3|4|5|6|7|8|9)+";

/**
 * String constant for replacing '#' wildcard in the subscribed topics.
 */
static const char *hash_regex = "(/(a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|0|1|2|3|4|5|6|7|8|9)+)*";

/**
 * Head of DLL of all multicast channels maintained by this service (publisher)
 */
static struct MulticastChannel* multicast_channels_head;

/**
 * Tail of DLL of all multicast channels maintained by this service (publisher)
 */
static struct MulticastChannel* multicast_channels_tail;


/**
 * Returns a string containing the hexadecimal representation of mem
 *
 * @param mem The memory to dump
 * @param size the amount of bytes to dump
 *
 * @return A string containing memory as hex, must be freed by caller
 */
static char *
memdump (const void *mem, size_t size)
{
  char *ret = GNUNET_malloc (2 * size + 1);
  uint32_t i = 0;
  for(i = 0; i < size; i++)
  {
    snprintf (&ret[2 * i], 2 * (size - i) + 1, "%02X", ((uint8_t *) mem)[i]);
  }
  return ret;
}


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

  struct DhtMonitorList *pos = subscription->dht_monitor_list_head;
  while (pos != NULL)
  {
    GNUNET_DHT_monitor_stop (pos->monitor_handle);
    GNUNET_CONTAINER_DLL_remove (subscription->dht_monitor_list_head,
                                 subscription->dht_monitor_list_tail,
                                 pos);
    GNUNET_free (pos);
    pos = pos->next;
  }

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
static void regex_search_context_free (struct RegexSearchContext *context)
{
	GNUNET_CONTAINER_multipeermap_destroy(context->subscribers);
	GNUNET_REGEX_search_cancel(context->regex_search_handle);
	GNUNET_free(context->publish_msg);
	GNUNET_free(context);
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
    LOG (GNUNET_ERROR_TYPE_DEBUG,
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
  LOG (GNUNET_ERROR_TYPE_DEBUG,
	      "Send message to subscriber.\n");
  if (buf == NULL)
  {
    /* subscriber disconnected */
    LOG (GNUNET_ERROR_TYPE_DEBUG,
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
    LOG (GNUNET_ERROR_TYPE_DEBUG,
                "Transmitting %u bytes to subscriber %s\n", msize,
                GNUNET_i2s (&subscriber->id));
    off += msize;
    set_timer_for_deleting_message(pm);
    GNUNET_free (pm->msg);
    GNUNET_free (pm);
  }

  LOG (GNUNET_ERROR_TYPE_DEBUG,
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
    LOG (GNUNET_ERROR_TYPE_DEBUG,
                "Not asking for transmission to %s now: %s\n",
                GNUNET_i2s (&subscriber->id),
                subscriber->pending_head ==
                NULL ? "no more messages" : "request already pending");
    return;
  }

  msg = subscriber->pending_head->msg;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
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
subscribed_peer_found (void *cls,
		const struct GNUNET_PeerIdentity *id,
		const struct GNUNET_PeerIdentity *get_path, unsigned int get_path_length,
		const struct GNUNET_PeerIdentity *put_path, unsigned int put_path_length,
		const struct GNUNET_HashCode *key)
{

	/** multicast stuff **/
	struct RegexSearchContext *context = cls;
	struct MulticastChannel* channel = context->channel;

	struct GNUNET_CRYPTO_EddsaPrivateKey *private_channel_key;
	struct GNUNET_CRYPTO_EddsaPublicKey public_channel_key;

	struct GNUNET_HashCode dht_key_of_accept_state;
	// hash of dht_key_af_acceptt_state
	struct GNUNET_HashCode announce_key;
	// loop variable
	struct AcceptState *as_iter;
	struct GNUNET_TIME_Relative remaing_announce_time =
			GNUNET_TIME_absolute_get_remaining(channel->expiration);

	LOG_DEBUG("--------> Found an active subscription for topic %s with accept state key %#x\n",
			channel->channel_topic, key->bits);

	// check if this channel serves the found accept state
	for (as_iter = channel->as_head; NULL != as_iter; as_iter = as_iter->next)
	{
		if (dht_key_of_accept_state.bits == as_iter->hash.bits)
		{
			// channel for this dht accept state
			if (remaing_announce_time.rel_value_us > 0 && as_iter->announced == GNUNET_YES)
				return; // active announcement in the DHT already, done here

		}
	}

	if (NULL == as_iter)
	{
		// new accept state for the channel
		struct AcceptState *new_ac = GNUNET_new(struct AcceptState);
		new_ac->announced = GNUNET_NO;
		new_ac->hash = *key;
		GNUNET_CONTAINER_DLL_insert(channel->as_head, channel->as_tail, new_ac);

		LOG_DEBUG("Adding new accept state to channel\n");
	}

	if (0 == remaing_announce_time.rel_value_us)
	{
		// channel announcment expired, need to reannounce for all accept states
		channel->expiration =
				GNUNET_TIME_relative_to_absolute(GNUNET_TIME_relative_multiply( GNUNET_TIME_UNIT_HOURS,
																																				12));
		for (as_iter = channel->as_head; NULL != as_iter; as_iter = as_iter->next)
		{
			as_iter->announced = GNUNET_NO;
		}
		LOG_DEBUG("Channel expired, re-announce under all known accept states\n");
	}


	GNUNET_CRYPTO_eddsa_key_get_public (&(channel->channel_private_key),
																			&public_channel_key);

	// announce channel
	for (as_iter = channel->as_head; NULL != as_iter; as_iter = as_iter->next)
	{
		if (GNUNET_NO == as_iter->announced)
		{
			// create announce key of the channel for this accept state
			GNUNET_CRYPTO_hash (&as_iter->hash,
													sizeof(struct GNUNET_HashCode),
													&announce_key);

			GNUNET_DHT_put (dht_handle, &announce_key, 3, // replication
											GNUNET_DHT_RO_NONE,
											GNUNET_BLOCK_TYPE_ANY, // TODO add mqtt channel blocktype to DHT
											sizeof(struct GNUNET_HashCode),
											&public_channel_key,
											channel->expiration,
											GNUNET_TIME_absolute_get_remaining(channel->expiration),
											NULL,
											NULL);
			as_iter->announced = GNUNET_YES;
			LOG_DEBUG ("Announcing Channel for topic %s under dht key %#x\n",
								channel->channel_topic,
								announce_key.bits);
		}
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
		struct MulticastChannel *channel)
{
	struct RegexSearchContext *context;
	LOG_DEBUG ("Searching for subscribers on topic %s\n", topic);

	context = GNUNET_new (struct RegexSearchContext);
	context->publish_msg = publish_msg;
	context->channel = channel;
	context->message_delivered = GNUNET_NO;
	context->free_task = GNUNET_SCHEDULER_NO_TASK;
	context->subscribers = GNUNET_CONTAINER_multipeermap_create(1, GNUNET_NO);

	context->regex_search_handle = GNUNET_REGEX_search (cfg,
																											topic,
																											&subscribed_peer_found,
																											context);
	GNUNET_CONTAINER_DLL_insert (sc_head, sc_tail, context);
}


static int
send_multicast_message (void *cls, size_t *data_size, void *data);


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
	LOG_DEBUG ("enter handle_publish\n");
	char *topic, *message, *prefixed_topic;
	size_t message_len;
	size_t msg_len = ntohs (msg->size);
	struct GNUNET_MQTT_ClientPublishMessage *publish_msg;

	/* Extract topic */
	publish_msg = GNUNET_malloc (msg_len);
	memcpy (publish_msg, msg, msg_len);
	topic = GNUNET_malloc (publish_msg->topic_len);
	strncpy (topic, (char * ) (publish_msg + 1), publish_msg->topic_len);
	topic[publish_msg->topic_len - 1] = '\0';


	struct GNUNET_CRYPTO_EddsaPrivateKey *private_channel_key;
	struct MulticastChannel *channel;

	for (channel = multicast_channels_head; NULL != channel;
			channel = channel->next)
	{
		if (0 == strncmp (channel->channel_topic, topic, strlen (topic)))
			break;
	}

	if (NULL == channel)
	{
		/* create Multicast channel */
		if (NULL == (private_channel_key = GNUNET_CRYPTO_eddsa_key_create ()))
		{
			LOG_ERROR ("Could not retrieve public key for private eddsa key %s, aborting\n",
								private_channel_key->d);
			return;
		}

		// create the channel
		channel = GNUNET_new (struct MulticastChannel);
		channel->channel_private_key = (*private_channel_key);
		GNUNET_free (private_channel_key);
		channel->channel_topic = GNUNET_malloc (strlen (topic) + 1);
		strcpy (channel->channel_topic, topic);
		channel->expiration =
				GNUNET_TIME_relative_to_absolute (GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_HOURS,
																																				12));

		channel->channel =
				GNUNET_MULTICAST_origin_start (cfg, /** config handle **/
																			 &channel->channel_private_key, /** private key **/
																			 1, /** max_fragment_id **/
																			 NULL, /** join_req_cb **/
																			 NULL, /** member_test_cb **/
																			 NULL, /** replay_fragment_cb **/
																			 NULL, /** replay_msg_cb **/
																			 NULL, /** req_cb **/
																			 NULL, /** msg_cb **/
																			 NULL);/** cls **/
		GNUNET_CONTAINER_DLL_insert (multicast_channels_head,
																 multicast_channels_tail,
																 channel);
		LOG_DEBUG ("Created Channel with key %#x\n", private_channel_key->d);
	}

	struct PendingMulticastMessage *pub_msg =
			GNUNET_new (struct PendingMulticastMessage);
	memcpy (&pub_msg->msg,
					publish_msg,
					sizeof (struct GNUNET_MQTT_ClientPublishMessage));
	GNUNET_CONTAINER_DLL_insert (channel->pending_head,
															 channel->pending_tail,
															 pub_msg);

	struct PendingMulticastMessage *pending_msg_iter;

	for (pending_msg_iter = channel->pending_head; NULL != pending_msg_iter; pending_msg_iter =
			pending_msg_iter->next)
	{
		// send all pending messages
		GNUNET_MULTICAST_origin_to_all (channel->channel,
																	 	0,
																		0,
																		send_multicast_message,
																		pending_msg_iter);
	}

	search_for_subscribers (topic, publish_msg, channel);

	LOG_DEBUG ("outgoing PUBLISH message received: %s [%d bytes] (%d overall)\n",
						 topic,
						 publish_msg->topic_len,
						 ntohs (publish_msg->header.size));
	GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Send multicast message callback
 *
 * @param cls closure, should be the message (of type PendingMulticastMessage)
 * @param data_size buffer size
 * @param data buffer
 *
 */
static int
send_multicast_message (void *cls, size_t *data_size, void *data)
{
	LOG_DEBUG ("enter send_munticast_message\n");

	struct PendingMulticastMessage *pub_msg = cls;
	size_t msg_size = pub_msg->msg.header.size;
	if (*data_size < msg_size)
	{
		GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
								"Failed to send multicast message, transmit_notify: buffer too small, need %d but only %d available.\n",
								msg_size,
								*data_size);
		*data_size = 0;
		return GNUNET_NO;
	}

	LOG_DEBUG ("transmit_notify: sending %u bytes.\n", msg_size);

	*data_size = msg_size;
	memcpy (data, &pub_msg->msg, msg_size);
	return GNUNET_YES;
}


/**
 * Callback called on each GET request going through the DHT.
 *
 * @param cls Closure.
 * @param options Options, for instance RecordRoute, DemultiplexEverywhere.
 * @param type The type of data in the request.
 * @param hop_count Hop count so far.
 * @param path_length number of entries in @a path (or 0 if not recorded).
 * @param path peers on the GET path (or NULL if not recorded).
 * @param desired_replication_level Desired replication level.
 * @param key Key of the requested data.
 */
static void
subscriber_monitor_get_cb (void *cls,
    enum GNUNET_DHT_RouteOption options,
    enum GNUNET_BLOCK_Type type,
    uint32_t hop_count,
    uint32_t desired_replication_level,
    unsigned int path_length,
    const struct GNUNET_PeerIdentity *path,
    const struct GNUNET_HashCode * key)
{
  char *key_str = memdump (key, sizeof (struct GNUNET_HashCode));
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Subscriber monitor get callback called 0x%s\n",
       key_str);
  GNUNET_free (key_str);
}


/**
 * Callback called on each GET reply going through the DHT.
 *
 * @param cls Closure.
 * @param type The type of data in the result.
 * @param get_path Peers on GET path (or NULL if not recorded).
 * @param get_path_length number of entries in @a get_path.
 * @param put_path peers on the PUT path (or NULL if not recorded).
 * @param put_path_length number of entries in @a get_path.
 * @param exp Expiration time of the data.
 * @param key Key of the data.
 * @param data Pointer to the result data.
 * @param size Number of bytes in @a data.
 */
static void
subscriber_monitor_get_response_cb (void *cls,
    enum GNUNET_BLOCK_Type type,
    const struct GNUNET_PeerIdentity *get_path,
    unsigned int get_path_length,
    const struct GNUNET_PeerIdentity *put_path,
    unsigned int put_path_length,
    struct GNUNET_TIME_Absolute exp,
    const struct GNUNET_HashCode *key,
    const void *data,
    size_t size)
{
  char *key_str = memdump (key, sizeof (struct GNUNET_HashCode));
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Subscriber monitor get response callback called 0x%s\n",
       key_str);
  GNUNET_free (key_str);

  char *data_str = memdump (data, size);
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Subscriber monitor get response data 0x%s\n",
       data_str);
  GNUNET_free (data_str);
}


/**
 * Callback called on each PUT request going through the DHT.
 *
 * @param cls Closure.
 * @param options Options, for instance RecordRoute, DemultiplexEverywhere.
 * @param type The type of data in the request.
 * @param hop_count Hop count so far.
 * @param path_length number of entries in @a path (or 0 if not recorded).
 * @param path peers on the PUT path (or NULL if not recorded).
 * @param desired_replication_level Desired replication level.
 * @param exp Expiration time of the data.
 * @param key Key under which data is to be stored.
 * @param data Pointer to the data carried.
 * @param size Number of bytes in data.
 */
static void
subscriber_monitor_put_cb (void *cls,
    enum GNUNET_DHT_RouteOption options,
    enum GNUNET_BLOCK_Type type,
    uint32_t hop_count,
    uint32_t desired_replication_level,
    unsigned int path_length,
    const struct GNUNET_PeerIdentity *path,
    struct GNUNET_TIME_Absolute exp,
    const struct GNUNET_HashCode *key,
    const void *data,
    size_t size)
{
  // todo implemento
  char *key_str = memdump (key, sizeof (struct GNUNET_HashCode) / 2);
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Subscriber monitor put callback called 0x%s\n",
       key_str);
  GNUNET_free (key_str);

  char *data_str = memdump (data, size);
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Subscriber monitor put data 0x%s\n",
       data_str);
  GNUNET_free (data_str);
}


/**
 * Issue DHT-Monitor for each accepting state
 *
 * @param cls Subscriber_Conf
 * @param key current key code. Will be monitored
 * @param value value in the hash map, expected to bet the proof
 *
 * @return #GNUNET_YES if we should continue to iterate, #GNUNET_NO if not.
 */
static int
monitor_accepting_state_iterator (void *cls,
    const struct GNUNET_HashCode *key,
    void *value)
{
  char *state_string = memdump (key, sizeof (struct GNUNET_HashCode) / 2);
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Subscriber monitoring state %s 0x%s\n",
       value,
       state_string);
  GNUNET_free (state_string);

  if (NULL == dht_handle)
  {
      LOG (GNUNET_ERROR_TYPE_ERROR,
           "Subscriber can not connect to DHT\n");
      return GNUNET_NO;
  }

  struct Subscription *subscription = (struct Subscription *) cls;

  struct DhtMonitorList *monitor_item = GNUNET_new (struct DhtMonitorList);
  monitor_item->monitor_handle = GNUNET_DHT_monitor_start (dht_handle,
                            GNUNET_BLOCK_TYPE_ANY, // todo tweak to get better results
                            key,
                            &subscriber_monitor_get_cb,
                            &subscriber_monitor_get_response_cb,
                            &subscriber_monitor_put_cb,
                            subscription);
  GNUNET_CONTAINER_DLL_insert (subscription->dht_monitor_list_head,
                               subscription->dht_monitor_list_tail,
                               monitor_item);

  return GNUNET_YES;
}


/**
 * Iterator over hash map entries and destroy each of them
 *
 * @param cls ignored
 * @param key current key code
 * @param value value in the hash map, expected to be a pointer to the proof
 *
 * @return #GNUNET_YES if we should continue to iterate, #GNUNET_NO if not.
 */
static int
destroy_accepting_state_iterator (void *cls,
    const struct GNUNET_HashCode *key,
    void *value)
{
  if (NULL != value)
  {
    GNUNET_free (value);
  }
  return GNUNET_YES;
}


/**
 * Issue a DHT monitor operation on the accepting states of the given DHT
 *
 * @param cls The subscription
 * @param a The original announcement
 * @param accepting_states A map containing all accepting states, or NULL if
 *        something went terribly wrong
 *
 * This is called as a callback from the lookup of
 * GNUNET_REGEX_announce_get_accepting_dht_entries done in
 * handle_mqtt_subscribe
 */
static void
monitor_accepting_states_for_subscription_cb (void *cls,
    struct GNUNET_REGEX_Announcement *a,
    struct GNUNET_CONTAINER_MultiHashMap *accepting_states)
{
  struct Subscription *subscription = (struct Subscription *) cls;
  if (a == subscription->regex_announce_handle)
  {
    int result = GNUNET_CONTAINER_multihashmap_iterate (accepting_states,
                                                        monitor_accepting_state_iterator,
                                                        subscription);
    if (GNUNET_OK != result)
    {
      // todo the monitoring failed, what to do?
    }
  }
  else
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, "Closure and Announcement do not match!\n");
  }

  GNUNET_CONTAINER_multihashmap_iterate (accepting_states,
                                         destroy_accepting_state_iterator,
                                         NULL);
  GNUNET_CONTAINER_multihashmap_destroy (accepting_states);
}


/**
 * Issue a DHT monitor operation on the accepting states of the given DHT
 *
 * @param cls The subscription
 * @param a The original announcement
 * @param accepting_states A map containing all accepting states, or NULL if
 *        something went terribly wrong
 *
 * This is called as a callback from the lookup of
 * GNUNET_REGEX_announce_get_accepting_dht_entries done in
 * handle_mqtt_subscribe
 */
static void
find_channel_information_for_accepting_states (void *cls,
    struct GNUNET_REGEX_Announcement *a,
    struct GNUNET_CONTAINER_MultiHashMap *accepting_states)
{
  // todo look up if you can find it somewhere somehow

  // HACKY HACK: by default we just assume there is no channel information in
  // the DHT and do a monitor for puts
  monitor_accepting_states_for_subscription_cb (cls, a, accepting_states);
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
  LOG_DEBUG ("enter handle_subscribe\n");
	struct Subscription *subscription;
  char *topic, *regex_topic;
  struct GNUNET_TIME_Relative refresh_interval;

  const struct GNUNET_MQTT_ClientSubscribeMessage *subscribe_msg;

  /* Extract topic */
  subscribe_msg = (const struct GNUNET_MQTT_ClientSubscribeMessage *) msg;
  topic = GNUNET_malloc (subscribe_msg->topic_len);
  strncpy (topic, (char *) (subscribe_msg + 1),
          subscribe_msg->topic_len);
  topic[subscribe_msg->topic_len - 1] = '\0';
  subscription = GNUNET_new (struct Subscription);
  get_regex (topic, &regex_topic);

  if (0 != regcomp (&subscription->automaton,
		    regex_topic,
		    REG_EXTENDED))
  {
    LOG_WARNING ("Error building Regex from topic String\n");
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

  refresh_interval = GNUNET_TIME_relative_multiply (
      GNUNET_TIME_UNIT_MINUTES, 1);
  // todo change to GNUNET_REGEX_announce_with_key
#if 1
	subscription->regex_announce_handle =
			GNUNET_REGEX_announce(cfg, regex_topic, refresh_interval, NULL);
#else
	subscription->regex_announce_handle =
	      GNUNET_REGEX_announce_with_key (cfg, regex_topic, refresh_interval,
	                                      NULL, GNUNET_CRYPTO_eddsa_key_get_anonymous ());
#endif
	if (NULL == subscription->regex_announce_handle)
	{
		LOG_ERROR ("MQTT SUBSCRIBE can not do REGEX announce: %s -> %s\n",
							topic,
							regex_topic);
	  GNUNET_CONTAINER_DLL_remove (subscription_head, subscription_tail,
	                               subscription);
	  GNUNET_free (subscription);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
	}

	int accepting_req_result = GNUNET_REGEX_announce_get_accepting_dht_entries (
	    subscription->regex_announce_handle,
	    find_channel_information_for_accepting_states,
	    subscription);
	if (GNUNET_YES != accepting_req_result)
	{
		LOG_ERROR ("MQTT SUBSCRIBE can not do REGEX accepting state lookup: %s -> %s\n",
							 topic,
							 regex_topic);
    GNUNET_CONTAINER_DLL_remove (subscription_head, subscription_tail,
                                 subscription);
    GNUNET_free (subscription);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
	}

	LOG_DEBUG ("MQTT SUBSCRIBE message received: %s -> %s\n", topic, regex_topic);
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

  LOG(GNUNET_ERROR_TYPE_DEBUG, "Sending reply to client\n");

  client->transmit_handle = NULL;
  if (buf == NULL)
  {
    /* client disconnected */
    LOG (GNUNET_ERROR_TYPE_DEBUG,
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
      set_timer_for_deleting_message (reply);
    }
    GNUNET_free (reply->msg);
    GNUNET_free (reply);
    LOG (GNUNET_ERROR_TYPE_DEBUG,
         "Transmitting %u bytes to client %p\n",
		     msize,
         client->client_handle);
    off += msize;
  }
  LOG (GNUNET_ERROR_TYPE_DEBUG,
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
    LOG (GNUNET_ERROR_TYPE_DEBUG,
         "Not asking for transmission to %p now: %s\n",
         client->client_handle,
         client->pending_head ==
         NULL ? "no more messages" : "request already pending");
    return;
  }
  LOG (GNUNET_ERROR_TYPE_DEBUG,
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
				   subscription);
      break;
    }
  }

  if (NULL == subscription)
  {
    LOG (GNUNET_ERROR_TYPE_ERROR,
         "Active subscription with ID %lu does not exist\n",
         subscription->request_id);
    return;
  }
  LOG (GNUNET_ERROR_TYPE_DEBUG,
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
 * Match a topic string against a subscription using regexec.
 * Also checks if the regex match goes all the way to the end of the topic string,
 * to ensure an exact match. If a client has multiple subscriptions this is necessary
 * to allow proper message demultiplexing.
 *
 * @param subscription the subscription which regex should be checked
 * @param prefixed_topic the topic string that was sent with a message, prefixed with the mqtt regex prefix string
 * @return 0 in case of a match, non-zero value otherwise
 */
static int
match_subcriber_regex(struct Subscription *subscription, const char *prefixed_topic)
{
	LOG(GNUNET_ERROR_TYPE_DEBUG, "Matching regex against:\n%s\n", prefixed_topic);
	size_t nmatch = 1;
	regmatch_t pmatch;
	int ret = regexec(&subscription->automaton, prefixed_topic, nmatch, &pmatch, 0);

	LOG(GNUNET_ERROR_TYPE_DEBUG,
				"Regexec topic match result: (ret, end_pos, strlen) = (%d, %d, %d)\n",
				ret, pmatch.rm_eo,
				strlen(prefixed_topic));

	if(ret == 0)
		return (((size_t) pmatch.rm_eo) == strlen(prefixed_topic)) ? 0: -1;
	else
		return ret;
}


/**
 * Deliver incoming PUBLISH messages to local subscribers.
 *
 * This function processes and forwards PUBLISH messages to the
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

  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Delivering incoming publish message to incoming client\n");

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
		if (0 == match_subcriber_regex(subscription, prefixed_topic))
		{
			LOG(GNUNET_ERROR_TYPE_DEBUG, "========> MATCH\n");
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
			LOG(GNUNET_ERROR_TYPE_DEBUG, "Serving Subscription %d\n",
					subscription->request_id);
			pm = GNUNET_new(struct PendingMessage);
			pm->msg = (struct GNUNET_MessageHeader*) return_msg;
			pm->context = context;
			add_pending_client_message(client_info, pm);
		}
	}
	GNUNET_free(topic);
	GNUNET_free(prefixed_topic);
	if (GNUNET_YES == free_publish_msg)
		GNUNET_free(publish_msg);
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

  LOG (GNUNET_ERROR_TYPE_DEBUG, "Client disconnected, cleaning up.\n");

  if (NULL == client)
    return;
  for (subscription = subscription_head; NULL != subscription; subscription = nxt)
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
  LOG(GNUNET_ERROR_TYPE_DEBUG, "Shutting down\n");

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
    LOG (GNUNET_ERROR_TYPE_ERROR,
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
	//look_for_old_messages();
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
