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
#include <string>
#include <map>
#include <set>
#include <pthread.h>
#include <time.h>
#include <fstream>
#include <boost/shared_ptr.hpp>
#include "huffman.hh"
using namespace boost;
using namespace std;

struct Bind2DNSRecord
{
  string qname;
  QType qtype;
  uint32_t ttl;
  shared_ptr<string> content;

  bool operator<(const Bind2DNSRecord& rhs) const
  {
    return qname < rhs.qname;
  }
};

class BB2DomainInfo
{
public:
  BB2DomainInfo();

  void setCtime();

  bool current();

  bool d_loaded;
  string d_status;
  bool d_checknow;
  time_t d_ctime;
  string d_name;
  string d_filename;
  unsigned int d_id;
  time_t d_last_check;
  string d_master;
  int d_confcount;
  u_int32_t d_lastnotified;

  bool tryRLock()
  {
    //    cout<<"[trylock!] "<<(void*)d_rwlock<<"/"<<getpid()<<endl;
    return pthread_rwlock_tryrdlock(d_rwlock)!=EBUSY;
  }
  
  void unlock()
  {
    //    cout<<"[unlock] "<<(void*)d_rwlock<<"/"<<getpid()<<endl;
    pthread_rwlock_unlock(d_rwlock);
  }
  
  void lock()
  {
    //cout<<"[writelock!] "<<(void*)d_rwlock<<"/"<<getpid()<<endl;

    pthread_rwlock_wrlock(d_rwlock);
  }

  void setCheckInterval(time_t seconds);
  vector <Bind2DNSRecord>* d_records;
private:
  time_t getCtime();
  time_t d_checkinterval;
  time_t d_lastcheck;
  pthread_rwlock_t *d_rwlock;
};

class Bind2Backend : public DNSBackend
{
public:
  Bind2Backend(const string &suffix=""); //!< Makes our connection to the database. Calls exit(1) if it fails.
  void getUnfreshSlaveInfos(vector<DomainInfo> *unfreshDomains);
  void getUpdatedMasters(vector<DomainInfo> *changedDomains);
  bool getDomainInfo(const string &domain, DomainInfo &di);
  time_t getCtime(const string &fname);
  

  void lookup(const QType &, const string &qdomain, DNSPacket *p=0, int zoneId=-1);
  bool list(const string &target, int id);
  bool get(DNSResourceRecord &);

  static DNSBackend *maker();
  static pthread_mutex_t s_startup_lock;

  void setFresh(u_int32_t domain_id);
  void setNotified(u_int32_t id, u_int32_t serial);
  bool startTransaction(const string &qname, int id);
  //  bool Bind2Backend::stopTransaction(const string &qname, int id);
  bool feedRecord(const DNSResourceRecord &r);
  bool commitTransaction();
  bool abortTransaction();
  void insert(int id, const string &qname, const string &qtype, const string &content, int ttl, int prio);  
  void rediscover(string *status=0);

  bool isMaster(const string &name, const string &ip);

  // for supermaster support
  bool superMasterBackend(const string &ip, const string &domain, const vector<DNSResourceRecord>&nsset, string *account, DNSBackend **db);
  bool createSlaveDomain(const string &ip, const string &domain, const string &account);
  
private:
  class handle
  {
  public:
    bool get(DNSResourceRecord &);
    ~handle() {
      if(d_bbd)
	d_bbd->unlock();
    }
    handle();

    Bind2Backend *parent;

    vector<Bind2DNSRecord>* d_records;
    vector<Bind2DNSRecord>::const_iterator d_iter;
    
    vector<Bind2DNSRecord>::const_iterator d_rend;

    vector<Bind2DNSRecord>::const_iterator d_qname_iter;
    vector<Bind2DNSRecord>::const_iterator d_qname_end;

    bool d_list;
    int id;
    BB2DomainInfo* d_bbd;  // appears to be only used for locking
    string qname;
    QType qtype;
  private:
    int count;
    
    bool get_normal(DNSResourceRecord &);
    bool get_list(DNSResourceRecord &);

    void operator=(const handle& ); // don't go copying this
    handle(const handle &);
  };

  static map<string,int> s_name_id_map;
  static map<u_int32_t,BB2DomainInfo* > s_id_zone_map;
  static int s_first;
  static pthread_mutex_t s_zonemap_lock;

  string d_logprefix;
  int d_transaction_id;
  string d_transaction_tmpname;
  ofstream *d_of;
  handle *d_handle;
  void queueReload(BB2DomainInfo *bbd);

  void reload();
  static string DLDomStatusHandler(const vector<string>&parts, Utility::pid_t ppid);
  static string DLListRejectsHandler(const vector<string>&parts, Utility::pid_t ppid);
  static string DLReloadNowHandler(const vector<string>&parts, Utility::pid_t ppid);
  void loadConfig(string *status=0);
  void nukeZoneRecords(BB2DomainInfo *bbd);


};
