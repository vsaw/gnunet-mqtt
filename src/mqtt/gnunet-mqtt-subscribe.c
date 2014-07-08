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
 * @file mqtt/gnunet-mqtt-subscribe.c
 * @brief MQTT SUBSCRIBE tool
 * @author Ramona Popa
 * @author Artur Grunau
 */
#include <gnunet/platform.h>
#include <gnunet/gnunet_util_lib.h>
#include "gnunet_mqtt_service.h"


/**
 * Handle to the MQTT
 */
static struct GNUNET_MQTT_Handle *mqtt_handle;

/**
 * Topic - target of the subscription
 */
static char *topic;

/**
 * User supplied timeout value
 */
static unsigned long long request_timeout = 5;

/**
 * Be verbose
 */
static int verbose;

/**
 * Global status value
 */
static int ret;

/**
 * Handle to the subscription
 */
static struct GNUNET_MQTT_SubscribeHandle *sub_handle;


/**
 * Disconnect from MQTT and free all allocated resources.
 *
 * @param cls closure
 * @param tc scheduler context
 */
static void
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
	if (NULL != sub_handle)
	{
		GNUNET_MQTT_unsubscribe(sub_handle);
		sub_handle = NULL;
	}
	if (NULL != mqtt_handle)
  {
  	GNUNET_MQTT_disconnect (mqtt_handle);
    mqtt_handle = NULL;
  }

	FPRINTF (stdout, "Unsubscribed from topic `%s' and disconnected from MQTT service\n", topic);
}


static void
subscribe_result_callback (void *cls, uint8_t topic_len, char *topic,
                           size_t size, void *data)
{
  FPRINTF (stdout, "%s: %s -> %s\n",  _("Message received"), topic,
           (char*) data);

  GNUNET_free (topic);
  GNUNET_free (data);
}


static void
subscribe_continuation (void *cls, int success)
{
  if (verbose)
  {
    switch (success)
    {
    case GNUNET_OK:
      FPRINTF (stderr, "%s `%s'\n",
               _("SUBSCRIBE request sent with topic"), topic);
      break;
    case GNUNET_NO:
      FPRINTF (stderr, "%s",  _("Timeout sending SUBSCRIBE request!\n"));
      break;
    case GNUNET_SYSERR:
      FPRINTF (stderr, "%s",  _("SUBSCRIBE request not confirmed!\n"));
      break;
    default:
      GNUNET_break (0);
      break;
    }
  }

  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL, shutdown_task,
                                NULL);
}

/**
 * Main function that will be run by the scheduler.
 *
 * @param cls closure
 * @param args remaining command-line arguments
 * @param cfgfile name of the configuration file used (for saving, can be NULL!)
 * @param cfg configuration
 */
static void
run (void *cls, char *const *args, const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  struct GNUNET_TIME_Relative timeout;

  if ((NULL == topic))
  {
    FPRINTF (stderr, "%s",
             _("Must provide TOPIC for MQTT SUBSCRIBE!\n"));
    ret = 1;
    return;
  }

  mqtt_handle = GNUNET_MQTT_connect (cfg);

  if (NULL == mqtt_handle)
  {
    FPRINTF (stderr, "%s",  _("Failed to connect to MQTT service!\n"));
    ret = 1;
    return;
  }

  timeout = GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS,
                                           request_timeout);

  sub_handle = GNUNET_MQTT_subscribe (mqtt_handle, strlen(topic) + 1, topic, timeout,
                         subscribe_continuation, NULL,
                         subscribe_result_callback, NULL);

  ret = 0;
}

/**
 * The main function to MQTT SUBSCRIBE.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  static const struct GNUNET_GETOPT_CommandLineOption options[] = {
    {'t', "topic", "TOPIC",
     gettext_noop ("topic - target of subscription"),
     1, &GNUNET_GETOPT_set_string, &topic},
    {'T', "timeout", "TIMEOUT",
     gettext_noop ("how long to execute this query before giving up"),
     1, &GNUNET_GETOPT_set_ulong, &request_timeout},
    {'V', "verbose", NULL,
     gettext_noop ("be verbose (print progress information)"),
     0, &GNUNET_GETOPT_set_one, &verbose},
    GNUNET_GETOPT_OPTION_END
  };
  return (GNUNET_OK ==
          GNUNET_PROGRAM_run (argc,
                              argv,
                              "gnunet-mqtt-subscribe [options [value]]",
                              gettext_noop
                              ("mqtt-subscribe"),
                              options, &run, NULL)) ? ret : 1;
}
