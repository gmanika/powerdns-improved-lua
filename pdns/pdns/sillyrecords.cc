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

void DNSPacket::addLOCRecord(const DNSResourceRecord &rr)
{
  addLOCRecord(rr.qname, rr.content, rr.ttl);
}

static unsigned int poweroften[10] = {1, 10, 100, 1000, 10000, 100000,
                                 1000000,10000000,100000000,1000000000};



/* converts ascii size/precision X * 10**Y(cm) to 0xXY. moves pointer.*/
static u_int8_t precsize_aton(const char **strptr)
{
  unsigned int mval = 0, cmval = 0;
  u_int8_t retval = 0;
  const char *cp;
  int exponent;
  int mantissa;

  cp = *strptr;

  while (isdigit(*cp))
    mval = mval * 10 + (*cp++ - '0');

  if (*cp == '.') {               /* centimeters */
    cp++;
    if (isdigit(*cp)) {
      cmval = (*cp++ - '0') * 10;
      if (isdigit(*cp)) {
	cmval += (*cp++ - '0');
      }
    }
  }
  cmval = (mval * 100) + cmval;

  for (exponent = 0; exponent < 9; exponent++)
    if (cmval < poweroften[exponent+1])
      break;

  mantissa = cmval / poweroften[exponent];
  if (mantissa > 9)
    mantissa = 9;

  retval = (mantissa << 4) | exponent;

  *strptr = cp;

  return (retval);
}

/* converts ascii lat/lon to unsigned encoded 32-bit number.
 *  moves pointer. */
static u_int32_t
latlon2ul(const char **latlonstrptr, int *which)
{
  const char *cp;
  u_int32_t retval;
  int deg = 0, min = 0, secs = 0, secsfrac = 0;

  cp = *latlonstrptr;

  while (isdigit(*cp))
    deg = deg * 10 + (*cp++ - '0');
  
  while (isspace(*cp))
    cp++;
  
  if (!(isdigit(*cp)))
    goto fndhemi;
  
  while (isdigit(*cp))
    min = min * 10 + (*cp++ - '0');
  
  
  while (isspace(*cp))
    cp++;
  
  if (!(isdigit(*cp)))
    goto fndhemi;
  
  while (isdigit(*cp))
    secs = secs * 10 + (*cp++ - '0');

  if (*cp == '.') {               /* decimal seconds */
    cp++;
    if (isdigit(*cp)) {
      secsfrac = (*cp++ - '0') * 100;
      if (isdigit(*cp)) {
	secsfrac += (*cp++ - '0') * 10;
	if (isdigit(*cp)) {
	  secsfrac += (*cp++ - '0');
	}
      }
    }
  }
  
  while (!isspace(*cp))   /* if any trailing garbage */
    cp++;
  
  while (isspace(*cp))
    cp++;
  
 fndhemi:
  switch (*cp) {
  case 'N': case 'n':
  case 'E': case 'e':
    retval = ((unsigned)1<<31)
      + (((((deg * 60) + min) * 60) + secs) * 1000)
      + secsfrac;
    break;
  case 'S': case 's':
  case 'W': case 'w':
    retval = ((unsigned)1<<31)
      - (((((deg * 60) + min) * 60) + secs) * 1000)
      - secsfrac;
    break;
  default:
    retval = 0;     /* invalid value -- indicates error */
    break;
  }
  
  switch (*cp) {
  case 'N': case 'n':
  case 'S': case 's':
    *which = 1;     /* latitude */
    break;
  case 'E': case 'e':
  case 'W': case 'w':
    *which = 2;     /* longitude */
    break;
  default:
    *which = 0;     /* error */
    break;
  }

  cp++;                   /* skip the hemisphere */
  
  while (!isspace(*cp))   /* if any trailing garbage */
    cp++;
  
  while (isspace(*cp))    /* move to next field */
    cp++;
  
  *latlonstrptr = cp;
  
  return (retval);
}

void DNSPacket::addLOCRecord(const string &domain, const string & content, u_int32_t ttl)
{
  const char *cp, *maxcp;
  unsigned char *bcp;
  
  u_int32_t latit = 0, longit = 0, alt = 0;
  u_int32_t lltemp1 = 0, lltemp2 = 0;
  int altmeters = 0, altfrac = 0, altsign = 1;
  u_int8_t hp = 0x16;    /* default = 1e6 cm = 10000.00m = 10km */
  u_int8_t vp = 0x13;    /* default = 1e3 cm = 10.00m */
  u_int8_t siz = 0x12;   /* default = 1e2 cm = 1.00m */
  int which1 = 0, which2 = 0;

  cp = content.c_str();
  maxcp = cp + strlen(content.c_str());

  lltemp1 = latlon2ul(&cp, &which1);


  lltemp2 = latlon2ul(&cp, &which2);

  switch (which1 + which2) {
  case 3:                 /* 1 + 2, the only valid combination */
    if ((which1 == 1) && (which2 == 2)) { /* normal case */
      latit = lltemp1;
      longit = lltemp2;
    } else if ((which1 == 2) && (which2 == 1)) {/*reversed*/
      longit = lltemp1;
      latit = lltemp2;
    } else {        /* some kind of brokenness */
      return;
    }
    break;
  default:                /* we didn't get one of each */
    return;
  }

  /* altitude */
  if (*cp == '-') {
    altsign = -1;
    cp++;
  }

  if (*cp == '+')
    cp++;
  
  while (isdigit(*cp))
    altmeters = altmeters * 10 + (*cp++ - '0');
  
  if (*cp == '.') {               /* decimal meters */
    cp++;
    if (isdigit(*cp)) {
      altfrac = (*cp++ - '0') * 10;
      if (isdigit(*cp)) {
	altfrac += (*cp++ - '0');
      }
    }
  }
  
  alt = (10000000 + (altsign * (altmeters * 100 + altfrac)));
  
  while (!isspace(*cp) && (cp < maxcp))
    /* if trailing garbage or m */
    cp++;
  
  while (isspace(*cp) && (cp < maxcp))
    cp++;
  
  
  if (cp >= maxcp)
    goto defaults;
  
  siz = precsize_aton(&cp);
  
  while (!isspace(*cp) && (cp < maxcp))/*if trailing garbage or m*/
    cp++;
  
  while (isspace(*cp) && (cp < maxcp))
    cp++;
  
  if (cp >= maxcp)
    goto defaults;
  
  hp = precsize_aton(&cp);
  
  while (!isspace(*cp) && (cp < maxcp))/*if trailing garbage or m*/
    cp++;
  
  while (isspace(*cp) && (cp < maxcp))
    cp++;
  
  if (cp >= maxcp)
    goto defaults;
  
  vp = precsize_aton(&cp);
  
 defaults:

  string piece1;
  toqname(domain, &piece1);

  char p[10];
  p[0]=0;p[1]=QType::LOC;
  p[2]=0;p[3]=1; 

  u_int32_t *ttlp=(u_int32_t *)(p+4);
  *ttlp=htonl(ttl); // 4, 5, 6, 7
  
  p[8]=0;
  p[9]=16; 
  
  string piece3;
  piece3.resize(4);
  piece3[0]=0;
  piece3[1]=siz;
  piece3[2]=hp;
  piece3[3]=vp;
 
  stringbuffer+=piece1;
  stringbuffer.append(p,10);
  stringbuffer+=piece3;
  latit=htonl(latit);   longit=htonl(longit);   alt=htonl(alt);
  stringbuffer.append((char *)&latit,4);
  stringbuffer.append((char *)&longit,4);
  stringbuffer.append((char *)&alt,4);
  d.ancount++;
}