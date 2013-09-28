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
 * @file mqtt/mqtt.h
 * @brief IPC messages exchanged between MQTT library and MQTT service
 * @author Ramona Popa
 * @author Artur Grunau
 */

#ifndef MQTT_H
#define MQTT_H


GNUNET_NETWORK_STRUCT_BEGIN

/**
 * MQTT PUBLISH message sent from clients to service. Indicates that
 * a PUBLISH request should be issued.
 */
struct GNUNET_MQTT_ClientPublishMessage
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_MQTT_CLIENT_PUBLISH
   */
  struct GNUNET_MessageHeader header;

  /**
   * Unique ID to match a future response to this request.
   * Picked by the client.
   */
  uint64_t request_id GNUNET_PACKED;

  /**
   * Length of the sent message's topic, including its terminating NUL.
   */
  uint8_t topic_len;

  /* Topic (NUL-terminated string) and data copied to end of this message */

};

/**
 * MQTT SUBSCRIBE message sent from clients to service. Indicates that
 * a SUBSCRIBE request should be issued.
 */
struct GNUNET_MQTT_ClientSubscribeMessage
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_MQTT_CLIENT_SUBSCRIBE
   */
  struct GNUNET_MessageHeader header;

  /**
   * Unique ID to match a future response to this request.
   * Picked by the client.
   */
  uint64_t request_id GNUNET_PACKED;

  /**
   * Length of the topic, including its terminating NUL.
   */
  uint8_t topic_len;

  /* Topic (NUL-terminated string) and data copied to end of this message */

};

/**
 * MQTT UNSUBSCRIBE message sent from clients to service. Indicates that
 * an active subscription should be cancelled.
 */
struct GNUNET_MQTT_ClientUnsubscribeMessage
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_MQTT_CLIENT_SUBSCRIBE
   */
  struct GNUNET_MessageHeader header;

  /**
   * ID of the subscription to be cancelled.
   */
  uint64_t request_id GNUNET_PACKED;
};

/**
 * MQTT UNSUBSCRIBE_ACK message sent from clients to service. Indicates
 * that an active subscription has been cancelled.
 */
struct GNUNET_MQTT_ClientUnsubscribeAckMessage
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_MQTT_CLIENT_SUBSCRIBE_ACK
   */
  struct GNUNET_MessageHeader header;

  /**
   * ID of the subscription that has been cancelled.
   */
  uint64_t request_id GNUNET_PACKED;
};

GNUNET_NETWORK_STRUCT_END

#endif
