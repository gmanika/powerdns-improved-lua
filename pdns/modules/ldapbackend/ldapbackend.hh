/*
 *  PowerDNS LDAP Backend
 *  Copyright (C) 2003 Norbert Sendetzky <norbert@linuxnetworks.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */



#include <algorithm>
#include <sstream>
#include <utility>
#include <string>
#include <ldap.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <pdns/dns.hh>
#include <pdns/utility.hh>
#include <pdns/dnspacket.hh>
#include <pdns/dnsbackend.hh>
#include <pdns/ueberbackend.hh>
#include <pdns/ahuexception.hh>
#include <pdns/arguments.hh>
#include <pdns/logger.hh>
#include "powerldap.hh"


#ifndef LDAPBACKEND_HH
#define LDAPBACKEND_HH

using namespace std;



static string backendname="[LdapBackend]";

static char* attrany[] = {
	"dNSTTL",
	"aRecord",
	"nSRecord",
	"cNAMERecord",
	"pTRRecord",
	"mXRecord",
	"tXTRecord",
	"rPRecord",
	"aAAARecord",
	"lOCRecord",
	"nAPTRRecord",
	"aXFRRecord",
	NULL
};



class LdapBackend : public DNSBackend
{

private:

	int m_msgid;
	u_int32_t m_ttl;
	QType m_qtype;
	string m_qname;
	PowerLDAP* m_pldap;
	PowerLDAP::sentry_t m_result;

public:

	LdapBackend( const string &suffix="" );
	~LdapBackend();

	void lookup( const QType &qtype, const string &qdomain, DNSPacket *p=0, int zoneid=-1 );
	bool list( int domain_id );
	bool get( DNSResourceRecord &rr );
};

#endif /* LDAPBACKEND_HH */
