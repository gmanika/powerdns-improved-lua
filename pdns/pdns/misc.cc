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
#include "misc.hh"
#include <vector>
#include <sstream>
#include <errno.h>
#include <sys/param.h>
#include <cstring>

#include <iomanip>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef WIN32
# include <sys/time.h>
# include <time.h>
# include <netinet/in.h>
# include <unistd.h>
#endif // WIN32

#include "utility.hh"



int sendData(const char *buffer, int replen, int outsock)
{
  u_int16_t nlen=htons(replen);
  Utility::iovec iov[2];
  iov[0].iov_base=(char*)&nlen;
  iov[0].iov_len=2;
  iov[1].iov_base=(char*)buffer;
  iov[1].iov_len=replen;
  int ret=Utility::writev(outsock,iov,2);

  if(ret<0) {
    return -1;
  }
  if(ret!=replen+2) {
    return -1;
  }
  return 0;
}


void parseService(const string &descr, ServiceTuple &st)
{
  vector<string>parts;
  stringtok(parts,descr,":");
  st.host=parts[0];
  if(parts.size()>1)
    st.port=atoi(parts[1].c_str());
}

int matchNetmask(const char *address, const char *omask)
{
  struct in_addr a,m;
  int bits=32;
  char *sep;

  char *mask=strdup(omask);
  sep=strchr(mask,'/');

  if(sep) {
    bits=atoi(sep+1);
    *sep=0;
  }

  if(!Utility::inet_aton(address, &a) || !Utility::inet_aton(mask, &m))
  {
    free(mask);
    return -1;
  }

  free(mask);

  // bits==32 -> 0xffffffff
  // bits==16 -> 0xffff0000
  // bits==0 ->  0x00000000
  unsigned int bmask=~((1<<(32-bits))-1);     // 1<<16 0000 0000  0000 0000  0000 0000  0000 0000

  /*
  fprintf(stderr,"%x\n",bmask);
  fprintf(stderr,"%x\n",(htonl((unsigned int)a.s_addr) & bmask));
  fprintf(stderr,"%x\n",(htonl((unsigned int)m.s_addr) & bmask));
  */

  return ((htonl((unsigned int)a.s_addr) & bmask) == (htonl((unsigned int)m.s_addr) & bmask));
}

int waitForData(int fd, int seconds)
{
  struct timeval tv;
  int ret;

  tv.tv_sec   = seconds;
  tv.tv_usec  = 0;

  fd_set readfds;
  FD_ZERO( &readfds );
  FD_SET( fd, &readfds );

  ret = select( fd + 1, &readfds, NULL, NULL, &tv );
  if ( ret == -1 )
  {
    ret = -1;
    errno = ETIMEDOUT;
  }

  return ret;
}


string humanDuration(time_t passed)
{
  ostringstream ret;
  if(passed<60)
    ret<<passed<<" seconds";
  else if(passed<3600)
    ret<<setprecision(2)<<passed/60.0<<" minutes";
  else if(passed<86400)
    ret<<setprecision(3)<<passed/3600.0<<" hours";
  else if(passed<(86400*30.41))
    ret<<setprecision(3)<<passed/86400.0<<" days";
  else
    ret<<setprecision(3)<<passed/(86400*30.41)<<" months";

  return ret.str();
}

DTime::DTime()
{
//  set();
}

DTime::DTime(const DTime &dt)
{
  d_set=dt.d_set;
}

time_t DTime::time()
{
  return d_set.tv_sec;
}

// Make s uppercase:
void upperCase(string& s) {
  for(unsigned int i = 0; i < s.length(); i++)
    s[i] = toupper(s[i]);
}


void chomp(string &line, const string &delim)
{
  string::reverse_iterator i;
  for( i=line.rbegin();i!=line.rend();++i) 
    if(delim.find(*i)==string::npos) 
      break;
  
  line.resize(line.rend()-i);
}




void stripLine(string &line)
{
  unsigned int pos=line.find_first_of("\r\n");
  if(pos!=string::npos) {
    line.resize(pos);
  }
}

string urlEncode(const string &text)
{
  string ret;
  for(string::const_iterator i=text.begin();i!=text.end();++i)
    if(*i==' ')ret.append("%20");
    else ret.append(1,*i);
  return ret;
}

string getHostname()
{
  char tmp[MAXHOSTNAMELEN];
  if(gethostname(tmp, MAXHOSTNAMELEN))
    return "UNKNOWN";

  return tmp;
}

string itoa(int i)
{
  ostringstream o;
  o<<i;
  return o.str();
}

string stringerror()
{
  return strerror(errno);
}

void cleanSlashes(string &str)
{
  string::const_iterator i;
  string out;
  for(i=str.begin();i!=str.end();++i) {
    if(*i=='/' && i!=str.begin() && *(i-1)=='/')
      continue;
    out.append(1,*i);
  }
  str=out;
}

const string sockAddrToString(struct sockaddr_in *remote, socklen_t socklen) 
{    
  if(socklen==sizeof(struct sockaddr_in))
     return inet_ntoa(((struct sockaddr_in *)remote)->sin_addr);

  // TODO: Add ipv6 support here.
#ifndef WIN32
  else {
    char tmp[128];
    
    if(!inet_ntop(AF_INET6, (void*)&((struct sockaddr_in6 *)remote)->sin6_addr, tmp, sizeof(tmp)))
      return "IPv6 untranslateable";

    return tmp;
  }
#endif // WIN32

  return "untranslateable";
}