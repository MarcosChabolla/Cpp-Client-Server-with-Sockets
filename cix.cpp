// $Id: cix.cpp,v 1.4 2016-05-09 16:01:56-07 - - $
//Amit Khatri
//ID:akhatri@ucsc.edu
//Partner: Marcos Chabolla
//ID: mchaboll@ucsc.edu
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
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

unordered_map<string,cix_command> command_map {
   {"exit", cix_command::EXIT   },
   {"help", cix_command::HELP   },
   {"ls"  , cix_command::LS     },
   {"get" , cix_command::GET    },
   {"put" , cix_command::PUT    },
   {"rm" , cix_command::RM      },
};

void cix_help() {
   static const vector<string> help = {
      "exit         - Exit the program.  Equivalent to EOF.",
      "get filename - Copy remote file to local host.",
      "help         - Print help summary.",
      "ls           - List names of files on remote server.",
      "put filename - Copy local file to remote host.",
      "rm filename  - Remove file from remote server.",
   };
   for (const auto& line: help) cout << line << endl;
}

void cix_ls (client_socket& server) {
    //Create new header
    cix_header header;
    //Set command to LS
    header.command = cix_command::LS;
   
   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   log << "received header " << header << endl;
   
   if (header.command != cix_command::LSOUT) {
      log << "sent LS, server did not return LSOUT" << endl;
      log << "server returned " << header << endl;
   }else {
      char buffer[header.nbytes + 1];
      recv_packet (server, buffer, header.nbytes);
      log << "received " << header.nbytes << " bytes" << endl;
      buffer[header.nbytes] = '\0';
      cout << buffer;
   }
}


void usage() {
   cerr << "Usage: " << log.execname() << " [host] [port]" << endl;
   throw cix_exit();
}

void cix_rm(client_socket& server, const string & fn) {
   // TODO: send filename to server, get ack and conf. that
   // file has been deleted.
   cix_header header;
   //Set header to RM
   header.command = cix_command::RM;
   if(fn.find('/') != string::npos) {
      log << "error " << fn << " : cannot contain slash (/)"
      << endl;
      return;
   }

   if(fn.size() > 59) {
      log << "Error : filename longer than 59 bytes" << endl;
   }
   //Properly clear the char array to avoid
   //segfault
   for(uint i = 0; i < fn.size(); i++) {
      header.filename[i] = fn[i];
   } 
   header.filename[59] = '\0';

   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);

   recv_packet (server, &header, sizeof header);
   
   log << "received header " << header << endl;
   if (header.command != cix_command::ACK) {
      log << "send cix_command::RM, did not receive CIX_ACK" << endl;
      log << "server returned " << header << endl;
   }else {
      log << "file " << fn << " : deleted from server" << endl;
   }
}

void cix_get(client_socket& server, const string fn) {
   // TODO: send filename to server, get ack and conf. that
   // file has been deleted.
   cix_header header;
   string data = "";

   header.command = cix_command::GET;
   header.nbytes = 0;
   if(fn.find('/') != string::npos) {
      log << "error " << fn << " : cannot contain slash (/)"
      << endl;
      return;
   }
   if(fn.size() > 59) {
      log << "Error : filename longer than 59 bytes" << endl;
      return;
   }
   uint i;
   for(i = 0; i < fn.size() && i < 59 ; i++) {
      header.filename[i] = fn[i];
   } 
   header.filename[i] = '\0';

   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   //cout << "TEST" << endl;
   recv_packet (server, &header, sizeof header);
   if(header.command != cix_command::FILE) {
      log << "error : invalid response from server" << endl;
      log << "file possible does not exist" << endl;
      return;
   }

   char buffer[header.nbytes+1];

   recv_packet (server, buffer, header.nbytes);
   cout<<"HURR"<<endl;

   if(write_file(fn, buffer)) {
      log << "file : " << fn << " : received from server" << endl;
   }
   else {
      log << "file : " << fn << " : failed to receive" << endl;
   }
}

void cix_put(client_socket& server, const string & fn) {
  
    // TODO: send file to server
   cix_header header;
   string data = "";
    
   header.command = cix_command::PUT;
   header.nbytes = 0;
   if(fn.find('/') != string::npos) {
      log << "error " << fn << " : cannot contain slash (/)"
      << endl;
      return;
   }
    if(fn.size() > 59) {
      log << "Error : filename longer than 59 bytes" << endl;
      return;
   }
   uint i;
   for(i = 0; i < fn.size() && i < 59 ; i++) {
      header.filename[i] = fn[i];
   } 
   header.filename[i] = '\0';
   
   try {
      data = read_file(fn);
   } catch(exception & e) {
      log << e.what() << endl;
      return;
   }
   // data.push_back('\0');
   header.nbytes = data.size();

   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);

   log << "sending contents of : " << fn << endl;
   send_packet (server, data.c_str(), data.size());

   recv_packet (server, &header, sizeof header);
   log << "received header " << header << endl;

   if(header.command == cix_command::ACK) {
      log << fn << " successfully transferred" << endl;
   }
   else {
      log << fn << " failed to transfer" << endl;
   }
}



int main (int argc, char** argv) {
   log.execname (basename (argv[0]));
   log << "starting" << endl;
   vector<string> args (&argv[1], &argv[argc]);
   if (args.size() > 2) usage();
   string host = get_cix_server_host (args, 0);
   in_port_t port = get_cix_server_port (args, 1);
   log << to_string (hostinfo()) << endl;
   try {
      log << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      log << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         vector<string> words = split (line, " \t");
         if (cin.eof()) throw cix_exit();
         log << "command " << line << endl;
         const auto& itor = command_map.find (words[0]);
         cix_command cmd = itor == command_map.end()
                         ? cix_command::ERROR : itor->second;
         switch (cmd) {
            case cix_command::EXIT:
               throw cix_exit();
               break;
            case cix_command::HELP:
               cix_help();
               break;
            case cix_command::LS:
               cix_ls (server);
               break;
            case cix_command::PUT:
               cout << "words[1]: "<< words[1] << endl; 
               cix_put (server, words[1]);
               break;
            case cix_command::GET:
               cout << "words[1]: "<< words[1] << endl; 
               cix_get (server, words[1]);
               cout<<"GOT BACK FROM GET"<<endl;
               break;
            case cix_command::RM:
               cout << "words[1]: "<< words[1] << endl; 
               cix_rm (server, words[1]);
               break;
            default:
               log << line << ": invalid command" << endl;
               break;
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

