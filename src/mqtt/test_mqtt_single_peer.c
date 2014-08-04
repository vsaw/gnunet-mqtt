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
 * @file mqtt/test_mqtt_single_peer.c
 * @brief testcase for the MQTT service
 * @author Ramona Popa
 */
#include <unistd.h>
#include <gnunet/platform.h>
#include <gnunet/gnunet_util_lib.h>
#include <gnunet/gnunet_testbed_service.h>
#include <gnunet/gnunet_dht_service.h>
#include "gnunet_mqtt_service.h"

/**
 * @brief The config file that will be passed to testbed
 */
#define CONFIG_FILE "template_single_peer.conf"
#define CONFIG_FILE_PARSED CONFIG_FILE ".tmp"

/**
 * Handle to the MQTT
 */
static struct GNUNET_MQTT_Handle *mqtt_handle;

static struct GNUNET_TESTBED_Operation *basic_mqtt_op;

static GNUNET_SCHEDULER_TaskIdentifier shutdown_tid;

static int result;

/**
 * User supplied timeout value
 */
static unsigned long long request_timeout = 5;


static void
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
   
  if (NULL != basic_mqtt_op)
  {
	GNUNET_TESTBED_operation_done (basic_mqtt_op); /* calls the gmqtt_da() for closing
	down the connection */
	basic_mqtt_op = NULL;
  }
  
  GNUNET_SCHEDULER_shutdown (); /* Also kills the testbed */
}


static void
in_time_shutdown_task ()
{
  if (NULL != basic_mqtt_op)
  {
    GNUNET_TESTBED_operation_done (basic_mqtt_op); /* calls the gmqtt_da() for closing
						      down the connection */
    basic_mqtt_op = NULL;
  }
  GNUNET_SCHEDULER_cancel(shutdown_tid);
  GNUNET_SCHEDULER_shutdown (); /* Also kills the testbed */
}


static void
subscribe_result_callback (void *cls, uint8_t topic_len, char *topic,
                           size_t size, void *data)
{
  result = GNUNET_OK;
  FPRINTF (stdout,
	   "%s: %s -> %s\n",
	   _("\nMessage received"), 
	   topic,
           (char*) data);

  GNUNET_free (topic);
  GNUNET_free (data);
  in_time_shutdown_task();
}


static void
service_connect_comp (void *cls, struct GNUNET_TESTBED_Operation *op,
		      void *ca_result,
		      const char *emsg)
{	
  struct GNUNET_TIME_Relative timeout;
  const char *topic = "some2/topic";
  const char *message = "test message";
  
  timeout = GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS,
                                           request_timeout);
  GNUNET_MQTT_subscribe (mqtt_handle, strlen(topic) + 1, topic, timeout,
                         NULL, NULL,
                         subscribe_result_callback, NULL);
  GNUNET_MQTT_publish (mqtt_handle, strlen(topic) + 1, topic,
                       strlen(message) + 1, message, timeout, NULL,
                       NULL);
  shutdown_tid = GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 10), 
					       &shutdown_task, NULL);
}


static void *
gmqtt_ca (void *cls, const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  /* Use the provided configuration to connect to service */
  mqtt_handle = GNUNET_MQTT_connect(cfg); 			   
	
  return mqtt_handle;
}


static void
gmqtt_da (void *cls, void *op_result)
{
   /* Disconnect from gnunet-service-mqtt service */
   GNUNET_MQTT_disconnect (mqtt_handle);
   mqtt_handle = NULL;  
}


static void
test_master (void *cls,
	     struct GNUNET_TESTBED_RunHandle *h,
	     unsigned int num_peers,
	     struct GNUNET_TESTBED_Peer **peers,
	     unsigned int links_succeeded,
	     unsigned int links_failed)
{
  basic_mqtt_op = GNUNET_TESTBED_service_connect(NULL, /* Closure for operation */
						 peers[0], /* The peer whose service to connect to */
						 "gnunet-service-mqtt", /* The name of the service */
						 service_connect_comp, /* callback to call after a handle to service is opened */
						 NULL, /* closure for the above callback */
						 gmqtt_ca, /* callback to call with peer's configuration; this should open the needed service connection */
						 gmqtt_da, /* callback to be called when closing the opened service connection */
						 NULL); /* closure for the above two callbacks */								
}


int main (int argc, char **argv)
{
  int ret;

  system("perl -p -i -e 's/\\$\\{([^}]+)\\}/defined $ENV{$1} ? $ENV{$1} : $&/eg' < " CONFIG_FILE " > " CONFIG_FILE_PARSED " 2> /dev/null\n");
  result = GNUNET_SYSERR;
  ret = GNUNET_TESTBED_test_run ("test mqtt single peer comumunication", /* test case name */
				 CONFIG_FILE_PARSED, /* template configuration */
				 1, /* number of peers to start */
				 0LL, /* Event mask -set to 0 for no event notifications */
				 NULL, /* Controller event callback */
				 NULL, /* Closure for controller event callback */
				 &test_master, /* continuation callback to be called when testbed setup is complete */
				 NULL); /* Closure for the test_master callback */
  if ( (GNUNET_OK != ret) || (GNUNET_OK != result) )
    return 1;
  return 0;
}
