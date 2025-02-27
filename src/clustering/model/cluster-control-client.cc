/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2015 University of Athens (UOA)
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
 * Author:  - Lampros Katsikas <lkatsikas@di.uoa.gr>
 *          - Konstantinos Chatzikokolakis <kchatzi@di.uoa.gr>
 */

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/packet.h"
#include "ns3/address.h"
#include "ns3/pointer.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket.h"
#include "ns3/address-utils.h"
#include "ns3/socket-factory.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/internet-module.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/packet-socket-address.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/random-variable-stream.h"

#include "cluster-control-client.h"
#include "propagation-control-header.h"
#include "kde.h"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <limits>

#include "meta-data.h"

// #define CLUSTER_CONTROL_CLIENT_DEBUG


namespace ns3 {

bool ClusterControlClient::DISABLE_STARTINGNODE;

static const std::string ClusterStatusName[ClusterControlClient::CLUSTER_STATES] =
		{ "CLUSTER_INITIALIZATION", "CLUSTER_HEAD_ELECTION",
				"CLUSTER_FORMATION", "CLUSTER_UPDATE", "EXCHANGE_DISTRO_MAP", "DECIDE_PROPAGATION_PARAM",
				"PROPAGATION_READY", "PROPAGATION_RUNNING", "PROPAGATION_COMPLETE" };

static const std::string & ToString(ClusterControlClient::NodeStatus status) {
	return ClusterStatusName[status];
}

static const std::string IncidentName[ClusterSap::INCIDENT_STATES] = {
		"EMERGENCY_EVENT", "NOTIFICATION_EVENT" };

static const std::string & ToString(ClusterSap::IncidentType incidentType) {
	return IncidentName[incidentType];
}

static const std::string DegreeName[ClusterSap::DEGREE_STATES] = { "STANDALONE",
		"CH", "CM" };

static const std::string & ToString(ClusterSap::NodeDegree nodeDegree) {
	return DegreeName[nodeDegree];
}

NS_LOG_COMPONENT_DEFINE("ClusterControlClient");
NS_OBJECT_ENSURE_REGISTERED(ClusterControlClient);

TypeId ClusterControlClient::GetTypeId(void) {
	static TypeId tid =
			TypeId("ns3::ClusterControlClient").SetParent<Application>().AddConstructor<
					ClusterControlClient>().AddAttribute("ListeningLocal",
					"The Address on which to Bind the rx socket.",
					AddressValue(),
					MakeAddressAccessor(&ClusterControlClient::m_peerListening),
					MakeAddressChecker()).AddAttribute("ProtocolListeningLocal",
					"The type id of the protocol to use for the rx socket.",
					TypeIdValue(UdpSocketFactory::GetTypeId()),
					MakeTypeIdAccessor(&ClusterControlClient::m_tidListening),
					MakeTypeIdChecker()).AddTraceSource("RxLocal",
					"A packet has been received",
					MakeTraceSourceAccessor(&ClusterControlClient::m_rxTrace))

			.AddAttribute("IncidentWindow", "The incident time window",
					DoubleValue(4),
					MakeDoubleAccessor(&ClusterControlClient::m_incidentWindow),
					MakeDoubleChecker<double>()).AddAttribute(
					"ClusterTimeMetric", "The maximun size of the TDMA window",
					DoubleValue(0.5),
					MakeDoubleAccessor(
							&ClusterControlClient::m_clusterTimeMetric),
					MakeDoubleChecker<double>()).AddAttribute("MinimumTdmaSlot",
					"The maximun size of the TDMA window", DoubleValue(0.001),
					MakeDoubleAccessor(
							&ClusterControlClient::m_minimumTdmaSlot),
					MakeDoubleChecker<double>()).AddAttribute("MaxUes",
					"The maximun size of ues permitted", UintegerValue(100),
					MakeUintegerAccessor(&ClusterControlClient::m_maxUes),
					MakeUintegerChecker<uint32_t>(1)).AddAttribute("PacketSize",
					"The size of packets sent in on state", UintegerValue(512),
					MakeUintegerAccessor(&ClusterControlClient::m_pktSize),
					MakeUintegerChecker<uint32_t>(1)).AddAttribute("TimeWindow",
					"The time to wait between packets", DoubleValue(1.0),
					MakeDoubleAccessor(&ClusterControlClient::m_timeWindow),
					MakeDoubleChecker<double>()).AddAttribute("Interval",
					"The time to wait between packets", TimeValue(Seconds(0.3)),
					MakeTimeAccessor(&ClusterControlClient::m_interval),
					MakeTimeChecker()).AddAttribute("SendingLocal",
					"The address of the destination", AddressValue(),
					MakeAddressAccessor(&ClusterControlClient::m_peer),
					MakeAddressChecker()).AddAttribute("ProtocolSendingLocal",
					"The type of protocol for the tx socket.",
					TypeIdValue(UdpSocketFactory::GetTypeId()),
					MakeTypeIdAccessor(&ClusterControlClient::m_tid),
					MakeTypeIdChecker()).AddAttribute("MobilityModel",
					"The mobility model of the node.", PointerValue(),
					MakePointerAccessor(&ClusterControlClient::m_mobilityModel),
					MakePointerChecker<MobilityModel>()).AddTraceSource(
					"TxLocal", "A new packet is created and is sent",
					MakeTraceSourceAccessor(&ClusterControlClient::m_txTrace)).AddTraceSource(
					"Status", "Status chenged",
					MakeTraceSourceAccessor(&ClusterControlClient::m_statusTrace),
					"ns3::V2ClusterControlClient::StatusTraceCallback")
					.AddAttribute ("ClusteringStartTime", "Time at which the application will start",
                   TimeValue (Seconds (0.0)),
                   MakeTimeAccessor (&ClusterControlClient::m_clusteringStartTime),
                   MakeTimeChecker ())
				   .AddAttribute ("ClusteringStopTime", "Time at which the application will stop",
                   TimeValue (TimeStep (0)),
                   MakeTimeAccessor (&ClusterControlClient::m_clusteringStopTime),
                   MakeTimeChecker ());
	return tid;
}

// Public Members
ClusterControlClient::ClusterControlClient() {
	NS_LOG_FUNCTION(this);

	m_socket = 0;
	m_socketIncident = 0;
	m_socketListening = 0;

	m_overalDelay = 0;
	m_sentCounter = 0;
	m_recvCounter = 0;
	m_changesCounter = 0;
	m_incidentCounter = 0;
	m_formationCounter = 0;

	m_sendEvent = EventId();
	m_chElectionEvent = EventId();

}

ClusterControlClient::~ClusterControlClient() {
	NS_LOG_FUNCTION(this);
	if(m_currentMobility.isStartingNode) std::cout << "starting_node:" << m_currentMobility.imsi << std::endl;
	if(m_currentMobility.degree == ClusterSap::CH && false){
		std::cout << "[" << m_currentMobility.imsi << "] " << ToString(m_currentMobility.degree) << ", "
				<< m_currentMobility.clusterId << ", " << ToString(m_status)
				<< ", sent:" << m_sentCounter << " times, recv:" << m_recvCounter
				<< " times, propagation direction:" << m_propagationDirection
				<< " Self Start Time:" << m_propagationStartTime.GetSeconds()
				<< std::boolalpha
				<< " IsStartingNode:" << m_currentMobility.isStartingNode;
		if(m_currentMobility.isStartingNode || m_currentMobility.degree == ClusterSap::CH){
			std::cout << " First Start Time:" << m_firstPropagationStartingTime.GetSeconds();
		}
		std::cout << std::endl;

		if(m_currentMobility.degree == ClusterSap::CH || m_currentMobility.degree == ClusterSap::STANDALONE){
			std::cout << "neighbor cluster list" << std::endl;
			for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
					m_neighborClusterList.begin(); it != m_neighborClusterList.end(); ++it) {
				uint64_t id = it->first;
				ClusterSap::NeighborInfo node = it->second;
				std::cout << " * key: " << id << " clusterId: " << node.clusterId << " Degree:" << ToString (node.degree) << " Addr:";
				node.address.GetLocal().Print(std::cout);
				std::cout << std::endl;
	//			std::cout << " ts: " << node.ts.GetSeconds () << " DistroMap: "
	//					<< std::boolalpha << (m_neighborDistroMap.find(id) != m_neighborDistroMap.end())
	//					<< " ack: " << (m_ackDistroMap.find(id)->second) << std::endl;
			}


			std::cout << "cluster list" << std::endl;
			for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
					m_clusterList.begin(); it != m_clusterList.end(); ++it) {
				uint64_t id = it->first;
				ClusterSap::NeighborInfo node = it->second;
				std::cout << " * key: " << id << " clusterId: " << node.clusterId << " Degree:" << ToString (node.degree) << " Addr:";
				node.address.GetLocal().Print(std::cout);
				std::cout << " propagation direction:" << m_propagationDirection
						<< std::boolalpha << " IsStartingNode:" << node.isStartingNode;
				std::cout << std::endl;
			}

		std::cout << std::endl;
		}
	}

	m_socket = 0;
	m_socketIncident = 0;
	m_socketListening = 0;

	m_overalDelay = 0;
	m_sentCounter = 0;
	m_recvCounter = 0;
	m_changesCounter = 0;
	m_incidentCounter = 0;
	m_formationCounter = 0;

}

void ClusterControlClient::PrintStatistics(std::ostream &os) {
	if (m_incidentCounter == 0) {
		m_incidentCounter = 1;       // Avoid division with zero
	}

	os << "***********************" << std::endl << "  - Cluster Metrics -  "
			<< std::endl << "Node:" << m_currentMobility.imsi
			<< " Sent overal: " << m_sentCounter << " Packets." << std::endl
			<< " Formation Messages: " << m_formationCounter << std::endl
			<< " Status Changes: " << m_changesCounter << std::endl
			<< "-----------------------" << std::endl
			<< "  - Insident Metrics -  " << std::endl
			<< "Mean delay of incidents delivered: "
			<< (double) m_overalDelay / m_incidentCounter << std::endl
			<< "***********************" << std::endl;
}

// Protected Members
void ClusterControlClient::DoDispose(void) {
	NS_LOG_FUNCTION(this);

	m_socket = 0;
	m_socketListening = 0;

	m_clusteringStartEvent.Cancel ();
	m_clusteringStopEvent.Cancel ();

	// chain up
	Application::DoDispose();
}

void
ClusterControlClient::DoInitialize (void)
{
	Application::DoInitialize();
	AcquireMobilityInfo();
	if (m_clusteringStartTime != TimeStep (0))
    {
		m_clusteringStartEvent = Simulator::Schedule (m_clusteringStartTime, &ClusterControlClient::StartClustering, this);
    }
	if (m_clusteringStopTime != TimeStep (0))
    {
      m_clusteringStopEvent = Simulator::Schedule (m_clusteringStopTime, &ClusterControlClient::StopClustering, this);
    }
}

void ClusterControlClient::StartApplication(void) {
	NS_LOG_FUNCTION(this);
	m_status = ClusterControlClient::CLUSTER_INITIALIZATION;

	// Create the socket if not already
	if (!m_socket) {
		m_socket = Socket::CreateSocket(GetNode(), m_tid);
		if (Inet6SocketAddress::IsMatchingType(m_peer)) {
			m_socket->Bind6();
		} else if (InetSocketAddress::IsMatchingType(m_peer)
				|| PacketSocketAddress::IsMatchingType(m_peer)) {
			m_socket->Bind();
		}
		m_socket->Connect(m_peer);
		m_socket->SetAllowBroadcast(true);
		m_socket->ShutdownRecv();

		m_socket->SetConnectCallback(
				MakeCallback(&ClusterControlClient::ConnectionSucceeded, this),
				MakeCallback(&ClusterControlClient::ConnectionFailed, this));
	}

	if (m_maxUes > 10000) {
		NS_FATAL_ERROR("Error: Maximum number of ues is 100.");
	}

	MetaData::GetInstance().RegisterInstance(GetNode()->GetId(), this);

	StartListeningLocal();
}

void ClusterControlClient::StartListeningLocal(void) // Called at time specified by Start
		{
	NS_LOG_FUNCTION(this);

	m_clusterList.clear();
	m_neighborList.clear();
	// Create the socket if not already
	if (!m_socketListening) {
		m_socketListening = Socket::CreateSocket(GetNode(), m_tidListening);
		m_socketListening->Bind(m_peerListening);
		m_socketListening->Listen();
		m_socketListening->ShutdownSend();
		if (addressUtils::IsMulticast(m_peerListening)) {
			Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(
					m_socketListening);
			if (udpSocket) {
				// equivalent to setsockopt (MCAST_JOIN_GROUP)
				udpSocket->MulticastJoinGroup(0, m_peerListening);
			} else {
				NS_FATAL_ERROR("Error: joining multicast on a non-UDP socket");
			}
		}
	}

	m_socketListening->SetRecvCallback(
			MakeCallback(&ClusterControlClient::HandleRead, this));
	m_socketListening->SetAcceptCallback(
			MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
			MakeCallback(&ClusterControlClient::HandleAccept, this));
	m_socketListening->SetCloseCallbacks(
			MakeCallback(&ClusterControlClient::HandlePeerClose, this),
			MakeCallback(&ClusterControlClient::HandlePeerError, this));

	m_socketListeningInterCH = Socket::CreateSocket (GetNode(),UdpSocketFactory::GetTypeId ());
    InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), 50000);
    m_socketListeningInterCH->Bind(local);
    m_socketListeningInterCH->SetRecvCallback(MakeCallback (&ClusterControlClient::HandleReadInterCluster, this));
}

void ClusterControlClient::ConnectSocketInterCH(void){
	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator itSearch =	m_neighborClusterList.begin(); itSearch != m_neighborClusterList.end(); ++itSearch) {
		uint64_t id = itSearch->first;
		ClusterSap::NeighborInfo neighborCH = itSearch->second;

		// connect UDP socket to CH
		Ptr<Socket> socket;
		socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId ());
		socket->Bind();

		std::map<uint64_t, Ptr<Socket>>::iterator it = m_neighborClustersSocket.find(id);
		if (it == m_neighborClustersSocket.end()) {
			m_neighborClustersSocket.insert(std::map<uint64_t, Ptr<Socket>>::value_type(id, socket));
		}
		else {
			it->second = socket;
		}

		socket->SetConnectCallback(
				MakeCallback(&ClusterControlClient::ConnectionSucceeded, this),
				MakeCallback(&ClusterControlClient::ConnectionFailed, this));

		socket->SetCloseCallbacks(
				MakeCallback(&ClusterControlClient::ConnectionClosed, this),
				MakeCallback(&ClusterControlClient::ConnectionClosedWithError, this));


		// socket->Connect(neighborCH.address.GetLocal());
		uint16_t controlPort = 50000;
		InetSocketAddress addr(neighborCH.address.GetLocal(), controlPort);
		socket->Connect(addr);
		socket->ShutdownRecv();
	}
}

void ClusterControlClient::DisconnectSocketInterCH(void) {

}

void ClusterControlClient::StopApplication(void) // Called at time specified by Stop
		{
	NS_LOG_FUNCTION(this);

	if (m_socket != 0) {
		m_socket->Close();
		m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket> >());
		m_socket = 0;
	} else {
		NS_LOG_WARN(
				"ClusterControlClient found null socket to close in StopApplication");
	}
	Simulator::Cancel(m_sendEvent);
	//Simulator::Cancel(m_sendIncidentEvent);
	StopListeningLocal();
	// PrintStatistics(std::cout);
}

void ClusterControlClient::StartClustering(void) {
	ScheduleTransmit(Seconds(m_timeWindow));
	AcquireMobilityInfo();
	m_neighborsListUpdateEvent = Simulator::Schedule(Seconds(m_minimumTdmaSlot * m_maxUes), &ClusterControlClient::UpdateNeighborList, this);
}

void ClusterControlClient::StopClustering(void) {
	Simulator::Cancel(m_neighborsListUpdateEvent);
	AcquireMobilityInfo();
	m_status = EXCHANGE_DISTRO_MAP;
	if(m_currentMobility.degree == ClusterSap::CH){
		ConnectSocketInterCH();
		UpdateDistroMap();
		ExchangeDistroMap();

		Simulator::Schedule(Seconds(1.0), &ClusterControlClient::DecidePropagationParam, this);
	}
	else if(m_currentMobility.degree == ClusterSap::STANDALONE && m_currentMobility.isStartingNode){
		Time delay = Seconds(5.0);
		m_firstPropagationStartingTime = Simulator::Now() + delay;
		m_propagationStartTime = m_firstPropagationStartingTime;
		m_propagationDirection = m_basePropagationDirection;
		ScheduleInterNodePropagation();
	}
}

void ClusterControlClient::ExchangeDistroMap (void){
	m_status = EXCHANGE_DISTRO_MAP;

	//std::cout << "ExchangeDistroMap " << m_currentMobility.imsi << " @ " << Simulator::Now().GetSeconds() << " sec" << std::endl;

	double time = 0;
	for (std::map<uint64_t, Ptr<Socket>>::iterator itSearch = m_neighborClustersSocket.begin(); itSearch != m_neighborClustersSocket.end(); ++itSearch) {
		uint64_t id = itSearch->first;
		Ptr<Socket> socket = itSearch->second;

		std::map<uint64_t, bool>::iterator it = m_ackDistroMap.find(id);
		if(it != m_ackDistroMap.end()){
			it->second = false;
		}
		else{
			m_ackDistroMap.insert(std::map<uint64_t, bool>::value_type(id, false));
		}
		it = m_ackDistroMap.find(id);

		// transmit DistroMap
		DistroMapHeader distroMapHeader;
		distroMapHeader.SetClusterId(m_currentMobility.imsi);
		distroMapHeader.SetDistroMap(m_distroMap);
		distroMapHeader.SetMobilityInfo(m_currentMobility);
		distroMapHeader.SetSeq(m_sentCounter);
		Ptr<Packet> packet = Create<Packet>(0);
		++m_sentCounter;

		packet->AddHeader(distroMapHeader);
		// Simulator::Schedule(Seconds(time), &ClusterControlClient::SendTo, this, id, packet, (bool*)&(it->second));
		time += (m_minimumTdmaSlot * (id + m_currentMobility.imsi));
	}
}

void ClusterControlClient::DecidePropagationParam(void){
	// std::cout << "DecidePropagationParam [" << m_currentMobility.imsi << "] " << std::endl;
	m_status = DECIDE_PROPAGATION_PARAM;
	// Stop Exchange DistroMap
	for (std::map<uint64_t, bool>::iterator it =
				m_ackDistroMap.begin(); it != m_ackDistroMap.end(); ++it) {
		if(it->second == false) {
			it->second = true;
		}
	}

	// complete distro map
	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
				m_neighborClusterList.begin(); it != m_neighborClusterList.end(); ++it) {
		uint64_t id = it->first;
		MetaData::DistroMap::iterator itDistro = m_neighborDistroMap.find(id);
		if(itDistro == m_neighborDistroMap.end()){
			MetaData::DistroMap *metaDistro = &MetaData::GetInstance().distroMap;
			MetaData::DistroMap::iterator itMetaDistro = metaDistro->find(id);
			if(itMetaDistro != metaDistro->end()){
				m_neighborDistroMap.insert(MetaData::DistroMap::value_type(id, itMetaDistro->second));
			}

			MetaData::ChInfoMap *chInfoMap = &MetaData::GetInstance().chInfo;
			MetaData::ChInfoMap::iterator itChInfo = chInfoMap->find(id);
			if(itChInfo != chInfoMap->end())
			{
				MetaData::ChInfoMap::iterator itCL = m_neighborClusterList.find(id);
				if(itCL != m_neighborClusterList.end()) {
					itCL->second = itChInfo->second;
				}
			}
		}
	}

	// calc propagation param if Cluster has StartingNode
	bool hasStartingNode = false;
	uint64_t startingNodeId = 0;
	for(std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it = m_clusterList.begin();
			it != m_clusterList.end(); ++it){
		uint64_t key = it->first;
		ClusterSap::NeighborInfo info = it->second;

		if(info.isStartingNode) {
			hasStartingNode = true;
			startingNodeId = key;
		}
	}
	if(m_currentMobility.isStartingNode){
		hasStartingNode = true;
		startingNodeId = m_currentMobility.imsi;
	}
	if(hasStartingNode == true){
		MetaData::PropagationVectorMap *vectorMap = &MetaData::GetInstance().basePropagationVector;
		MetaData::PropagationVectorMap::iterator it = vectorMap->find(startingNodeId);
		if(it != vectorMap->end()){
			Time delay = Seconds(5.0);
			m_firstPropagationStartingTime = Simulator::Now() + delay;
			m_firstPropagationStartNodeId = startingNodeId;

			TransmitPropagationDirection(startingNodeId, it->second); // First StartingNode's basePropagationDirection
		}
	}
}

void ClusterControlClient::TransmitPropagationDirection(uint64_t id, Vector propVector) {
	// checking Sending event
	for(std::vector<EventId>::iterator itEvent = m_sendingInterClusterPropagationEvent.begin();
			itEvent != m_sendingInterClusterPropagationEvent.end(); ++itEvent) {
		if(itEvent->IsRunning()){
			itEvent->Cancel();
		}
		else{
			//std::cout << "duplicated" << std::endl;
		}
	}

	// define income vector
	Vector income_ave = propVector;
	double income_velocity = std::sqrt( income_ave.x * income_ave.x + income_ave.y * income_ave.y );

	Vector startingPosition(0, 0, 0);
	std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it = m_clusterList.find(id);
	if(it != m_clusterList.end()){
		startingPosition = it->second.position;
	}
	else if( id == m_currentMobility.imsi ){
		startingPosition = m_currentMobility.position;
	}
	else{
		//std::cout << "Error!!" << std::endl;
		return;
	}

	// define outcome vector
	Vector outcome_sum;
	int outcome_num = 0;
//	Vector outcome_sum = income_ave;
//	int outcome_num = 1;

	Time sending_timeslot = Seconds(m_minimumTdmaSlot * m_maxUes);
	for(MetaData::DistroMap::iterator it = m_neighborDistroMap.begin();
			it != m_neighborDistroMap.end(); ++it){
		uint64_t key = it->first;
		std::vector<float> dist = it->second;

		Vector basePosition(0, 0, 0);
		std::map<uint64_t, ClusterSap::NeighborInfo>::iterator itCl = m_neighborClusterList.find(key);
		if(itCl != m_neighborClusterList.end()){
			basePosition = itCl->second.position;
		}
		else{
			//std::cout << "Error!!!" << std::endl; continue;
		}

		Vector candidate_pos(0, 0, 0);
		Vector candidate_outcome(0, 0, 0);
		bool candidate_found = false;
		double candidate_distance = std::numeric_limits<double>::max();
		for(std::vector<float>::iterator ite = dist.begin(); ite != dist.end(); ++ite) {
			float value = *ite;

			if(value > 1.0) {
				int index = std::distance(dist.begin(), ite);
				double x = (( index % Constants::DISTRO_MAP_SIZE ) - (int)(Constants::DISTRO_MAP_SIZE/2)) * Constants::DISTRO_MAP_SCALE + basePosition.x;
				double y = (( (index - index % Constants::DISTRO_MAP_SIZE ) / Constants::DISTRO_MAP_SIZE ) - (int)(Constants::DISTRO_MAP_SIZE/2)) * Constants::DISTRO_MAP_SCALE + basePosition.y;
				double dx = x - startingPosition.x;
				double dy = y - startingPosition.y;

				if(IsInSector(startingPosition, Vector(x,y,0), income_ave, 100.0, Constants::PROPAGATION_THETA)){
					double distance = std::sqrt(dx*dx + dy*dy);
					// std::cout << key << "(" << x << ", " << y << ") distance:" << distance << " horizontal:" << horizontal << " vertical:" << vertical << std::endl;
					candidate_found = true;
					if(candidate_distance > distance) {
						candidate_distance = distance;
						candidate_pos = Vector(x, y, 0);
						candidate_outcome = Vector(income_velocity * dx/distance, income_velocity * dy/distance, 0);
					}
				}
			}
		}

		if(candidate_found){
			// Sending packet Next Cluster (candidate_outcome, candidate_pos, starting_pos, starting_time)
			ClusterSap::InterClusterPropagationInfo info;
			info.startingTime = m_firstPropagationStartingTime;
			info.source = startingPosition;
			info.distination = candidate_pos;
			info.direction = candidate_outcome;

			InterClusterPropagationHeader header;
			header.SetSeq(m_sentCounter);
			header.SetClusterId(m_currentMobility.clusterId);
			header.SetInterClusterInfo(info);

			Ptr<Packet> packet = Create<Packet>(0);
			++m_sentCounter;
			packet->AddHeader(header);

			std::map<uint64_t, bool>::iterator it_ack = m_ackInterClusterPropagation.find(key);
			if(it_ack == m_ackInterClusterPropagation.end()){
				m_ackInterClusterPropagation.insert(std::map<uint64_t, bool>::value_type(key, false));
			}
			else{
				it_ack->second = false;
			}
			it_ack = m_ackInterClusterPropagation.find(key);
			EventId eventId = Simulator::Schedule(sending_timeslot, &ClusterControlClient::SendTo, this, key, packet, (bool*)&(it_ack->second));
			m_sendingInterClusterPropagationEvent.push_back(eventId);
			sending_timeslot = sending_timeslot + Seconds(m_minimumTdmaSlot * 50);

			//std::cout << id <<"@"<< m_currentMobility.imsi << " PROPAGATE TO " << key << " Inter cluster " << std::endl;

			outcome_sum = outcome_sum + candidate_outcome;
			outcome_num++;
		}
	}

	double abs = std::sqrt(outcome_sum.x * outcome_sum.x + outcome_sum.y * outcome_sum.y);
	m_propagationDirection = Vector(income_velocity * outcome_sum.x / abs, income_velocity * outcome_sum.y / abs, 0.0);

	// transmit intraPropagationInfo
	if(m_clusterList.size() > 0){
		ScheduleTransmit(sending_timeslot);
		m_sendingInterClusterPropagationEvent.push_back(m_sendEvent);
	}
	// if CH is starting node
	if(id == m_currentMobility.imsi && (!DISABLE_STARTINGNODE || m_currentMobility.isStartingNode))
	{
		// m_currentMobility.isStartingNode = true;
		m_propagationStartTime = m_firstPropagationStartingTime;
		// Ready running
		ScheduleInterNodePropagation();
	}
}

uint64_t ClusterControlClient::FindNodeByPosition(Vector pos){
	// initialize self position
	Vector ch_pos = m_currentMobility.position;
	double ch_dx = pos.x - ch_pos.x;
	double ch_dy = pos.y - ch_pos.y;
	double ch_dz = pos.z - ch_pos.z;

	uint64_t id = m_currentMobility.imsi;
	double distance = ch_dx * ch_dx + ch_dy * ch_dy + ch_dz * ch_dz;

	for(std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it = m_clusterList.begin(); it != m_clusterList.end(); ++it) {
		Vector p = it->second.position;
		double dx = pos.x - p.x;
		double dy = pos.y - p.y;
		double dz = pos.z - p.z;
		double dist = dx*dx + dy*dy + dz*dz;

		if(distance > dist){
			distance = dist;
			id = it->first;
		}
	}
	return id;
}

Time ClusterControlClient::CalcPropagationDelay(Vector source, Vector destination, Vector direction){
	double a_ =  direction.x;  //  d;
	double b_ =  direction.y;  // -c;
	double c_ = -direction.y;  // -b;
	double d_ =  direction.x;  //  a;
	double verocity = a_ * a_ + b_ * b_;
	Vector delta = destination - source;

	double delta_horizontal = (a_*delta.x + b_*delta.y) / verocity; // dalta_horizontal
	//double delta_vertical   = (c_*delta.x + d_*delta.y) /verocity;  // delta_vertical

	Time delta_time = Seconds(std::fabs(delta_horizontal));
	// std::cout << "[DELAY]: " << delta_time.GetSeconds() << " sec" << std::endl;
	return delta_time;
}

bool ClusterControlClient::IsInSector(Vector source, Vector destination, Vector direction, double radius, double theta)
{
	double a_ =  direction.x;  //  d;
	double b_ =  direction.y;  // -c;
	double c_ = -direction.y;  // -b;
	double d_ =  direction.x;  //  a;
	double absol = a_ * a_ + b_ * b_;
	Vector delta = destination - source;

	double dx = (a_*delta.x + b_*delta.y) / absol; // dalta_horizontal
	double dy   = (c_*delta.x + d_*delta.y) / absol;  // delta_vertical

	double ex = std::cos(theta/2);
	double ey = std::sin(theta/2);
	double sx = std::cos(-theta/2);
	double sy = std::sin(-theta/2);

	if(delta.x * delta.x + delta.y * delta.y > radius*radius) {
		return false;
	}

	if(sx * ey - ex * sy > 0) {
		if(sx * dy - dx * sy < 0){
			return false;
		}
		if(ex * dy - dx * ey > 0){
			return false;
		}
		return true;
	}
	else{
		if(sx * dy - dx * sy >= 0){
			return true;
		}
		if(ex * dy - dx * ey <= 0){
			return true;
		}
		return false;
	}
}

void ClusterControlClient::ScheduleInterNodePropagation(void) {
	m_status = PROPAGATION_READY;
	if (Simulator::Now() > m_propagationStartTime) {
		return;
	}
	else {
		if(m_interNodePropagationEvent.IsRunning()) {
			m_interNodePropagationEvent.Cancel();
		}
		// std::cout << "StartProp Scheduled" << m_currentMobility.imsi << " @ " << m_propagationStartTime.GetSeconds() << "sec" << std::endl;

		if(!Constants::REVERSE_PROPAGATION)
		{
			m_interNodePropagationEvent = Simulator::Schedule(m_propagationStartTime - Simulator::Now(), &ClusterControlClient::StartNodePropagation, this);
		}
		else
		{
			m_interNodePropagationEvent = Simulator::Schedule(Seconds(0.1), &ClusterControlClient::StartNodePropagation, this); // reverse
		}
	}
}

void ClusterControlClient::StartNodePropagation(void) {
	Time running_time = Seconds(1.5);
	m_status = PROPAGATION_RUNNING;
	// std::cout << "StartProp Running  " << m_currentMobility.imsi << " @ " << Simulator::Now().GetSeconds() << "sec" << std::endl;
	// std::cout << "direction:" << m_propagationDirection << std::endl;

	Send();

	if(!Constants::REVERSE_PROPAGATION)
	{
		Simulator::Schedule(running_time, &ClusterControlClient::StopNodePropagation, this);
	}
	else
	{
		// reverse
		Time reversedTime = Now() - m_propagationStartTime;
		while(reversedTime < 0){
			reversedTime += Seconds(20.0);
		}
		reversedTime += Seconds(3.0); // offset
		std::cout << m_currentMobility.imsi << " <- " << reversedTime.GetSeconds() << "sec" << std::endl;
		Simulator::Schedule(reversedTime, &ClusterControlClient::ActivateNode, this);
	}
}

void ClusterControlClient::StopNodePropagation(void) {
	m_status = PROPAGATION_COMPLETE;
	// std::cout << "[" << m_currentMobility.imsi << "] Finite!" << std::endl;
}

void ClusterControlClient::ActivateNode(){
	Time running_time = Seconds(1.0);
	m_status = ACTIVE;
	m_interNodePropagationEvent = Simulator::Schedule(running_time, &ClusterControlClient::InactivateNode, this);
}

void ClusterControlClient::InactivateNode(){
	Time running_time = Seconds(19.0);
	m_status = PROPAGATION_COMPLETE;
	m_interNodePropagationEvent = Simulator::Schedule(running_time, &ClusterControlClient::ActivateNode, this);
}

void ClusterControlClient::SendTo(uint64_t id, Ptr<Packet> packet, bool *ack)
{
	std::map<uint64_t, Ptr<Socket>>::iterator it = m_neighborClustersSocket.find(id);
	if(ack == 0 || *ack == false){
		if(it != m_neighborClustersSocket.end()){
			Ptr<Socket> socket = it->second;
			// socket->Send(packet);
			MetaData::GetInstance().Call(id, packet);
			if(ack != 0 && *ack == false){
//				std::cout << "[RETRY] SendTo " << id << " from " << m_currentMobility.imsi << std::endl;
				Simulator::Schedule(Seconds(m_minimumTdmaSlot * 1000), &ClusterControlClient::SendTo, this, id, packet, ack);
			}
		}
	}
}

void ClusterControlClient::StopListeningLocal(void) // Called at time specified by Stop
{
	NS_LOG_FUNCTION(this);
	if (m_socketListening) {
		m_socketListening->Close();
		m_socketListening->SetRecvCallback(
				MakeNullCallback<void, Ptr<Socket> >());
		m_socketListening = 0;
	}
}

Ptr<Socket> ClusterControlClient::GetListeningSocket(void) const {
	NS_LOG_FUNCTION(this);
	return m_socketListening;
}

Ptr<Socket> ClusterControlClient::GetSocket(void) const {
	NS_LOG_FUNCTION(this);
	return m_socket;
}

void ClusterControlClient::SetClusteringStartTime(Time start){
	m_clusteringStartTime = start;
}

void ClusterControlClient::SetClusteringStopTime(Time stop){
	m_clusteringStopTime = stop;
}

const ClusterSap::NeighborInfo ClusterControlClient::GetCurrentMobility() const {
	ClusterSap::NeighborInfo info = m_currentMobility;
	info.ts = Simulator::Now();
	info.imsi = this->GetNode()->GetId();
	info.position = m_mobilityModel->GetPosition();
	const ClusterSap::NeighborInfo const_info = info;
	return const_info;
}

const ClusterControlClient::NodeStatus ClusterControlClient::GetNodeStatus() const {
	return m_status;
}

void ClusterControlClient::SetStartingNode(bool isStartingNode) {
	m_currentMobility.isStartingNode = isStartingNode;
}

void ClusterControlClient::SetBasePropagationDirection(Vector vector){
	m_basePropagationDirection = vector;
	MetaData::PropagationVectorMap *propMap = &MetaData::GetInstance().basePropagationVector;
	MetaData::PropagationVectorMap::iterator it = propMap->find(GetNode()->GetId());
	if( it == propMap->end() ) {
		propMap->insert(MetaData::PropagationVectorMap::value_type(GetNode()->GetId(), vector));
	}
	else{
		it->second = vector;
	}
}

const Vector ClusterControlClient::GetPropagationDirection(void) const {
	return m_propagationDirection;
}

// Private Members
void ClusterControlClient::HandleRead(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
	Ptr<Packet> packet;
	Address from;
	while ((packet = socket->RecvFrom(from))) {
		if (packet->GetSize() == 0) { //EOF
			break;
		}

		ClusterSap::NeighborInfo prev_mobility = m_currentMobility;

		PacketMetadata::ItemIterator metadataIterator = packet->BeginItem();
		PacketMetadata::Item item;

		while (metadataIterator.HasNext()) {
			++m_recvCounter;
			item = metadataIterator.Next();

			// ClusterInfoHeader
			if (item.tid.GetName() == "ns3::ClusterInfoHeader") {

				ClusterSap::ClusterSap::NeighborInfo otherInfo;
				ClusterInfoHeader clusterInfo;
				packet->RemoveHeader(clusterInfo);
				otherInfo = clusterInfo.GetMobilityInfo();

				//!< Update 2rStable and cluster List
				std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it2r = m_neighborList.find(otherInfo.imsi);

				// Range check
				Vector v1 = m_currentMobility.position;
				Vector v2 = otherInfo.position;
				double dx = v1.x - v2.x;
				double dy = v1.y - v2.y;
				double dz = v1.z - v2.z;
				double range = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));

				if (range >= Constants::OMNI_RANGE){
					continue;
				}
				if (it2r == m_neighborList.end()) {
					m_neighborList.insert(std::map<uint64_t, ClusterSap::NeighborInfo>::value_type(otherInfo.imsi, otherInfo));
				} else {
					it2r->second = otherInfo;
				}

				if(m_status == ClusterControlClient::CLUSTER_INITIALIZATION){
					if(otherInfo.degree == ClusterSap::CH && m_currentMobility.degree == ClusterSap::STANDALONE){
						m_status = ClusterControlClient::CLUSTER_UPDATE;
						m_currentMobility.degree = ClusterSap::CM;
						m_currentMobility.clusterId = otherInfo.clusterId;
						m_currentMobility.chAddress = otherInfo.address;
						ScheduleTransmit(Seconds((m_timeWindow)));
					}
				}
				// ClusterInfoHeader @ CLUSTER_UPDATE
				if (m_status == ClusterControlClient::CLUSTER_UPDATE || m_status == ClusterControlClient::CLUSTER_HEAD_ELECTION ) {
					if (m_currentMobility.degree == ClusterSap::CH || m_currentMobility.degree == ClusterSap::CM) {
						if (otherInfo.clusterId == m_currentMobility.imsi) {
							std::map<uint64_t, ClusterSap::NeighborInfo>::iterator itc = m_clusterList.find(otherInfo.imsi);
							if (itc == m_clusterList.end()) {
								m_clusterList.insert(
										std::map<uint64_t, ClusterSap::NeighborInfo>::value_type(otherInfo.imsi, otherInfo));
							}
							else {
								//!< Update UE Info
								itc->second = otherInfo;
							}
						}
						else {
							//!< Check Cluster Merge
							if (m_clusterList.size() == 0) {
								uint64_t potentialCH = MergeCheck();
								if (m_neighborList.count(potentialCH) > 0 && potentialCH != UINT64_MAX) {
									ClusterSap::NeighborInfo potential = m_neighborList.find(potentialCH)->second;

									if (m_currentMobility.imsi < potential.imsi) {
										NS_LOG_DEBUG("[HandleRead] => Node:" << m_currentMobility.imsi << " - merge with node:" << potential.imsi);
										m_currentMobility.degree = ClusterSap::CM;
										m_currentMobility.clusterId = potential.imsi;
										m_changesCounter++;

										//RemoveIncidentSocket();
										//CreateIncidentSocket(from);
									}
								}
							}
						}
					}
					else if (m_currentMobility.degree == ClusterSap::STANDALONE) {
						uint64_t potentialCH = MergeCheck();
						if (m_neighborList.count(potentialCH) > 0 && potentialCH != UINT64_MAX) {
							ClusterSap::NeighborInfo potential =
									m_neighborList.find(potentialCH)->second;

							NS_LOG_DEBUG(
									"[HandleRead] => Node:" << m_currentMobility.imsi << " - Attach to new CH node:" << potential.imsi);
							m_currentMobility.degree = ClusterSap::CM;
							m_currentMobility.clusterId = potential.imsi;
							m_changesCounter++;

							//RemoveIncidentSocket();
							//CreateIncidentSocket(from);
						} else {
							NS_LOG_DEBUG(
									"[HandleRead] => To Become new CH: " << m_currentMobility.imsi);

							NS_LOG_DEBUG("Node Status: " << ToString(m_status));

#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
							std::cout << "id: " << m_currentMobility.imsi << " become CH in HandleRead()@CLUSTER_UPDATE" << std::endl;
#endif

							m_currentMobility.degree = ClusterSap::CH;
							m_currentMobility.clusterId = m_currentMobility.imsi;
							m_changesCounter++;
							Simulator::Schedule(Seconds(0.), &ClusterControlClient::UpdateNeighbors, this);
						}
					}
				}

				// Update NeighborClusterList
				std::map<uint64_t, ClusterSap::NeighborInfo>::iterator itNC = m_neighborClusterList.find(otherInfo.clusterId);
				if(m_currentMobility.clusterId != otherInfo.clusterId &&
						(otherInfo.degree == ClusterSap::CH || otherInfo.degree == ClusterSap::CM)){
					ClusterSap::NeighborInfo neighborCluster;
					neighborCluster.imsi = otherInfo.clusterId;
					neighborCluster.clusterId = otherInfo.clusterId;
					neighborCluster.position = otherInfo.position;
					neighborCluster.address = otherInfo.chAddress;
					neighborCluster.chAddress = otherInfo.chAddress;
					neighborCluster.degree = ClusterSap::CH;
					neighborCluster.ts = Simulator::Now();
					if (itNC == m_neighborClusterList.end()) {
						m_neighborClusterList.insert(std::map<uint64_t, ClusterSap::NeighborInfo>::value_type(neighborCluster.imsi, neighborCluster));
					} else {
						itNC->second = neighborCluster;
					}
				}
			}

			else if (item.tid.GetName() == "ns3::InitiateClusterHeader") {
				//!< Parse InitiateClusterHeader Info
				InitiateClusterHeader initiateCluster;
				packet->RemoveHeader(initiateCluster);
				ClusterSap::NeighborInfo chInfo = initiateCluster.GetMobilityInfo();

				if (m_status == ClusterControlClient::CLUSTER_INITIALIZATION ) {
					m_status = ClusterControlClient::CLUSTER_HEAD_ELECTION;
					// Range check
					Vector v1 = m_currentMobility.position;
					Vector v2 = chInfo.position;
					double dx = v1.x - v2.x;
					double dy = v1.y - v2.y;
					double dz = v1.z - v2.z;
					double range = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));

					if (range >= Constants::OMNI_RANGE){
						continue;
					}

#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
					std::cout << "id: " << m_currentMobility.imsi << " become CH in HandleRead()@InitiateCluster " << m_currentMobility.clusterId << std::endl;
#endif
					std::map<uint64_t, ClusterSap::NeighborInfo>::iterator foundIt = m_neighborList.find(initiateCluster.GetClusterId());
					if ((foundIt != m_neighborList.end())) {
						foundIt->second = chInfo;

						m_status = ClusterControlClient::CLUSTER_UPDATE;
						m_currentMobility.degree = ClusterSap::CM;
						m_currentMobility.clusterId = chInfo.clusterId;
						m_currentMobility.chAddress = chInfo.address;
						ScheduleTransmit(Seconds((m_timeWindow)));

					} else {
						/// Not in 2rStableList
						m_status = ClusterControlClient::CLUSTER_INITIALIZATION;
					}
				}
				else {
					//!< Process only the first request and ignore the rest
					NS_LOG_DEBUG("[HandleRead] => NodeId: " << m_currentMobility.imsi << " Ignore further requests for CH suitability...");
				}

				// Update NeighborClusterList
				std::map<uint64_t, ClusterSap::NeighborInfo>::iterator itNC = m_neighborClusterList.find(chInfo.clusterId);
				if(m_currentMobility.clusterId != chInfo.clusterId &&
						(chInfo.degree == ClusterSap::CH || chInfo.degree == ClusterSap::CM)){
					ClusterSap::NeighborInfo neighborCluster;
					neighborCluster.imsi = chInfo.clusterId;
					neighborCluster.clusterId = chInfo.clusterId;
					neighborCluster.position = chInfo.position;
					neighborCluster.address = chInfo.chAddress;
					neighborCluster.chAddress = chInfo.chAddress;
					neighborCluster.degree = ClusterSap::CH;
					neighborCluster.ts = Simulator::Now();
					if (itNC == m_neighborClusterList.end()) {
						m_neighborClusterList.insert(std::map<uint64_t, ClusterSap::NeighborInfo>::value_type(neighborCluster.imsi, neighborCluster));
					} else {
						itNC->second = neighborCluster;
					}
				}
			}

			else if (item.tid.GetName() == "ns3::FormClusterHeader") {

//				NS_ASSERT((m_status != ClusterControlClient::CLUSTER_HEAD_ELECTION)
//								|| (m_status != ClusterControlClient::CLUSTER_FORMATION));

				FormClusterHeader formCluster;
				packet->RemoveHeader(formCluster);

				//!< Update 2rStable and cluster List
				ClusterSap::NeighborInfo otherInfo = formCluster.GetMobilityInfo();

				// Range check
				Vector v1 = m_currentMobility.position;
				Vector v2 = otherInfo.position;
				double dx = v1.x - v2.x;
				double dy = v1.y - v2.y;
				double dz = v1.z - v2.z;
				double range = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));

				if (range >= Constants::OMNI_RANGE){
					continue;
				}

				std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it2r = m_neighborList.find(otherInfo.imsi);
				if (it2r == m_neighborList.end()) {
					NS_LOG_DEBUG("[HandleRead] => Node:" << m_currentMobility.imsi << " Insert packet:" << otherInfo.imsi);
					m_neighborList.insert(std::map<uint64_t, ClusterSap::NeighborInfo>::value_type(otherInfo.imsi, otherInfo));
				}
				else {
					it2r->second = otherInfo;
				}

				std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it = m_neighborList.find(formCluster.GetMobilityInfo().clusterId);

				if (it != m_neighborList.end()) {
					if (m_status == ClusterControlClient::CLUSTER_HEAD_ELECTION) {

						//m_status = ClusterControlClient::CLUSTER_FORMATION;
						m_chElectionEvent.Cancel();
						NS_LOG_DEBUG("[HandleRead] => NodeId: " << m_currentMobility.imsi << " connected to cluster: " << formCluster.GetMobilityInfo().clusterId);

						//!< Apply received CH info
						m_status = ClusterControlClient::CLUSTER_UPDATE;
						m_currentMobility.degree = ClusterSap::CM;
						m_currentMobility.clusterId = formCluster.GetMobilityInfo().clusterId;
						m_currentMobility.chAddress = formCluster.GetMobilityInfo().address;
						ScheduleTransmit(Seconds((m_timeWindow)));

//						double updateTime = (int) Simulator::Now().GetSeconds() + 1.5;
//							Simulator::Schedule(Seconds(updateTime - Simulator::Now().GetSeconds()),
//									&ClusterControlClient::UpdateNeighborList, this);

						//CreateIncidentSocket(from);
					}
					else if (m_status == ClusterControlClient::CLUSTER_FORMATION) {
						NS_LOG_DEBUG("[HandleRead] => NodeId: " << m_currentMobility.imsi << " Node is already a Cluster Member.");
					}
				} else {
					/// Not in 2rStableList
#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
					std::cout << "id:" << m_currentMobility.imsi << " Get FormClusterHeader from "
							<< formCluster.GetMobilityInfo().imsi << ", "
							<< formCluster.GetMobilityInfo().clusterId << ", but it's unknown node" << std::endl;
#endif
				}
			}

			else if (item.tid.GetName() == "ns3::IncidentEventHeader") {

				IncidentEventHeader incidentHeader;
				packet->RemoveHeader(incidentHeader);

				if (m_incidentTimestamp.GetSeconds()
						== incidentHeader.GetTs().GetSeconds()) {

					/// Calculate Delay
					m_overalDelay += Simulator::Now().GetSeconds()
							- m_incidentTimestamp.GetSeconds();
					NS_LOG_UNCOND(
							"Node: " << m_currentMobility.imsi << " received back IncidentEventHeader:" << ". Incident Delay is: "
							<< Simulator::Now ().GetSeconds () - m_incidentTimestamp.GetSeconds () << " Seconds");
					NS_LOG_UNCOND(
							"--------------------------------------------------------------------------------------------");
				}

				if ((m_currentMobility.degree == ClusterSap::CH)
						&& (m_currentMobility.clusterId
								== incidentHeader.GetIncidentInfo().clusterId)) {

					//!< Broadcast Incident to Cluster Members
					Ptr<Packet> packet = Create<Packet>(0);
					packet->AddHeader(incidentHeader);

					m_socket->Send(packet);
					if (InetSocketAddress::IsMatchingType(m_peer)) {
						NS_LOG_UNCOND(
								"[Send] Broadcast Incident Message from " << m_currentMobility.imsi << "=> At time "
								<< Simulator::Now ().GetSeconds () <<" sent " << packet->GetSize () << " bytes to "
								<< InetSocketAddress::ConvertFrom(m_peer).GetIpv4 () << " port "
								<< InetSocketAddress::ConvertFrom (m_peer).GetPort () << " - Event Type is:"
								<< ToString (incidentHeader.GetIncidentInfo ().incidentType));
					} else if (Inet6SocketAddress::IsMatchingType(m_peer)) {
						NS_LOG_UNCOND(
								"[Send] Broadcast Incident Message from " << m_currentMobility.imsi << "=> At time "
								<< Simulator::Now ().GetSeconds () <<" sent " << packet->GetSize () << " bytes to "
								<< packet->GetSize () << " bytes to " << Inet6SocketAddress::ConvertFrom(m_peer).GetIpv6 ()
								<< " port " << Inet6SocketAddress::ConvertFrom (m_peer).GetPort ()
								<< " - Event Type is:" << ToString (incidentHeader.GetIncidentInfo ().incidentType));
					}
				}
			}
			else if (item.tid.GetName() == "ns3::NeighborClusterInfoHeader") {
				ClusterSap::ClusterSap::NeighborInfo chInfo;
				NeighborClusterInfoHeader neighborClusterInfo;
				packet->RemoveHeader(neighborClusterInfo);
				chInfo = neighborClusterInfo.GetMobilityInfo();
				uint64_t clusterId = neighborClusterInfo.GetClusterId();

				std::map<uint64_t, ClusterSap::NeighborInfo>::iterator itNC = m_neighborClusterList.find(chInfo.imsi);

				if(m_currentMobility.degree == ClusterSap::CH && clusterId == m_currentMobility.imsi && chInfo.imsi != m_currentMobility.imsi){
					if (itNC == m_neighborClusterList.end()) {
						m_neighborClusterList.insert(std::map<uint64_t, ClusterSap::NeighborInfo>::value_type(chInfo.imsi, chInfo));
					} else {
						itNC->second =chInfo;
					}
				}
			}

			else if (item.tid.GetName() == "ns3::IntraClusterPropagationHeader") {
				IntraClusterPropagationHeader header;
				packet->RemoveHeader(header);

				ClusterSap::IntraClusterPropagationInfo info = header.GetIntraClusterInfo();
				uint64_t clusterId = header.GetClusterId();

				if(m_currentMobility.clusterId == clusterId && m_currentMobility.degree == ClusterSap::CM) {
					m_propagationDirection = info.direction;
					// std::cout << "[IntraClusterPropagationHeader received " << m_currentMobility.imsi << "] " << std::endl;
					if(m_currentMobility.imsi == info.startingNode &&
							(!DISABLE_STARTINGNODE || m_currentMobility.isStartingNode) &&
							(m_status == EXCHANGE_DISTRO_MAP ||
							m_status == PROPAGATION_READY)) {
						if(m_propagationStartTime >= info.startingTime &&
								 info.startingTime > Simulator::Now()) {
							if(m_propagationStartTime == Time::Max()){
								// std::cout << "[Starting Node " << m_currentMobility.imsi << "] Starting Time is " << info.startingTime.GetSeconds() << " sec (now:" << Simulator::Now().GetSeconds() << " sec)" << std::endl;
							}
							m_propagationStartTime = info.startingTime;
							m_firstPropagationStartingTime = info.startingTime;
						}
						else {
//							std::cout << "Starting Time is rejected : " << info.startingTime.GetSeconds()
//									<< " sec > " << Simulator::Now().GetSeconds() << " sec or "
//									<< m_propagationStartTime.GetSeconds() << " sec" << std::endl;
						}
						// m_currentMobility.isStartingNode = true;

						// Ready running
						std::cout << "schedule accepted@" << m_currentMobility.imsi << std::endl;
						ScheduleInterNodePropagation();
					}
					else{
						//std::cout << "schedule rejected@" << m_currentMobility.imsi << std::endl;
					}
				}
			}

			else if (item.tid.GetName() == "ns3::InterNodePropagationHeader") {
				InterNodePropagationHeader header;
				packet->RemoveHeader(header);

				ClusterSap::InterNodePropagationInfo info = header.GetInterNodeInfo();
				uint64_t clusterId = header.GetClusterId();

				if(IsInSector(info.position, m_currentMobility.position, info.direction, Constants::BF_RANGE, Constants::PROPAGATION_THETA)) {
					if(m_propagationDirection.x == 0 && m_propagationDirection.y == 0){
						// std::cout << "[WARN] Alternate direction setted " << m_currentMobility.imsi << "@" << m_currentMobility.clusterId << std::endl;

						double speed = std::sqrt(info.direction.x * info.direction.x + info.direction.y * info.direction.y);
						Vector come_direction(info.direction.x / speed, info.direction.y / speed, 0.0);

						Vector delta = m_currentMobility.position - info.position;
						double delta_abs = std::sqrt(delta.x*delta.x + delta.y*delta.y);

						Vector out_direction;
						out_direction.x = (come_direction.x + delta.x / delta_abs);
						out_direction.y = (come_direction.y + delta.y / delta_abs);
						double out_abs =std::sqrt(out_direction.x*out_direction.x + out_direction.y*out_direction.y);

						m_propagationDirection.x = speed * out_direction.x / out_abs;
						m_propagationDirection.y = speed * out_direction.y / out_abs;
					}
					// Time delay = CalcPropagationDelay(info.position, m_currentMobility.position, m_propagationDirection);
					double distance = CalculateDistance(info.position, m_currentMobility.position);
					double velocity = std::sqrt(m_propagationDirection.x * m_propagationDirection.x + m_propagationDirection.y * m_propagationDirection.y);
					Time delay = Seconds(distance/velocity);
					Time newTime = info.startingTime + delay;
					if(newTime < m_propagationStartTime &&
							Simulator::Now() < m_propagationStartTime){
						m_propagationStartTime = newTime;
						ScheduleInterNodePropagation();
						//std::cout << "recv " << m_currentMobility.imsi << std::endl;
					}
					else{
						//std::cout << "reject " << m_currentMobility.imsi << std::endl;
					}
				}
			}

			m_rxTrace(packet, from);
		}

		// Callback for Update anim
		if ((prev_mobility.clusterId != m_currentMobility.clusterId)
				|| (prev_mobility.degree != m_currentMobility.degree)) {
#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
			std::cout << "cluster changed@receive : " << m_currentMobility.imsi
					<< ", " << prev_mobility.clusterId << " -> "
					<< m_currentMobility.clusterId << ", "
					<< ToString(prev_mobility.degree) << " -> "
					<< ToString(m_currentMobility.degree) << std::endl;
#endif
			m_statusTrace(this);
		}
	}
}

void ClusterControlClient::HandleReadInterCluster(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
	Ptr<Packet> packet;
	Address from;
	while ((packet = socket->RecvFrom(from))) {
		if (packet->GetSize() == 0) { //EOF
			break;
		}
		HandlePacketInterCluster(packet);
	}
}

void ClusterControlClient::HandlePacketInterCluster(Ptr<Packet> packet) {
	PacketMetadata::ItemIterator metadataIterator = packet->BeginItem();
	PacketMetadata::Item item;

	while (metadataIterator.HasNext()) {
		++m_recvCounter;
		item = metadataIterator.Next();

		// DistroMapHeader
		if (item.tid.GetName() == "ns3::DistroMapHeader") {
			float otherDistroMap[Constants::DISTRO_MAP_SIZE * Constants::DISTRO_MAP_SIZE];
			DistroMapHeader distroMapHeader;
			packet->RemoveHeader(distroMapHeader);
			uint64_t id = distroMapHeader.GetClusterId();
			ClusterSap::NeighborInfo otherInfo = distroMapHeader.GetMobilityInfo();
			distroMapHeader.GetDistroMap(otherDistroMap);

			// convert distro map to vector
			std::vector<float> otherDistroMapV =
					std::vector<float>(otherDistroMap, otherDistroMap + (sizeof(otherDistroMap) / sizeof(otherDistroMap[0]) ));

			std::map<uint64_t, ClusterSap::NeighborInfo>::iterator itNC = m_neighborClusterList.find(id);
			if (itNC == m_neighborClusterList.end()) {
				m_neighborClusterList.insert(std::map<uint64_t, ClusterSap::NeighborInfo>::value_type(id, otherInfo));
			} else {
				itNC->second = otherInfo;
			}

			std::map<uint64_t, std::vector<float>>::iterator itND = m_neighborDistroMap.find(id);
			if (itND == m_neighborDistroMap.end()) {
				m_neighborDistroMap.insert(std::map<uint64_t, std::vector<float>>::value_type(id, otherDistroMapV));
			}
			else {
				itND->second = otherDistroMapV;
			}
//			std::cout << m_currentMobility.imsi << " receive distro map from " << id << std::endl;

			// return ack
			AckHeader ack;
			ack.SetSeq(m_sentCounter);
			ack.SetAckTypeId(DistroMapHeader::GetTypeId());
			ack.SetClusterId(m_currentMobility.clusterId);

			Ptr<Packet> packet = Create<Packet>(0);
			++m_sentCounter;
			packet->AddHeader(ack);
			Simulator::Schedule(Seconds(.0), &ClusterControlClient::SendTo, this, id, packet, (bool*)0);
		}

		else if (item.tid.GetName() == "ns3::InterClusterPropagationHeader") {
			InterClusterPropagationHeader header;
			packet->RemoveHeader(header);
			uint64_t clusterId = header.GetClusterId();
			ClusterSap::InterClusterPropagationInfo info = header.GetInterClusterInfo();

//			std::cout << "RECEIVE InterClusterPropagationHeader @ " << m_currentMobility.imsi << " from " << clusterId << std::endl;

			uint64_t candidateId = FindNodeByPosition(info.distination);
			Vector candidatePos;
			if(candidateId == m_currentMobility.imsi) {
				candidatePos = m_currentMobility.position;
			}
			else {
				candidatePos = m_clusterList.find(candidateId)->second.position;
			}
			Time delay = CalcPropagationDelay(info.source, candidatePos, info.direction);
			Time newTime;
//			if(!ClusterControlClient::DISABLE_STARTINGNODE){
				 newTime = info.startingTime + (delay * 1.3);
//			}
//			else{
//				newTime = Time::Max(); // Disable StartingNode
//			}
			if(m_firstPropagationStartingTime > newTime) {
				m_firstPropagationStartingTime = newTime;
				m_firstPropagationStartNodeId = candidateId;
//				std::cout << "TRANSMIT PROPAGATION HEADER " << Simulator::Now().GetSeconds() << std::endl;
//				std::cout << "Prev: " << info.startingTime.GetSeconds()
//						<< ", Delay: " << delay.GetSeconds()
//						<< ", New Time: " << newTime.GetSeconds()
//						<< std::endl;
				TransmitPropagationDirection(candidateId, info.direction);
			}

			// return ack
			AckHeader ack;
			ack.SetSeq(m_sentCounter);
			ack.SetAckTypeId(InterClusterPropagationHeader::GetTypeId());
			ack.SetClusterId(m_currentMobility.clusterId);

			Ptr<Packet> packet = Create<Packet>(0);
			++m_sentCounter;
			packet->AddHeader(ack);
			Simulator::Schedule(Seconds(.0), &ClusterControlClient::SendTo, this, clusterId, packet, (bool*)0);
		}

		// AckHeader
		else if (item.tid.GetName() == "ns3::AckHeader") {
			AckHeader ackHeader;
			packet->RemoveHeader(ackHeader);
			uint64_t clusterId = ackHeader.GetClusterId();
			TypeId ackId = ackHeader.GetAckTypeId();

			if(ackId.GetName() == "ns3::DistroMapHeader") {
				m_ackDistroMap.find(clusterId)->second = true;
			}

			if(ackId.GetName() == "ns3::InterClusterPropagationHeader") {
				m_ackInterClusterPropagation.find(clusterId)->second = true;
			}
		}
	}
}

void ClusterControlClient::CreateIncidentSocket(Address from) {
	NS_LOG_FUNCTION(this);

	//!< Create p2p socket with ClusterHead for incident event transmission
	Ipv4Address chAddress = InetSocketAddress::ConvertFrom(from).GetIpv4();
	uint16_t chPort = InetSocketAddress::ConvertFrom(m_peer).GetPort();
	m_peerIncident = Address(InetSocketAddress(chAddress, chPort));

	// Create the socket if not already
	if (!m_socketIncident) {
		m_socketIncident = Socket::CreateSocket(GetNode(), m_tid);
		if (Inet6SocketAddress::IsMatchingType(m_peerIncident)) {
			m_socketIncident->Bind6();
		} else if (InetSocketAddress::IsMatchingType(m_peerIncident)
				|| PacketSocketAddress::IsMatchingType(m_peerIncident)) {
			m_socketIncident->Bind();
		}
		m_socketIncident->Connect(m_peerIncident);
		m_socketIncident->SetAllowBroadcast(false);
		m_socketIncident->ShutdownRecv();

		m_socketIncident->SetConnectCallback(
				MakeCallback(&ClusterControlClient::ConnectionCHSucceeded, this),
				MakeCallback(&ClusterControlClient::ConnectionCHFailed, this));
	}
}

void ClusterControlClient::RemoveIncidentSocket(void) {
	NS_LOG_FUNCTION(this);

	if (m_socketIncident != 0) {
		m_socketIncident->Close();
		m_socketIncident->SetRecvCallback(
				MakeNullCallback<void, Ptr<Socket> >());
		m_socketIncident = 0;
	} else {
		NS_LOG_WARN("m_socketIncident null socket to close...");
	}
}

void ClusterControlClient::ConnectionCHSucceeded(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
	NS_LOG_DEBUG("P2P Connection with CH Successful");
}

void ClusterControlClient::ConnectionCHFailed(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
	NS_FATAL_ERROR("Error: joining CH socket");
}

uint64_t ClusterControlClient::MergeCheck(void) {
	uint64_t id = UINT64_MAX;
//	double r = 100;              //!< transmition range
//	double rt = 0.0;            //!< Suitability metric for CH  selection

	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator itSearch =	m_neighborList.begin(); itSearch != m_neighborList.end();++itSearch) {
		ClusterSap::NeighborInfo node = itSearch->second;

		if (node.degree == ClusterSap::CH) {
//			rt = r - std::sqrt((m_currentMobility.position.x - node.position.x)	* (m_currentMobility.position.x	- node.position.x)
//									+ (m_currentMobility.position.y	- node.position.y) * (m_currentMobility.position.y - node.position.y));

			if (id == UINT64_MAX || itSearch->first > id) {
				id = itSearch->first;
			}
		}
	}
	return id;
}

void ClusterControlClient::AcquireMobilityInfo(void) {
	//!< Acquire current mobility stats
	m_currentMobility.ts = Simulator::Now();
	m_currentMobility.imsi = this->GetNode()->GetId();
	m_currentMobility.position = m_mobilityModel->GetPosition();
	m_currentMobility.address = this->GetNode()->GetObject<Ipv4> ()->GetAddress(1,0);
}

void ClusterControlClient::UpdateDistroMap(void){
	Vector ch_pos = m_currentMobility.position;
	std::array<float, 2> ch_pos_offset = {0.0, 0.0};
	std::vector<std::array<float, 2>> data = {ch_pos_offset};

	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it = m_clusterList.begin();
			it != m_clusterList.end(); it++) {
		uint64_t key = it->first;
		ClusterSap::NeighborInfo value = it->second;
		Vector position = value.position;
		std::array<float, 2> ar_pos = {(float)(position.x - ch_pos.x), (float)(position.y - ch_pos.y)};
		data.push_back(ar_pos);
//		std::cout << key << ", " ;
	}
//	std::cout << std::endl;

//	std::cout << "distromap of " << m_currentMobility.imsi << std::endl << std::fixed;
	if(data.size() > 1){
		std::array<float, 4> bandwidth = {0.1, 0.0, 0.0, 0.1};
		kdepp::Kde2d<std::array<float,2>> kernel(data, bandwidth);

		for(int i = 0; i < Constants::DISTRO_MAP_SIZE; i++){
			for(int j = 0; j < Constants::DISTRO_MAP_SIZE; j++){
				float offset = Constants::DISTRO_MAP_SCALE * (Constants::DISTRO_MAP_SIZE / 2);
				std::array<float, 2> sample_point = {Constants::DISTRO_MAP_SCALE * j - offset, Constants::DISTRO_MAP_SCALE * i - offset};

				m_distroMap[Constants::DISTRO_MAP_SIZE * i + j] = kernel.eval(sample_point);
//				std::cout << std::setprecision(5) << m_distroMap[Constants::DISTRO_MAP_SIZE * i + j] <<", ";
			}
//			std::cout << std::endl;
		}
//		std::cout << std::endl;
	}
	else{
		for(int i = 0; i < Constants::DISTRO_MAP_SIZE; i++){
			for(int j = 0; j < Constants::DISTRO_MAP_SIZE; j++){
				float offset = Constants::DISTRO_MAP_SCALE * (Constants::DISTRO_MAP_SIZE / 2);
				m_distroMap[Constants::DISTRO_MAP_SIZE * i + j] =
						Constants::DISTRO_MAP_SCALE * i - offset == 0 && Constants::DISTRO_MAP_SCALE * j - offset == 0
						? 1.0 : 0.0;
				//std::cout << std::setprecision(5) << m_distroMap[Constants::DISTRO_MAP_SIZE * i + j] << ", ";
			}
			//std::cout << std::endl;
		}
	}

	// register distro map
	std::vector<float> distroMapV =
						std::vector<float>(m_distroMap, m_distroMap + (sizeof(m_distroMap) / sizeof(m_distroMap[0]) ));
	MetaData::DistroMap *distroMap = &MetaData::GetInstance().distroMap;
	MetaData::DistroMap::iterator it = distroMap->find(m_currentMobility.imsi);
	if(it == distroMap->end()){
		distroMap->insert(MetaData::DistroMap::value_type(m_currentMobility.imsi, distroMapV));
	}
	else{
		it->second = distroMapV;
	}

	// register ch info
	MetaData::ChInfoMap *chInfo = &MetaData::GetInstance().chInfo;
	MetaData::ChInfoMap::iterator itChInfo = chInfo->find(m_currentMobility.imsi);
	if(itChInfo == chInfo->end()){
//		std::cout << "[" << m_currentMobility.imsi << "] Register ChInfo" << std::endl;
		chInfo->insert(MetaData::ChInfoMap::value_type(m_currentMobility.imsi, m_currentMobility));
	}
	else{
//		std::cout << "[" << m_currentMobility.imsi << "] Update ChInfo" << std::endl;
		itChInfo->second = m_currentMobility;
	}

}

void ClusterControlClient::FormCluster(void) {
	m_status = ClusterControlClient::CLUSTER_FORMATION;
	ScheduleTransmit(Seconds(0.));
}

void ClusterControlClient::StatusReport(void) {
	NS_LOG_UNCOND(
			"\n\n-----------------------------------------------------------------------------");
	NS_LOG_UNCOND(
			"[StatusReport] => At time " << Simulator::Now ().GetSeconds () << "s node ["<< m_currentMobility.imsi << "] is: " << ToString (m_currentMobility.degree)
			<< " in Cluster: " << m_currentMobility.clusterId << " having  ===> \n position: " << m_currentMobility.position
			<< "\n last packet sent:" << m_currentMobility.ts.GetSeconds () << "s" << "\n Neighbors: " << m_neighborList.size());
	NS_LOG_UNCOND(
			"----------------------------  2rStableList  ---------------------------------");
	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
			m_neighborList.begin(); it != m_neighborList.end(); ++it) {
		uint64_t id = it->first;
		ClusterSap::NeighborInfo node = it->second;
		NS_LOG_UNCOND(
				" * key: " << id << " clusterId: " << node.clusterId << " Degree:" << ToString (node.degree) << " Imsi:" << node.imsi << " Position:" << node.position
				<< " last packet sent:" << node.ts.GetSeconds() << "s");
	}

	NS_LOG_UNCOND(
			"-----------------------------  clusterList  ---------------------------------");
	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
			m_clusterList.begin(); it != m_clusterList.end(); ++it) {
		uint64_t id = it->first;
		ClusterSap::NeighborInfo node = it->second;
		NS_LOG_UNCOND(
				" * key: " << id << " clusterId: " << node.clusterId << " Degree:" << ToString (node.degree) << " Imsi:" << node.imsi << " Position:" << node.position);
	}
	NS_LOG_UNCOND(
	"-----------------------------  neighborClusterList  ---------------------------------");
	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
			m_neighborClusterList.begin(); it != m_neighborClusterList.end(); ++it) {
		uint64_t id = it->first;
		ClusterSap::NeighborInfo node = it->second;
		NS_LOG_UNCOND(
				" * key: " << id << " clusterId: " << node.clusterId << " Degree:" << ToString (node.degree) << " Imsi:" << node.imsi << " Position:" << node.position);
	}
}

void ClusterControlClient::HandleAccept(Ptr<Socket> s, const Address& from) {
	NS_LOG_FUNCTION(this << s << from);
	s->SetRecvCallback(MakeCallback(&ClusterControlClient::HandleRead, this));
}

void ClusterControlClient::HandlePeerClose(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
}

void ClusterControlClient::HandlePeerError(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
}

void ClusterControlClient::ScheduleTransmit(Time dt) {
	NS_LOG_FUNCTION(this << dt);

	if (!m_sendEvent.IsExpired()) {
#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
		std::cout << "avoid duplicated event " << m_currentMobility.imsi << " : not Expired "
				<< ToString(m_currentMobility.degree) << ", "
				<< ToString(m_status)
				<< ", event@" << (m_sendEvent.GetTs() / 1000000000.0) << std::endl; // for debug
#endif
	}
	else{
		m_sendEvent = Simulator::Schedule(dt, &ClusterControlClient::Send, this);
	}
	NS_LOG_DEBUG(
			"[ScheduleTransmit] => NodeId:" << m_currentMobility.imsi << " EventInfo:" << m_sendEvent.GetTs() << " status: " << ToString(m_status));

}

void ClusterControlClient::Send(void) {

	NS_LOG_FUNCTION(this);
	NS_LOG_DEBUG("[Send] => NodeId:" << m_currentMobility.imsi << " EventInfo:" << m_sendEvent.GetTs() << " status: " << ToString(m_status));

	// NS_ASSERT(m_sendEvent.IsExpired());

	ClusterSap::NeighborInfo prev_mobility = m_currentMobility;

	switch (m_status) {
	case ClusterControlClient::CLUSTER_INITIALIZATION: {
		AcquireMobilityInfo();
		ClusterInfoHeader clusterInfo;
		clusterInfo.SetSeq(m_sentCounter);
		clusterInfo.SetMobilityInfo(m_currentMobility);

		Ptr<Packet> packet = Create<Packet>(0);
		packet->AddHeader(clusterInfo);
		m_txTrace(packet);
		m_socket->Send(packet);
		++m_sentCounter;
		m_formationCounter++;

		Simulator::Schedule(Seconds(m_minimumTdmaSlot * m_maxUes),
				&ClusterControlClient::InitiateCluster, this);
		break;
	}
	case ClusterControlClient::CLUSTER_HEAD_ELECTION: {
		AcquireMobilityInfo();
		m_currentMobility.degree = ClusterSap::CH;
		m_currentMobility.clusterId = m_currentMobility.imsi;

		InitiateClusterHeader initiateCluster;
		initiateCluster.SetSeq(m_sentCounter);
		initiateCluster.SetClusterId(m_currentMobility.imsi);
		initiateCluster.SetMobilityInfo(m_currentMobility);

		Ptr<Packet> packet = Create<Packet>(0);
		packet->AddHeader(initiateCluster);
		m_txTrace(packet);
		if(PeekPointer(m_socket) <= 0) {
//			std::cout << m_currentMobility.imsi << " socket missing " << std::endl;
		}
		m_socket->Send(packet);
		++m_sentCounter;
		m_formationCounter++;

		m_status = ClusterControlClient::CLUSTER_UPDATE;
		ScheduleTransmit(Seconds(m_minimumTdmaSlot * m_maxUes));

		break;
	}
	case ClusterControlClient::CLUSTER_FORMATION: {
		AcquireMobilityInfo();
#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
		std::cout << "id: " << m_currentMobility.imsi << " become CH in Send()@CLUSTER_FORMATION" << std::endl;
#endif
		m_currentMobility.degree = ClusterSap::CH;
		m_currentMobility.clusterId = m_currentMobility.imsi;

		FormClusterHeader clusterInfo;
		clusterInfo.SetSeq(m_sentCounter);
		clusterInfo.SetMobilityInfo(m_currentMobility);

		Ptr<Packet> packet = Create<Packet>(0);
		packet->AddHeader(clusterInfo);
		m_txTrace(packet);
		m_socket->Send(packet);
		++m_sentCounter;
		m_formationCounter++;

		Simulator::Schedule(Seconds(0.), &ClusterControlClient::UpdateNeighbors, this);

		break;
	}
	case ClusterControlClient::CLUSTER_UPDATE: {
//		if (m_currentMobility.degree == ClusterSap::CH) {
//			StatusReport();
//		}
#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
		std::cout << "Send update from " << m_currentMobility.imsi
				<< "(" << ToString(m_currentMobility.degree) << ")"
				<< " at " << Simulator::Now ().GetSeconds () <<"s" << std::endl;
#endif

		AcquireMobilityInfo();
		ClusterInfoHeader clusterInfo;
		clusterInfo.SetSeq(m_sentCounter);
		clusterInfo.SetMobilityInfo(m_currentMobility);

		Ptr<Packet> packet = Create<Packet>(0);
		packet->AddHeader(clusterInfo);

		for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
			m_neighborClusterList.begin(); it != m_neighborClusterList.end(); ++it) {
			//uint64_t id = it->first;
			ClusterSap::NeighborInfo node = it->second;
			NeighborClusterInfoHeader neighborClusterInfo;
			neighborClusterInfo.SetSeq(m_sentCounter);
			neighborClusterInfo.SetClusterId(m_currentMobility.clusterId);
			neighborClusterInfo.SetMobilityInfo(node);

			if(packet->GetSize() + neighborClusterInfo.GetSerializedSize() > 2296){
				m_txTrace(packet);
				m_socket->Send(packet);
				++m_sentCounter;
				packet = Create<Packet>(0);
			}
			packet->AddHeader(neighborClusterInfo);
		}
		m_txTrace(packet);
		m_socket->Send(packet);
		++m_sentCounter;

		ScheduleTransmit(m_interval); // loop
		break;
	}
	case ClusterControlClient::DECIDE_PROPAGATION_PARAM: {
		if(m_currentMobility.degree == ClusterSap::CH) {
			ClusterSap::IntraClusterPropagationInfo info;
			info.startingNode = m_firstPropagationStartNodeId;
			info.startingTime = m_firstPropagationStartingTime;
			info.direction = m_propagationDirection;
			IntraClusterPropagationHeader header;
			header.SetClusterId(m_currentMobility.clusterId);
			header.SetIntraClusterInfo(info);
			header.SetSeq(m_sentCounter);

			Ptr<Packet> packet = Create<Packet>(0);
			packet->AddHeader(header);
			m_txTrace(packet);
			m_socket->Send(packet);
			++m_sentCounter;
			ScheduleTransmit(m_interval); // loop
		}
		break;
	}
	case ClusterControlClient::PROPAGATION_RUNNING: {
		ClusterSap::InterNodePropagationInfo info;
		info.startingTime = m_propagationStartTime;
		info.position = m_currentMobility.position;
		info.direction = m_propagationDirection;

		InterNodePropagationHeader header;
		header.SetClusterId(m_currentMobility.imsi);
		header.SetInterNodeInfo(info);
		header.SetSeq(m_sentCounter);

		Ptr<Packet> packet = Create<Packet>(0);
		packet->AddHeader(header);
		m_txTrace(packet);
		m_socket->Send(packet);
		++m_sentCounter;
		ScheduleTransmit(m_interval); // loop
	}
	default:
		NS_LOG_DEBUG(
				"[Send] => Default Case NodeId [Transmit] " << m_currentMobility.imsi << " - Current Status: " << ToString(m_status));
		break;
	}
	if ((prev_mobility.clusterId != m_currentMobility.clusterId)
			|| (prev_mobility.degree != m_currentMobility.degree)) {
#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
		std::cout << "cluster changed@send : " << m_currentMobility.imsi << ", "
				<< prev_mobility.clusterId << " -> "
				<< m_currentMobility.clusterId << ", "
				<< ToString(prev_mobility.degree) << " -> "
				<< ToString(m_currentMobility.degree) << std::endl;
#endif
		m_statusTrace(this);
	}
}

void ClusterControlClient::UpdateNeighbors(void) {
	m_status = ClusterControlClient::CLUSTER_UPDATE;
	ScheduleTransmit(m_interval);
}

void ClusterControlClient::InitiateCluster(void) {
	if (m_status == ClusterControlClient::CLUSTER_INITIALIZATION ) {
		if(m_currentMobility.clusterId == ClusterSap::CH || m_currentMobility.clusterId == ClusterSap::CM ){
			m_status = ClusterControlClient::CLUSTER_UPDATE;
			ScheduleTransmit(m_interval);
		}
		else{
			if (HasMaxId()) {
				// retry
#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
				 << "Initiating from " << m_currentMobility.imsi << " ...";
				 << " become CH? " << ToString(m_status) << std::endl;
#endif
				m_status = ClusterControlClient::CLUSTER_HEAD_ELECTION;
				ScheduleTransmit(Seconds(m_minimumTdmaSlot * m_maxUes));
			}
			else{
				Simulator::Schedule(Seconds(m_minimumTdmaSlot * m_maxUes), &ClusterControlClient::InitiateCluster, this);
			}
		}
	}
}

bool ClusterControlClient::HasMaxId(void) {

	uint64_t maxId = m_currentMobility.imsi;
	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
			m_neighborList.begin(); it != m_neighborList.end(); ++it) {
		ClusterSap::NeighborInfo value = it->second;
		if (value.imsi > maxId && value.degree != ClusterSap::CM) {
			maxId = value.imsi;
		}
	}
	if (maxId == m_currentMobility.imsi) {
		return true;
	}
	return false;
}

void ClusterControlClient::ConnectionSucceeded(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
//	std::cout << "connection Succeeded " << m_currentMobility.imsi << " to ";
	for (std::map<uint64_t, Ptr<Socket>>::iterator it = m_neighborClustersSocket.begin(); it != m_neighborClustersSocket.end(); ++it )
	{
		if (it->second == socket){
//			std::cout << it->first;
		}
	}
//	std::cout << std::endl;
}

void ClusterControlClient::ConnectionFailed(Ptr<Socket> socket) {
	NS_LOG_FUNCTION(this << socket);
//	std::cout << "connection Failed" << std::endl;
}

void ClusterControlClient::ConnectionClosed(Ptr<Socket> socket) {
//	std::cout << "connection Closed " << m_currentMobility.imsi << " to ";
	for (std::map<uint64_t, Ptr<Socket>>::iterator it = m_neighborClustersSocket.begin(); it != m_neighborClustersSocket.end(); ++it )
	{
		if (it->second == socket){
//			std::cout << it->first;
		}
	}
//	std::cout << std::endl;
}

void ClusterControlClient::ConnectionClosedWithError(Ptr<Socket> socket) {
//	std::cout << "connection Closed with error " << m_currentMobility.imsi << " to ";
	for (std::map<uint64_t, Ptr<Socket>>::iterator it = m_neighborClustersSocket.begin(); it != m_neighborClustersSocket.end(); ++it )
	{
		if (it->second == socket){
//			std::cout << it->first;
		}
	}
//	std::cout << " errno: " << socket->GetErrno() << std::endl;
}


void ClusterControlClient::UpdateNeighborList(void) {
	AcquireMobilityInfo();
	ClusterSap::NeighborInfo prev_mobility = m_currentMobility;

	bool hasCH = false;

	//!< Update Neighbor's List according to Timestamps
	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
			m_neighborList.begin(); it != m_neighborList.end();) {
		uint64_t key = it->first;
		ClusterSap::NeighborInfo value = it->second;

		if(m_currentMobility.clusterId == value.imsi &&
				m_currentMobility.clusterId == value.clusterId &&
				value.degree == ClusterSap::CH){
			hasCH = true;
		}

		// Remove Old CM (Node changed other cluster)
		if (m_clusterList.find(key) != m_clusterList.end() &&
				m_currentMobility.imsi != value.clusterId) {
			m_clusterList.erase(key);
		}

		// Update Neighbor Cluster List
		if (value.degree == ClusterSap::CH && m_currentMobility.clusterId != value.imsi){
			if (m_neighborClusterList.find(key) == m_neighborClusterList.end()){
				m_neighborClusterList.insert(std::map<uint64_t, ClusterSap::NeighborInfo>::value_type(value.imsi, value));
			}
		}
		else if (m_neighborClusterList.find(key) != m_neighborClusterList.end()){
			m_neighborClusterList.erase(key);
		}

		// timestamp check
		if (m_currentMobility.ts.GetSeconds() - value.ts.GetSeconds() > (double)(2.0 * m_interval.GetSeconds())) {
			m_neighborList.erase(it++);
			if (value.imsi == m_currentMobility.clusterId) {
				// Lost CH
				//!< go to STANDALONE State
				m_currentMobility.clusterId = 0;
				m_currentMobility.degree = ClusterSap::STANDALONE;
				NS_LOG_DEBUG(
						"[UpdateNeighborList] => Go to STANDALONE state: " << m_currentMobility.imsi);
				m_status = ClusterControlClient::CLUSTER_INITIALIZATION;
//				std::cout << "[" << m_currentMobility.imsi << "]  Initialization Prosessing " << (double)value.ts.GetSeconds() << std::endl;
			}

			if (m_neighborList.find(key) != m_neighborList.end()) {
				// Lost Neighbor
				m_neighborList.erase(key);
			}

			if (m_clusterList.find(key) != m_clusterList.end()) {
				// Lost CM
				m_clusterList.erase(key);
			}

			if ((m_neighborList.size() == 0)&& (m_currentMobility.degree != ClusterSap::CH)) {
				// become CH
#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
				std::cout << "id: " << m_currentMobility.imsi << " become CH in UpdateNeighborList() " << ToString(m_status) << std::endl;
#endif
				m_currentMobility.degree = ClusterSap::CH;
				m_currentMobility.clusterId = m_currentMobility.imsi;
				m_changesCounter++;

				ScheduleTransmit(Seconds(.0));
			}
		} else {
			++it;
		}

	}

	if(m_currentMobility.degree == ClusterSap::CM && !hasCH){
		// Lost CH
		//!< go to STANDALONE State
		m_currentMobility.clusterId = UINT64_MAX;
		m_currentMobility.degree = ClusterSap::STANDALONE;
	}

	//!< Update Neighbor Cluster List according to Timestamps
	for (std::map<uint64_t, ClusterSap::NeighborInfo>::iterator it =
			m_neighborClusterList.begin(); it != m_neighborClusterList.end();) {
		//uint64_t key = it->first;
		ClusterSap::NeighborInfo value = it->second;
		if (m_currentMobility.ts.GetSeconds() - value.ts.GetSeconds() > (double)(2.0 * m_interval.GetSeconds())) {
			m_neighborClusterList.erase(it++);
		}
		else {
			++it;
		}
	}


	if ((prev_mobility.clusterId != m_currentMobility.clusterId)
			|| (prev_mobility.degree != m_currentMobility.degree)) {
#ifdef CLUSTER_CONTROL_CLIENT_DEBUG
		std::cout << "cluster changed@updateNeighborList : " << m_currentMobility.imsi << ", "
				<< prev_mobility.clusterId << " -> "
				<< m_currentMobility.clusterId << ", "
				<< ToString(prev_mobility.degree) << " -> "
				<< ToString(m_currentMobility.degree) << std::endl;
#endif
		m_statusTrace(this);
	}

	if (m_neighborsListUpdateEvent.IsExpired()) {
		 m_neighborsListUpdateEvent = Simulator::Schedule(m_interval, &ClusterControlClient::UpdateNeighborList, this);
	}
}

void ClusterControlClient::ScheduleIncidentEvent(Time dt) {
	NS_LOG_FUNCTION(this << dt);
	m_sendIncidentEvent = Simulator::Schedule(dt,
			&ClusterControlClient::SendIncident, this);
}

void ClusterControlClient::SendIncident(void) {

	ClusterSap::IncidentInfo incidentInfo;
	incidentInfo.clusterId = m_currentMobility.clusterId;
	incidentInfo.incidentType = ClusterSap::EMERGENCY_EVENT;

	IncidentEventHeader incidentHeader;
	incidentHeader.SetIncidentInfo(incidentInfo);
	m_incidentTimestamp = incidentHeader.GetTs();

	Ptr<Packet> packet = Create<Packet>(0);
	packet->AddHeader(incidentHeader);

	if ((m_currentMobility.degree == ClusterSap::CH)
			|| (m_currentMobility.degree == ClusterSap::STANDALONE)) {

		//!< Broadcast Incident to Cluster Members directly
		m_socket->Send(packet);
		if (InetSocketAddress::IsMatchingType(m_peer)) {
			NS_LOG_UNCOND(
					"[Send] Broadcast Incident Message from " << m_currentMobility.imsi << "=> At time " << Simulator::Now ().GetSeconds () <<" sent " << packet->GetSize () << " bytes to "
					<< InetSocketAddress::ConvertFrom(m_peer).GetIpv4 () << " port " << InetSocketAddress::ConvertFrom (m_peer).GetPort ()
					<< " - Event Type is:" << ToString (incidentHeader.GetIncidentInfo ().incidentType));
		} else if (Inet6SocketAddress::IsMatchingType(m_peer)) {
			NS_LOG_UNCOND(
					"[Send] Broadcast Incident Message from " << m_currentMobility.imsi << "=> At time " << Simulator::Now ().GetSeconds () <<" sent " << packet->GetSize () << " bytes to "
					<< packet->GetSize () << " bytes to " << Inet6SocketAddress::ConvertFrom(m_peer).GetIpv6 () << " port " << Inet6SocketAddress::ConvertFrom (m_peer).GetPort ()
					<< " - Event Type is:" << ToString (incidentHeader.GetIncidentInfo ().incidentType));
		}
	}
	else {

		//!< Send Incident event to Cluster Head firstly
		m_socketIncident->Send(packet);
		m_incidentCounter++;
		if (InetSocketAddress::IsMatchingType(m_peerIncident)) {
			NS_LOG_UNCOND(
					"[Send] Incident Message => At time " << Simulator::Now ().GetSeconds () << "s node[IMSI] ["<< m_currentMobility.imsi <<"] sent " << packet->GetSize () << " bytes to "
					<< InetSocketAddress::ConvertFrom(m_peerIncident).GetIpv4 () << " port " << InetSocketAddress::ConvertFrom (m_peerIncident).GetPort ()
					<< " - Event Type is:" << ToString (incidentInfo.incidentType));
		} else if (Inet6SocketAddress::IsMatchingType(m_peerIncident)) {
			NS_LOG_INFO(
					"[Send] Incident Message => At time " << Simulator::Now ().GetSeconds () << "s node[IMSI] ["<< m_currentMobility.imsi <<"] sent " << packet->GetSize () << " bytes to "
					<< Inet6SocketAddress::ConvertFrom(m_peerIncident).GetIpv6 () << " port " << Inet6SocketAddress::ConvertFrom (m_peerIncident).GetPort ()
					<< " - Event Type is:" << ToString (incidentInfo.incidentType));
		}
	}

	//!< Schedule event generation in random time
	ScheduleIncidentEvent(Seconds(m_incidentWindow));

}

} // Namespace ns3
