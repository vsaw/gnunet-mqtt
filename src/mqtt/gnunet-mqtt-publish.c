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
 * @file mqtt/gnunet-mqtt-publish.c
 * @brief MQTT PUBLISH tool
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
 * Topic for the message to be published
 */
static char *topic;

/**
 * The message to be published
 */
static char *message;

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
 * Disconnect from MQTT and free all allocated resources.
 *
 * @param cls closure
 * @param tc scheduler context
 */
static void
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  if (NULL != mqtt_handle)
  {
    GNUNET_MQTT_disconnect (mqtt_handle);
    mqtt_handle = NULL;
  }
}


static void
publish_callback (void *cls, int success)
{
  if (verbose)
  {
    switch (success)
    {
    case GNUNET_OK:
      FPRINTF (stderr, "%s `%s', %s `%s'\n",
               _("PUBLISH request sent with topic"), _("message"), topic,
               message);
      break;
    case GNUNET_NO:
      FPRINTF (stderr, "%s", _("Timeout sending PUBLISH request!\n"));
      break;
    case GNUNET_SYSERR:
      FPRINTF (stderr, "%s", _("PUBLISH request not confirmed!\n"));
      break;
    default:
      GNUNET_break (0);
      break;
    }
  }

  GNUNET_SCHEDULER_add_now (&shutdown_task, NULL);
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

  if ((NULL == topic) || (NULL == message))
  {
    FPRINTF (stderr, "%s",
             _("Must provide TOPIC and MESSAGE for MQTT PUBLISH!\n"));
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

  GNUNET_MQTT_publish (mqtt_handle, strlen(topic) + 1, topic,
                       strlen(message) + 1, message, timeout, publish_callback,
                       NULL);

  ret = 0;
}

/**
 * The main function to MQTT PUBLISH.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  static const struct GNUNET_GETOPT_CommandLineOption options[] = {
    {'m', "message", "MESSAGE",
     gettext_noop ("the message to be published"),
     1, &GNUNET_GETOPT_set_string, &message},
    {'t', "topic", "TOPIC",
     gettext_noop ("the message topic"),
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
                              "gnunet-mqtt-publish [options [value]]",
                              gettext_noop
                              ("mqtt-publish"),
                              options, &run, NULL)) ? ret : 1;
}
