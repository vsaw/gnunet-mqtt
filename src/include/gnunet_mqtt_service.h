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
 * @file include/gnunet_mqtt_service.h
 * @brief API to the MQTT service
 * @author Ramona Popa
 * @author Artur Grunau
 */
#ifndef GNUNET_MQTT_SERVICE_H
#define GNUNET_MQTT_SERVICE_H

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif

/**
 * Version of the MQTT API.
 */
#define GNUNET_MQTT_VERSION 0x00000000


/*
 * Connection to the MQTT service.
 */
struct GNUNET_MQTT_Handle;


/**
 * Initialize the connection with the MQTT service.
 *
 * @param cfg configuration to use
 * @return NULL on error
 */
struct GNUNET_MQTT_Handle *
GNUNET_MQTT_connect (const struct GNUNET_CONFIGURATION_Handle *cfg);


/**
 * Shut down connection with the MQTT service.
 *
 * @param handle handle of the MQTT connection to stop
 */
void
GNUNET_MQTT_disconnect (struct GNUNET_MQTT_Handle *handle);


/**
 * Opaque handle to cancel a PUBLISH operation.
 */
struct GNUNET_MQTT_PublishHandle;

/**
 * Opaque handle to cancel a SUBSCRIBE operation.
 */
struct GNUNET_MQTT_SubscribeHandle;


/**
 * Type of a PUBLISH continuation. You must not call
 * GNUNET_MQTT_disconnect in this continuation.
 *
 * @param cls closure
 * @param success GNUNET_OK if the PUBLISH was transmitted,
 *                GNUNET_NO on timeout,
 *                GNUNET_SYSERR on disconnect from service after the
 *                PUBLISH message was transmitted (so we don't know if
 *                it was received or not)
 */
typedef void (*GNUNET_MQTT_PublishContinuation) (void *cls, int success);

/**
 * Perform a PUBLISH operation storing data in the DHT.
 *
 * @param handle handle to MQTT service
 * @param topic_len length of the topic the message should be published
 *                  on
 * @param topic the topic (as a NUL-terminated string) the message
 *              should be published on
 * @param size number of bytes in data; must be less than 64k
 * @param data the payload of the message
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
                     GNUNET_MQTT_PublishContinuation cont, void *cont_cls);

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
GNUNET_MQTT_publish_cancel (struct GNUNET_MQTT_PublishHandle *ph);

/**
 * Type of a SUBSCRIBE continuation. You must not call
 * GNUNET_MQTT_disconnect in this continuation.
 *
 * @param cls closure
 * @param success GNUNET_OK if the SUBSCRIBE was transmitted,
 *                GNUNET_NO on timeout,
 *                GNUNET_SYSERR on disconnect from service after the
 *                SUBSCRIBE message was transmitted (so we don't know if
 *                it was received or not)
 */
typedef void (*GNUNET_MQTT_SubscribeContinuation) (void *cls, int success);

/**
 * Callback invoked on each result obtained for a MQTT SUBSCRIBE
 * operation.
 *
 * This callback is given ownership of topic and data, and must
 * eventually free the corresponding memory.
 *
 * @param cls closure
 * @param topic_len length of the topic the message was published on
 * @param topic the topic (as a NUL-terminated string) the message was
 *              published on
 * @param size number of bytes in data; must be less than 64k
 * @param data the payload of the message
 */
typedef void (*GNUNET_MQTT_SubscribeResultCallback) (void *cls,
                                                     uint8_t topic_len,
                                                     char *topic, size_t size,
                                                     void *data);

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
 *             this continuation; can be NULL
 * @param cont_cls closure for cont
 * @param cb callback to call when messages for this subscription are
 *           received; you must not call GNUNET_MQTT_disconnect in this
 *           callback; mustn't be NULL
 * @param cb_cls closure for cb
 * @return handle to cancel the SUBSCRIBE operation, or NULL on error
 */
struct GNUNET_MQTT_SubscribeHandle *
GNUNET_MQTT_subscribe (struct GNUNET_MQTT_Handle *handle, uint8_t topic_len,
                       const char *topic, struct GNUNET_TIME_Relative timeout,
                       GNUNET_MQTT_SubscribeContinuation cont, void *cont_cls,
                       GNUNET_MQTT_SubscribeResultCallback cb, void *cb_cls);

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
GNUNET_MQTT_unsubscribe (struct GNUNET_MQTT_SubscribeHandle *handle);
					 

#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif

#endif
