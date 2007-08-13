#include <bitset>
#include "dnsparser.hh"
#include "iputils.hh"
#undef L
#include <boost/program_options.hpp>

#include <boost/format.hpp>
#include <boost/utility.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/algorithm/string.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mplexer.hh"

using namespace boost;
using namespace ::boost::multi_index;
using namespace std;

namespace po = boost::program_options;
po::variables_map g_vm;

SelectFDMultiplexer g_fdm;
int g_pdnssocket;
bool g_verbose;

struct NotificationInFlight
{
  ComboAddress source;
  time_t resentTime;
  string domain;
  uint16_t origID, resentID;
  int origSocket;
};

typedef map<uint16_t, NotificationInFlight> nifs_t;
nifs_t g_nifs;

void handleOutsideUDPPacket(int fd, boost::any&)
try
{
  char buffer[1500];
  struct NotificationInFlight nif;
  nif.origSocket = fd;

  socklen_t socklen=sizeof(nif.source);

  int res=recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&nif.source, &socklen);
  if(!res)
    return;

  if(res < 0) 
    throw runtime_error("reading packet from remote: "+stringerror());
    
  string packet(buffer, res);
  MOADNSParser mdp(packet);
  nif.domain = mdp.d_qname;
  nif.origID = mdp.d_header.id;

  cerr<<"External notification received for: "<< nif.domain << endl;

  if(mdp.d_header.opcode != Opcode::Notify || mdp.d_qtype != QType::SOA) {
    cerr<<"Opcode: "<<mdp.d_header.opcode<<", != notify\n";
    return;
  }
  
  vector<uint8_t> outpacket;
  DNSPacketWriter pw(outpacket, mdp.d_qname, mdp.d_qtype, 1, Opcode::Notify);

  static uint16_t s_idpool;
  pw.getHeader()->id = nif.resentID = s_idpool++;
  
  if(send(g_pdnssocket, &outpacket[0], outpacket.size(), 0) < 0) {
    throw runtime_error("Unable to send notify to PowerDNS: "+stringerror());
  }
  nif.resentTime=time(0);
  g_nifs[nif.resentID] = nif;

}
catch(exception &e)
{
  cerr<<"Error parsing incoming packet: "<<e.what()<<endl;
}


void handleInsideUDPPacket(int fd, boost::any&)
try
{
  char buffer[1500];
  struct NotificationInFlight nif;

  socklen_t socklen=sizeof(nif.source);

  int len=recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&nif.source, &socklen);
  if(!len)
    return;

  if(len < 0) 
    throw runtime_error("reading packet from remote: "+stringerror());
    
  string packet(buffer, len);
  MOADNSParser mdp(packet);

  cerr<<"Inside notification response for: "<<mdp.d_qname<<endl;

  if(!g_nifs.count(mdp.d_header.id)) {
    cerr<<"Response from inner PowerDNS with unknown ID "<<mdp.d_header.id<<endl;
    return;
  }
  
  nif=g_nifs[mdp.d_header.id];

  if(!iequals(nif.domain,mdp.d_qname)) {
    cerr<<"Response from inner PowerDNS for different domain '"<<mdp.d_qname<<"' than original notification '"<<nif.domain<<"'"<<endl;
  } else {
    struct dnsheader dh;
    memcpy(&dh, buffer, sizeof(dh));
    dh.id = nif.origID;
    
    if(sendto(nif.origSocket, buffer, len, 0, (sockaddr*) &nif.source, nif.source.getSocklen()) < 0) {
      throw runtime_error("Unable to send notify to PowerDNS: "+stringerror());
    }
  }
  g_nifs.erase(mdp.d_header.id);

}
catch(exception &e)
{
  cerr<<"Error parsing incoming packet: "<<e.what()<<endl;
}

void expireOldNotifications()
{
  time_t limit = time(0) - 10;
  for(nifs_t::iterator iter = g_nifs.begin(); iter != g_nifs.end(); ) {
    if(iter->second.resentTime < limit) {
      cerr<<"Removing notification proxy entry for '"<<iter->second.domain<<"', expired"<<endl;
      g_nifs.erase(iter++);
    }
    else
      ++iter;
  }
}

void daemonize();

int main(int argc, char** argv)
try
{
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h", "produce help message")
    ("powerdns-address", po::value<string>(), "IP address of PowerDNS server")
    ("origin-address", po::value<string>()->default_value("::"), "Source address for notifications to PowerDNS")
    ("listen-address", po::value<vector<string> >(), "IP addresses to listen on")
    ("daemon,d", po::value<bool>()->default_value(true), "operate in the background")
    ("verbose,v", "be verbose");

  po::store(po::command_line_parser(argc, argv).options(desc).run(), g_vm);
  po::notify(g_vm);

  if (g_vm.count("help")) {
    cerr << desc << "\n";
    return EXIT_SUCCESS;
  }

  if(!g_vm.count("powerdns-address")) {
    cerr<<"Mandatory setting 'powerdns-address' unset:\n"<<desc<<endl;
    return EXIT_FAILURE;
  }

  if(!g_vm.count("verbose")) {
    g_verbose=true;
  }
  
  vector<string> addresses;
  if(g_vm.count("listen-address"))
    addresses=g_vm["listen-address"].as<vector<string> >();
  else
    addresses.push_back("::");

  // create sockets to listen on
  
  cerr<<"Binding sockets\n";
  for(vector<string>::const_iterator address = addresses.begin(); address != addresses.end(); ++address) {
    ComboAddress local(*address, 53);
    int sock = socket(local.sin4.sin_family, SOCK_DGRAM, 0);
    if(sock < 0)
      throw runtime_error("Creating socket for incoming packets: "+stringerror());

    if(::bind(sock,(sockaddr*) &local, local.getSocklen()) < 0)
      throw runtime_error("Binding socket for incoming packets to '"+ local.toStringWithPort()+"': "+stringerror());

    g_fdm.addReadFD(sock, handleOutsideUDPPacket); // add to fdmultiplexer for each socket
  }

  // create socket that talks to inner PowerDNS

  g_pdnssocket=socket(AF_INET, SOCK_DGRAM, 0);
  if(g_pdnssocket < 0)
    throw runtime_error("Creating socket for packets to PowerDNS: "+stringerror());

  ComboAddress originAddress(g_vm["origin-address"].as<string>(), 0);
  if(::bind(g_pdnssocket,(sockaddr*) &originAddress, originAddress.getSocklen()) < 0)
      throw runtime_error("Binding local address of inward socket to '"+ originAddress.toStringWithPort()+"': "+stringerror());
  

  ComboAddress pdns(g_vm["powerdns-address"].as<string>(), 53);
  if(connect(g_pdnssocket, (struct sockaddr*) &pdns, pdns.getSocklen()) < 0) 
    throw runtime_error("Failed to connect PowerDNS socket to address "+pdns.toString()+": "+stringerror());

  g_fdm.addReadFD(g_pdnssocket, handleInsideUDPPacket);

  if(g_vm.count("chroot")) {
    if(chroot(g_vm["chroot"].as<string>().c_str()) < 0)
      throw runtime_error("while chrooting to "+g_vm["chroot"].as<string>());
  }

  if(g_vm["daemon"].as<bool>()) {
    daemonize();
  }


  // start loop
  struct timeval now;
  for(;;) {
    gettimeofday(&now, 0);
    g_fdm.run(&now);
    // check for notifications that have been outstanding for more than 10 seconds
    expireOldNotifications();
  }
}
catch(exception& e)
{
  cerr<<"Fatal: "<<e.what()<<endl;
}
catch(AhuException& e)
{
  cerr<<"Fatal: "<<e.reason<<endl;
}

/* added so we don't have to link in most of powerdns */

const char *Utility::inet_ntop(int af, const char *src, char *dst, size_t size)
{
  return ::inet_ntop(af,src,dst,size);
}

// Converts an address from presentation format to network format.
int Utility::inet_pton( int af, const char *src, void *dst )
{
  return ::inet_pton(af, src, dst);
}

// Compares two string, ignoring the case.
int Utility::strcasecmp( const char *s1, const char *s2 )
{
  return ::strcasecmp( s1, s2 );
}

// Returns the current time.
int Utility::gettimeofday( struct timeval *tv, void *tz )
{
  return ::gettimeofday(tv,0);
}

string stringerror()
{
  return strerror(errno);
}

bool IpToU32(const string &str, uint32_t *ip)
{
  if(str.empty()) {
    *ip=0;
    return true;
  }
  
  struct in_addr inp;
  if(inet_aton(str.c_str(), &inp)) {
    *ip=inp.s_addr;
    return true;
  }
  return false;
}

void daemonize(void)
{
  if(fork())
    exit(0); // bye bye
  
  setsid(); 

  int i=open("/dev/null",O_RDWR); /* open stdin */
  if(i < 0) 
    cerr<<"Unable to open /dev/null: "<<stringerror()<<endl;
  else {
    dup2(i,0); /* stdin */
    dup2(i,1); /* stderr */
    dup2(i,2); /* stderr */
    close(i);
  }
}
