#include <iostream>
#include <array>
#include <signal.h>
#include <chrono>
#include <thread>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <argp.h>

#include "libcec/cec.h"
#include "libcec/cecloader.h"
#include "lirc_client.h"

using namespace std;
using namespace CEC;

// The main loop will just continue until a ctrl-C is received
static bool exit_now = false;
static int lircFd = -1;
static uint32_t logMask = (CEC_LOG_ERROR | CEC_LOG_WARNING);
static ICECAdapter *CECAdapter;

const char *argp_program_version = "cec-lirc 1.0";
const char *argp_program_bug_address = "https://github.com/ballle98/cec-lirc";

/* The options we understand. */
static struct argp_option options[] = { { "verbose", 'v', 0, 0,
    "Produce verbose output" },
    { "quiet", 'q', 0, 0, "Don't produce any output" }, { 0 } };

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  switch (key) {
  case 'q':
    logMask = 0;
    break;
  case 'v':
    logMask = CEC_LOG_ALL;
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = { options, parse_opt, 0, 0 };

void handle_signal(int signal) {
  exit_now = true;
}

void CECLogMessage(void *not_used, const cec_log_message *message) {
  auto const now = chrono::system_clock::now();
  auto now_time = chrono::system_clock::to_time_t(now);
  auto duration = now.time_since_epoch();
  auto millis = chrono::duration_cast<chrono::milliseconds>(duration).count()
      % 1000;

  if (logMask & message->level) {
    cout << "[" << put_time(localtime(&now_time), "%D %T") << "." << dec
        << setw(4) << setfill('0') << millis << "] " << "LOG" << message->level
        << " " << message->message << endl;
  }
}

int send_packet(lirc_cmd_ctx *ctx, int fd) {
  int r;
  do {
    r = lirc_command_run(ctx, fd);
    if (r != 0 && r != EAGAIN) {
      cerr << "send_packet lirc_command_run Error " << strerror(r) << endl;
    }
  } while (r == EAGAIN);
  return r == 0 ? 0 : -1;
}

void CECKeyPress(void *cbParam, const cec_keypress *key) {
  static lirc_cmd_ctx ctx;

  (logMask & CEC_LOG_DEBUG)
      && cout << "CECKeyPress: key " << hex << unsigned(key->keycode)
          << " duration " << dec << unsigned(key->duration) << endl;

  switch (key->keycode) {
  case CEC_USER_CONTROL_CODE_VOLUME_UP:
    if (key->duration == 0) // key pressed
        {
      lirc_command_init(&ctx, "SEND_START Yamaha_RAV283 KEY_VOLUMEUP\n");
    } else {
      lirc_command_init(&ctx, "SEND_STOP Yamaha_RAV283 KEY_VOLUMEUP\n");
    }
    if (logMask & CEC_LOG_DEBUG) {
      lirc_command_reply_to_stdout(&ctx);
    }
    send_packet(&ctx, lircFd);
    break;
  case CEC_USER_CONTROL_CODE_VOLUME_DOWN:
    if (key->duration == 0) // key pressed
        {
      lirc_command_init(&ctx, "SEND_START Yamaha_RAV283 KEY_VOLUMEDOWN\n");
    } else {
      lirc_command_init(&ctx, "SEND_STOP Yamaha_RAV283 KEY_VOLUMEDOWN\n");
    }
    if (logMask & CEC_LOG_DEBUG) {
      lirc_command_reply_to_stdout(&ctx);
    }
    send_packet(&ctx, lircFd);
    break;
  case CEC_USER_CONTROL_CODE_MUTE:
    if (key->duration > 0) // key released
        {
      if (lirc_send_one(lircFd, "Yamaha_RAV283", "KEY_MUTE") == -1) {
        cerr << "CECKeyPress: lirc_send_one KEY_MUTE failed" << endl;
      }
    }
    break;
  default:
    break;
  }

}

void turnAudioOn() {
  (logMask & CEC_LOG_DEBUG)
      && cout << "CECCommand: lirc_send_one KEY_POWER" << endl;
  if (lirc_send_one(lircFd, "Yamaha_RAV283", "KEY_POWER") == -1) {
    cerr << "CECCommand: lirc_send_one KEY_POWER failed" << endl;
  }
  CECAdapter->AudioEnable(true);
}

void turnAudioOff() {
  (logMask & CEC_LOG_DEBUG)
      && cout << "CECCommand: lirc_send_one KEY_SUSPEND" << endl;
  if (lirc_send_one(lircFd, "Yamaha_RAV283", "KEY_SUSPEND") == -1) {
    cerr << "CECCommand: lirc_send_one KEY_SUSPEND failed" << endl;
  }
  // :TODO: CCECAudioSystem::SetSystemAudioModeStatus
  CECAdapter->AudioEnable(false);
}

void CECCommand(void *cbParam, const cec_command *command) {
  (logMask & CEC_LOG_DEBUG)
      && cout << "CECCommand: opcode " << hex << unsigned(command->opcode)
          << " " << unsigned(command->initiator) << " -> "
          << unsigned(command->destination) << endl;
  switch (command->opcode) {
  case CEC_OPCODE_STANDBY: //0x36 0f:36
    turnAudioOff();
    break;
  case CEC_OPCODE_USER_CONTROL_PRESSED: // 0x44
    break;
  case CEC_OPCODE_USER_CONTROL_RELEASE: // 0x45
    break;
  case CEC_OPCODE_SYSTEM_AUDIO_MODE_REQUEST: // 0x70  05:70:00:00
    // From https://www.hdmi.org/docs/Hdmi13aSpecs
    //
    // The amplifier comes out of standby (if necessary) and switches to the
    // relevant connector for device specified by [Physical Address]. It then
    // sends a <Set System Audio Mode> [On] message.
    //
    // <System Audio Mode Request> sent without a [Physical Address]
    // parameter requests termination of the feature. In this case, the
    // amplifier sends a <Set System Audio Mode> [Off] message.
    turnAudioOn();
    // libCEC should return 50:72:01 (on) or 50:72:00 (off)
    break;
  case CEC_OPCODE_SYSTEM_AUDIO_MODE_STATUS: // 0x7E
    break;
  case CEC_OPCODE_ROUTING_CHANGE: // 0x80
    break;
  case CEC_OPCODE_ACTIVE_SOURCE: // 0x82
    break;
  case CEC_OPCODE_VENDOR_COMMAND: //0x89
    break;
  case CEC_OPCODE_REPORT_POWER_STATUS: // 0x90
    if (command->parameters.data[0] == CEC_POWER_STATUS_STANDBY) {
      turnAudioOff();
    }
    break;
  case CEC_OPCODE_REQUEST_SHORT_AUDIO_DESCRIPTORS:  // 0xA4
    break;
  default:
    break;
  }
}

void CECAlert(void *cbParam, const libcec_alert type,
    const libcec_parameter param) {
  switch (type) {
  case CEC_ALERT_CONNECTION_LOST:
    cout << "Connection lost" << endl;
    break;
  default:
    break;
  }
}

int main(int argc, char *argv[]) {
  ICECCallbacks CECCallbacks;
  libcec_configuration CECConfig;

  argp_parse(&argp, argc, argv, 0, 0, 0);

  // Install the ctrl-C signal handler
  if ( SIG_ERR == signal(SIGINT, handle_signal)) {
    cerr << "Failed to install the SIGINT signal handler\n";
    return 1;
  }

  lircFd = lirc_get_local_socket("/var/run/lirc/lircd-tx", 0);
  if (lircFd < 0) {
    cerr << "Failed to get LIRC local socket" << endl;
    UnloadLibCec(CECAdapter);
    return 1;
  }

  (logMask & CEC_LOG_DEBUG)
      && cout << "lirc_get_local_socket " << lircFd << endl;

  CECConfig.Clear();
  CECCallbacks.Clear();
  snprintf(CECConfig.strDeviceName, LIBCEC_OSD_NAME_SIZE, "CECtoIR");
  CECConfig.clientVersion = LIBCEC_VERSION_CURRENT;
  CECConfig.cecVersion = CEC_VERSION_1_3A;
  CECConfig.bActivateSource = 0;
  CECCallbacks.logMessage = &CECLogMessage;
  CECCallbacks.keyPress = &CECKeyPress;
  CECCallbacks.commandReceived = &CECCommand;
  CECCallbacks.alert = &CECAlert;
  CECConfig.callbacks = &CECCallbacks;

  CECConfig.deviceTypes.Add(CEC_DEVICE_TYPE_AUDIO_SYSTEM);
//  CECConfig.deviceTypes.Add(CEC_DEVICE_TYPE_TUNER);

  if (!(CECAdapter = LibCecInitialise(&CECConfig))) {
    cerr << "LibCecInitialise failed" << endl;
    return 1;
  }

  (logMask & CEC_LOG_DEBUG)
      && cout << "*** LibCecInitialise complete ***" << endl;

  array<cec_adapter_descriptor, 10> devices;

  (logMask & CEC_LOG_DEBUG) && cout << "*** DetectAdapters start ***" << endl;

  int8_t devices_found = CECAdapter->DetectAdapters(devices.data(),
      devices.size(), nullptr, false);
  if (devices_found <= 0) {
    cerr << "Could not automatically determine the cec adapter devices" << endl;
    UnloadLibCec(CECAdapter);
    return 1;
  }

  (logMask & CEC_LOG_DEBUG)
      && cout << unsigned(devices_found) << " devices found" << endl;

  // Open a connection to the zeroth CEC device
  if (!CECAdapter->Open(devices[0].strComName)) {
    cerr << "Failed to open the CEC device on port " << devices[0].strComName
        << endl;
    UnloadLibCec(CECAdapter);
    return 1;
  }
  (logMask & CEC_LOG_DEBUG) && cout << "*** CEC device opened ***" << endl;

  if (logMask & CEC_LOG_DEBUG) {
    cec_version audioCecVer = CECAdapter->GetDeviceCecVersion(
        CECDEVICE_AUDIOSYSTEM);
    cout << "Audio CEC Version 0x" << hex << audioCecVer << endl;
  }

  (logMask & CEC_LOG_DEBUG) && cout << "waiting for ctl-c" << endl;

  // Loop until ctrl-C occurs
  while (!exit_now) {
    // :TODO: change to a task suspend
    // nothing to do.  All happens in the CEC callback on another thread
    this_thread::sleep_for(chrono::seconds(1));
  }

  // Close down and cleanup
  cerr << "Close and cleanup" << endl;

  CECAdapter->Close();
  UnloadLibCec(CECAdapter);

  // :TODO: lirc cleanup

  return 0;
}

