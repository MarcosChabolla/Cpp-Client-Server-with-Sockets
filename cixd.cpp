// $Id: cixd.cpp,v 1.7 2016-05-09 16:01:56-07 - - $
//Marcos Chabolla
//ID:mchaboll@ucsc.edu
//Partner: Amit Khatri
//ID: akhatri@ucsc.edu
#include <string>
#include <vector>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"
#include "util.h"

logstream log (cout);
struct cix_exit: public exception {};

bool do_put(accepted_socket& client_sock, cix_header&);
bool do_rm(accepted_socket& client_sock, cix_header & header);

void reply_ls (accepted_socket& client_sock, cix_header& header) {
   const char* ls_cmd = "ls -l 2>&1";
   FILE* ls_pipe = popen (ls_cmd, "r");
   if (ls_pipe == NULL) { 
      log << "ls -l: popen failed: " << strerror (errno) << endl;
      header.command = cix_command::NAK;
      header.nbytes = errno;
      send_packet (client_sock, &header, sizeof header);
      return;
   }
   string ls_output;
   char buffer[0x1000];
   for (;;) {
      char* rc = fgets (buffer, sizeof buffer, ls_pipe);
      if (rc == nullptr) break;
      ls_output.append (buffer);
   }
   int status = pclose (ls_pipe);
   if (status < 0) log << ls_cmd << ": " << strerror (errno) << endl;
              else log << ls_cmd << ": exit " << (status >> 8)
                       << " signal " << (status & 0x7F)
                       << " core " << (status >> 7 & 1) << endl;
   header.command = cix_command::LSOUT;
   header.nbytes = ls_output.size();
   memset (header.filename, 0, FILENAME_SIZE);
   log << "sending header " << header << endl;
   send_packet (client_sock, &header, sizeof header);
   send_packet (client_sock, ls_output.c_str(), ls_output.size());
   log << "sent " << ls_output.size() << " bytes" << endl;
}

void reply_put (accepted_socket& client_sock, cix_header& header) {
   if(do_put(client_sock, header)) {
      header.command = cix_command::ACK;
      header.nbytes = 0;
      memset (header.filename, 0, FILENAME_SIZE);
      log << "sending header " << header << endl;
      send_packet (client_sock, &header, sizeof header);
      // send_packet (client_sock, ls_output.c_str(), ls_output.size());
      // log << "sent " << 0 << " bytes" << endl;
   }
   else {
      header.command = cix_command::NAK;
      header.nbytes = 0;
      memset (header.filename, 0, FILENAME_SIZE);
      log << "sending header " << header << endl;
      send_packet (client_sock, &header, sizeof header);
   }
}

bool do_put(accepted_socket& client_sock, cix_header & header) {
   char buffer[header.nbytes+1];

   recv_packet (client_sock, buffer, header.nbytes);

   string data(buffer);
   string fn(header.filename);

   return write_file(fn, buffer);
}

void reply_rm (accepted_socket& client_sock, cix_header& header) {
   if(do_rm(client_sock, header)) {
      header.command = cix_command::ACK;
      header.nbytes = 0;
      memset (header.filename, 0, FILENAME_SIZE);
      log << "sending header " << header << endl;
      send_packet (client_sock, &header, sizeof header);
   }
   else {
      header.command = cix_command::NAK;
      header.nbytes = 0;
      memset (header.filename, 0, FILENAME_SIZE);
      log << "sending header " << header << endl;
      send_packet (client_sock, &header, sizeof header);
   }
}

bool do_rm(accepted_socket& client_sock, cix_header & header) {
   return unlink(header.filename) >= 0;
}


void reap_zombies() {
   for (;;) {
      int status;
      pid_t child = waitpid (-1, &status, WNOHANG);
      if (child <= 0) break;
      log << "child " << child
          << " exit " << (status >> 8)
          << " signal " << (status & 0x7F)
          << " core " << (status >> 7 & 1) << endl;
   }
}

void signal_handler (int signal) {
   log << "signal_handler: caught " << strsignal (signal) << endl;
   reap_zombies();
}

void signal_action (int signal, void (*handler) (int)) {
   struct sigaction action;
   action.sa_handler = handler;
   sigfillset (&action.sa_mask);
   action.sa_flags = 0;
   int rc = sigaction (signal, &action, nullptr);
   if (rc < 0) log << "sigaction " << strsignal (signal) << " failed: "
                   << strerror (errno) << endl;
}

void reply_get (accepted_socket& client_sock, cix_header& header) {
   cout<<"HELLO!!!!"<<endl;
    string data = "";
   header.command = cix_command::FILE;
   // header.filename is same as was recieved by CIX_GET
   string fn(header.filename);
   try {
      data = read_file(fn);
   } catch(exception & e) {
      log << e.what() << endl;
      header.command = cix_command::NAK;
      header.nbytes = 0;
      send_packet (client_sock, &header, sizeof header);
      return;
   }

   // data.push_back('\0');
   header.nbytes = data.size();

   log << "sending header " << header << endl;
   send_packet (client_sock, &header, sizeof header);

   log << "sending contents of : " << fn << endl;
   send_packet (client_sock, data.c_str(), data.size());
}

void run_server (accepted_socket& client_sock) {
   log.execname (log.execname() + "-server");
   log << "connected to " << to_string (client_sock) << endl;
   try {   
      for (;;) {
         cix_header header; 
         recv_packet (client_sock, &header, sizeof header);
         log << "received header " << header << endl;
         switch (header.command) {
            case cix_command::LS: 
               reply_ls (client_sock, header);
               break;
            case cix_command::PUT:
               reply_put (client_sock, header);
               break;
            case cix_command::RM: 
               reply_rm (client_sock, header);
               break;
            case cix_command::GET: 
               reply_get (client_sock, header);
               break;
            default:
               log << "invalid header from client" << endl;
               log << "cix_nbytes = " << header.nbytes << endl;
//               log << "cix_command = " << header.command << endl;
               log << "cix_filename = " << header.filename << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      log << error.what() << endl;
   }catch (cix_exit& error) {
      log << "caught cix_exit" << endl;
   }
   log << "finishing" << endl;
   throw cix_exit();
}

void fork_cixserver (server_socket& server, accepted_socket& accept) {
   pid_t pid = fork();
   if (pid == 0) { // child
      server.close();
      run_server (accept);
      throw cix_exit();
   }else {
      accept.close();
      if (pid < 0) {
         log << "fork failed: " << strerror (errno) << endl;
      }else {
         log << "forked cixserver pid " << pid << endl;
      }
   }
}


int main (int argc, char** argv) {
   log.execname (basename (argv[0]));
   log << "starting" << endl;
   vector<string> args (&argv[1], &argv[argc]);
   signal_action (SIGCHLD, signal_handler);
   in_port_t port = get_cix_server_port (args, 0);
   try {
      server_socket listener (port);
      for (;;) {
         log << to_string (hostinfo()) << " accepting port "
             << to_string (port) << endl;
         accepted_socket client_sock;
         for (;;) {
            try {
               listener.accept (client_sock);
               break;
            }catch (socket_sys_error& error) {
               switch (error.sys_errno) {
                  case EINTR:
                     log << "listener.accept caught "
                         << strerror (EINTR) << endl;
                     break;
                  default:
                     throw;
               }
            }
         }
         log << "accepted " << to_string (client_sock) << endl;
         try {
            fork_cixserver (listener, client_sock);
            reap_zombies();
         }catch (socket_error& error) {
            log << error.what() << endl;
         }
      }
   }catch (socket_error& error) {
      log << error.what() << endl;
   }catch (cix_exit& error) {
      log << "caught cix_exit" << endl;
   }
   log << "finishing" << endl;
   return 0;
}

