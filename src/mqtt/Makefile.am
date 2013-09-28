SUBDIRS = .

INCLUDES = \
  -I$(top_srcdir)/src/include \
  -I$(top_srcdir)

AM_CPPFLAGS = \
  $(GNUNET_CPPFLAGS)

# Set this variable if you are using GNUNET libraries for all programs and
# libraries. You don't then need to target-specific _LDFLAGS with GNUNET_LDFLAGS
# AM_LDFLAGS = \
#   $(GNUNET_LDFLAGS) \
#   $(WINFLAGS) \
#   -export-dynamic

lib_LTLIBRARIES = libgnunetmqtt.la

pkgcfgdir= $(prefix)/share/gnunet/config.d/

libexecdir= $(prefix)/lib/gnunet/libexec/

noinst_LTLIBRARIES = libregexutils.la
libregexutils_la_SOURCES = regex_utils.c
libregexutils_la_LDFLAGS = \
  $(GNUNET_LDFLAGS) -lgnunetregex 


libgnunetmqtt_la_SOURCES = \
  mqtt_api.c regex_utils.c
libgnunetmqtt_la_LDFLAGS = \
  $(GNUNET_LDFLAGS) -lgnunetregex  $(WINFLAGS) \
  -version-info 0:0:0


bin_PROGRAMS = gnunet-mqtt-publish gnunet-mqtt-subscribe

libexec_PROGRAMS = gnunet-service-mqtt

gnunet_service_mqtt_SOURCES = \
  gnunet-service-mqtt.c
gnunet_service_mqtt_LDADD = \
  -lgnunetutil -lgnunetcore -lgnunetdht -lgnunetregex -lgnunethello \
  -lgnunetmesh $(top_builddir)/src/mqtt/libregexutils.la  $(INTLLIBS)
gnunet_service_mqtt_LDFLAGS = \
  $(GNUNET_LDFLAGS)  $(WINFLAGS) -export-dynamic

gnunet_mqtt_publish_SOURCES = \
  gnunet-mqtt-publish.c
gnunet_mqtt_publish_LDADD = \
  -lgnunetutil \
  $(top_builddir)/src/mqtt/libgnunetmqtt.la \
  $(INTLLIBS)
gnunet_mqtt_publish_LDFLAGS = \
  $(GNUNET_LDFLAGS) $(WINFLAGS) -export-dynamic
gnunet_mqtt_publish_DEPENDENCIES = \
  libgnunetmqtt.la

gnunet_mqtt_subscribe_SOURCES = \
  gnunet-mqtt-subscribe.c
gnunet_mqtt_subscribe_LDADD = \
  -lgnunetutil \
  $(top_builddir)/src/mqtt/libgnunetmqtt.la \
  $(INTLLIBS)
gnunet_mqtt_subscribe_LDFLAGS = \
  $(GNUNET_LDFLAGS) $(WINFLAGS) -export-dynamic
gnunet_mqtt_subscribe_DEPENDENCIES = \
  libgnunetmqtt.la

check_PROGRAMS = \
  test_mqtt_single_peer test_mqtt_multiple_peers test_mqtt_unsubscribe test_mqtt_regex_hash test_mqtt_regex_plus

TESTS = $(check_PROGRAMS)

test_mqtt_single_peer_SOURCES = \
 test_mqtt_single_peer.c
test_mqtt_single_peer_LDADD = \
  $(top_builddir)/src/mqtt/libgnunetmqtt.la \
  -lgnunetutil -lgnunettestbed
test_mqtt_single_peer_LDFLAGS = \
  $(GNUNET_LDFLAGS)  $(WINFLAGS) -export-dynamic
  
test_mqtt_multiple_peers_SOURCES = \
 test_mqtt_multiple_peers.c
test_mqtt_multiple_peers_LDADD = \
  $(top_builddir)/src/mqtt/libgnunetmqtt.la \
  -lgnunetutil -lgnunettestbed
test_mqtt_multiple_peers_LDFLAGS = \
  $(GNUNET_LDFLAGS)  $(WINFLAGS) -export-dynamic
  
test_mqtt_unsubscribe_SOURCES = \
 test_mqtt_unsubscribe.c
test_mqtt_unsubscribe_LDADD = \
  $(top_builddir)/src/mqtt/libgnunetmqtt.la \
  -lgnunetutil -lgnunettestbed
test_mqtt_unsubscribe_LDFLAGS = \
  $(GNUNET_LDFLAGS)  $(WINFLAGS) -export-dynamic
  
test_mqtt_regex_hash_SOURCES = \
 test_mqtt_regex_hash.c
test_mqtt_regex_hash_LDADD = \
  $(top_builddir)/src/mqtt/libgnunetmqtt.la \
  -lgnunetutil -lgnunettestbed
test_mqtt_regex_hash_LDFLAGS = \
  $(GNUNET_LDFLAGS)  $(WINFLAGS) -export-dynamic
  
test_mqtt_regex_plus_SOURCES = \
 test_mqtt_regex_plus.c
test_mqtt_regex_plus_LDADD = \
  $(top_builddir)/src/mqtt/libgnunetmqtt.la \
  -lgnunetutil -lgnunettestbed
test_mqtt_regex_plus_LDFLAGS = \
  $(GNUNET_LDFLAGS)  $(WINFLAGS) -export-dynamic
 
pkgcfg_DATA = mqtt.conf

EXTRA_DIST = \
  template.conf template_single_peer.conf test_mqtt_multipeer_topology.dat

