/* 
 *  Copyright (C) 2007 Dip. Ing. dell'Informazione, University of Pisa, Italy
 *  http://info.iet.unipi.it/~cng/ns2mesh80216/
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA, USA
 */

#include <wimsh_mac.h>
#include <wimax_defs.h>
#include <wimsh_packet.h>
#include <wimsh_phy.h>
#include <wimsh_channel.h>
#include <wimax_buffers.h>
#include <wimsh_buffers.h>
#include <wimsh_topology.h>
#include <wimsh_forwarding.h>
#include <wimsh_bwmanager.h>
#include <wimsh_bwmanager_frr.h>
#include <wimsh_coordinator.h>
#include <wimsh_coordinator_std.h>
#include <wimsh_scheduler.h>
#include <wimsh_scheduler_frr.h>

#include <ll.h>
#include <packet.h>
#include <ip.h>
#include <stat.h>

#include <iostream>
#include <vector>
#include <queue>


/*
 *
 * class WimshMacMib
 *
 */

static class WimshMacMibClass : public TclClass {
public:
   WimshMacMibClass() : TclClass("WimshMacMib") {}
   TclObject* create(int, const char*const*) {
      return (new WimshMacMib);
   }
} class_wimsh_mac_mib;

int
WimshMacMib::command (int argc, const char*const* argv)
{
	if ( argc == 3 && strcmp(argv[1], "phymib") == 0 ) {
		phyMib_ = (WimshPhyMib*) TclObject::lookup(argv[2]);
		timer_.start (phyMib_->frameDuration() - .000001);
		return TCL_OK;
	} else if ( argc == 3 && strcmp(argv[1], "allocation") == 0 ) {
		if ( strcmp (argv[2], "basic") == 0 ) {
			WimshMshDsch::allocationType() = WimshMshDsch::BASIC;
		} else if ( strcmp (argv[2], "contiguous") == 0 ) {
			WimshMshDsch::allocationType() = WimshMshDsch::CONTIGUOUS;
		} else {
			fprintf (stderr, "invalid MSH-DSCH allocation type '%s' "
					"Please choose 'basic' or 'contiguous'\n", argv[2]);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 4 && strcmp(argv[1], "crc") == 0 ) {
		int fid = atoi (argv[2]);
		if ( fid < 0 ) {
			fprintf (stderr, "invalid flow ID '%d'\n", fid);
			return TCL_ERROR;
		}
		if ( strcmp (argv[3], "crc") == 0 ) {
			flow2crc_[fid] = true;
		} else if ( strcmp (argv[2], "nocrc") == 0 ) {
			flow2crc_[fid] = false;
		} else {
			fprintf (stderr, "invalid command '%s' on flow ID %d. "
					"Please choose 'crc' or 'nocrc'\n", argv[3], fid);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 4 && strcmp(argv[1], "priority") == 0 ) {
		unsigned int fid = atoi (argv[2]);
		unsigned int prio = atoi (argv[3]);
		if (  atoi (argv[2]) < 0 ) {
			fprintf (stderr, "invalid flow ID '%d'\n",  atoi (argv[2]));
			return TCL_ERROR;
		}
		if ( prio <= WimaxMeshCid::MAX_PRIO ) {
			flow2prio_[fid] = prio;
		} else {
			fprintf (stderr, "invalid priority value'%d' on flow ID %d. "
					"Please choose a number between 0 and '%d''\n", prio, fid,
					WimaxMeshCid::MAX_PRIO);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 4 && strcmp(argv[1], "precedence") == 0 ) {
		unsigned int fid = atoi (argv[2]);
		unsigned int prec = atoi (argv[3]);
		if ( atoi (argv[2]) < 0 ) {
			fprintf (stderr, "invalid flow ID '%d'\n", atoi (argv[2]));
			return TCL_ERROR;
		}
		if ( prec <= WimaxMeshCid::MAX_PREC ) {
			flow2drop_[fid] = prec;
		} else {
			fprintf (stderr, "invalid precedence value'%d' on flow ID %d. "
					"Please choose a number between 0 and '%d''\n", prec, fid,
					WimaxMeshCid::MAX_PREC);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if (argc == 3 && strcmp(argv[1],"numNodes") == 0) { 
	  numNodes = atoi(argv[2]);
	  nodeMac.resize(numNodes);
	  return TCL_OK;
	} else if (argc == 3 && strcmp(argv[1],"mac") == 0) {
	  WimshMac *t = (WimshMac*) TclObject::lookup(argv[2]);
	  nodeMac[t->nodeId()] = t;
	  return TCL_OK;
	}/*jpyu*/else if(argc == 3 && strcmp(argv[1],"avlChannel") == 0) {
	  WimshChannel* chan;
	  chan = (WimshChannel *) TclObject::lookup(argv[2]);
	  avlChannels.push_back(chan);
	  return TCL_OK;
	} /* jpyu */ else if(argc == 2 && strcmp(argv[1],"dump") == 0) {
	  //FILE *f = fopen("channel","w+");
	  dump(stderr);
	  return TCL_OK;
	} else if(argc == 3 && strcmp(argv[1],"HRthreshold") == 0) {
	    HRthreshold_ = atoi(argv[2]);
	    return TCL_OK;
	}

	return TCL_ERROR;
}


// ! JPYU
void 
WimshMacMib::dump(FILE *os)
{
  for(unsigned i = 0; i < avlChannels.size(); i++) {
    fprintf(os,"the channel is %d\n",avlChannels[i]->uid());
  }
}

void
WimshMacMib::handle ()
{
	++frame_;

	if ( WimaxDebug::trace("WMMB::nextFrame") ) fprintf (stderr,
			"%.9f WMMB::nextFrame  %d\n", NOW, frame_);
	timer_.start (phyMib_->frameDuration());
}

wimax::BurstProfile
WimshMacMib::getBurstProfile ( WimaxNodeId src, WimaxNodeId dst )
{
	// assume that the value is present
	std::list< Link >::iterator it;
	for ( it = linkList_.begin(); it != linkList_.end(); ++it ){
		if ( ( it->src_ == src ) && ( it->dst_ == dst )  ) break;
	}
	return it->profile_;
}

void 
WimshMacMib::updateBurstProfile(WimaxNodeId src, 
								WimaxNodeId dst, wimax::BurstProfile p)
{
	std::list< Link >::iterator it;
	for ( it = linkList_.begin(); it != linkList_.end(); ++it ){
		if ( ( it->src_ == src ) && ( it->dst_ == dst )  )
			it->profile_ = p;
	}	
	// if not present, insert
	linkList_.push_back ( Link( src, dst, p ) );
}

/*
 *
 * class WimshMac
 *
 */

static class WimshMacClass : public TclClass {
public:
   WimshMacClass() : TclClass("WimshMac") {}
   TclObject* create(int, const char*const*) {
      return (new WimshMac);
   }
} class_wimsh_mac;


static class BSWimshMacClass : public TclClass {
public:
  BSWimshMacClass() : TclClass("WimshMac/BSWimshMac") {}
  TclObject* create(int, const char*const*) {
    return (new BSWimshMac);
  }
} class_BSwimsh_mac;

static class SSWimshMacClass : public TclClass {
public:
  SSWimshMacClass() : TclClass("WimshMac/SSWimshMac") {}
  TclObject* create(int,const char*const*) {
    return (new SSWimshMac);
  }
} class_SSwimsh_mac;


WimshMac::WimshMac ()
{
	nodeId_ = 0;
	phyMib_ = 0;
	macMib_ = 0;
	ll_ = 0;
	topology_ = 0;
	initialized_ = false;
	bwmanager_ = 0;
	forwarding_ = 0;
	coordinator_ = 0;
	hSelf_ = 0;
	hLast_ = 0;
	index_ = 0;
	sponsorState_ = SS_IDLE;
	linkEstState_ = LE_IDLE;
	linkEstId_ = 0;
	sponsorStart_ = 0;
	linkEstStart_ = 0;
	linkEstCurrent_ = 0;
	scanStart_ = 0;
	scanMode_ = false;
	hErrorTagged_ = false;
	mshDschAvgError_ = -1.0;
	mshDschAvgGood_ = -1.0;
}

int
WimshMac::command (int argc, const char*const* argv)
{
	if ( argc == 2 && strcmp (argv[1], "initialize") == 0 ) {
		initialize ();
		return TCL_OK;
	} else if ( argc == 2 && strcmp (argv[1], "scan") == 0 ) {
		scanMode_ = true;
		scanStart_ = NOW;
		return TCL_OK;
	} else if ( argc == 2 && strcmp (argv[1], "h-error-tagged") == 0 ) {
		hErrorTagged_ = true;
		return TCL_OK;
	} else if ( argc == 2 && strcmp (argv[1], "link-establishment") == 0 ) {
		linkEstCurrent_ = 0;
		linkEstState_ = LE_SEND_CHALLENGE;
		linkEstStart_ = NOW;
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "open-sponsor") == 0 ) {
		sponsorId_ = (WimaxNodeId) atoi (argv[2]);
		sponsorState_ = SS_SEND_REQ;
		sponsorStart_ = NOW;
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "phymib") == 0 ) {
		phyMib_ = (WimshPhyMib*) TclObject::lookup (argv[2]);
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "macmib") == 0 ) {
		macMib_ = (WimshMacMib*) TclObject::lookup (argv[2]);
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "ll") == 0 ) {
		ll_ = (LL*) TclObject::lookup (argv[2]);
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "topology") == 0 ) {
		topology_ = (WimshTopology*) TclObject::lookup (argv[2]);
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "phy") == 0 ) {
		phy_.push_back ((WimshPhy*) TclObject::lookup (argv[2]));
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "channel") == 0 ) {
		channel_.push_back ((WimshChannel*) TclObject::lookup (argv[2]));
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "nodeid") == 0 ) {
		nodeId_ = (WimaxNodeId) atoi (argv[2]);
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "index") == 0 ) {
		index_ = (WimaxNodeId) atoi (argv[2]);
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "msh-dsch-avg-bad") == 0 ) {
		mshDschAvgError_ = atof (argv[2]);
		if ( mshDschAvgError_ > 0 && mshDschAvgError_ < 1.0 ) {
			fprintf (stderr, "Bad value '%f' for the average number of "
					"consecutive MSH-DSCH messages received incorrectly. "
					"Please select a number >= 1.\n", mshDschAvgError_);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "msh-dsch-avg-good") == 0 ) {
		mshDschAvgGood_ = atof (argv[2]);
		if ( mshDschAvgGood_ > 0 && mshDschAvgGood_ < 1.0 ) {
			fprintf (stderr, "Bad value '%f' for the average number of "
					"consecutive MSH-DSCH messages received correctly. "
					"Please select a number >= 1.\n", mshDschAvgGood_);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "scheduler") == 0 ) {
		if ( strcmp (argv[2], "fifo") == 0 ) {
			scheduler_ = new WimshSchedulerFifo (this);
		} else if ( strcmp (argv[2], "fair-rr") == 0 ) {
			scheduler_ = new WimshSchedulerFairRR (this);
		} else {
			fprintf (stderr, "packet scheduler '%s' not supported", argv[2]);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "forwarding") == 0 ) {
		if ( strcmp (argv[2], "spf") == 0 ) {
			forwarding_ = new WimshForwardingSpf (this, topology_);
		} else if ( strcmp (argv[2], "dump") == 0 ) {
			assert ( initialized_ );
			return forwarding_->command ( argc - 2, argv + 2);
		} else {
			fprintf (stderr, "forwarding module '%s' not supported", argv[2]);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "bwmanager") == 0 ) {
		if ( strcmp (argv[2], "dummy") == 0 ) {
			bwmanager_ = new WimshBwManagerDummy (this);
		// } else if ( strcmp (argv[2], "round-robin") == 0 ) {
		//	bwmanager_ = new WimshBwManagerRoundRobin (this);
		} else if ( strcmp (argv[2], "fair-rr") == 0 ) {
			bwmanager_ = new WimshBwManagerFairRR (this);
		} else {
			fprintf (stderr, "bandwidth manager '%s' not supported", argv[2]);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "coordinator") == 0 ) {
		if ( strcmp (argv[2], "dummy") == 0 ) {
			coordinator_ = new WimshCoordinatorDummy (this, phyMib_);
      } else if ( strcmp (argv[2], "standard") == 0 ) {
         coordinator_ = new WimshCoordinatorStandard (this, phyMib_);
		} else {
			fprintf (stderr, "coordinator '%s' not supported", argv[2]);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "dump") == 0 ) {
		if ( strcmp (argv[2], "profile") == 0 ) {
			std::map<WimaxNodeId, unsigned int>::iterator it = neigh2ndx_.begin();
			for ( ; it != neigh2ndx_.end() ; it++ ) {
				fprintf (stderr, "%.9f %d -> %d profile %s alpha %d\n",
						NOW, nodeId_, it->first,
						WimaxDebug::format(profile_[it->second]), alpha_[it->second]);
			}
		} else {
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "estcurr") == 0 ) {
		hEstCurr_ = atof (argv[2]);
		if ( hEstCurr_ < 0 || hEstCurr_ > 1.0 ) {
			fprintf (stderr, "invalid weight '%f' for the current sample of "
					"the estimation of H. Choose a value in [0, 1]\n", hEstCurr_);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 3 && strcmp (argv[1], "estpast") == 0 ) {
		hEstPast_ = atof (argv[2]);
		if ( hEstPast_ < 0 || hEstPast_ > 1.0 ) {
			fprintf (stderr, "invalid weight '%f' for the past sample of "
					"the estimation of H. Choose a value in [0, 1]\n", hEstPast_);
			return TCL_ERROR;
		}
		return TCL_OK;
	} else if ( argc == 4 && strcmp (argv[1], "profile") == 0 ) {
		assert ( initialized_ );

		// get the burst profile index
		wimax::BurstProfile bp = (wimax::BurstProfile) atoi (argv[2]);

		if ( bp >= wimax::N_BURST_PROFILES ) {
			fprintf (stderr, "burst profile %d is not valid. "
					"Choose a number between 0 and %d\n",
					bp, wimax::N_BURST_PROFILES - 1);
			return TCL_ERROR;
		}

		// if the special keyword 'all' is used instead of the NodeID
		// of the neighbor, then the profile of all the links are updated
		if ( strcmp (argv[3], "all") == 0 ) {

			// get the list of this node's neighbors
			std::vector<WimaxNodeId> neighbors;
			std::vector<WimaxNodeId>::iterator it;
			topology_->neighbors (nodeId_, neighbors);

			// for each neighbor, update the burst profile and alpha data
			// structures, and the MAC MIB
			for ( it = neighbors.begin() ; it != neighbors.end() ; ++it ) {
				unsigned int ndx = neigh2ndx_[*it];
				profile_[ndx] = bp;
				alpha_[ndx] = WimshPhyMib::alpha[bp];
				macMib_->updateBurstProfile ( nodeId(), *it, bp );
			}

		// otherwise, update only the specified burst profile
		} else {
			// get the neighbor NodeID
			WimaxNodeId neigh = (WimaxNodeId) atoi (argv[3]);

			if ( neigh2ndx_.count (neigh) != 1 ) {
				fprintf (stderr, "node %d is not a neighbor of node %d\n",
						nodeId_, neigh);
				return TCL_ERROR;
			}
			
			// get the neighbor index
			unsigned int ndx = neigh2ndx_[neigh];

			// update the burst profile & alpha data structures, and the MAC MIB
			profile_[ndx] = bp;
			alpha_[ndx] = WimshPhyMib::alpha[bp];
			macMib_->updateBurstProfile ( nodeId(), neigh, bp );
		}

		return TCL_OK;

	// try to send commands to the bandwidth manager
	} else if ( argc > 2 && strcmp (argv[1], "bwmanager") == 0 ) {
		assert ( initialized_ );
		return bwmanager_->command ( argc - 2, argv + 2);

	// try to send commands to the forwarding module
	} else if ( argc > 2 && strcmp (argv[1], "forwarding") == 0 ) {
		assert ( initialized_ );
		return forwarding_->command ( argc - 2, argv + 2);

	// try to send commands to the coordinator
	} else if ( argc > 2 && strcmp (argv[1], "coordinator") == 0 ) {
		assert ( initialized_ );
		return coordinator_->command ( argc - 2, argv + 2);

	// try to send commands to the scheduler
	} else if ( argc > 2 && strcmp (argv[1], "scheduler") == 0 ) {
		assert ( initialized_ );
		return scheduler_->command (argc - 2, argv + 2);
	} else if ( argc == 3 && strcmp (argv[1], "BSnode") == 0) {
	  BSnode_ = (WimshMac *) TclObject::lookup(argv[2]);
	  return TCL_OK;
	}

	return TCL_ERROR;
}

void
WimshMac::initialize ()
{
	// this function must be called only once for each MAC object
	assert ( ! initialized_ );

	// retrieve an array of neighbors of this node
	std::vector<WimaxNodeId> neighbors;
	topology_->neighbors (nodeId_, neighbors);

	// set the number of neighbors of this node
	nneighs_ = neighbors.size();

	// resize the arrays that hold one entry for each neighbor
	alpha_.resize (nneighs_);
	profile_.resize (nneighs_);
	fragbuf_.resize (nneighs_);
	reasbuf_.resize (nneighs_);
	hNeigh_.resize (nneighs_);
	hNeighLast_.resize (nneighs_);
	ndx2neigh_.resize (nneighs_);
	mshDschLinkQuality_.resize (nneighs_);

	// for each neighbor:
	// - set its local numerical identier (and the other way around)
	// - add a default transmission profile
	// - add a default alpha value
	// - create a fragmentation buffer
	// - create a reassembly buffer
	// - set to zero the estimated H value
	// - set to zero the last received opportunity time
	// - set the default burst profile in MAC Mib
	for ( unsigned int i = 0 ; i < nneighs_ ; i++ ) {
		neigh2ndx_[neighbors[i]] = i;
		ndx2neigh_[i] = neighbors[i];
		profile_[i] = wimax::QPSK_1_2;
		alpha_[i] = WimshPhyMib::alpha[wimax::QPSK_1_2];
		//alpha_[i] = WimshPhyMib::alpha[5];
		fragbuf_[i] = new WimshFragmentationBuffer;
		reasbuf_[i] = new WimaxReassemblyBuffer;
		hNeigh_[i] = 0;
		hNeighLast_[i] = 0;
		mshDschLinkQuality_[i].frames_ =
			geometric (mshDschRngGood_, 1/mshDschAvgGood_);
		
		macMib_->updateBurstProfile ( nodeId(), neighbors[i], 
											wimax::QPSK_1_2 );
	}

	// resize the internal data structures of the packet scheduler and
	// the bandwidth manager
	scheduler_->initialize();
	bwmanager_->initialize();
	coordinator_->initialize();
	forwarding_->initialize();

	// listen to the control channel
	setControlChannel (wimax::RX);

	// set this MAC as initialized
	initialized_ = true;
}

//#define initialized initialized_

void
WimshMac::recvPacket (Packet* pkt)
{
  assert ( initialized ) ;

	if ( WimaxDebug::trace("WMAC::recvPacket") ) fprintf (stderr,
			"%.9f WMAC::recvPacket [%d] %s\n",
			NOW, nodeId_, WimaxDebug::format (pkt));

	// we encapsulate the incoming IP datagram into a MAC SDU
	WimaxSdu* sdu = new WimaxSdu;
	sdu->ip() = pkt;

	// pass the SDU to the appropriate function
	recvSdu (sdu);
}

void
WimshMac::recvSdu (WimaxSdu* sdu)
{
	assert ( initialized ) ;

	if ( WimaxDebug::trace("WMAC::recvSdu") ) fprintf (stderr,
			"%.9f WMAC::recvSdu    [%d] %s\n",
			NOW, nodeId_, WimaxDebug::format(sdu));

	// if this node is the final destination of the SDU, then
	// we pass the encapsulated IP datagram to the upper layer (ie. LL)
	if ( (WimaxNodeId) HDR_IP(sdu->ip())->daddr() == nodeId_ ) {
		HDR_CMN(sdu->ip())->direction () = hdr_cmn::UP;
		ll_->recv (sdu->ip(), 0);
		delete sdu;

	// otherwise, we first add our NodeID to the list of traversed hops,
	// and then encapsulate the MAC SDU into a MAC PDU, which is
	// buffered by the packet scheduler
	} else {

		// this is the IP packet incapsulated into the SDU
		Packet* ip = sdu->ip();

		// retrieve the flow ID of the IP datagram
		int fid = HDR_IP(ip)->flowid();

		// add our NodeID to the list of traversed hops
		sdu->addHop (nodeId_);

		// timestamp the SDU to compute the access delay
		sdu->timestamp() = NOW;

		// create a new MAC PDU which encapsulates the MAC SDU
		WimaxPdu* pdu = new WimaxPdu;
		pdu->sdu() = sdu;

		// set the relevant fields of the MAC PDU
		pdu->nodeId() = nodeId_;
		// pdu->error() is false by default
		pdu->hdr().crc() = macMib_->flow2crc (fid);
		// pdu->hdr().fragmentation() is false by default
		pdu->hdr().mesh() = true;
		pdu->hdr().meshCid().type() = WimaxMeshCid::DATA;
		// pdu->hdr().meshCid().reliability() is NO_ARQ by default
		pdu->hdr().meshCid().priority() = macMib_->flow2prio (fid);
		pdu->hdr().meshCid().drop() = macMib_->flow2drop (fid);
		pdu->hdr().meshCid().dst() = forwarding_->nextHop (sdu);
		pdu->size (sdu->size());

		// pass the new PDU to the packet scheduler
		scheduler_->addPdu (pdu);
	}
}

void
WimshMac::recvMshDsch (WimshMshDsch* dsch, double txtime)
{
	assert ( initialized ) ;

	if ( WimaxDebug::trace("WMAC::recvMshDsch") ) fprintf (stderr,
			"%.9f WMAC::recvMshDsch[%d]\n", NOW, nodeId_);
	
	// update the H value of this neighbor
	WimaxNodeId snode = dsch->myself().nodeId_;
	unsigned int ndx  = neigh2ndx_[snode];
	Stat::put ("wimsh_dsch_inter_frame_rcv", index_,
			round ( ( NOW - hNeighLast_[ndx] ) / phyMib_->frameDuration() ) );
	Stat::put ("wimsh_dsch_inter_time_rcv", index_, NOW - hNeighLast_[ndx]);
	Stat::put ("wimsh_dsch_inter_time_rcv_d", index_, NOW - hNeighLast_[ndx]);
	updateH (hNeigh_[ndx], hNeighLast_[ndx]);

	// compute the difference between the actual next transmit time
	// and the estimated (H) value, if this is the tagged node for this metric
	if ( hErrorTagged_ ) {
		double diff = fabs ( dsch->myself().nextXmtTimeSec_ - hNeigh_[ndx] );
		//
		// [CC] not sure whether it is correct to mix together the
		// samples collected from different neighbors (uncommented line below)
		// while I believe it is ok to get separate statistics (commented line
		// below). However, the latter makes scripts more difficult, thus
		// for now I will stick to the easy & dirty way.
		// 
		// Stat::put ("wimsh_dsch_h_error", snode, diff);
		Stat::put ("wimsh_dsch_h_error", index_, diff);
	}

	// send the MSH-DSCH message to the coordinator and to the bw manager
	coordinator_->recvMshDsch (dsch, txtime);
	bwmanager_->recvMshDsch (dsch);
	forwarding_->recvMshDsch (dsch);
}

void
WimshMac::recvMshNcfg (WimshMshNcfg* ncfg, double txtime)
{
	assert ( initialized ) ;

	if ( WimaxDebug::trace("WMAC::recvMshNcfg") ) fprintf (stderr,
			"%.9f WMAC::recvMshNcfg[%d]\n", NOW, nodeId_);
	
	coordinator_->recvMshNcfg (ncfg, txtime);

	if ( scanMode_ ) {
		// fprintf (stderr, "%.9f MSH-NCFG received from node %d\n",
		//		NOW, ncfg->src());
		if ( scanSet_.count (ncfg->src()) == 1 ) {
			Stat::put ("wimsh_scan_latency", index_, NOW - scanStart_);
			char** argv = new char*[1];
			argv[0] = "print";
			Stat::command (1, argv);
			exit (0);
		}
		scanSet_.insert (ncfg->src());
	}

	// fprintf (stderr, "** node %d from node %d to node %d type %d\n",
	//		nodeId_, ncfg->src(), ncfg->hdr().meshCid().dst(),
	//		ncfg->type());
	if ( ncfg->type() == WimshMshNcfg::CHALLENGE &&
			ncfg->hdr().meshCid().dst() == nodeId_ ) {
		// fprintf (stderr, "%.9f node %d LE_SEND_CHALLENGE from node %d\n",
		//		NOW, nodeId_, ncfg->src());
		linkEstState_ = LE_SEND_RESPONSE;
		linkEstId_ = ncfg->src();
	} else if ( ncfg->type() == WimshMshNcfg::RESPONSE &&
			ncfg->hdr().meshCid().dst() == nodeId_ ) {
		// fprintf (stderr, "%.9f node %d LE_SEND_RESPONSE from node %d\n",
		//		NOW, nodeId_, ncfg->src());
		linkEstState_ = LE_SEND_ACK;
	} 
}

void
WimshMac::recvMshNent (WimshMshNent* nent, double txtime)
{
	assert ( initialized ) ;

	if ( WimaxDebug::trace("WMAC::recvMshNent") ) fprintf (stderr,
			"%.9f WMAC::recvMshNent[%d]\n", NOW, nodeId_);

	// This MSH-NENT is directed to myself. Thus, a MSH-NCFG message
	// is scheduled to be sent back to the new node.
	if ( nent->hdr().meshCid().dst() == nodeId_ ) {
		// fprintf (stderr, "%.9f node %d SS_SEND_OPEN from node %d\n",
		//	NOW, nodeId_, nent->src());
		sponsorState_ = SS_SEND_OPEN;
		sponsorId_ = nent->src();
	}
}

void
WimshMac::opportunity (WimshMshDsch* dsch)
{
	assert ( initialized ) ;

	if ( WimaxDebug::trace ("WMAC::opportunity") ) fprintf (stderr,
			"%.9f WMAC::opportunity[%d] MSH-DSCH\n", NOW, nodeId_);

	// let the bandwidth manager fill the remaining field of the MSH-DSCH message
	bwmanager_->schedule (dsch);

	// encapsulate the MSH-DSCH into a PDU burst
	WimshBurst* burst = new WimshBurst;
	burst->addMshDsch (dsch);

	if ( WimaxDebug::trace ("WMAC::opportunity") )
		WimaxDebug::print (dsch, stderr);

	Stat::put ("wimsh_dsch_inter_frame_snd", index_,
		round ( ( NOW - hLast_ ) / phyMib_->frameDuration() ) );
	Stat::put ("wimsh_dsch_inter_time_snd", index_, NOW - hLast_);
	Stat::put ("wimsh_dsch_inter_time_snd_d", index_, NOW - hLast_);
	Stat::put ("wimsh_dsch_size_a", index_, dsch->size());
	Stat::put ("wimsh_dsch_size_d", index_, dsch->size());

	// update the estimated interval between two consecutive opportunities
	updateH (hSelf_, hLast_);

	// set the PHY in tx mode to the control channel
	setControlChannel (wimax::TX);

	// send the burst using sendBurst(burst)
	phy_[0]->sendBurst (burst);
}


void
WimshMac::opportunity (WimshMshNcfg* ncfg)
{
	assert ( initialized ) ;

	if ( WimaxDebug::trace ("WMAC::opportunity") ) fprintf (stderr,
			"%.9f WMAC::opportunity[%d] MSH-NCFG\n", NOW, nodeId_);

	Stat::put ("wimsh_ncfg_inter_frame", index_,
		round ( ( NOW - hLastNcfg_ ) / phyMib_->frameDuration() ) );
	Stat::put ("wimsh_ncfg_inter_time", index_, NOW - hLastNcfg_);
	hLastNcfg_ = NOW;

	// encapsulate the MSH-DSCH into a PDU burst
	WimshBurst* burst = new WimshBurst;
	burst->addMshNcfg (ncfg);

	ncfg->src() = nodeId_;

	// If there is a pending MSH-NCFG Net Entry Open message, send it now.
	if ( sponsorState_ == SS_SEND_OPEN ) {
		ncfg->type() = WimshMshNcfg::NET_ENTRY_OPEN;
		ncfg->hdr().meshCid().dst() = sponsorId_;
		sponsorState_ = SS_IDLE;
	}

	// get the list of this node's neighbors
	std::vector<WimaxNodeId> neighbors;
	topology_->neighbors (nodeId_, neighbors);

	if ( linkEstState_ == LE_SEND_CHALLENGE ) {
		ncfg->hdr().meshCid().dst() = neighbors[linkEstCurrent_];
		ncfg->type() = WimshMshNcfg::CHALLENGE;
		linkEstState_ = LE_WAIT_RESPONSE;
		// fprintf (stderr, "%.9f node %d LE_SEND_CHALLENGE to node %d\n",
		//		NOW, nodeId_, ncfg->hdr().meshCid().dst());

	} else if ( linkEstState_ == LE_SEND_ACK ) {
		// fprintf (stderr, "%.9f node %d LE_SEND_ACK to node %d\n",
		//		NOW, nodeId_, neighbors[linkEstCurrent_]);
		linkEstCurrent_++;
		linkEstState_ = LE_SEND_CHALLENGE;

		if ( linkEstCurrent_ >= neighbors.size() ) {
			Stat::put ("wimsh_linkest_latency", index_, NOW - linkEstStart_);
			char** argv = new char*[1];
			argv[0] = "print";
			Stat::command (1, argv);
			exit (0);
		}

	} else if ( linkEstState_ == LE_SEND_RESPONSE ) {
		// fprintf (stderr, "%.9f node %d LE_SEND_RESPONSE to node %d\n",
		// 		NOW, nodeId_, linkEstId_);
		ncfg->hdr().meshCid().dst() = linkEstId_;
		ncfg->type() = WimshMshNcfg::RESPONSE;
		linkEstState_ = LE_IDLE;
	}

	// set the PHY in tx mode to the control channel
	setControlChannel (wimax::TX);

	// send the burst using sendBurst(burst)
	phy_[0]->sendBurst (burst);
}

void
WimshMac::opportunity (WimshMshNent* nent)
{
	assert ( initialized ) ;

	if ( WimaxDebug::trace ("WMAC::opportunity") ) fprintf (stderr,
			"%.9f WMAC::opportunity[%d] MSH-NENT\n", NOW, nodeId_);

	Stat::put ("wimsh_nent_inter_frame", index_,
		round ( ( NOW - hLastNent_ ) / phyMib_->frameDuration() ) );
	Stat::put ("wimsh_nent_inter_time", index_, NOW - hLastNent_);
	hLastNent_ = NOW;

	if ( sponsorState_ != SS_SEND_REQ && sponsorState_ != SS_SEND_ACK ) {
		delete nent;
		return;
	}

	if ( sponsorState_ == SS_SEND_REQ ) {
		// fprintf (stderr, "%.9f node %d SS_SEND_REQ to node %d\n",
		//	NOW, nodeId_, sponsorId_);
		nent->type() = WimshMshNent::REQUEST;
		sponsorState_ = SS_SEND_ACK;
	} else if ( sponsorState_ == SS_SEND_ACK ) {
		// nent->type() = WimshMshNent::ACK;
		// sponsorState_ = SS_IDLE;
		Stat::put ("wimsh_sponsor_latency", index_, NOW - sponsorStart_);
      char** argv = new char*[1];
      argv[0] = "print";
      Stat::command (1, argv);
      exit (0);
	}
	else
		abort ();

	nent->hdr().meshCid().dst() = sponsorId_;
	nent->src() = nodeId_;

	WimshBurst* burst = new WimshBurst;
	burst->addMshNent (nent);
	burst->source() = nodeId_;

	// set the PHY in tx mode to the control channel
	setControlChannel (wimax::TX);

	// send the burst using sendBurst(burst)
	phy_[0]->sendBurst (burst);
}

void
WimshMac::recvBurst (WimshBurst* burst)
{
	assert ( initialized ) ;

	if ( WimaxDebug::trace("WMAC::recvBurst") ) {
		fprintf (stderr,
			"%.9f WMAC::recvBurst  [%d] src %d error %d\n",
			NOW, nodeId_, burst->source(), burst->error());
		WimaxDebug::print (burst, stderr);
	}

	//
	// manage a control message
	//
	if ( burst->type() == wimax::MSHDSCH ) {
		// check if the burst needs be corrupted and update
		// the quality of the current link
		//
		if ( mshDschAvgError_ > 0 && mshDschAvgError_ > 0 ) {
			// get the quality of the sender node's link
			LinkQuality& link = mshDschLinkQuality_[neigh2ndx_[burst->source()]];

			// if the link is bad, corrupt the burst
			// otherwise, leave the error flag unaltered (can be true)
			if ( ! link.good_ ) burst->error() = true;

			// decrease the number of frames before transition
			// if this counter reaches zero, then flip the link quality
			if ( --link.frames_ == 0 ) {
				// select the number of frames to go
				if ( ! link.good_ )
					link.frames_ = geometric (mshDschRngGood_, 1/mshDschAvgGood_);
				else
					link.frames_ = geometric (mshDschRngError_, 1/mshDschAvgError_);

				// flip the link quality status
				link.good_ = ! link.good_;
			}
		}

		// actually decode data contained into the MSH-DSCH message
		// if the burst is not corrupted
		if ( ! burst->error() ) {
			Stat::put ("wimsh_dsch_error", index_, 0.0);
			recvMshDsch (burst->mshDsch(), burst->txtime());
		} else {
			Stat::put ("wimsh_dsch_error", index_, 1.0);
		}

	} else if ( burst->type() == wimax::MSHNCFG ) {
		if ( ! burst->error() ) recvMshNcfg (burst->mshNcfg(), burst->txtime());

	} else if ( burst->type() == wimax::MSHNENT ) {
		if ( ! burst->error() ) recvMshNent (burst->mshNent(), burst->txtime());
	} else if ( burst->type() == wimax::MSHCSCH ) {
	  if( ! burst->error() ) recvMshCsch( burst->mshCsch(), burst->txtime());
	  /////////////////shit I have defined twice ,one day waste
	//
	// manage a data burst
	//
	} else {

		// remove PDUs one by one from the burst
		for ( WimaxPdu* pdu = burst->pdu() ; pdu != 0 ; pdu = burst->pdu() ) {

			// if the PDU does not contain errors, then we check for the
			// destination NodeID in the CID in the MAC header
			// if the PDU is directed to this node, we pass it to the
			// appropriate reassembly buffer
			if ( ! burst->error() && ! pdu->error() &&
					pdu->hdr().meshCid().dst() == nodeId_ ) {
				
				Stat::put ("wimsh_mac_tpt", index_, pdu->size());
				WimaxSdu* sdu = reasbuf_[neigh2ndx_[pdu->nodeId()]]->addPdu (pdu);
				// if a full SDU is reassembled, then reschedule it to recvSDU
				if ( sdu ) {
					// compute the per-hop delay
					const int fid = HDR_IP(sdu->ip())->flowid();
					const double delay = NOW - sdu->timestamp();
					Stat::put ("wimsh_delay_access_a", fid, delay);
					Stat::put ("wimsh_delay_access_d", fid, delay);
					Stat::put ("wimsh_delay_hopbyhop_a", index_, delay);
					Stat::put ("wimsh_delay_hopbyhop_d", index_, delay);

					// indicate to the bandwidth manager that we received an SDU
					bwmanager_->received (
							HDR_IP(sdu->ip())->saddr(),
							HDR_IP(sdu->ip())->daddr(),
							pdu->hdr().meshCid().priority(),
							neigh2ndx_[sdu->lastHop()], pdu->size()
							);
					recvSdu (sdu);
				}

			} else {
				// deallocate the SDU/ns2 packet
				pdu->sdu()->freePayload ();
				delete pdu->sdu();
			}

			delete pdu;
		} // for each PDU of the received burst
	}

	// in any case the burst is deallocated
	delete burst;
}

void
WimshMac::updateH (double& h, double& last)
{
	h = hEstPast_ * h + hEstCurr_ * (NOW - last);
	last = NOW;
}

void
WimshMac::setControlChannel (wimax::ChannelStatus s)
{
	if ( WimaxDebug::trace ("WMAC::setControlChannel" ) ) fprintf (stderr,
			"%.9f WMAC::setCtrlChn [%d] status %s\n",
			NOW, nodeId_,
			(s == wimax::TX) ? "TX" : (s == wimax::RX) ? "RX" : "NONE");

	phy_[0]->setMode (s, channel_[0]);
}

void
WimshMac::receive (unsigned int channel)
{
	if ( WimaxDebug::trace ("WMAC::receive" ) ) fprintf (stderr,
			"%.9f WMAC::receive    [%d] chn %d\n",
			NOW, nodeId_, channel);

	phy_[0]->setMode ( wimax::RX, channel_[channel] );
}

void
WimshMac::transmit (unsigned int range, WimaxNodeId dst, unsigned int channel)
{
	if ( WimaxDebug::trace ("WMAC::transmit" ) ) fprintf (stderr,
			"%.9f WMAC::transmit   [%d] dst %d range %d chn %d\n",
			NOW, nodeId_, dst, range, channel);

	// dst's index
	const unsigned int ndx = neigh2ndx_[dst];

	// compute the amount of bytes that fit into the range towards dst
	// we account for the physical preamble that must be transmitted
	unsigned int bytes = slots2bytes (ndx, range, true);
	//unsigned int bytes = slots2bytes(ndx,range);
	// create a new burst into the fragmentation buffer
	bool room = fragbuf_[ndx]->newBurst (profile_[ndx], bytes);

	// if there is room schedule more PDUs from the scheduler
	if ( room ) scheduler_->schedule (*fragbuf_[ndx], dst);

	// do not send out the buffer is there are not scheduled PDUs within
	if ( fragbuf_[ndx]->getBurst()->npdus() == 0 ) {
		delete fragbuf_[ndx]->getBurst();
		return;
	}

	// if there are backlogged PDUs waiting to be sent to the current
	// neighbor, but there is spare room into the granted set of slots,
	// this means that some capacity could not be used => new backlog
	// should be requested

	const unsigned int spare = 
		( fragbuf_[ndx]->getBurst()->npdus() > 0 )
		? bytes - fragbuf_[ndx]->getBurst()->size()
		: bytes;
	if ( scheduler_->neighbor (ndx) > 0 && spare > 0 )
		bwmanager_->backlog (dst, spare);               // :TODO: check

	// set transmission mode to the given channel
	phy_[0]->setMode ( wimax::TX, channel_[channel] );

	// the backlog must be updated so as to take into account the
	// additional fragmentation overhead of the fragmentation buffer:
	// - each initial fragment incurs an additional overhead equal to
	//   the fragmentation subheader itself
	// - any other fragment adds an additional overhead equal to the
	//   sum of the MAC header + any other subheaders, including fragmentation
	WimshBurst::List& pdus = fragbuf_[ndx]->getBurst()->pdus();
	WimshBurst::List::const_iterator it = pdus.begin();

	// look at the fragmentation subheader (if any) of each PDU
	for ( ; it != pdus.end() ; ++it ) {
		// additional backlog to add, if necessary
		unsigned int backlog = 0;

		// alias to the PDU
		WimaxPdu* pdu = *it;

		// this is a fragment
		if ( pdu->hdr().fragmentation() ) {
			if ( pdu->fsh().state() == WimaxFsh::FIRST_FRAG ) // first frag
				backlog = WimaxFsh::size();
			else                                              // middle/last frag
				backlog = pdu->hdr().size() + WimaxPdu::meshSubhdrSize() + WimaxFsh::size();
		}

		// if we encountered a fragment there is something to tell the bwmanager
		if ( backlog ) {
			bwmanager_->backlog (
					(WimaxNodeId) HDR_IP(pdu->sdu()->ip())->saddr(), // src
					(WimaxNodeId) HDR_IP(pdu->sdu()->ip())->daddr(), // dst
					pdu->hdr().meshCid().priority(),                 // priority
					pdu->hdr().meshCid().dst(),                      // next-hop
					backlog );                                       // backlog
		}
	}

	// transmit the burst to the PHY (unless it is empty)
	WimshBurst* burst = fragbuf_[ndx]->getBurst();
	if ( burst->npdus() > 0 ) {
		bwmanager_->sent ( dst, burst->size() );
		phy_[0]->sendBurst ( burst );
	}
}




//! add by jpyu

void 
BSWimshMac::recvMshCsch(WimshMshCsch* csch, double txtime) {
  assert (initialized );
  //store the csch of children
  if( csch->getFlag() ) {
    WimshMshCsch * tcsch = new WimshMshCsch(*csch);
    message.push_back(tcsch);
  }
}



void 
BSWimshMac::opportunity(int startFrame,int endFrame)
{
  assert (initialized );
  //get BS request information
  WimshMshCsch * myself = new WimshMshCsch;

  myself->getTransmitId() = nodeId();
  bwmanager_->schedule(myself);

  message.push_back(myself);
  
  WimshBurst* burst = new WimshBurst;
  
  WimshMshCsch * csch = new WimshMshCsch;

  csch->getFlag() = false;

  csch->getFlowSE() = 0;
  csch->getTransmitId() = nodeId();
  
  csch->startFrame = startFrame;
  csch->endFrame = endFrame;
  
  int frames = endFrame - startFrame;

  assert (frames > 0);

  bwmanager_->slotAllocation(message,startFrame,frames);



  for(unsigned int i = 0; i < message.size();++i) delete message[i];
  message.clear();
  //send csch(contains no message),just to notify SS to update the grants information
  setControlChannel(wimax::TX);
  burst->addMshCsch(csch);
  phy_[0]->sendBurst (burst);

}

/*
void 
BSWimshMac::opportunity(int startFrame,int endFrame)
{
  assert ( initialized );

  WimshBurst* burst = new WimshBurst;

  WimshMshCsch * csch = new WimshMshCsch;
  //grant frame
  csch->getFlag() = false;
  //burst->addMshCsch (csch);
  //compute the frame that the grant message reaches the end node
  csch->getFlowSE() = 0;
  csch->getTransmitId() = nodeId();
  int frames = endFrame - startFrame;

  assert ( frames > 0);
  //compute the up flow
  //the hop of node i in the scheduling tree
  std::vector<int> hops = topology()->getHops();
  //the node's parent in the scheduling tree
  std::vector<int> parent = topology()->getParent();
  //the node's request slot
  std::vector<int> reqSlot;

  //std::vector<int> traSlot;
  //std::vector<int> dst;
  
  reqSlot.resize(topology()->numNodes());

  //traSlot.resize(topology_->numNodes());
  //dst.resize(topology_->numNodes());
  
  for(unsigned i = 0; i < reqSlot.size() ; ++i) {
    reqSlot[i] = 0;
    //tranSlot[i] = 0;
  }
  int totalUpByte = 0;
  for(unsigned i = 0; i < message.size() ; ++i) {
    WimshMshCsch *childCsch = message[i];
    std::list<WimshMshCsch::FlowEntry*> &flow = childCsch->getFlowEntries();
    std::list<WimshMshCsch::FlowEntry*>::iterator it;
    for(it = flow.begin(); it != flow.end(); ++it) {
      totalUpByte += ((*it)->upFlow) * hops[(*it)->id];
      reqSlot[(*it)->id] += (*it)->upFlow;
    }
  }


  //compute the down flow
  int totalDownByte = bwmanager_->ReadyByte();

  int totalByte = totalUpByte + totalDownByte;
  //allocate the minislots
  //int NeededSlot = bytes2Slots(totalByte,false);
  int alpha = WimshPhyMib::alpha[0];
  // no preamble
  //int NeededSlot = 1 + (( totalByte - 1) / alpha) / phyMib_->symPerSlot();
  int NeededSlot = 0;
  for(unsigned i = 1; i < reqSlot.size(); ++i) {
  	if(reqSlot[i]) reqSlot[i] = 1 + ((reqSlot[i] -1) / alpha) / phyMib_->symPerSlot();
   	NeededSlot += reqSlot[i] * hops[i];
  }
  int avaliableSlot = frames * phyMib_->slotPerFrame();
  
  if(totalDownByte) NeededSlot += 1 + ((totalDownByte -1) / alpha) / phyMib_->symPerSlot();
  
  std::queue<int> allocSeq;
  double scale;
  if(NeededSlot == 0) scale = 0;
  else scale = (double) avaliableSlot / NeededSlot;
  if(scale - 1.0 > 1e-6) scale = 1;
    // from SS to BS
  int totalHops = topology_->totalHops();
  for(int i = totalHops; i > 0; --i)
    for(int j = 0; j < hops.size(); ++j)
      if(hops[j] == i)
	allocSeq.push(j);
  allocSeq.push(0);//bs
  int currentFrame = startFrame;
  int currentSlot = 0;
  while(!allocSeq.empty()) {
    int p = allocSeq.front();
    allocSeq.pop();
    int req =(int) ( reqSlot[p] * scale );
    while(p && req) {
      WimshMshCsch::FlowEntry* entry = new WimshMshCsch::FlowEntry;
      entry->id = p;
      entry->towardId = parent[p];
      entry->uchannel = 0;
      entry->uframe = currentFrame;
      entry->ustart = currentSlot;
      entry->ufrange = req;
      csch->add(entry);
      currentSlot += req;
      if(currentSlot >= phyMib_->slotPerFrame()) {
	currentFrame++;
	currentSlot -= phyMib_->slotPerFrame();
      }
      p = parent[p];
    }
  }
  //from BS to SS
  bwmanager_->allocSlot(csch,currentFrame,currentSlot,scale);
  
  std::vector<int> id;
  std::vector<int> tranSlot;
  std::vector<int> dst;
  
  for(int i = 0;i < message.size(); ++i) {
    WimshMshCsch *cschChild = message[i];
    std::list<WimshMshCsch::FlowEntry*> &flow = cschChild->getFlowEntries();
    std::list<WimshMshCsch::FlowEntry*>::iterator it;
    for(it = flow.begin();it != flow.end(); it++) {
	unsigned j;
	if ((*it)->downFlow == 0) continue;
      	for(j = 0;j < tranSlot.size(); ++j) {
		if( (*it)->id == id[j] && (*it)->towardId == dst[j]) {
		//if((*it)->id == id[j]) {
	  		tranSlot[j] += (*it)->downFlow;
	  		break;
		}
     	}
	if(j == tranSlot.size() ) {
	  id.push_back((*it)->id);
	  tranSlot.push_back((*it)->downFlow);
	  dst.push_back((*it)->towardId);
      }
    }
  }
  for(unsigned i = 0;i < tranSlot.size(); ++i)
  	if(tranSlot[i]) tranSlot[i] = 1 + ((tranSlot[i] - 1) / alpha) / phyMib_->symPerSlot();
  
  for(unsigned i = 0;i < id.size(); ++i) {
    WimshMshCsch::FlowEntry* entry = new WimshMshCsch::FlowEntry;
    entry->id = id[i];
    entry->towardId = dst[i];
    entry->dchannel = 0;
    entry->dframe = currentFrame;
    entry->dstart = currentSlot;
    entry->dfrange = (int) (tranSlot[i] * scale);
    csch->add(entry);
    currentSlot += entry->dfrange;
    if(currentSlot >= phyMib_->slotPerFrame()) {
      currentFrame++;
      currentSlot -= phyMib_->slotPerFrame();
    }
  }

  message.clear();
  //hLastCsch_ = NOW;
  //do the work that BS shall allocate the minislot to SS


  bwmanager_->recvMshCsch(csch);
  
  setControlChannel(wimax::TX);
  burst->addMshCsch(csch);
  phy_[0]->sendBurst (burst);
}
*/

int
BSWimshMac::command(int argc,const char*const* argv) {
  /*
  if(argc == 3 && strcmp ( argv[1],"HRthreshold") == 0) {
    HRthreshold_ = atoi(argv[2]);
    return TCL_OK;
  }
  */
  return (WimshMac::command(argc,argv));
}


void 
SSWimshMac::recvMshCsch(WimshMshCsch* csch,double txtime) {
  assert ( initialized );
  if(csch->getFlag() && nodeId() == topology()->parent(csch->getTransmitId())) //request
    coordinator_->recvMshCsch(csch,txtime);
  bwmanager_->recvMshCsch(csch);
  //forwarding_->recvMshCsch(csch);
  //pass the grant to children of the node
  if(!csch->getFlag() && csch->getTransmitId() == topology()->parent(nodeId())) { 
    std::vector<int> parent = topology()->getParent();
    bool hasChild = false;
    for(unsigned i = 0; i < parent.size(); ++i) {
      if (parent[i] == nodeId() ) {
	  	hasChild = true;
		break;
      	}
    }
    if(hasChild) {
      setControlChannel(wimax::TX);
      WimshBurst *burst = new WimshBurst;
      WimshMshCsch *tcsch = new WimshMshCsch(*csch);
	tcsch->getTransmitId() = nodeId();
      burst->addMshCsch (tcsch);
      phy_[0]->sendBurst (burst);
    }
    //child.clear();
  }
}

void
SSWimshMac::opportunity(WimshMshCsch* csch)
{
  assert( initialized );


  bwmanager_->schedule(csch);

  
  //  hLastCsch_ = NOW;


  WimshBurst* burst = new WimshBurst;

  burst->addMshCsch (csch);
  

  setControlChannel (wimax::TX);

  phy_[0]->sendBurst (burst);
}

int 
SSWimshMac::command(int argc,const char*const* argv) {
  /*
  if(argc == 3 && strcmp ( argv[1],"HRthreshold") == 0) {
    HRthreshold_ = atoi(argv[2]);
    return TCL_OK;
  }
  */
  return (WimshMac::command(argc,argv));
}
