# Need to independently check for fd-net-device support
# because CMake is not exporting any symbol from fd-net-device module
check_include_file_cxx(netpacket/packet.h HAVE_PACKET_H)

if(HAVE_PACKET_H)
  add_definitions(-DHAVE_PACKET_H)
endif()

build_lib(
  LIBNAME docsis
  SOURCE_FILES
    helper/docsis-helper.cc
    helper/docsis-scenario-helper.cc
    model/cm-net-device.cc
    model/cmts-net-device.cc
    model/cmts-upstream-scheduler.cc
    model/docsis-configuration.cc
    model/docsis-channel.cc
    model/docsis-header.cc
    model/docsis-l4s-packet-filter.cc
    model/docsis-net-device.cc
    model/docsis-queue-disc-item.cc
    model/dual-queue-coupled-aqm.cc
    model/queue-protection.cc
  HEADER_FILES
    helper/docsis-helper.h
    helper/docsis-scenario-helper.h
    model/cm-net-device.h
    model/cmts-net-device.h
    model/cmts-upstream-scheduler.h
    model/docsis-channel.h
    model/docsis-configuration.h
    model/docsis-header.h
    model/docsis-l4s-packet-filter.h
    model/docsis-queue-disc-item.h
    model/docsis-net-device.h
    model/dual-queue-coupled-aqm.h
    model/queue-protection.h
    model/microflow-descriptor.h
  LIBRARIES_TO_LINK
    ${libapplications}
    ${libbridge}
    ${libcore}
    ${libcsma}
    ${libflow-monitor}
    ${libnetwork}
    ${libpoint-to-point}
    ${libstats}
    ${libtraffic-control}
  TEST_SOURCES
    test/docsis-link-test-class.cc
    test/docsis-link-test-suite.cc
    test/docsis-lld-test-suite.cc
    test/dual-queue-coupled-aqm-test-suite.cc
    test/dual-queue-test.cc
    test/queue-protection-test-suite.cc
)
