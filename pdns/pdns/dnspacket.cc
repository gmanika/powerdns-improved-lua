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
// $Id: dnspacket.cc,v 1.6 2002/12/17 16:50:30 ahu Exp $
#include "utility.hh"
#include <cstdio>

#include <cstdlib>
#include <sys/types.h>

#include <iostream>  

#include <string>
#include <errno.h>

#include <algorithm>

#include "dns.hh"
#include "dnsbackend.hh"
#include "ahuexception.hh"
#include "dnspacket.hh"
#include "logger.hh"
#include "arguments.hh"


DNSPacket::DNSPacket() 
{
  d_wrapped=false;
  d_compress=true;
}


string DNSPacket::getString()
{
  return stringbuffer;
}


string DNSPacket::getRemote() const
{
  return sockAddrToString((struct sockaddr_in *)remote, d_socklen);
}



// Make s lowercase:
void lowercase(string& s) {
  for(unsigned int i = 0; i < s.length(); i++)
    s[i] = tolower(s[i]);
}

void DNSPacket::trim()
{
  rrs.clear();
  qdomain=""; // .clear();
  string(stringbuffer).swap(stringbuffer); // kudos Scott
}

DNSPacket::DNSPacket(const DNSPacket &orig)
{
  DLOG(L<<"DNSPacket copy constructor called!"<<endl);
  d_socket=orig.d_socket;
  memcpy(remote, orig.remote, sizeof(remote));
  len=orig.len;
  d_qlen=orig.d_qlen;
  d_dt=orig.d_dt;
  d_socklen=orig.d_socklen;
  d_compress=orig.d_compress;
  qtype=orig.qtype;
  qclass=orig.qclass;
  qdomain=orig.qdomain;

  rrs=orig.rrs;

  d_wrapped=orig.d_wrapped;

  stringbuffer=orig.stringbuffer;
  d=orig.d;

}

int DNSPacket::expand(const char *begin, const char *end, string &expanded, int depth)
{
  if(depth>10)
    throw AhuException("Looping label when parsing a packet");

  unsigned int n;
  const char *p=begin;

  while((n=*(unsigned char *)p++)) {
    char tmp[256];
      if((n & 0xc0) == 0xc0 ) { 
	unsigned int labelOffset=(n&~0xc0)*256+ (int)*(unsigned char *)p;
	expand(stringbuffer.c_str()+labelOffset,end,expanded,depth++);
	return 1+p-begin;
      }

      if(p+n>=end) { // this is a bogus packet, references beyond the end of the buffer
	throw AhuException("Label claims to be longer than packet");
      }
      strncpy(tmp,p,n);

      if(*(p+n)) { // add a ., except at the end
	  tmp[n]='.';
	  tmp[n+1]=0;
      }
      else
	tmp[n]=0;

      expanded+=tmp;

      p+=n;
    }
    
  // lowercase(qdomain); (why was this?)

  return p-begin;

}

/** copies the question into our class
 *  and returns offset of question type & class. Returns -1 in case of an error
 */
int DNSPacket::getq()
{
  const char *orig=stringbuffer.c_str()+12;
  const char *end=orig+(stringbuffer.length()-12);
  qdomain="";
  try {
    return expand(orig,end,qdomain);
  }
  catch(AhuException &ae) {
    L<<Logger::Error<<"On retrieving question of packet, encountered error: "<<ae.reason<<endl;
  }
  return -1;
}

/*
                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      ID                       |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    QDCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ANCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    NSCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ARCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
*/

void DNSPacket::setRcode(int v)
{
  d.rcode=v;
}

void DNSPacket::setAnswer(bool b)
{
  if(b) {
    stringbuffer.assign(12,(char)0);
    memset((void *)&d,0,sizeof(d));
    
    d.qr=b;
  }
}

void DNSPacket::setA(bool b)
{
  d.aa=b;
}

void DNSPacket::setID(u_int16_t id)
{
  d.id=id;
}

void DNSPacket::setRA(bool b)
{
  d.ra=b;
}

void DNSPacket::setRD(bool b)
{
  d.rd=b;
}


void DNSPacket::setOpcode(u_int16_t opcode)
{
  d.opcode=opcode;
}

const char *DNSPacket::getRaw(void)
{
  return stringbuffer.data();
}

void DNSPacket::setRaw(char *mesg, int length)
{
  stringbuffer.assign(mesg,length); 
}

void DNSPacket::addARecord(const DNSResourceRecord &rr)
{
  DLOG(L<<"Adding an A record to the packet!"<<endl);
  addARecord(rr.qname, htonl(inet_addr(rr.content.c_str())), rr.ttl, rr.d_place);
}

void DNSPacket::addRecord(const DNSResourceRecord &rr)
{
  if(d_compress)
    for(vector<DNSResourceRecord>::const_iterator i=rrs.begin();i!=rrs.end();++i) 
      if(rr.qname==i->qname && rr.qtype==i->qtype && rr.content==i->content)
	return;

  rrs.push_back(rr);
}

void DNSPacket::addARecord(const string &name, u_int32_t ip, u_int32_t ttl, DNSResourceRecord::Place place)
{
  string piece1;
  toqname(name, &piece1);

  char p[14];

  p[0]=0;  
  p[1]=1; // A
  p[2]=0;
  p[3]=1; // IN

  u_int32_t *ttlp=(u_int32_t *)(p+4);

  *ttlp=htonl(ttl); // 4, 5, 6, 7

  p[8]=0;
  p[9]=4; // length of data

  p[10]=(ip>>24)&0xff;
  p[11]=(ip>>16)&0xff;
  p[12]=(ip>>8)&0xff;
  p[13]=ip&0xff;
  
  stringbuffer.append(piece1);
  stringbuffer.append(p,14);

  if(place==DNSResourceRecord::ADDITIONAL)
    d.arcount++;
  else
    d.ancount++;
}

void DNSPacket::addAAAARecord(const DNSResourceRecord &rr)
{
  DLOG(L<<"Adding an AAAA record to the packet!"<<endl);
  unsigned char addr[16];

#ifdef HAVE_IPV6
  if( Utility::inet_pton( AF_INET6, rr.content.c_str(), static_cast< void * >( addr )))
    addAAAARecord(rr.qname, addr, rr.ttl);
  else
#endif
    L<<Logger::Error<<"Unable to convert IPv6 TEXT '"<<rr.content<<"' into binary for record '"<<rr.qname<<"': "
     <<endl;
}



void DNSPacket::addAAAARecord(const string &name, unsigned char addr[16], u_int32_t ttl)
{
  string piece1;
  toqname(name.c_str(),&piece1);

  char p[26];

  p[0]=0;  
  p[1]=28; // AAAA
  p[2]=0;
  p[3]=1; // IN

  u_int32_t *ttlp=(u_int32_t *)(p+4);

  *ttlp=htonl(ttl); // 4, 5, 6, 7

  p[8]=0;
  p[9]=16; // length of data

  for(int n=0;n<16;n++)
    p[10+n]=addr[n];

  stringbuffer.append(piece1);
  stringbuffer.append(p,26);

  d.ancount++;
}


void DNSPacket::addMXRecord(const DNSResourceRecord &rr)
{
  addMXRecord(rr.qname, rr.content, rr.priority, rr.ttl);
}

void DNSPacket::addMXRecord(const string &domain, const string &mx, int priority, u_int32_t ttl)
{
  string piece1;

  toqname(domain,&piece1);

  char piece2[12];

  piece2[0]=0;

  piece2[1]=15; // MX
  piece2[2]=0;
  piece2[3]=1; // IN

  u_int32_t *ttlp=(u_int32_t *)(piece2+4);
  *ttlp=htonl(ttl); // 4, 5, 6, 7

  piece2[8]=0;
  piece2[9]=0;  // need to fill this in

  // start of payload for which we need to specify the length in 8 & 9

  piece2[10]=(priority>>8)&0xff;
  piece2[11]=priority&0xff;
  
  string piece3;
  toqname(mx,&piece3);
  // end of payload

  piece2[9]=piece3.length()+2; // fill in length

  stringbuffer+=piece1;
  stringbuffer.append(piece2,12);
  stringbuffer+=piece3;

  d.ancount++;
}

string &DNSPacket::attodot(string &str)
{
   if(str.find_first_of("@")==string::npos)
      return str;

   for (unsigned int i = 0; i < str.length(); i++)
   {
      if (str[i] == '@') {
         str[i] = '.';
         break;
      } else if (str[i] == '.') {
         str.insert(i++, "\\");
      }
   }

   return str;
}

void DNSPacket::fillSOAData(const string &content, SOAData &data)
{
  // content consists of fields separated by spaces:
  //  nameservername hostmaster serial-number [refresh [retry [expire [ minimum] ] ] ]

  // fill out data with some plausible defaults:
  // 10800 3600 604800 3600
  data.serial=0;
  data.refresh=10800;
  data.retry=3600;
  data.expire=604800;
  data.default_ttl=arg().asNum("soa-minimum-ttl");

  vector<string>parts;
  stringtok(parts,content);
  int pleft=parts.size();

  //  cout<<"'"<<content<<"'"<<endl;

  if(pleft)
    data.nameserver=parts[0];

  if(pleft>1) 
    data.hostmaster=attodot(parts[1]); // ahu@ds9a.nl -> ahu.ds9a.nl, piet.puk@ds9a.nl -> piet\.puk.ds9a.nl

  if(pleft>2)
    data.serial=atoi(parts[2].c_str());

  if(pleft>3)
    data.refresh=atoi(parts[3].c_str());

  if(pleft>4)
    data.retry=atoi(parts[4].c_str());


  if(pleft>5)
    data.expire=atoi(parts[5].c_str());

  if(pleft>6)
    data.default_ttl=atoi(parts[6].c_str());

}


string DNSPacket::serializeSOAData(const SOAData &d)
{
  ostringstream o;

  //  nameservername hostmaster serial-number [refresh [retry [expire [ minimum] ] ] ]

  o<<d.nameserver<<" "<< d.hostmaster <<" "<< d.serial <<" "<< d.refresh << " "<< d.retry << " "<< d.expire << " "<< d.default_ttl;

  return o.str();

}

  /* the hostmaster is encoded as two parts - the bit UNTIL the first unescaped '.'
     is encoded as a TXT string, the rest as a domain 

     we might encounter escaped dots in the first part though: bert\.hubert.powerdns.com for example should be
     11bert.hubert7powerdns3com0 */

/* Very ugly btw, this needs to be better */
const string DNSPacket::makeSoaHostmasterPiece(const string &hostmaster)
{
  string ret;
  string first;
  string::size_type i;

  for(i=0;i<hostmaster.length();++i) {
    if(hostmaster[i]=='.') {
      break;
    }
    if(hostmaster[i]=='\\' && i+1<hostmaster.length()) {
      ++i;
      first.append(1,hostmaster[i]);
      continue;
    }
    first.append(1,hostmaster[i]);
  }

  ret.resize(1);
  ret[0]=first.length();
  ret+=first;
  
  string second;
  if(i+1<hostmaster.length())
     toqname(hostmaster.substr(i+1),&second); 
  else {
     second.resize(1);
     second[0]=0;
  }

  return ret+second;
}


void DNSPacket::addSOARecord(const DNSResourceRecord &rr)
{
  addSOARecord(rr.qname, rr.content, rr.ttl, rr.d_place);
}

void DNSPacket::addSOARecord(const string &domain, const string & content, u_int32_t ttl,DNSResourceRecord::Place place)
{
  SOAData soadata;
  fillSOAData(content, soadata);

  string piece1;
  toqname(domain, &piece1);

  char p[10];
  
  p[0]=0;
  p[1]=6; // SOA
  p[2]=0;
  p[3]=1; // IN
  
  
  u_int32_t *ttlp=(u_int32_t *)(p+4);
  *ttlp=htonl(ttl); // 4, 5, 6, 7
  
  p[8]=0;
  p[9]=0;  // need to fill this in (length)
  
  string piece3;  
  toqname(soadata.nameserver,&piece3, false);
  
  string piece4=makeSoaHostmasterPiece(soadata.hostmaster);

  char piece5[20];
  
  u_int32_t *i_p=(u_int32_t *)piece5;
  
  u_int32_t soaoffset;
  if(soadata.serial && (soaoffset=arg().asNum("soa-serial-offset")))
    if(soadata.serial<soaoffset)
      soadata.serial+=soaoffset; // thank you DENIC

  *i_p++=htonl(soadata.serial ? soadata.serial : time(0));
  *i_p++=htonl(soadata.refresh);
  *i_p++=htonl(soadata.retry);
  *i_p++=htonl(soadata.expire);
  *i_p++=htonl(soadata.default_ttl);
  
  
  p[9]=piece3.length()+piece4.length()+20; 

  stringbuffer+=piece1;
  stringbuffer.append(p,10);
  stringbuffer+=piece3;
  stringbuffer+=piece4;
  stringbuffer.append(piece5,20);
  if(place==DNSResourceRecord::ANSWER)
    d.ancount++;
  else
    d.nscount++;
}



 
void DNSPacket::addCNAMERecord(const DNSResourceRecord &rr)
{
  addCNAMERecord(rr.qname, rr.content, rr.ttl);
}

void DNSPacket::addCNAMERecord(const string &domain, const string &alias, u_int32_t ttl)
{
 string piece1;

 //xtoqname(domain.c_str(),&piece1); 
 toqname(domain.c_str(),&piece1);
 char p[10];
 
 p[0]=0;
 p[1]=5; // CNAME
 p[2]=0;
 p[3]=1; // IN
 
 u_int32_t *ttlp=(u_int32_t *)(p+4);
 
 *ttlp=htonl(ttl); // 4, 5, 6, 7
 
 p[8]=0;
 p[9]=0;  // need to fill this in
 
 string piece3;
 //xtoqname(alias,&piece3);
 toqname(alias,&piece3);
 
 p[9]=piece3.length();

 stringbuffer+=piece1;
 stringbuffer.append(p,10);
 stringbuffer+=piece3;

 d.ancount++;
}


void DNSPacket::addRPRecord(const DNSResourceRecord &rr)
{
  addRPRecord(rr.qname, rr.content, rr.ttl);
}

void DNSPacket::addRPRecord(const string &domain, const string &content, u_int32_t ttl)
{
 string piece1;

 toqname(domain.c_str(),&piece1);
 char p[10];
 
 p[0]=0;
 p[1]=17; // RP
 p[2]=0;
 p[3]=1; // IN
 
 u_int32_t *ttlp=(u_int32_t *)(p+4);
 
 *ttlp=htonl(ttl); // 4, 5, 6, 7
 
 p[8]=0;
 p[9]=0;  // need to fill this in
 
 // content contains: mailbox-name more-info-domain (Separated by a space)
 unsigned int pos;
 if((pos=content.find(" "))==string::npos) {
   L<<Logger::Warning<<"RP record for domain '"<<domain<<"' has malformed content field"<<endl;
   return;
 }

 string mboxname=content.substr(0,pos);
 string moreinfo=content.substr(pos+1);

 string piece3;
 toqname(mboxname,&piece3);

 string piece4;
 toqname(moreinfo,&piece4);
 
 p[9]=(piece3.length()+piece4.length())%256;
 p[10]=(piece3.length()+piece4.length())/256;

 stringbuffer+=piece1;
 stringbuffer.append(p,10);
 stringbuffer+=piece3;
 stringbuffer+=piece4;

 // done
 d.ancount++;
}




void DNSPacket::addNAPTRRecord(const DNSResourceRecord &rr)
{
  addNAPTRRecord(rr.qname, rr.content, rr.ttl);
}



void DNSPacket::addNAPTRRecord(const string &domain, const string &content, u_int32_t ttl)
{
  string piece1;

  //xtoqname(domain.c_str(),&piece1);
  toqname(domain.c_str(),&piece1);
  char p[10];
  
  p[0]=0;
  p[1]=QType::NAPTR; 
  p[2]=0;
  p[3]=1; // IN
 
  u_int32_t *ttlp=(u_int32_t *)(p+4);
 
  *ttlp=htonl(ttl); // 4, 5, 6, 7
  
  p[8]=0;
  p[9]=0;  // need to fill this in
 
  // content contains: 100  100  "s"   "http+I2R"   ""    _http._tcp.foo.com.

  vector<string> parts;
  stringtok(parts,content);
  if(parts.size()<2) 
    return;

  int order=atoi(parts[0].c_str());
  int pref=atoi(parts[1].c_str());

  vector<string::const_iterator>poss;
  string::const_iterator i;
  for(i=content.begin();i!=content.end();++i)
    if(*i=='"')
      poss.push_back(i);

  if(poss.size()!=6)
    return;
 
  string flags, services, regex;
  insert_iterator<string> flagsi(flags, flags.begin());
  copy(poss[0]+1,poss[1],flagsi);
  insert_iterator<string> servicesi(services, services.begin());
  copy(poss[2]+1,poss[3],servicesi);
  insert_iterator<string> regexi(regex, regex.begin());
  copy(poss[4]+1,poss[5],regexi);
  
  for(i=poss[5]+1;i<content.end() && isspace(*i);++i); // skip spaces
  string replacement;
  insert_iterator<string> replacementi(replacement,replacement.begin());
  copy(i,content.end(),replacementi);

/* 
 The packet format for the NAPTR record is:

                                          1  1  1  1  1  1
            0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
          +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
          |                     ORDER                     |
          +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
          |                   PREFERENCE                  |
          +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
          /                     FLAGS                     /
          +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
          /                   SERVICES                    /
          +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
          /                    REGEXP                     /
          +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
          /                  REPLACEMENT                  /
          /                                               /
          +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
	  (jeez)
*/


  string piece3;
  piece3.resize(4);
 
  piece3[0]=(order>>8)&0xff;
  piece3[1]=(order)&0xff;

  piece3[2]=(pref>>8)&0xff;
  piece3[3]=(pref)&0xff;

  piece3.append(1,flags.length());
  piece3.append(flags);
  piece3.append(1,services.length());
  piece3.append(services);
  piece3.append(1,regex.length());
  piece3.append(regex);

  string piece4;
  toqname(replacement,&piece4);
 
  p[9]=(piece3.length()+piece4.length())%256;
  p[10]=(piece3.length()+piece4.length())/256;

  stringbuffer+=piece1;
  stringbuffer.append(p,10);
  stringbuffer+=piece3;
  stringbuffer+=piece4;
  
  // done
  d.ancount++;
}
 
void DNSPacket::addPTRRecord(const DNSResourceRecord &rr)
{
  addPTRRecord(rr.qname, rr.content, rr.ttl);
}

void DNSPacket::addPTRRecord(const string &domain, const string &alias, u_int32_t ttl)
{
 string piece1;

 //xtoqname(domain,&piece1);
 toqname(domain,&piece1);
 char p[10];
 
 p[0]=0;
 p[1]=12; // PTR
 p[2]=0;
 p[3]=1; // IN
 
 u_int32_t *ttlp=(u_int32_t *)(p+4);
 
 *ttlp=htonl(ttl); // 4, 5, 6, 7
 
 p[8]=0;
 p[9]=0;  // need to fill this in

 
 string piece3;
 //xtoqname(alias,&piece3);
 toqname(alias,&piece3);
 
 p[9]=piece3.length();
 
 stringbuffer+=piece1;
 stringbuffer.append(p,10);
 stringbuffer+=piece3;

 d.ancount++;
}



void DNSPacket::addTXTRecord(const DNSResourceRecord& rr)
{
  addTXTRecord(rr.qname, rr.content, rr.ttl);
}

void DNSPacket::addTXTRecord(string domain, string txt, u_int32_t ttl)
{
 string piece1;
 //xtoqname(domain, &piece1);
 toqname(domain, &piece1);
 char p[10];
 
 p[0]=0;
 p[1]=16; // TXT
 p[2]=0;
 p[3]=1; // IN

 u_int32_t *ttlp=(u_int32_t *)(p+4);
 
 *ttlp=htonl(ttl); // 4, 5, 6, 7

 p[8]=0;
 p[9]=0; // need to fill this in

 string piece3;
 piece3.reserve(txt.length()+1);
 piece3.append(1,txt.length());
 piece3.append(txt);

 p[8]=piece3.length()/256;;
 p[9]=piece3.length()%256;

 stringbuffer+=piece1;
 stringbuffer.append(p,10);
 stringbuffer+=piece3;

 d.ancount++;
}

void DNSPacket::addHINFORecord(const DNSResourceRecord& rr)
{
  addHINFORecord(rr.qname, rr.content, rr.ttl);
}

/** First word of content is the CPU */
void DNSPacket::addHINFORecord(string domain, string content, u_int32_t ttl)
{
 string piece1;
 toqname(domain, &piece1);
 char p[10];
 
 p[0]=0;
 p[1]=13; // HINFO
 p[2]=0;
 p[3]=1; // IN

 u_int32_t *ttlp=(u_int32_t *)(p+4);
 
 *ttlp=htonl(ttl); // 4, 5, 6, 7

 p[8]=0;
 p[9]=0; // need to fill this in

 unsigned int offset=content.find(" ");
 string cpu, host;
 if(offset==string::npos) {
   cpu=content;
 } else {
   cpu=content.substr(0,offset);
   host=content.substr(offset);
 }
 

 string piece3;
 piece3.reserve(cpu.length()+1);
 piece3.append(1,cpu.length());
 piece3.append(cpu);

 string piece4;
 piece4.reserve(host.length()+1);
 piece4.append(1,host.length());
 piece4.append(host);


 p[8]=0;
 p[9]=piece3.length()+piece4.length();

 stringbuffer+=piece1;
 stringbuffer.append(p,10);
 stringbuffer+=piece3;
 stringbuffer+=piece4;

 d.ancount++;
}



void DNSPacket::addNSRecord(const DNSResourceRecord &rr)
{
  addNSRecord(rr.qname, rr.content, rr.ttl, rr.d_place);
}

void DNSPacket::addNSRecord(string domain, string server, u_int32_t ttl, DNSResourceRecord::Place place)
{
  string piece1;
  toqname(domain, &piece1);

  char p[10];

  p[0]=0;
  p[1]=2; // NS
  p[2]=0;
  p[3]=1; // IN

  u_int32_t *ttlp=(u_int32_t *)(p+4);

  *ttlp=htonl(ttl); // 4, 5, 6, 7

  p[8]=0;
  p[9]=0;  // need to fill this in

  string piece3;
  string::size_type pos=server.find('@'); // chop off @
  if(pos!=string::npos)
    server.resize(pos);

  toqname(server,&piece3);

  p[9]=piece3.length();;

  stringbuffer.append(piece1);
  stringbuffer.append(p,10);
  stringbuffer.append(piece3);

  if(place==DNSResourceRecord::AUTHORITY)
    d.nscount++;
  else
    d.ancount++;

}


static int rrcomp(const DNSResourceRecord &A, const DNSResourceRecord &B)
{
  if(A.d_place<B.d_place)
    return 1;

  return 0;
}

/** You can call this function to find out if there are any records that need additional processing. 
    This holds for MX records and CNAME records, where information about the content may need further resolving. */
bool DNSPacket::needAP()
{
  // if speed ever becomes an issue, this function might be implemented in the addRecord() method, which would set a flag
  // whenever a record that needs additional processing is added

  for(vector<DNSResourceRecord>::const_iterator i=rrs.begin();
      i!=rrs.end();
      ++i)
    {
      if(i->d_place!=DNSResourceRecord::ADDITIONAL && 
	 ( (i->qtype.getCode()==QType::NS && i->content.find('@')==string::npos) ||  // NS records with @ in them are processed
	  i->qtype.getCode()==QType::MX )) 
	{
	  return true;
	}
    }
  return false;
}

vector<DNSResourceRecord> DNSPacket::getAPRecords()
{
  vector<DNSResourceRecord> arrs;

  for(vector<DNSResourceRecord>::const_iterator i=rrs.begin();
      i!=rrs.end();
      ++i)
    {
      if(i->d_place!=DNSResourceRecord::ADDITIONAL && 
	 (i->qtype.getCode()==15 || 
	  i->qtype.getCode()==2 )) // CNAME or MX or NS
	{
	  arrs.push_back(*i);
	}
    }

  return arrs;

}

void DNSPacket::setCompress(bool compress)
{
  d_compress=compress;
  stringbuffer.reserve(65000);
  rrs.reserve(200);
}

/** Must be called before attempting to access getData(). This function stuffs all resource
 *  records found in rrs into the data buffer. It also frees resource records queued for us.
 */
void DNSPacket::wrapup(void)
{
  if(d_wrapped) {
    return;
  }
  
  // do embedded-additional processing decapsulation
  DNSResourceRecord rr;
  vector<DNSResourceRecord>::iterator pos;

  vector<DNSResourceRecord> additional;
  for(pos=rrs.begin();pos<rrs.end();++pos) {
    if(pos->qtype.getCode()==QType::NS) {
      vector<string>pieces;
      stringtok(pieces,pos->content,"@");
      
      if(pieces.size()>1) { // INSTANT ADDITIONAL PROCESSING!
	rr.qname=pieces[0];
	rr.qtype=QType::A;
	rr.ttl=pos->ttl;
	rr.content=pieces[1];
	rr.d_place=DNSResourceRecord::ADDITIONAL;
	additional.push_back(rr);
      }
    }
  }
  int ipos=rrs.size();
  rrs.resize(rrs.size()+additional.size());
  copy(additional.begin(), additional.end(), rrs.begin()+ipos);

  // we now need to order rrs so that the different sections come at the right place
  // we want a stable sort, based on the d_place field

  stable_sort(rrs.begin(),rrs.end(),rrcomp);

  d_wrapped=true;


  for(pos=rrs.begin();pos<rrs.end();++pos) {
    rr=*pos;
    DLOG(L<<"Added to data, RR: " << rr.qname);
    DLOG(L<<"(" << rr.qtype.getName() << ")" << " " << (int) rr.d_place<< endl);

    switch(rr.qtype.getCode()) {
    case 1:  // A
      addARecord(rr);
      break;
    case 2:  // NS
      addNSRecord(rr);
      break;

    case 5:  // CNAME
      addCNAMERecord(rr);
      break;

    case 6:  // SOA
      addSOARecord(rr);
      break;

    case 12:  // PTR
      addPTRRecord(rr);
      break;

    case 13: // HINFO
      addHINFORecord(rr);
      break;

    case 15: // MX
      addMXRecord(rr);
      break;

    case 16: // TXT
      addTXTRecord(rr);
      break;

    case 17: // RP
      addRPRecord(rr);
      break;


    case 28: // AAAA
      addAAAARecord(rr);
      break;

    case QType::LOC:
      addLOCRecord(rr);
      break;

    case QType::NAPTR:
      addNAPTRRecord(rr);
      break;

    case 258: // CURL
    case 256: // URL
      addARecord(rr.qname,htonl(inet_addr(arg()["urlredirector"].c_str())),rr.ttl,DNSResourceRecord::ANSWER);   
      break;

    case 257: // MBOXFW
      unsigned int pos;
      pos=rr.qname.find("@");
      DLOG(L<<Logger::Warning<<"Adding rr.qname: '"<<rr.qname<<"'"<<endl);
      if(pos!=string::npos)
	{
	  string substr=rr.qname.substr(pos+1);

	  addMXRecord(substr,arg()["smtpredirector"],25,rr.ttl);
	}
      break;

    default:
      L<<Logger::Warning<<"Unable to insert a record of type "<<rr.qtype.getName()<<" for '"<<rr.qname<<"'"<<endl;
    }
  }
  d.ancount=htons(d.ancount);
  d.qdcount=htons(d.qdcount);
  d.nscount=htons(d.nscount);
  d.arcount=htons(d.arcount);

  commitD();


  len=stringbuffer.length();
}


/** Truncates a packet that has already been wrapup()-ed, possibly via a call to getData(). Do not call this function
    before having done this - it will possibly break your packet, or crash your program. 

    This method sets the 'TC' bit in the stringbuffer, and caps the len attributed to new_length.
*/ 

void DNSPacket::truncate(int new_length)
{
  if(new_length>len || !d_wrapped)
    return;

  DLOG(L<<Logger::Warning<<"Truncating a packet to "<<inet_ntoa( reinterpret_cast< sockaddr_in * >( remote )->sin_addr )<<endl);

  len=new_length;
  stringbuffer[2]|=2; // set TC
}

string DNSPacket::compress(const string &qd)
{
  // input www.casema.net, output 3www6casema3net
  // input www.casema.net., output 3www6casema3net
  string qname = "";

  // Convert the name to a qname

  const char *p = qd.c_str();
  const char *q = strchr(p, '.');
  
  while (p <= (qd.c_str() + qd.length()))
    {
      int length = (q == NULL) ? strlen(p) : (q - p);
      if (length == 0) {
        break;
      }
      qname += (char) length;
      qname.append(p, length);
      
      if (q == NULL) {
	break;
      } else {
	p = q + 1;
	q = strchr(p, '.');
      }
    }
  
  qname += (char) 0x00;
  return qname;
}

void DNSPacket::setQuestion(int op, const string &qd, int qtype)
{
  memset(&d,0,sizeof(d));
  d.id=Utility::random();
  d.rd=d.tc=d.aa=false;
  d.qr=false;
  d.qdcount=1; // is htons'ed later on
  d.ancount=d.arcount=d.nscount=0;
  d.opcode=op;

  string label=compress(qd);
  stringbuffer.assign((char *)&d,sizeof(d));
  stringbuffer.append(label);
  u_int16_t tmp=htons(qtype);
  stringbuffer.append((char *)&tmp,2);
  tmp=htons(1);
  stringbuffer.append((char *)&tmp,2);
}




/** A DNS answer packets needs to include the original question. This function allows you to
    paste in a question */

void DNSPacket::pasteQ(const char *question, int length)
{
  stringbuffer.replace(12,length,question,length);  // bytes 12 & onward need to become *question
}

string DNSPacket::parseLOC(const unsigned char *p, unsigned int length)
{
  /*
    MSB                                           LSB
       +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      0|        VERSION        |         SIZE          |
       +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      2|       HORIZ PRE       |       VERT PRE        |
       +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      4|                   LATITUDE                    |
       +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      6|                   LATITUDE                    |
       +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      8|                   LONGITUDE                   |
       +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     10|                   LONGITUDE                   |
       +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     12|                   ALTITUDE                    |
       +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     14|                   ALTITUDE                    |
       +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  */

  struct RP
  {
    unsigned int version:8;
    unsigned int size:8;
    unsigned int horizpre:8;
    unsigned int vertpre:8;
  }rp;

  rp=*(RP *)p;
  char ret[256];

  double latitude= (((p[4]<<24)  + (p[5]<<16)  +  (p[6]<<8) +  p[7])  - (1<<31))/3600000.0;
  double longitude=(((p[8]<<24)  + (p[9]<<16)  + (p[10]<<8) + p[11])  - (1<<31))/3600000.0;
  double altitude= (((p[12]<<24) + (p[13]<<16) + (p[14]<<8) + p[15])           )/100 - 100000;
  
  double size=0.01*((rp.size>>4)&0xf);
  int count=rp.size&0xf;
  while(count--)
    size*=10;

  double horizpre=0.01*((rp.horizpre>>4)&0xf);
  count=rp.horizpre&0xf;
  while(count--)
    horizpre*=10;

  double vertpre=0.01*((rp.vertpre>>4)&0xf);
  count=rp.vertpre&0xf;
  while(count--)
    vertpre*=10;


  double remlat=60.0*(latitude-(int)latitude);
  double remlong=60.0*(longitude-(int)longitude);
  snprintf(ret,sizeof(ret)-1,"%d %d %2.03f %c %d %d %2.03f %c %.2fm %.2fm %.2fm %.2fm",
	   abs((int)latitude), (int) ((latitude-(int)latitude)*60),
	   (double)((remlat-(int)remlat)*60.0),
	   latitude>0 ? 'N' : 'S',
	   abs((int)longitude), (int) ((longitude-(int)longitude)*60),
	   (double)((remlong-(int)remlong)*60.0),
	   longitude>0 ? 'E' : 'W',
	   altitude, size, horizpre, vertpre);


  return ret;
}

vector<DNSResourceRecord> DNSPacket::getAnswers()
{
  // XXX FIXME a lot of this code happily touches bytes beyond your packet! 

  vector<DNSResourceRecord> rrs;
  if(!(d.ancount|d.arcount|d.nscount))
    return rrs;

  const char *answerp=stringbuffer.c_str()+d_qlen+12;
  const char *end=stringbuffer.c_str()+len;

  int numanswers=ntohs(d.ancount) + ntohs(d.nscount) + ntohs(d.arcount);
  int length;
  int pos=0;
  while(numanswers--) {
    string name;  
    int offset=0;
    offset=expand(answerp,end,name);

    DNSResourceRecord rr;
    rr.qname=name;
    rr.qtype=ntohs(*((short int *)(answerp+offset)));
    rr.ttl=ntohl(*(u_int32_t*)(answerp+offset+4)); // 4, 5, 6, 7
    rr.content="";
    length=ntohs(*(u_int16_t*)(answerp+offset+8));
    // XXX check if this 'length' extends beyond the end of the packet!

    const char *datapos=answerp+offset+10;
    string part;
    offset=0;

    ostringstream o;
    int ip;

    switch(rr.qtype.getCode()) {

    case QType::SOA:
      part=""; offset+=expand(datapos+offset,end,part); rr.content=part;      // mname
      part=""; offset+=expand(datapos+offset,end,part); rr.content+=" "+part;  // hostmaster

      rr.content+=" ";rr.content+=itoa(ntohl(*(unsigned int *)(datapos+offset)));
      rr.content+=" ";rr.content+=itoa(ntohl(*(unsigned int *)(datapos+offset+4)));
      rr.content+=" ";rr.content+=itoa(ntohl(*(unsigned int *)(datapos+offset+8)));
      rr.content+=" ";rr.content+=itoa(ntohl(*(unsigned int *)(datapos+offset+12)));
      rr.content+=" ";rr.content+=itoa(ntohl(*(unsigned int *)(datapos+offset+16)));

      break;

    case QType::A:
      ip = ntohl(*((int *)datapos));

      o.clear();
      o<<((ip>>24)&0xff)<<".";
      o<<((ip>>16)&0xff)<<".";
      o<<((ip>>8)&0xff)<<".";
      o<<((ip>>0)&0xff);
      
      rr.content=o.str();
      break;
      
    case QType::MX:
      rr.priority=(datapos[0] << 8) + datapos[1];
      expand(datapos+2,end,rr.content);

      break;


    case QType::TXT:
      rr.content.assign(datapos+offset+1,(int)datapos[offset]);
      break;

    case QType::LOC:
      rr.content=parseLOC(reinterpret_cast<const unsigned char *>(datapos+offset),length);
      break;


    case QType::RP:
      offset+=expand(datapos+offset,end,rr.content);
      expand(datapos+offset,end,part);
      rr.content+=" "+part;
      break;


    case QType::CNAME:
    case QType::NS:
    case QType::PTR:
      expand(datapos+offset,end,rr.content);
      break;

    case QType::AAAA:
      if(length!=16)
	throw AhuException("Wrong length AAAA record returned from remote");
      char tmp[128];
#ifdef AF_INET6	
      if(!Utility::inet_ntop(AF_INET6, datapos, tmp, sizeof(tmp)-1))
#endif
	throw AhuException("Unable to translate record of type AAAA in resolver");

      rr.content=tmp;
      break;

    default:
      throw AhuException("Unknown type number "+itoa(rr.qtype.getCode())+" for: '"+rr.qname+"'");
    }
    if(pos<ntohs(d.ancount))
      rr.d_place=DNSResourceRecord::ANSWER;
    else if(pos<ntohs(d.ancount)+ntohs(d.nscount))
      rr.d_place=DNSResourceRecord::AUTHORITY;
    else
      rr.d_place=DNSResourceRecord::ADDITIONAL;
      
    rrs.push_back(rr);    
    pos++;
    //    cout<<"Added '"<<rr.qname<<"' '"<<rr.content<<"' "<<rr.qtype.getName()<<endl;
    //    cout<<"Advancing "<<length<<" bytes"<<endl;
    answerp=datapos+length; 
  }
  return rrs;
  
}

/** convenience function for creating a reply packet from a question packet. Do not forget to delete it after use! */
DNSPacket *DNSPacket::replyPacket() const
{
  DNSPacket *r=new DNSPacket;
  r->setSocket(d_socket);


  r->setRemote((struct sockaddr *)remote, d_socklen);
  r->setAnswer(true);  // this implies the allocation of the header
  r->setA(true); // and we are authoritative
  r->setRA(0); // no recursion available
  r->setRD(d.rd); // if you wanted to recurse, answer will say you wanted it (we don't do it)
  r->setID(d.id);
  r->setOpcode(d.opcode);

  // reserve some space
  r->stringbuffer.reserve(d_qlen+12);
  // copy the question in
  r->pasteQ(stringbuffer.c_str()+12,d_qlen);
  r->d.qdcount=1;
  
  r->d_dt=d_dt;

  return r;
}

int DNSPacket::findlabel(string &label)
{
  const char *data = stringbuffer.data();
  const char *p = data + 12;

  // Look in the question section
   
  for (unsigned int i = 0; i < d.qdcount; i++) {
    while (*p != 0x00) {
      // Skip compressed labels
      if ((*p & 0xC0) == 0xC0) {
	p += 1;
	break;
      }
      else {
	if (strcmp(p, label.data()) == 0)
	  return (p - data);
	p += (*p + 1);
      }
    }
      
    // Skip the tailing zero
    p++;
    
    // Skip the header
    p += 4;
  }

  //
  // Look in the answer sections
  //

  for (unsigned int i = 0; i < d.ancount + d.nscount + d.arcount; i++) {
    while (*p != 0x00) {
      // Skip compressed labels - means the end
      if ((*p & 0xC0) == 0xC0)
	{
	  p += 1;
	  break;
	}
      else
	{
	  if (strcmp(p, label.data()) == 0)
	    {
	      return (p - data);
	    }
	  
	  p += (*p + 1);
	}
    }
    
    // Skip the trailing zero or other half of the ptr
       
    p++;

    // Skip the header and data
    
    short int dataLength = ntohs(*(short int*) (p + 8));
    short int type = ntohs(*(short int*) (p));

    p += 10;
    
    // Check for NS, CNAME, PTR and MX records
    
    if (type == QType::NS || type == QType::CNAME || type == QType::PTR || type == QType::MX) {
      // For MX records, skip the preference field
      if (type == QType::MX){
	p += 2;
      }

      while (*p != 0x00) {
	//
	// Skip compressed labels
	//
	
	if ((*p & 0xC0) == 0xC0) {
	  p += 1;
	  break;
	}
	else {

	  if (strcmp(p, label.data()) == 0) {
	    return (p - data);
	  }
		   
	  p += (*p + 1);
	}
      }
	   
      // Skip the trailing zero or the last byte of a compresed label
      p++;	 
    }
    else {
      p += dataLength;
    }
  }
  
  return -1;
}

int DNSPacket::toqname(const char *name, string &qname, bool comp)
{
  qname = compress(name);

  if (d_compress && comp) {
    // Now find a previous declared label. We work through the complete
    // name from left to right like this:
    //  ns1.norad.org
    //  norad.org
    //  org
    
    int i = 0;
    
    while (qname[i] != 0x00) {
      // Get a portion of the name
      
      string s = qname.substr(i);

      // Did we see this before?
      int offset = findlabel(s);
      
      if ( offset != -1) {

	qname[i + 0] = (char) (((offset | 0xC000) & 0x0000FF00) >> 8);
	qname[i + 1] = (char)  ((offset | 0xC000) & 0x000000FF);
	qname = qname.substr(0, i + 2); // XX setlength() ?
	// qname now consists of unique prefix+known suffix (on location 'offset')
	break;
      }
      // Move to the next label
      i += (qname[i] + 1); // doesn't quite handle very long labels
    }
  }
  
  return qname.length();
}

int DNSPacket::toqname(const string &name, string &qname, bool compress)
{
   return toqname(name.c_str(), qname, compress);
}

int DNSPacket::toqname(const string &name, string *qname, bool compress)
{
   return toqname(name.c_str(), *qname, compress);
}

