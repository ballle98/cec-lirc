#include <iostream>
#include <array>
#include <signal.h>
#include <chrono>
#include <thread>

#include "libcec/cec.h"
#include "libcec/cecloader.h"

using namespace std;
using namespace CEC;

// The main loop will just continue until a ctrl-C is received
bool exit_now = false;
void handle_signal(int signal)
{
    exit_now = true;
}

void CECLogMessage(void *not_used, const cec_log_message* message)
{
	cout << "LOG" << message->level << " " << message->message << endl;
}

void CECKeyPress(void *cbParam, const cec_keypress* key)
{
    cout << "CECKeyPress: key " << unsigned(key->keycode) << " duration " << unsigned(key->duration) << std::endl;
}

void CECCommand(void *cbParam, const cec_command* command)
{
    cout << "CECCommand: opcode " << unsigned(command->opcode) << " dest " << unsigned(command->destination) << std::endl;
}

void CECAlert(void *cbParam, const libcec_alert type, const libcec_parameter param)
{
  switch (type)
  {
  case CEC_ALERT_CONNECTION_LOST:
  	  cout << "Connection lost" <<endl;
	  break;
  default:
    break;
  }
}


int main (int argc, char *argv[])
{
  ICECCallbacks         CECCallbacks;
  libcec_configuration  CECConfig;
  ICECAdapter*          CECAdapter;

  // Install the ctrl-C signal handler
  if( SIG_ERR == signal(SIGINT, handle_signal) )
  {
      cerr << "Failed to install the SIGINT signal handler\n";
      return 1;
  }

  CECConfig.Clear();
  CECCallbacks.Clear();
  snprintf(CECConfig.strDeviceName, LIBCEC_OSD_NAME_SIZE, "CECtoIR");
  CECConfig.clientVersion      = LIBCEC_VERSION_CURRENT;
  CECConfig.bActivateSource    = 0;
  CECCallbacks.logMessage      = &CECLogMessage;
  CECCallbacks.keyPress        = &CECKeyPress;
  CECCallbacks.commandReceived = &CECCommand;
  CECCallbacks.alert           = &CECAlert;
  CECConfig.callbacks          = &CECCallbacks;

  // CECConfig.deviceTypes.Add(CEC_DEVICE_TYPE_TUNER);
  CECConfig.deviceTypes.Add(CEC_DEVICE_TYPE_AUDIO_SYSTEM);

  if (! (CECAdapter = LibCecInitialise(&CECConfig)) )
  {
	  cerr << "LibCecInitialise failed"  << endl;
	  return 1;
  }

  array<cec_adapter_descriptor,10> devices;

  int8_t devices_found = CECAdapter->DetectAdapters(devices.data(), devices.size(), nullptr, false);
  if( devices_found <= 0)
   {
       cerr << "Could not automatically determine the cec adapter devices\n";
       UnloadLibCec(CECAdapter);
       return 1;
   }

  cerr << unsigned(devices_found) << " devices found" << endl;

  // Open a connection to the zeroth CEC device
   if( !CECAdapter->Open(devices[0].strComName) )
   {
       cerr << "Failed to open the CEC device on port " << devices[0].strComName << std::endl;
       UnloadLibCec(CECAdapter);
       return 1;
   }

   cerr << "waiting for ctl-c" << endl;

  // Loop until ctrl-C occurs
  while( !exit_now )
  {
      // nothing to do.  All happens in the CEC callback on another thread
      this_thread::sleep_for( std::chrono::seconds(1) );
  }

  // Close down and cleanup
  cerr << "Close and cleanup" << endl;

  CECAdapter->Close();
  UnloadLibCec(CECAdapter);

  return 0;
}


