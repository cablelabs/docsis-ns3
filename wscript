# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

def build(bld):
    module = bld.create_ns3_module('docsis', ['core', 'network', 'bridge', 'point-to-point', 'traffic-control', 'flow-monitor', 'stats', 'applications', 'fd-net-device', 'csma'])
    module.source = [
        'model/docsis-configuration.cc',
        'model/docsis-channel.cc',
        'model/docsis-net-device.cc',
        'model/cm-net-device.cc',
        'model/cmts-net-device.cc',
        'model/docsis-header.cc',
        'model/docsis-l4s-packet-filter.cc',
        'model/docsis-queue-disc-item.cc',
        'model/cmts-upstream-scheduler.cc',
        'model/dual-queue-coupled-aqm.cc',
        'model/queue-protection.cc',
        'helper/docsis-helper.cc',
        'helper/docsis-scenario-helper.cc',
        ]

    module_test = bld.create_ns3_module_test_library('docsis')
    module_test.source = [
        'test/docsis-lld-test-suite.cc',
        'test/docsis-link-test-class.cc',
        'test/docsis-link-test-suite.cc',
        'test/dual-queue-test.cc',
        'test/dual-queue-coupled-aqm-test-suite.cc',
        'test/queue-protection-test-suite.cc',
        ]

    headers = bld(features='ns3header')
    headers.module = 'docsis'
    headers.source = [
        'model/docsis-channel.h',
        'model/docsis-net-device.h',
        'model/cm-net-device.h',
        'model/cmts-net-device.h',
        'model/docsis-configuration.h',
        'model/docsis-header.h',
        'model/docsis-l4s-packet-filter.h',
        'model/docsis-queue-disc-item.h',
        'model/cmts-upstream-scheduler.h',
        'model/dual-queue-coupled-aqm.h',
        'model/queue-protection.h',
        'model/microflow-descriptor.h',
        'helper/docsis-helper.h',
        'helper/docsis-scenario-helper.h',
        ]

    if bld.env.ENABLE_EXAMPLES:
        bld.recurse('examples')

    # bld.ns3_python_bindings()
