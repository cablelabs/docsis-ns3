/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Shravya K.S. <shravya.ks0@gmail.com>
 *
 */

#include "ns3/test.h"
#include "ns3/dual-queue-coupled-aqm.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "dual-queue-test.h"

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE ("DualQCoupledPiSquaredTestSuite");

class DualQIaqmThresholdsTestCase : public TestCase
{
public:
  DualQIaqmThresholdsTestCase ();
private:
  virtual void DoRun (void);
};

DualQIaqmThresholdsTestCase::DualQIaqmThresholdsTestCase ()
  : TestCase ("Check operation of IAQM ramp thresholds")
{
}

// Dummy function to provide qDelaySingle() callback
Time
qDelaySingle (void)
{
  return Seconds (0);
}

void
DualQIaqmThresholdsTestCase::DoRun (void)
{
  Ptr<DualQueueCoupledAqm> queue = CreateObject<DualQueueCoupledAqm> ();
  queue->SetAttribute ("Amsr", DataRateValue (DataRate ("50Mbps")));
  queue->SetAttribute ("LgRange", UintegerValue (19));
  queue->SetAttribute ("MaxTh", TimeValue (MicroSeconds (1000)));
  Ptr<DualQueueTestFilter> filter = CreateObject<DualQueueTestFilter> ();
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  queue->AddPacketFilter (filter);
  queue->Initialize ();
  // FLOOR = 2 * 8 * 2000 * 10^9/50000000 = 640 us
  // RANGE = 1 << 19 = 524 us
  // MINTH = max (1000 - 524, 640) = 640
  Time tolerance = MicroSeconds (1);
  TimeValue minTh;
  queue->GetAttribute ("MinTh", minTh);
  NS_TEST_EXPECT_MSG_EQ_TOL (minTh.Get (), MicroSeconds (640), tolerance, "MinTh not within tolerance");

  // Check value without FLOOR.  If MAXTH = 2000 us, MINTH = (2000 - 524) us
  queue = CreateObject<DualQueueCoupledAqm> ();
  queue->SetAttribute ("Amsr", DataRateValue (DataRate ("50Mbps")));
  queue->SetAttribute ("LgRange", UintegerValue (19));
  queue->SetAttribute ("MaxTh", TimeValue (MicroSeconds (2000)));
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  filter = CreateObject<DualQueueTestFilter> ();
  queue->AddPacketFilter (filter);
  queue->Initialize ();
  queue->GetAttribute ("MinTh", minTh);
  NS_TEST_EXPECT_MSG_EQ_TOL (minTh.Get (), MicroSeconds (2000 - 524), tolerance, "MinTh not within tolerance");
}


static class DualQueueCoupledAqmTestSuite : public TestSuite
{
public:
  DualQueueCoupledAqmTestSuite ()
    : TestSuite ("dual-queue-coupled-aqm", UNIT)
  {
//    AddTestCase (new DualQueueCoupledAqmTestCase (), TestCase::QUICK);
    AddTestCase (new DualQIaqmThresholdsTestCase (), TestCase::QUICK);
  }
} g_DualQCoupledAqmQueueTestSuite;
