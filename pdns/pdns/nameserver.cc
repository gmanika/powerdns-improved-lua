/*
    PowerDNS Versatile Database Driven Nameserver
    Copyright (C) 2002  PowerDNS.COM BV

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*
    Copyright (C) 2002  PowerDNS.COM BV

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
// $Id: nameserver.cc,v 1.1 2002/11/27 15:18:31 ahu Exp $ 
#include "utility.hh"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <string>
#include <sys/types.h>

#include "dns.hh"
#include "dnsbackend.hh"
#include "dnspacket.hh"
#include "nameserver.hh"
#include "distributor.hh"
#include "logger.hh"
#include "arguments.hh"
#include "statbag.hh"

extern StatBag S;

/** \mainpage 
    ahudns is a very versatile nameserver that can answer questions from different backends. To implement your
    own backend, see the documentation for the DNSBackend class.

    \section copyright Copyright and License
    AhuDNS is (C) 2002 PowerDNS BV.

    AhuDNS is NOT open source (yet), so treat this code as a trade secret. If it has been supplied to you for evaluation,
    this does not mean that you can deploy the code!

    \section overview High level overview

    AhuDNS is highly threaded, which means that several tasks each have their own process thread. Two of the pivotal
    threads are the qthread() and the athread().

    - The qthread() receives questions over the network (via the Nameserver class, which returns DNSPacket objects), and gives them to the Distributor.
    - The athread() waits on the Distributor to return answers, ready to send back over the network, again via UDPNameserver.

    The Distributor contains a configurable number of PacketHandler instances, each in its own thread, for connection pooling

    The PacketHandler implements the RFC1034 algorithm and converts question packets into DNSBackend queries.

    A DNSBackend is an entity that returns DNSResourceRecord objects in return to explicit questions for domains with a specified QType

    AhuDNS uses the UeberBackend as its DNSBackend. The UeberBackend by default has no DNSBackends within itself, those are loaded
    using the dynloader tool. This way DNSBackend implementations can be kept completely separate.

    If one or more DNSBackends are loaded, the UeberBackend fields the queries to all of them until one answers.

    \section TCP TCP Operations

    The TCP operation runs within a single thread called tcpreceiver(), that also queries the PacketHandler. 

    \section Cache Caching
 
    On its own, this setup is not suitable for high performance operations. A single DNS query can turn into many DNSBackend questions,
    each taking many miliseconds to complete. This is why the qthread() first checks the PacketCache to see if an answer is known to a packet
    asking this question. If so, the entire Distributor is shunted, and the answer is sent back *directly*, within a few microseconds.

    In turn, the athread() offers each outgoing packet to the PacketCache for possible inclusion.

    \section misc Miscellaneous
    Configuration details are available via the ArgvMap instance arg. Statistics are created by making calls to the StatBag object called S. 
    These statistics are made available via the UeberBackend on the same socket that is used for dynamic module commands.

    \section Main Main 
    The main() of AhuDNS can be found in receiver.cc - start reading there for further insights into the operation of the nameserver


*/

void UDPNameserver::bindIPv4()
{
  vector<string>locals;
  stringtok(locals,arg()["local-address"]," ,");

  if(locals.empty())
    throw AhuException("No local address specified");

  int s;
  for(vector<string>::const_iterator i=locals.begin();i!=locals.end();++i) {
    string localname(*i);
    struct sockaddr_in locala;

    s=socket(AF_INET,SOCK_DGRAM,0);
    if(s<0)
      throw AhuException("Unable to acquire a UDP socket: "+string(strerror(errno)));
  
    memset(&locala,0,sizeof(locala));
    locala.sin_family=AF_INET;

    if(localname=="0.0.0.0") {
      L<<Logger::Warning<<"It is advised to bind to explicit addresses with the --local-address option"<<endl;

      locala.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
      struct hostent *h=0;
      h=gethostbyname(localname.c_str());
      if(!h)
        throw AhuException("Unable to resolve local address"); 
    
      locala.sin_addr.s_addr=*(int*)h->h_addr;
    }

    locala.sin_port=htons(arg().asNum("local-port"));
    
    if(bind(s, (sockaddr*)&locala,sizeof(locala))<0) {
      L<<Logger::Error<<"binding to UDP socket: "<<strerror(errno)<<endl;
      throw AhuException("Unable to bind to UDP socket");
    }
    d_highfd=max(s,d_highfd);
    d_sockets.push_back(s);
    L<<Logger::Error<<"UDP server bound to "<<localname<<":"<<arg()["local-port"]<<endl;
    FD_SET(s, &d_rfds);
  }
}

void UDPNameserver::bindIPv6()
{
  // TODO: Add Windows ipv6 support.
#ifndef WIN32
  vector<string>locals;
  stringtok(locals,arg()["local-ipv6"]," ,");

  if(locals.empty())
    return;


  int s;
  for(vector<string>::const_iterator i=locals.begin();i!=locals.end();++i) {
    string localname(*i);
    struct sockaddr_in6 locala;

    s=socket(AF_INET6,SOCK_DGRAM,0);
    if(s<0)
      throw AhuException("Unable to acquire a UDPv6 socket: "+string(strerror(errno)));
  
    memset(&locala,0,sizeof(locala));
    locala.sin6_family=AF_INET6;

    if(localname=="::0") {
      L<<Logger::Warning<<"It is advised to bind to explicit addresses with the --local-ipv6 option"<<endl;
    }

    struct hostent *h;

    h=gethostbyname2(localname.c_str(),AF_INET6);

    if(!h)
      throw AhuException("Unable to resolve local IPv6 address"); 
    
    memcpy(&locala.sin6_addr.s6_addr,h->h_addr,16);

    locala.sin6_port=htons(arg().asNum("local-port"));
    
    if(bind(s, (sockaddr*)&locala,sizeof(locala))<0) {
      L<<Logger::Error<<"binding to UDP ipv6 socket: "<<strerror(errno)<<endl;
      throw AhuException("Unable to bind to UDP ipv6 socket");
    }
    d_highfd=max(s,d_highfd);
    d_sockets.push_back(s);
    L<<Logger::Error<<"UDPv6 server bound to "<<localname<<":"<<arg()["local-port"]<<endl;
    FD_SET(s, &d_rfds);
  }
#endif // WIN32
}

UDPNameserver::UDPNameserver()
{
  d_highfd=0;
  FD_ZERO(&d_rfds);  
  if(!arg()["local-address"].empty())
    bindIPv4();
  if(!arg()["local-ipv6"].empty())
    bindIPv6();

  if(arg()["local-address"].empty() && arg()["local-ipv6"].empty()) 
    L<<Logger::Critical<<"PDNS is deaf and mute! Not listening on any interfaces"<<endl;
    
}

void UDPNameserver::send(DNSPacket *p)
{
  const char *buffer=p->getData();
  DLOG(L<<Logger::Notice<<"Sending a packet to "<<inet_ntoa( reinterpret_cast< sockaddr_in * >(( p->remote ))->sin_addr)<<" ("<<p->len<<" octets)"<<endl);
  if(p->len>512) {
    p=new DNSPacket(*p);
    p->truncate(512);
    buffer=p->getData();
    if(sendto(p->getSocket(),buffer,p->len,0,(struct sockaddr *)(p->remote),p->d_socklen)<0)
      L<<Logger::Error<<"Error sending reply with sendto (socket="<<p->getSocket()<<"): "<<strerror(errno)<<endl;
    delete p;
  }
  else {

    if(sendto(p->getSocket(),buffer,p->len,0,(struct sockaddr *)(p->remote),p->d_socklen)<0)
      L<<Logger::Error<<"Error sending reply with sendto (socket="<<p->getSocket()<<"): "<<strerror(errno)<<endl;



  }
}
