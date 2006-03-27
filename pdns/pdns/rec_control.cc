/*
    PowerDNS Versatile Database Driven Nameserver
    Copyright (C) 2006 PowerDNS.COM BV

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as 
    published by the Free Software Foundation

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include "rec_channel.hh"
#include <iostream>
#include "ahuexception.hh"
#include "arguments.hh"

using namespace std;

ArgvMap &arg()
{
  static ArgvMap arg;
  return arg;
}

static void initArguments(int argc, char** argv)
{
  arg().set("config-dir","Location of configuration directory (pdns.conf)")=SYSCONFDIR;
  arg().set("socket-dir","Where the controlsocket will live")=LOCALSTATEDIR;
  arg().setCmd("help","Provide this helpful message");

  arg().laxParse(argc,argv);  
  if(arg().mustDo("help")) {
    cerr<<"syntax:"<<endl<<endl;
    cerr<<arg().helpstring(arg()["help"])<<endl;
    exit(99);
  }

}

int main(int argc, char** argv)
try
{
  initArguments(argc, argv);

  RecursorControlChannel rccS;
  rccS.connect(arg()["socket-dir"], "pdns_recursor.controlsocket");

  const vector<string>&commands=arg().getCommands();
  string command;
  for(unsigned int i=0; i< commands.size(); ++i) {
    if(i>0)
      command+=" ";
    command+=commands[i];
  }
  rccS.send(command);
  string receive=rccS.recv();
  cout<<receive;
}
catch(AhuException& ae)
{
  cerr<<"Fatal: "<<ae.reason<<"\n";
}
