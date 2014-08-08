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
 * @file include/gnunet_protocols_mqtt.h
 * @brief constants for network protocols
 * @author Ramona Popa, Artur Grunau
 */

#ifndef GNUNET_PROTOCOLS_MQTT_H
#define GNUNET_PROTOCOLS_MQTT_H

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif



/**
 * MQTT PUBLISH message
 */
#define GNUNET_MESSAGE_TYPE_MQTT_CLIENT_PUBLISH 49380

/**
 * MQTT SUBSCRIBE message
 */
#define GNUNET_MESSAGE_TYPE_MQTT_CLIENT_SUBSCRIBE 49381

/**
 * MQTT UNSUBSCRIBE message
 */
#define GNUNET_MESSAGE_TYPE_MQTT_CLIENT_UNSUBSCRIBE 49382

/**
 * MQTT UNSUBSCRIBE_ACK message
 */
#define GNUNET_MESSAGE_TYPE_MQTT_CLIENT_UNSUBSCRIBE_ACK 49383

#define GNUNET_MESSAGE_TYPE_MQTT_CLIENT_JOIN 49384
#define GNUNET_MESSAGE_TYPE_MQTT_CLIENT_JOIN_ACK 49385


#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif

#endif
