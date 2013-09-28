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
 * @file mqtt/regex_utils.c
 * @brief regex utils functions implementation
 * @author Ramona Popa
 */

#include "regex_utils.h"
#include <gnunet/platform.h>
#include <gnunet/gnunet_regex_lib.h>

/**
 * String constant for prefixing the topic
 */
 
 static const char *prefix = "GNUNET-MQTT 0001 00000"; 
/**
 * String constant for replacing '+' wildcard in the subscribed topics.
 */

static const char *plus_regex = "(a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|0|1|2|3|4|5|6|7|8|9)+";
//static const char *plus_regex = "a+";

/**
 * String constant for replacing '#' wildcard in the subscribed topics.
 */
static const char *hash_regex = "(/(a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|0|1|2|3|4|5|6|7|8|9)+)*";
//static const char *hash_regex = "(a|b)+";

/**
 * Validates subscribe topic syntax.
 *
 * @param topic topic of subscription as provided by the subscriber
 * @return 1 if the topic is syntactically valid and 0 if not
  * 
 */
int
validate_subscribe_topic (const char *topic)
{
  char *general_expression = "(/|#|a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|0|1|2|3|4|5|6|7|8|9)+";
  char *p;
  int result = GNUNET_YES;
 
  struct GNUNET_REGEX_Automaton *automaton;

  automaton = GNUNET_REGEX_construct_dfa(general_expression, strlen(general_expression), 1);

  if (0 != GNUNET_REGEX_eval(automaton, topic))
  {
    if( (p = strchr(topic, '+')) == NULL)
    {
      return GNUNET_NO;
    }
  }
  if (strstr(topic, "//") != NULL)
    {
      return GNUNET_NO;
    }

    p = strchr(topic, '+');
    if (p != NULL)
    {	
      p = p-1;
      if (*p != '/')
      {
	result = GNUNET_NO;
      }
     }
    
    p = strchr(topic, '#');
    if (p != NULL)
    {	
      p = p-1;
      if (*p != '/')
      {
	result = GNUNET_NO;
      }
      p = p+2;
      if (*p != '\0')
      {
	result = GNUNET_NO;
      }
    }
      
    
    return result;
}

/**
 * Validates publish topic syntax.
 *
 * @param topic topic of the published as provided by the publisher
 * @return 1 if the topic is syntactically valid and 0 if not
  * 
 */
int
validate_publish_topic (const char *topic)
{
  char *general_expression = "(/|a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|0|1|2|3|4|5|6|7|8|9)+";

  int result = GNUNET_YES;
 
  struct GNUNET_REGEX_Automaton *automaton;

  automaton = GNUNET_REGEX_construct_dfa(general_expression, strlen(general_expression), 1);

  if (0 != GNUNET_REGEX_eval(automaton, topic))
  {
    return GNUNET_NO;
  }
  if (strstr(topic, "//") != NULL)
    {
      return GNUNET_NO;
    }    
    
    return result;
}


/**
 * Adds the prefix to the toopic (App ID + Version + Padding)
 *
 * @param topic topic of subscription as provided by the subscriber
 * @param regex_topic client identification of the client
 * 
 */
void
add_prefix (const char *topic, char **prefixed_topic)
{
	int n, i;
	*prefixed_topic = GNUNET_malloc(strlen(prefix) + strlen(topic)+1);
	n = 0;
	 
	for (i = 0; prefix[i] != '\0'; i++) 
  {
    (*prefixed_topic)[i] = prefix[i];
	}
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
 * 
 */
void
get_regex (char *topic, char **regex_topic)
{
  char *plus;
  char *hash;
  char *prefixed_topic;
  int i, j, k; 
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
  
  *regex_topic = GNUNET_malloc(strlen(prefixed_topic) - plus_counter - hash_exists + plus_counter*strlen(plus_regex) + hash_exists*strlen(hash_regex)+1);
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
     }else
     {
      (*regex_topic)[j] = prefixed_topic[i];
      j++;
      }
    }
    (*regex_topic)[j] = '\0';
  }
  

/**
 *Helper for building correct regular expressions
 *
 * 
 */
void
try_regexes ()
{
  char *expression = "GNUNET-MQTT 0001 00000some/ana(/(a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z|0|1|2|3|4|5|6|7|8|9)+)*";
  struct GNUNET_REGEX_Automaton *automaton;

  automaton = GNUNET_REGEX_construct_dfa(expression, strlen(expression), 1);

  if (0 == GNUNET_REGEX_eval(automaton, "GNUNET-MQTT 0001 00000some/ana/topic1/sudo2"))
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "\n\nDid match!\n");
  else
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "\n\nDid NOT match!\n");
}
