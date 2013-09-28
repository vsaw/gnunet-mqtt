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
 * @file mqtt/regex_utils.h
 * @brief functions for managing the regular expressions 
 * @author Ramona Popa
 */

#ifndef REGEXUTILS_H
#define REGEXUTILS_H


/**
 * Validates subscribe topic syntax.
 *
 * @param topic topic of subscription as provided by the subscriber
 * @return 1 if the topic is syntactically valid and 0 if not
  * 
 */
int
validate_subscribe_topic (const char *topic);

/**
 * Validates publish topic syntax.
 *
 * @param topic topic of the published as provided by the publisher
 * @return 1 if the topic is syntactically valid and 0 if not
  * 
 */
int
validate_publish_topic (const char *topic);

/**
 * Adds the prefix to the toopic (App ID + Version + Padding)
 *
 * @param topic topic of subscription as provided by the subscriber
 * @param regex_topic client identification of the client
 * 
 */
void
add_prefix (const char *topic, char **prefixed_topic);

/**
 * Transform topics to regex expression.
 *
 * @param topic topic of subscription as provided by the subscriber
 * @param regex_topic client identification of the client
 * 
 */
void
get_regex (char *topic, char **regex_topic);

#endif
