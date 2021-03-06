// $Id$ 
#include <string>
#include <map>

using namespace std;

#include "pdns/dns.hh"
#include "pdns/dnsbackend.hh"
#include "gmysqlbackend.hh"
#include "pdns/dnspacket.hh"
#include "pdns/ueberbackend.hh"
#include "pdns/ahuexception.hh"
#include "pdns/logger.hh"
#include "pdns/arguments.hh"

#include "smysql.hh"


#include <sstream>

gMySQLBackend::gMySQLBackend(const string &mode, const string &suffix)  : GSQLBackend(mode,suffix)
{
  try {
    setDB(new SMySQL(getArg("dbname"),
        	     getArg("host"),
        	     getArgAsNum("port"),
        	     getArg("socket"),
        	     getArg("user"),
        	     getArg("password")));
    
  }
  
  catch(SSqlException &e) {
    L<<Logger::Error<<mode<<" Connection failed: "<<e.txtReason()<<endl;
    throw AhuException("Unable to launch "+mode+" connection: "+e.txtReason());
  }
  L<<Logger::Warning<<mode<<" Connection succesful"<<endl;
}

class gMySQLFactory : public BackendFactory
{
public:
  gMySQLFactory(const string &mode) : BackendFactory(mode),d_mode(mode) {}
  
  void declareArguments(const string &suffix="")
  {
    declare(suffix,"dbname","Pdns backend database name to connect to","powerdns");
    declare(suffix,"user","Database backend user to connect as","powerdns");
    declare(suffix,"host","Database backend host to connect to","");
    declare(suffix,"port","Database backend port to connect to","0");
    declare(suffix,"socket","Pdns backend socket to connect to","");
    declare(suffix,"password","Pdns backend password to connect with","");
    declare(suffix,"dnssec","Assume DNSSEC Schema is in place","false");

    declare(suffix,"basic-query","Basic query","select content,ttl,prio,type,domain_id,name from records where type='%s' and name='%s'");
    declare(suffix,"id-query","Basic with ID query","select content,ttl,prio,type,domain_id,name from records where type='%s' and name='%s' and domain_id=%d");
    declare(suffix,"wildcard-query","Wildcard query","select content,ttl,prio,type,domain_id,name from records where type='%s' and name like '%s'");
    declare(suffix,"wildcard-id-query","Wildcard with ID query","select content,ttl,prio,type,domain_id,name from records where type='%s' and name like '%s' and domain_id='%d'");

    declare(suffix,"any-query","Any query","select content,ttl,prio,type,domain_id,name from records where name='%s'");
    declare(suffix,"any-id-query","Any with ID query","select content,ttl,prio,type,domain_id,name from records where name='%s' and domain_id=%d");
    declare(suffix,"wildcard-any-query","Wildcard ANY query","select content,ttl,prio,type,domain_id,name from records where name like '%s'");
    declare(suffix,"wildcard-any-id-query","Wildcard ANY with ID query","select content,ttl,prio,type,domain_id,name from records where name like '%s' and domain_id='%d'");

    declare(suffix,"list-query","AXFR query", "select content,ttl,prio,type,domain_id,name from records where domain_id='%d'");
  
    // and now with auth
    declare(suffix,"basic-query-auth","Basic query","select content,ttl,prio,type,domain_id,name, auth from records where type='%s' and name='%s'");
    declare(suffix,"id-query-auth","Basic with ID query","select content,ttl,prio,type,domain_id,name, auth from records where type='%s' and name='%s' and domain_id=%d");
    declare(suffix,"wildcard-query-auth","Wildcard query","select content,ttl,prio,type,domain_id,name, auth from records where type='%s' and name like '%s'");
    declare(suffix,"wildcard-id-query-auth","Wildcard with ID query","select content,ttl,prio,type,domain_id,name, auth from records where type='%s' and name like '%s' and domain_id='%d'");

    declare(suffix,"any-query-auth","Any query","select content,ttl,prio,type,domain_id,name, auth from records where name='%s'");
    declare(suffix,"any-id-query-auth","Any with ID query","select content,ttl,prio,type,domain_id,name, auth from records where name='%s' and domain_id=%d");
    declare(suffix,"wildcard-any-query-auth","Wildcard ANY query","select content,ttl,prio,type,domain_id,name, auth from records where name like '%s'");
    declare(suffix,"wildcard-any-id-query-auth","Wildcard ANY with ID query","select content,ttl,prio,type,domain_id,name, auth from records where name like '%s' and domain_id='%d'");

    declare(suffix,"list-query-auth","AXFR query", "select content,ttl,prio,type,domain_id,name, auth from records where domain_id='%d'");
    
    
    
    declare(suffix,"master-zone-query","Data", "select master from domains where name='%s' and type='SLAVE'");

    declare(suffix,"info-zone-query","","select id,name,master,last_check,notified_serial,type from domains where name='%s'");

    declare(suffix,"info-all-slaves-query","","select id,name,master,last_check,type from domains where type='SLAVE'");
    declare(suffix,"supermaster-query","", "select account from supermasters where ip='%s' and nameserver='%s'");
    declare(suffix,"insert-slave-query","", "insert into domains (type,name,master,account) values('SLAVE','%s','%s','%s')");
    declare(suffix,"insert-record-query","", "insert into records (content,ttl,prio,type,domain_id,name) values ('%s',%d,%d,'%s',%d,'%s')");
    
    declare(suffix,"get-order-before-query","DNSSEC Ordering Query, before", "select ordername, name from records where ordername <= '%s' and auth=1 and domain_id=%d order by 1 desc limit 1");
    declare(suffix,"get-order-after-query","DNSSEC Ordering Query, afer", "select min(ordername) from records where ordername > '%s' and auth=1 and domain_id=%d");
    declare(suffix,"set-order-and-auth-query", "DNSSEC set ordering query", "update records set ordername='%s',auth=%d where name='%s' and domain_id='%d'");
    
    
    declare(suffix,"update-serial-query","", "update domains set notified_serial=%d where id=%d");
    declare(suffix,"update-lastcheck-query","", "update domains set last_check=%d where id=%d");
    declare(suffix,"info-all-master-query","", "select id,name,master,last_check,notified_serial,type from domains where type='MASTER'");
    declare(suffix,"delete-zone-query","", "delete from records where domain_id=%d");
    declare(suffix,"check-acl-query","", "select value from acls where acl_type='%s' and acl_key='%s'");
  }
  
  DNSBackend *make(const string &suffix="")
  {
    return new gMySQLBackend(d_mode,suffix);
  }
private:
  const string d_mode;
};


//! Magic class that is activated when the dynamic library is loaded
class gMySQLLoader
{
public:
  //! This reports us to the main UeberBackend class
  gMySQLLoader()
  {
    BackendMakers().report(new gMySQLFactory("gmysql"));
    L<<Logger::Warning<<"This is module gmysqlbackend.so reporting"<<endl;
  }
};
static gMySQLLoader gmysqlloader;
