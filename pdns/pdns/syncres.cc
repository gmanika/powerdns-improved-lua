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
#include <iostream>
#include <map>
#include <algorithm>
#include <set>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include <utility>
#include "statbag.hh"
#include "arguments.hh"
#include "lwres.hh"

/** Issues:
    - we need a cache pruning thing
    - case issues
    - transparency 
      everything should be record=unknown except for A, MX, NS, AAAA
    - negative caching
    - compressions sucks [ortho]
*/

typedef map<string,set<DNSResourceRecord> > cache_t;
cache_t cache;

bool operator<(const DNSResourceRecord &a, const DNSResourceRecord &b)
{
  if(a.qname<b.qname)
    return true;
  if(a.qname==b.qname)
    return(a.content<b.content);
  return false;
}

int doResolveAt(set<string> nameservers, string auth, const string &qname, const QType &qtype, vector<DNSResourceRecord>&ret,int depth=0);
int doResolve(const string &qname, const QType &qtype, vector<DNSResourceRecord>&ret, int depth=0);

string getA(const string &qname, int depth=0)
{
  vector<DNSResourceRecord> res;
  string ret;

  if(!doResolve(qname,QType(QType::A), res,depth+1)) 
    ret=res[0].content;

  return ret;
}

void getBestNSFromCache(const string &qname, vector<DNSResourceRecord>&ret, int depth=0)
{
  string prefix;
  prefix.assign(3*depth, ' ');

  vector<string>parts;
  stringtok(parts,qname,".");  // www.us.powerdns.com -> 'www' 'us' 'powerdns' 'com'
  
  unsigned int spos=0;
  string subdomain;

  while(spos<=parts.size()) {
    if(spos<parts.size()) { // www.us.powerdns.com -> us.powerdns.com -> powerdns.com -> com ->
      subdomain=parts[spos++];
      for(unsigned int i=spos;i<parts.size();++i) {
	subdomain+=".";
	subdomain+=parts[i];
      }
    }
    else {
      subdomain=""; // ROOT!
      spos++;
    }
    cout<<prefix<<qname<<": Checking if we have NS for '"<<subdomain<<"'"<<endl;
    cache_t::const_iterator j=cache.find(toLower(subdomain)+"|NS");
    if(j!=cache.end() && j->first==toLower(subdomain)+"|NS") {
      cout<<prefix<<qname<<": Adding authority records for '"<<subdomain<<"'"<<endl;
      for(set<DNSResourceRecord>::const_iterator k=j->second.begin();k!=j->second.end();++k) {
	DNSResourceRecord ns=*k;
	ns.d_place=DNSResourceRecord::AUTHORITY;
	if(ns.ttl>time(0)) {
	  ns.ttl-=time(0);
	  ret.push_back(ns);
	}
      }
      return;
    }
  }
}


void addCruft(const string &qname, vector<DNSResourceRecord>& ret)
{
  for(vector<DNSResourceRecord>::const_iterator k=ret.begin();k!=ret.end();++k)  // don't add stuff to an NXDOMAIN!
    if(k->d_place==DNSResourceRecord::AUTHORITY && k->qtype==QType(QType::SOA))
      return;

  getBestNSFromCache(qname,ret);
  
  cout<<qname<<": Additional processing"<<endl;
  vector<DNSResourceRecord> addit;
  for(vector<DNSResourceRecord>::const_iterator k=ret.begin();k!=ret.end();++k) 
    if((k->d_place==DNSResourceRecord::ANSWER && k->qtype==QType(QType::MX)) || 
       (k->d_place==DNSResourceRecord::AUTHORITY && k->qtype==QType(QType::NS))) {
      cout<<qname<<": record '"<<k->content<<"|"<<k->qtype.getName()<<"' needs an IP address"<<endl;
      doResolve(k->content,QType(QType::A),addit,1);
    }

  
  for(vector<DNSResourceRecord>::iterator k=addit.begin();k!=addit.end();++k) {
    k->d_place=DNSResourceRecord::ADDITIONAL;
    ret.push_back(*k);
  }
  cout<<qname<<": done with additional processing"<<endl;
}

int beginResolve(const string &qname, const QType &qtype, vector<DNSResourceRecord>&ret)
{
  int res=doResolve(qname, qtype, ret,0);
  if(!res)
    addCruft(qname, ret);
  return res;
}


int doResolve(const string &qname, const QType &qtype, vector<DNSResourceRecord>&ret, int depth)
{
  string prefix;
  prefix.assign(3*depth, ' ');
  
  // see if we have a CNAME hit
  string tuple=toLower(qname)+"|CNAME";
  cout<<prefix<<"Looking for CNAME cache hit of '"<<tuple<<"'"<<endl;

  cache_t::const_iterator i=cache.find(tuple);
  if(i!=cache.end() && i->first==tuple) { // found it
    for(set<DNSResourceRecord>::const_iterator j=i->second.begin();j!=i->second.end();++j) {
      if(j->ttl>time(0)) {
	cout<<prefix<<"Found cache CNAME hit for '"<<tuple<<"' to '"<<i->second.begin()->content<<"'"<<endl;    
	DNSResourceRecord rr=*j;
	rr.ttl-=time(0);
	ret.push_back(rr);
	return doResolve(i->second.begin()->content, qtype, ret, depth);
      }
    }
  }

  tuple=toLower(qname)+"|"+qtype.getName();
  cout<<prefix<<"Looking for direct cache hit of '"<<tuple<<"'"<<endl;

  i=cache.find(tuple);
  if(i!=cache.end() && i->first==tuple) { // found it
    cout<<prefix<<"Found cache hit for '"<<tuple<<"': ";
    for(set<DNSResourceRecord>::const_iterator j=i->second.begin();j!=i->second.end();++j) {
      cout<<j->content;
      if(j->ttl>time(0)) {
	DNSResourceRecord rr=*j;
	rr.ttl-=time(0);
	ret.push_back(rr);
	cout<<"[fresh] ";
      }
      else
	cout<<"[expired] ";
    }
  
    cout<<endl;
    return 0;
  }

  cout<<prefix<<"No cache hit for '"<<tuple<<"', trying to find an appropriate NS record"<<endl;
  // bummer, get the best NS record then

  vector<string>parts;
  stringtok(parts,qname,".");  // www.us.powerdns.com -> 'www' 'us' 'powerdns' 'com'
  
  unsigned int spos=0;
  string subdomain;

  while(spos<=parts.size()) {
    if(spos<parts.size()) { // www.us.powerdns.com -> us.powerdns.com -> powerdns.com -> com ->
      subdomain=parts[spos++];
      for(unsigned int i=spos;i<parts.size();++i) {
	subdomain+=".";
	subdomain+=parts[i];
      }
    }
    else {
      subdomain=""; // ROOT!
      spos++;
    }
    cout<<prefix<<qname<<": Checking if we have NS for '"<<subdomain<<"'"<<endl;
    cache_t::const_iterator j=cache.find(toLower(subdomain)+"|NS");
    if(j!=cache.end() && j->first==toLower(subdomain)+"|NS") {
      
      set<string>nsset;
      for(set<DNSResourceRecord>::const_iterator k=j->second.begin();k!=j->second.end();++k) 
	if(k->ttl>time(0))
	  nsset.insert(k->content);
      

      if(nsset.empty())
      	cout<<prefix<<"Found only expired NS for '"<<subdomain<<"', heading lower"<<endl;
      else {
	cout<<prefix<<"Found NS for '"<<subdomain<<"', heading there for further questions"<<endl;
	int hasResults=doResolveAt(nsset,subdomain,qname,qtype,ret,depth);
	if(hasResults<0)
	  continue; // perhaps less specific nameservers can help us
	return hasResults;
      }
    }
  }
  cout<<prefix<<qname<<": FOUND NO NS FOR '"<<subdomain<<"'"<<endl;
  return -1;
}

bool endsOn(const string &domain, const string &suffix) 
{
  if(domain==suffix || suffix.empty())
    return true;
  if(domain.size()<=suffix.size())
    return false;
  return (domain.substr(domain.size()-suffix.size()-1,suffix.size()+1)=="."+suffix);
}

/** returns -1 in case of no results, rcode otherwise */
int doResolveAt(set<string> nameservers, string auth, const string &qname, const QType &qtype, vector<DNSResourceRecord>&ret, int depth)
{
  string prefix;
  prefix.assign(3*depth, ' ');
  
  LWRes r;
  LWRes::res_t result;
  vector<DNSResourceRecord>usefulrrs;
  set<string> nsset;  
  cout<<prefix<<qname<<": start of recursion!"<<endl;


  for(;;) { // we may get more specific nameservers
    result.clear();

    vector<string>rnameservers;
    for(set<string>::const_iterator i=nameservers.begin();i!=nameservers.end();++i)
      rnameservers.push_back(*i);

    random_shuffle(rnameservers.begin(),rnameservers.end());
    for(vector<string>::const_iterator i=rnameservers.begin();;++i){ 
      if(i==rnameservers.end()) {
	cout<<prefix<<qname<<": failed to resolve via any of the "<<rnameservers.size()<<" offered nameservers"<<endl;
	return -1;
      }
      cout<<prefix<<qname<<": trying to resolve nameserver "<<*i<<endl;
      string remoteIP=getA(*i, depth+1);
      if(remoteIP.empty()) {
	cout<<prefix<<qname<<": failed to resolve nameserver "<<*i<<", trying next if available"<<endl;
	continue;
      }
      cout<<prefix<<qname<<": resolved nameserver "<<*i<<" to "<<remoteIP<<", asking it for '"<<qname<<"|"<<qtype.getName()<<"'"<<endl;

      if(r.asyncresolve(remoteIP,qname.c_str(),qtype.getCode())!=1) { // <- we go out on the wire!
	cout<<prefix<<qname<<": error resolving"<<endl;
      }
      else {
	result=r.result();
	
	cout<<prefix<<qname<<": got "<<result.size()<<" answers from "<<*i<<" ("<<remoteIP<<")"<<endl;
	break;
      }
    }


    cache_t tcache;
    // reap all answers from this packet that are acceptable
    for(LWRes::res_t::const_iterator i=result.begin();i!=result.end();++i) {
      cout<<prefix<<qname<<": accept answer '"<<i->qname<<"|"<<i->qtype.getName()<<"|"<<i->content<<"' from '"<<auth<<"' nameservers? ";
      cout.flush();
      if(endsOn(i->qname, auth)) {
	cout<<"YES!"<<endl;

	DNSResourceRecord rr=*i;
	rr.d_place=DNSResourceRecord::ANSWER;
	rr.ttl+=time(0);
	tcache[toLower(i->qname)+"|"+i->qtype.getName()].insert(rr);
      }
      else
	cout<<"NO!"<<endl;

    }
    
    // supplant
    for(cache_t::const_iterator i=tcache.begin();i!=tcache.end();++i)
      cache[i->first]=i->second;
    
    nsset.clear();
    cout<<prefix<<qname<<": determining status after receiving this packet"<<endl;
    bool done=false;
    for(LWRes::res_t::const_iterator i=result.begin();i!=result.end();++i) {
      if(i->d_place==DNSResourceRecord::AUTHORITY && endsOn(qname,i->qname) && i->qtype.getCode()==QType::SOA) {
	cout<<prefix<<qname<<": got a NXDOMAIN"<<endl;
	ret.push_back(*i);
	return RCode::NXDomain; // NXDOMAIN
      }
      if(i->d_place==DNSResourceRecord::ANSWER && i->qname==qname && i->qtype.getCode()==QType::CNAME) {
	cout<<prefix<<qname<<": got a CNAME referral, starting over with "<<i->content<<endl<<endl;
	ret.push_back(*i);
	return doResolve(i->content, qtype, ret,0);
      }
      if(i->d_place==DNSResourceRecord::ANSWER && i->qname==qname && i->qtype==qtype) {
	cout<<prefix<<qname<<": resolved to "<<i->content<<endl;
	done=true;
	ret.push_back(*i);
      }
      if(i->d_place==DNSResourceRecord::AUTHORITY && i->qtype.getCode()==QType::NS) { // XXX FIXME check if suffix!
	auth=i->qname;
	cout<<prefix<<qname<<": got NS record "<<i->content<<endl;
	nsset.insert(toLower(i->content));
      }
    }
    if(done){ 
      cout<<prefix<<qname<<": got results, returning"<<endl;
      return 0;
    }
    if(nsset.empty()) {
      cout<<prefix<<qname<<": did not resolve "<<qname<<", did not get referral"<<endl;
      return -1;
    }
    
    cout<<prefix<<qname<<": did not resolve "<<qname<<", did get "<<nsset.size()<<" nameservers, looping to them"<<endl;
    nameservers=nsset;
  }
  return -1;
}

void init(void)
{
  // prime root cache
  static char*ips[]={"198.41.0.4", "128.9.0.107", "192.33.4.12", "128.8.10.90", "192.203.230.10",
		     "192.5.5.241", "192.112.36.4", "128.63.2.53", "192.36.148.17",
		     "198.41.0.10", "193.0.14.129", "198.32.64.12", "202.12.27.33"};
  DNSResourceRecord arr, nsrr;
  arr.qtype=QType::A;
  arr.ttl=time(0)+86400;

  nsrr.qtype=QType::NS;
  nsrr.ttl=time(0)+86400;
  nsrr.qname="";
  
  for(char c='a';c<='m';++c) {
    static char templ[40];
    strncpy(templ,"a.root-servers.net", sizeof(templ) - 1);
    *templ=c;
    arr.qname=templ;
    arr.content=ips[c-'a'];
    cache[string(templ)+"|A"].insert(arr);

    nsrr.content=templ;
    cache["|NS"].insert(nsrr);
  }
}
