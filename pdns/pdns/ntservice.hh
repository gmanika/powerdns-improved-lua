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

/*!
\file ntservice.hh
\brief This file contains the NTService class specification.
*/

#ifndef NTSERVICE_HH
#define NTSERVICE_HH

#define WINDOWS_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include "singleton.hh"


//! The NTService class is responsible for giving the program NT Service characteristics.
class NTService : public Singleton< NTService >
{
private:
  //! Is the program running as a NT Service?
  bool m_runningAsService;
  
  //! Service status handle.
  SERVICE_STATUS_HANDLE m_serviceStatusHandle;
  
protected:
  //! Status code.
  DWORD m_statusCode;

  //! Error code.
  DWORD m_errorCode;

  //! Main function.
  virtual int main( int argc, char *argv[] )
  {
    return 0;
  }

  //! Control handler.
  virtual void ctrlHandler( DWORD controlCode )
  {
  }

  //! Sets the service's status and error codes.
  void setStatus( DWORD status, DWORD error = 0 );
  
public:
  //! Default constructor.
  NTService( void );

  //! Destructor.
  virtual ~NTService( void );

  //! Starts the service.
  int start( int argc, char *argv[], bool asService = true );

  //! Control handler (calls NTService::ctrlHandler()).
  static void WINAPI s_ctrlHandler( DWORD controlCode );
  
  //! Service main (calls NTService::main()).
  static void WINAPI s_serviceMain( DWORD argc, LPTSTR *argv );
  
  //! Returns the name of the service.
  virtual std::string getServiceName( void )
  {
    return "NTService";
  }

  //! Returns whether the program is running as a service or not.
  bool isRunningAsService( void );
  
  //! Registers the service with the system.
  bool registerService( const std::string & description, bool registerLog = true );

  //! Unregisters the service.
  bool unregisterService( void );
  
  //! Stops the service.
  bool stop( void );

};


#endif // NTSERVICE_H

