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
 * @file mqtt/test_mqtt_multiple_peers.c
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
#define CONFIG_FILE "template.conf"
#define CONFIG_FILE_PARSED CONFIG_FILE ".tmp"

/**
 * Handle to the MQTT
 */
static struct GNUNET_MQTT_Handle *mqtt_handle_pub, *mqtt_handle_sub;

static struct GNUNET_TESTBED_Operation *basic_mqtt_op_pub, *basic_mqtt_op_sub;

static GNUNET_SCHEDULER_TaskIdentifier shutdown_tid;

static int result;

/**
 * counter for received messages
 */
static int message_count_sub_1 = 0,  message_count_sub_2 = 0, message_count_sub_3 = 0;

/**
 * User supplied timeout value
 */
static unsigned long long request_timeout = 5;

static void
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  if(message_count_sub_1 == 3 && message_count_sub_2 == 1 && message_count_sub_3 == 2)
  {
  	result = GNUNET_OK;
		FPRINTF(stdout,
						"All subscriptions received exactly the expected amount messages - (%d,%d,%d)\n",
						message_count_sub_1,
						message_count_sub_2,
						message_count_sub_3);
  }
  else
  {
		FPRINTF(stdout,
						"Received amount of messages not as expected - (%d,%d,%d)\nExpected - (3,1,2)\n",
						message_count_sub_1,
						message_count_sub_2,
						message_count_sub_3);
  }

	if (NULL != basic_mqtt_op_pub)
  {
		GNUNET_TESTBED_operation_done (basic_mqtt_op_pub); /* calls the gmqtt_da() for closing
							      down the connection */
    basic_mqtt_op_pub = NULL;
  }
  
  if (NULL != basic_mqtt_op_sub)
  {
    GNUNET_TESTBED_operation_done (basic_mqtt_op_sub); /* calls the gmqtt_da_sub_1() for closing
								down the connection */
    basic_mqtt_op_sub = NULL;
  }

  GNUNET_SCHEDULER_shutdown (); /* Also kills the testbed */
}

static void
result_callback_sub_1 (void *cls, uint8_t topic_len, char *topic,
                           size_t size, void *data)
{
  message_count_sub_1++;
	FPRINTF(stdout,
					"Subscription 1 (%s) received %d. Message:\n\t%s -> %s\n",
					cls,
					message_count_sub_1,
					topic,
					(char*) data);
  GNUNET_free (topic);
  GNUNET_free (data);
}

static void
result_callback_sub_2 (void *cls, uint8_t topic_len, char *topic,
                           size_t size, void *data)
{
  message_count_sub_2++;
	FPRINTF(stdout,
					"Subscription 2 (%s) received %d. Message:\n\t%s -> %s\n",
					cls,
					message_count_sub_2,
					topic,
					(char*) data);
  GNUNET_free (topic);
  GNUNET_free (data);
}

static void
result_callback_sub_3 (void *cls, uint8_t topic_len, char *topic,
                           size_t size, void *data)
{
  message_count_sub_3++;
	FPRINTF(stdout,
					"Subscription 3 (%s) received %d. Message:\n\t%s -> %s\n",
					cls,
					message_count_sub_3,
					topic,
					(char*) data);
  GNUNET_free (topic);
  GNUNET_free (data);
}

static void
service_connect_comp_pub (void *cls, 
			      struct GNUNET_TESTBED_Operation *op,
			      void *ca_result,
			      const char *emsg)
{	
	struct GNUNET_TIME_Relative timeout;
	const char *topic_1 = "some/topic";
	const char *message_1 = "message on topic 1";

	timeout = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS,
																					request_timeout);
	GNUNET_MQTT_publish(mqtt_handle_pub, strlen(topic_1) + 1, topic_1,
											strlen(message_1) + 1, message_1, timeout,
											NULL,
											NULL);

	const char *topic_2 = "some/topic/deeper";
	const char *message_2 = "message on topic 2";

	GNUNET_MQTT_publish(mqtt_handle_pub, strlen(topic_2) + 1, topic_2,
											strlen(message_2) + 1, message_2, timeout,
											NULL,
											NULL);

	const char *topic_3 = "some/other";
	const char *message_3 = "message on topic 3";

	GNUNET_MQTT_publish(mqtt_handle_pub, strlen(topic_3) + 1, topic_3,
											strlen(message_3) + 1, message_3, timeout,
											NULL,
											NULL);
}


static void
service_connect_comp_sub (void *cls,
				struct GNUNET_TESTBED_Operation *op,
				void *ca_result,
				const char *emsg)
{	
  struct GNUNET_TIME_Relative timeout;
  const char *topic = "some/#";

  timeout = GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS,
                                           request_timeout);

  GNUNET_MQTT_subscribe (mqtt_handle_sub, strlen(topic) + 1, topic, timeout,
                         NULL, NULL,
                         result_callback_sub_1, topic);

  const char *topic_2 = "some/topic";

  GNUNET_MQTT_subscribe (mqtt_handle_sub, strlen(topic_2) + 1, topic_2, timeout,
                           NULL, NULL,
                           result_callback_sub_2, topic_2);

  const char *topic_3 = "some/+";

  GNUNET_MQTT_subscribe (mqtt_handle_sub, strlen(topic_3) + 1, topic_3, timeout,
                             NULL, NULL,
                             result_callback_sub_3, topic_3);
}


static void *
gmqtt_ca_publish (void *cls, 
		  const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  /* Use the provided configuration to connect to service */
  mqtt_handle_pub = GNUNET_MQTT_connect(cfg); 				   
  return mqtt_handle_pub;
}


static void *
gmqtt_ca_sub (void *cls,
		    const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  /* Use the provided configuration to connect to service */
  mqtt_handle_sub = GNUNET_MQTT_connect(cfg);
  return mqtt_handle_sub;
}


static void
gmqtt_da_publish (void *cls, void *op_result)
{
  /* Disconnect from gnunet-service-mqtt service */
  GNUNET_MQTT_disconnect (mqtt_handle_pub);
  mqtt_handle_pub = NULL;  
}


static void
gmqtt_da_sub (void *cls, void *op_result)
{
  /* Disconnect from gnunet-service-mqtt service */
  GNUNET_MQTT_disconnect (mqtt_handle_sub);
  mqtt_handle_sub = NULL;
}

static void
test_master (void *cls,
	     struct GNUNET_TESTBED_RunHandle *h,
	     unsigned int num_peers,
	     struct GNUNET_TESTBED_Peer **peers,
	     unsigned int links_succeeded,
	     unsigned int links_failed)
{
  basic_mqtt_op_pub = GNUNET_TESTBED_service_connect(NULL, /* Closure for operation */
					  peers[0], /* The peer whose service to connect to */
					  "gnunet-service-mqtt", /* The name of the service */
					  service_connect_comp_pub, /* callback to call after a handle to service is opened */
					  NULL, /* closure for the above callback */
					  gmqtt_ca_publish, /* callback to call with peer's configuration; this should open the needed service connection */
					  gmqtt_da_publish, /* callback to be called when closing the opened service connection */
				   NULL); /* closure for the above two callbacks */
  basic_mqtt_op_sub = GNUNET_TESTBED_service_connect(NULL, /* Closure for operation */
					   peers[1], /* The peer whose service to connect to */
					   "gnunet-service-mqtt", /* The name of the service */
					   service_connect_comp_sub, /* callback to call after a handle to service is opened */
				     NULL, /* closure for the above callback */
					   gmqtt_ca_sub, /* callback to call with peer's configuration; this should open the needed service connection */
					   gmqtt_da_sub, /* callback to be called when closing the opened service connection */
					   NULL); /* closure for the above two callbacks */
						
  shutdown_tid = GNUNET_SCHEDULER_add_delayed(GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 10), &shutdown_task, NULL);

}


int
main (int argc, char **argv)
{
  int ret;
  result = GNUNET_SYSERR;
  
  system("perl -p -i -e 's/\\$\\{([^}]+)\\}/defined $ENV{$1} ? $ENV{$1} : $&/eg' < " CONFIG_FILE " > " CONFIG_FILE_PARSED " 2> /dev/null\n");
  ret = GNUNET_TESTBED_test_run("test mqtt multiple peer communication with overlapping subscriber topics", /* test case name */
			       CONFIG_FILE_PARSED, /* template configuration */
			       3, /* number of peers to start */
			       0LL, /* Event mask -set to 0 for no event notifications */
			       NULL, /* Controller event callback */
			       NULL, /* Closure for controller event callback */
			       &test_master, /* continuation callback to be called when testbed setup is complete */
			       NULL); /* Closure for the test_master callback */
  if ( (GNUNET_OK != ret) || (GNUNET_OK != result) )
    return 1;
  return 0;
}
