#include <iostream>
#include "recpacketcache.hh"
#include "cachecleaner.hh"
#include "dns.hh"
#include "namespaces.hh"
#include "lock.hh"


RecursorPacketCache::RecursorPacketCache()
{
  d_hits = d_misses = 0;
}

bool RecursorPacketCache::getResponsePacket(const std::string& queryPacket, time_t now, 
  std::string* responsePacket, uint32_t* age)
{
  struct Entry e;
  e.d_packet=queryPacket;
  
  packetCache_t::const_iterator iter = d_packetCache.find(e);
  
  if(iter == d_packetCache.end()) {
    d_misses++;
    return false;
  }
    
  if((uint32_t)now < iter->d_ttd) { // it is fresh!
//    cerr<<"Fresh for another "<<iter->d_ttd - now<<" seconds!"<<endl;
    *age = now - iter->d_creation;
    uint16_t id = ((struct dnsheader*)queryPacket.c_str())->id;
    *responsePacket = iter->d_packet;
    ((struct dnsheader*)responsePacket->c_str())->id=id;
    d_hits++;
    moveCacheItemToBack(d_packetCache, iter);

    return true;
  }
  moveCacheItemToFront(d_packetCache, iter);
  d_misses++;
  return false;
}

void RecursorPacketCache::insertResponsePacket(const std::string& responsePacket, time_t now, uint32_t ttl)
{
  struct Entry e;
  e.d_packet = responsePacket;
  e.d_ttd = now+ttl;
  e.d_creation = now;
  packetCache_t::iterator iter = d_packetCache.find(e);
  
  if(iter != d_packetCache.end()) {
    iter->d_packet = responsePacket;
    iter->d_ttd = now + ttl;
    iter->d_creation = now;
  }
  else 
    d_packetCache.insert(e);
}

uint64_t RecursorPacketCache::size()
{
  return d_packetCache.size();
}

void RecursorPacketCache::doPruneTo(unsigned int maxCached)
{
  pruneCollection(d_packetCache, maxCached);
}

